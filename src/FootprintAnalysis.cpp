/*******************************************************************************
 * Copyright (c) 2022, 2022 IBM Corp. and others
 *
 * This program and the accompanying materials are made available under
 * the terms of the Eclipse Public License 2.0 which accompanies this
 * distribution and is available at https://www.eclipse.org/legal/epl-2.0/
 * or the Apache License, Version 2.0 which accompanies this distribution and
 * is available at https://www.apache.org/licenses/LICENSE-2.0.
 *
 * This Source Code may also be made available under the following
 * Secondary Licenses when the conditions for such availability set
 * forth in the Eclipse Public License, v. 2.0 are satisfied: GNU
 * General Public License, version 2 with the GNU Classpath
 * Exception [1] and GNU General Public License, version 2 with the
 * OpenJDK Assembly Exception [2].
 *
 * [1] https://www.gnu.org/software/classpath/license.html
 * [2] http://openjdk.java.net/legal/assembly-exception.html
 *
 * SPDX-License-Identifier: EPL-2.0 OR Apache-2.0 OR GPL-2.0 WITH Classpath-exception-2.0 OR LicenseRef-GPL-2.0 WITH Assembly-exception
 *******************************************************************************/
#include <algorithm> // for sort
#include <iostream>
#include <stdexcept> // runtime_error
#include <type_traits> // for is_same_v<>
#include <unistd.h> // for getopt
#include <unordered_map>
#include <utility> // for std::pair
#include <vector>
#include "smap.hpp"
#include "CallSites.hpp"
#include "Util.hpp"
#include "vmmap.hpp"
#include "AddrRange.hpp"
#include "PageMapSupport.hpp"
#undef WINDOWS_FOOTPRINT
using namespace std;


// T can be either a J9Segment or a CallSite
template <typename MAPENTRY, typename T>
void annotateMapWithSegments(std::vector<MAPENTRY>&maps, const std::vector<T>& segments)
   {
   // Annotate maps with j9segments
   // TODO: use binary search because smaps are sorted by address.
   cout << "Annotate maps with segments ...";
   for (auto map = maps.begin(); map != maps.end(); ++map)
      {
      for (auto seg = segments.cbegin(); seg != segments.cend(); ++seg)
         {
         if (seg->disjoint(map->getAddrRange()))
            continue;
         if (map->getAddrRange().includes(*seg))
            {
            map->addCoveringRange(*seg);
            // For segments, lets identify the maps that are covered by Java heap segments or CODECACHES.
            // These segments cannot share an smap with other types of segments.
            if constexpr (std::is_same_v<T, J9Segment>)
               {
               if (seg->getSegmentType() == J9Segment::JAVAHEAP)
                  map->setPurpose(SmapEntry::JAVAHEAP);
               if (seg->getSegmentType() == J9Segment::CODECACHE)
                  map->setPurpose(SmapEntry::CODECACHE);
               }
            }
         else
            {
            // A class segment can span several smaps used for SCC.
            // In this case we ignore the class segment. The RSS
            // contribution will go enterely to the SCC category.
            if constexpr (std::is_same_v<T, J9Segment>)
               {
               if (map->getPurpose() == SmapEntry::SCC && seg->getSegmentType() == J9Segment::CLASS)
                  {
                  seg->setAccountedFor(true);
                  continue;
                  }
               }
            cerr << "Overlapping range SEG:" << *seg << " and SMAP:" << *map << endl;
            map->addOverlappingRange(*seg);
            //map->setPurpose(SmapEntry::GENERIC);
            }
         }
      }
   cout << "Done\n";
   }

template <typename MAPENTRY>
void annotateMapWithThreadStacks(std::vector<MAPENTRY>&maps, std::vector<ThreadStack>& stacks)
   {
   // Annotate maps with threadStacks
   cout << "Annotate maps with threads stacks ...";
   for (auto map = maps.begin(); map != maps.end(); ++map)
      {
      for (auto stackRegion = stacks.begin(); stackRegion != stacks.end(); ++stackRegion)
         {
         if (stackRegion->disjoint(map->getAddrRange()))
            continue;
         // A stackRegion may span two smaps: one for the stack guard
         // which is protected to R/W and one for the stack itself.
         // In the newer OpenJ9 versions the start-end addresses for a thread stack
         // from javacore form a region that is totally included in an smap.
         // There is another smap for the guard page, but that can be ignored
         // because it does not use any RSS.
         // Note that the stack region can share the same smap with other segments (e.g. JIT persistent).
         if (map->getAddrRange().includes(*stackRegion))
            {
            map->addCoveringRange(*stackRegion);
            // Since stack regions can share an smap with other segments we must not set a purpose
            //map->setPurpose(SmapEntry::STACK);
            break; // go to next smap
            }
         else
            {
            cerr << "WARNING: Unexpected situation with ThreadStack " << *stackRegion << " and smap " << *map;
            map->addOverlappingRange(*stackRegion);
            }
         }
      }
   cout << "Done\n";
   }



template <typename MAPENTRY>
unsigned long long printSpaceKBTakenBySharedLibraries(const vector<MAPENTRY> &smaps)
   {
   unsigned long long spaceTakenBySharedLibraries = 0;
   TopTen<MAPENTRY, MemoryEntrySizeLessThan> topTen;
   for (auto crtMap = smaps.cbegin(); crtMap != smaps.cend(); ++crtMap)
      {
      if (crtMap->isMapForSharedLibrary())
         {
         spaceTakenBySharedLibraries += crtMap->sizeKB();
         topTen.processElement(*crtMap);
         }
      }
   cout << "Total space taken by shared libraries: " << dec << spaceTakenBySharedLibraries << " KB\n";
   topTen.print();
   return spaceTakenBySharedLibraries;
   }


/*
 * An smap could be covered by adress ranges of different categories (e,g. DataCache, JITPersist, CallSites, etc).
 * Given that we don't known exactly which resident pages belong to these different categories,
 * we use a "proportional" approach: if category X uses n% of the virtual space of the smap,
 * then we assume that category X also uses n% of the RSS for that smap.
 * A better estimation could be done if we use the pageMap from the kernel. This requires
 * the target process to be live when we do the footprint analysis.
 * @param crtMap     The smap to proces
 * @param usePageMap Indicates whether to read the mapPage from the kernel
 * @param virtualSize [OUT] Array of virtual sizes for each possible memory category
 * @param rssSize [OUT] Array with RSS contributions of each possible memory category
 */
template <typename MAPENTRY>
void computeProportionalRssContribution(const MAPENTRY &crtMap,
                                        PageMapReader *pageMapReader,
                                        unsigned long long virtualSize[], // output
                                        unsigned long long rssSize[] // output
                                        )
   {
   const list<const AddrRange*> &coveringRanges = crtMap.getCoveringRanges();
   const list<const AddrRange*> &overlapRanges = crtMap.getOverlappingRanges();

   unsigned long long totalCoveredSize = 0;
   // The following sums up the virtual size for each category covering this smap
   unsigned long long sz[AddrRange::NUM_CATEGORIES] = {0}; // one entry for each category

   for (auto seg = coveringRanges.cbegin(); seg != coveringRanges.cend(); ++seg)
      {
      AddrRange::RangeCategories category = (*seg)->getRangeCategory();
      unsigned long long size = (*seg)->size();
      virtualSize[category] += size; // virtualSize[] sums up the virtual size of covering ranges for all smaps
      sz[category] += size; // sz[] sums up the virtual size of covering ranges for this smap
      totalCoveredSize += size;
      } // end for

   // Determine if the smap is covered by more than one type of range
   int lastNonNullCategory = 0;
   int numDifferentCategories = 0;
   for (int i = 0; i < AddrRange::NUM_CATEGORIES; i++)
      {
      if (sz[i] > 0)
         {
         numDifferentCategories++;
         lastNonNullCategory = i;
         }
      }
   if (totalCoveredSize > 0)
      {
      // If the map is covered by segments of the same type
      // then we can charge the entire RSS to that type
      if (numDifferentCategories == 1)
         {
         rssSize[lastNonNullCategory] += (crtMap.getResidentSizeKB() << 10); // convert to bytes
         }
      else // Proportional allocation based on virtual size
         {
         unsigned long long rssAccountedFor = 0;
         for (int i = 0; i <= lastNonNullCategory; i++)
            {
            if (sz[i] != 0)
               {
               unsigned long long rssFraction = (crtMap.getResidentSizeKB() << 10) * sz[i] / crtMap.size();
               rssSize[i] += rssFraction;
               rssAccountedFor += rssFraction;
               }
            }
         rssSize[AddrRange::UNKNOWN] += (crtMap.getResidentSizeKB() << 10) - rssAccountedFor;
         virtualSize[AddrRange::UNKNOWN] += crtMap.size() - totalCoveredSize;
         }
      }
   else // This map is not covered by anything
      {
      // Does it have overlapping ranges?
      // An overlapping range could happen if smaps are gathered first and then by the time
      // we collect the javacore the GC expands which makes the GC segment in the javacore
      // be larger than the smap.
      if (overlapRanges.size() != 0)
         {
         // If the j9segment totally overlaps the smap, then we know the exact type
         // of j9memory for that smap and the entire RSS can be attributed to that type.
         // We could relax this heuristic: if the overlapping segments for this smap are of the same kind
         // then we can guess the kind of memory for the smap.
         AddrRange::RangeCategories smapCategory = AddrRange::UNKNOWN;
         for (auto seg = overlapRanges.cbegin(); seg != overlapRanges.cend(); ++seg)
            {
            AddrRange::RangeCategories segCategory = (*seg)->getRangeCategory();
            if (segCategory == AddrRange::UNKNOWN)
               {
               // Type of segment is not known, so type of smap is unknown
               smapCategory = AddrRange::UNKNOWN;
               break;
               }
            else
               {
               if (smapCategory == AddrRange::UNKNOWN) // Not yet set
                  {
                  smapCategory = segCategory;
                  }
               else // Different types of segments. Cannot determine the type of smap
                  {
                  smapCategory = AddrRange::UNKNOWN;
                  break;
                  }
               }
            }
         rssSize[smapCategory] += (crtMap.getResidentSizeKB() << 10);
         virtualSize[smapCategory] += crtMap.size();
         if (smapCategory == AddrRange::UNKNOWN)
            {
            cout << "smap with different/unknown segments that are not totaly included in this smap\n";
            //topTenPartiallyCovered.processElement(*crtMap);
            }
         }
      }
   }


template <typename MAPENTRY>
void printSpaceKBTakenByVmComponents(const vector<MAPENTRY> &smaps, // From smap
                                     const vector<J9Segment> &segments, // From javacore
                                     const vector<ThreadStack> &threadStacks, // From javacore
                                     const vector<CallSite> &callSites, // From coredump
                                     PageMapReader *pageMapReader)
   {
   cout << "\nprintSpaceKBTakenByVmComponents...\n";

   // categories of covering ranges
   unsigned long long virtualSize[AddrRange::NUM_CATEGORIES] = {0}; // one entry for each category
   unsigned long long rssSize[AddrRange::NUM_CATEGORIES] = {0}; // one entry for each category

   TopTen<MAPENTRY, MemoryEntryRssLessThan> topTenDlls;

   TopTen<MAPENTRY, MemoryEntryRssLessThan> topTenNotCovered;

   // Hashtable that maps <dllname --> dllrss>
   unordered_map<string, unsigned long long> dllCollection;

   unsigned long long totalVirtSize = 0;
   unsigned long long totalRssSize = 0;

   // Iterate through all smaps/vmmaps
   for (auto crtMap = smaps.cbegin(); crtMap != smaps.cend(); ++crtMap)
      {
      totalVirtSize += crtMap->size();
      totalRssSize += crtMap->getResidentSizeKB() << 10; // convert to bytes

      if (crtMap->getCoveringRanges().size() != 0 && crtMap->getOverlappingRanges().size() != 0)
         {
         cerr << "Warning: smap starting at addr " << crtMap->getAddrRange().getStart() << " has both covering and overlapping ranges\n";
         }

      // Check if shared library; these require some extra processing
      if (crtMap->getPurpose() == SmapEntry::DLL)
         {
         topTenDlls.processElement(*crtMap);

         // Note that in Linux a DLL may have 3 or even 4 smaps. e.g.
         // Size = 11968 rss = 11136 Prot = r-xp / home / jbench / mpirvu / JITDll_gcc / libj9jit28.so
         // Size = 960   rss = 256   Prot = r--p / home / jbench / mpirvu / JITDll_gcc / libj9jit28.so
         // Size = 448   rss = 448   Prot = rw-p / home / jbench / mpirvu / JITDll_gcc / libj9jit28.so
         // We want to sum-up all contributions for the same DLL. Thus let's create a hashtable
         // that accumulates the sums (key is the name of the DLL, value is the total RSS)
         // Then we need to sort by the total RSS
         //
         auto& dllTotalRSSSize = dllCollection[crtMap->getDetailsString()]; // If key does not exist, it will be inserted
         dllTotalRSSSize += crtMap->getResidentSizeKB() << 10;
         }
      // The following types of smaps have a sole purpose
      // and we can read the RSS summary directly from the smap
      AddrRange::RangeCategories addrRangeCategory = AddrRange::UNKNOWN;
      switch (crtMap->getPurpose())
         {
         case SmapEntry::DLL:
            addrRangeCategory = AddrRange::DLL;
            break;
         case SmapEntry::SCC:
            addrRangeCategory = AddrRange::SCC;
            break;
         case SmapEntry::STACK:
            addrRangeCategory = AddrRange::STACK;
            // These are smaps dedicated to stack and are not shared with anythinf else.
            // Note that we also have separate stacks for Java threads. Those can share
            // the same smap with other categories, but they will not have a set purpose.
            break;
         case SmapEntry::JAVAHEAP:
            addrRangeCategory = AddrRange::JAVAHEAP;
            break;
         case SmapEntry::CODECACHE:
            addrRangeCategory = AddrRange::CODECACHE;
            break;
         default:
            addrRangeCategory = AddrRange::UNKNOWN;
         }
       if (addrRangeCategory != AddrRange::UNKNOWN)
          {
          virtualSize[addrRangeCategory] += crtMap->size();
          rssSize[addrRangeCategory] += crtMap->getResidentSizeKB() << 10;

          // Mark all included segments as accounted for.
          // Note that some CLASS segments can be included in the SCC smap.
         const std::list<const AddrRange*> &coveringRanges = crtMap->getCoveringRanges();
         for (auto seg = coveringRanges.cbegin(); seg != coveringRanges.cend(); ++seg)
            {
            (*seg)->setAccountedFor(true);
            }
          continue; // These smaps are not shared with other categories
          }

      // The remaining smaps can be shared by several types of data structures.
      // However, some smaps are not covered by anything. Exclude them from further processing.
      // Here's an example:  Prot=r--p /usr/lib/locale/locale-archive
      // Here's another example: Prot=r--s /home/mpirvu/sdks/OpenJ9-JDK21-x86-64_linux-20260402-213720/lib/modules
      if (crtMap->getCoveringRanges().size() == 0 && crtMap->getOverlappingRanges().size() == 0)
         {
         rssSize[AddrRange::NOTCOVERED] += (crtMap->getResidentSizeKB() << 10);
         virtualSize[AddrRange::NOTCOVERED] += crtMap->size();
         topTenNotCovered.processElement(*crtMap);
         continue;
         }

      // Determine whether a map is covered by ranges of different types and assign RSS in proportional values.
      // We can do a better job if we know for each page of the smap whether it is in RSS or not.
      if (!pageMapReader)
         computeProportionalRssContribution(*crtMap, pageMapReader, virtualSize, rssSize);
      } // end for (iterate through smaps)

   // If the pageMapReader is available, then, for each virtual range
   // of the categories that can share a single smap, we can go page
   // by page to determine its RSS contribution.
   if (pageMapReader)
      {
      for (auto seg = segments.cbegin(); seg != segments.cend(); ++seg)
         {
         if (seg->isAccountedFor())
            continue;
         auto category = seg->getRangeCategory();
         if (category == AddrRange::DATACACHE ||
             category == AddrRange::SCRATCH ||
             category == AddrRange::PERSIST ||
             category == AddrRange::OTHER_INTERNAL ||
             category == AddrRange::CLASS)
            {
            // Determine the RSS contribution.
            if (seg->getRSS() == AddrRange::INVALID_RSS)
               {
               unsigned long long rss = pageMapReader->computeRssForAddrRange(seg->getStart(), seg->getEnd());
               seg->setRSS(rss);
               }
            rssSize[category] += seg->getRSS();
            virtualSize[category] += seg->size();
            seg->setAccountedFor(true);
            }

         }
      for (auto stackRegion = threadStacks.cbegin(); stackRegion != threadStacks.cend(); ++stackRegion)
         {
         // Determine the RSS contribution.
         if (stackRegion->getRSS() == AddrRange::INVALID_RSS)
            {
            unsigned long long rss = pageMapReader->computeRssForAddrRange(stackRegion->getStart(), stackRegion->getEnd());
            stackRegion->setRSS(rss);
            }
         rssSize[AddrRange::STACK] += stackRegion->getRSS();
         virtualSize[AddrRange::STACK] += stackRegion->size();
         stackRegion->setAccountedFor(true);
         }
      for (auto callSite = callSites.cbegin(); callSite != callSites.cend(); ++callSite)
         {
         // Determine the RSS contribution.
         if (callSite->getRSS() == AddrRange::INVALID_RSS)
            {
            unsigned long long rss = pageMapReader->computeRssForAddrRange(callSite->getStart(), callSite->getEnd());
            callSite->setRSS(rss);
            }
         rssSize[AddrRange::CALLSITE] += callSite->getRSS();
         virtualSize[AddrRange::CALLSITE] += callSite->size();
         callSite->setAccountedFor(true);
         }
      }

   // Now sum-up virtual memory and RSS from all categories and substract
   // from the total values computed for all smaps. The difference will be
   // attributed to the UNKNOWN category.
   unsigned long long accountedVirtualSize = 0;
   unsigned long long accountedRssSize = 0;
   for (int category = 0; category < AddrRange::NUM_CATEGORIES; category++)
      {
      accountedVirtualSize += virtualSize[category];
      accountedRssSize += rssSize[category];
      }
   if (accountedVirtualSize < totalVirtSize)
      virtualSize[AddrRange::UNKNOWN] += totalVirtSize - accountedVirtualSize;
   if (accountedRssSize < totalRssSize)
      rssSize[AddrRange::UNKNOWN] += totalRssSize - accountedRssSize;


   cout << dec << endl;
   cout << "Totals:       Virtual= " << setw(8) << (totalVirtSize >> 10) << " KB; RSS= " << setw(8) << (totalRssSize >> 10) << " KB\n";
   for (int i = 0; i < AddrRange::NUM_CATEGORIES; i++)
      {
      cout << setw(11) << AddrRange::RangeCategoryNames[i] << ":  Virtual= " << setw(8) << (virtualSize[i] >> 10) << " KB; RSS= " << setw(8) << (rssSize[i] >> 10) << " KB\n";
      }

   // Print explanation
   cout << endl;
   cout << "Unknown portion comes from maps that are partially covered by segments and callsites" << endl;
   cout << "'Not covered' are maps that are really not covered by any segment or callsite" << endl;

   // Process the hashtable with DLLs
   //
   using PairStringULL = pair < string, unsigned long long >;
   // Ideally, above I would use something like  decltype(dllCollection)::value_type
   // which is of type <const string, unsigned long long>
   // However, the sort algorithm used below uses operator = and the const in front of the
   // string creates problems when moving elements

   // Take all elements from the hashtable and put them into a vector
   vector<PairStringULL> vectorWithDlls(dllCollection.cbegin(), dllCollection.cend());


   // Sort the vector using a custom comparator (lambda) that knows how to
   // compare pairs (want to sort based on value of the map entry)
   //
   sort(vectorWithDlls.begin(), vectorWithDlls.end(),
        [](decltype(vectorWithDlls)::const_reference p1, decltype(vectorWithDlls)::const_reference p2)
           { return p1.second > p2.second; } // compare using the second element of the pair
       );

   // print the most expensive entries
   cout << "\n RSS of dlls\n";
   for_each(vectorWithDlls.cbegin(), vectorWithDlls.cend(),
            [](decltype(vectorWithDlls)::const_reference element)
                {cout << dec << setw(8) << (element.second >> 10) << " KB   " << element.first << endl; }
           );

   cout << "\nTop 10 DDLs based on RSS:\n";
   topTenDlls.print();

   cout << "\nTop 10 maps not covered by anything\n";
   topTenNotCovered.print();
   }



#ifdef WINDOWS_FOOTPRINT
typedef VmmapEntry MapEntry; // For Windows
#else
typedef SmapEntry MapEntry;  // For Linux
#endif

void printUsage(const char *progName)
   {
   cerr << "Usage: " << progName << " -s smapsFile -j javacoreFile [-c callsitesFile] [-i PID] [-v]\n" << endl;
   }

int main(int argc, char* argv[])
   {
   int opt;
   const char *javacoreFilename = nullptr;
   const char *callsitesFilename = nullptr;
   const char *smapsFilename = nullptr;
   std::string sccCachePath;
   int pid = 0;
   bool verbose = false;
   while ((opt = getopt(argc, argv, "c:i:j:ps:v")) != -1)
      {
      switch (opt)
         {
         case 's':
            smapsFilename = optarg;
            break;
         case 'i':
            pid = atoi(optarg);
            break;
         case 'j':
            javacoreFilename = optarg;
            break;
         case 'c':
            callsitesFilename = optarg;
            break;
         case 'p':
            pid = atoi(optarg);
            break;
         case 'v':
            verbose = true;
            break;
         default: /* '?' */
            printUsage(argv[0]);
            exit(EXIT_FAILURE);
         } // end switch
      } // end while

   if (smapsFilename == nullptr || javacoreFilename == nullptr)
      {
      printUsage(argv[0]);
      exit(EXIT_FAILURE);
      }

   // If PID is given, open the page map file
   PageMapReader *pageMapReader = pid ? new PageMapReader(pid) : nullptr;


   //===================== Javacore processing ============================
   // This needs to be done before smaps processing because it computes
   // the mapped file name for SCC
   vector<J9Segment> segments; // must not become out-of-scope until I am done with the maps
   vector<ThreadStack> threadStacks;

   readJavacore(javacoreFilename, segments, threadStacks, sccCachePath);
   SmapEntry::setSccCachePath(sccCachePath);
   cout << "Found SCC file path: " << sccCachePath << std::endl;
#ifdef DEBUG
   // let's print all segments
   cout << "Print segments:\n";
   for (vector<J9Segment>::iterator it = segments.begin(); it != segments.end(); ++it)
      cout << *it << endl;
#endif
   //====================== smaps file processing ======================
   vector<MapEntry> sMaps;
#ifdef WINDOWS_FOOTPRINT
   readVmmapFile(smapsFilename, sMaps);
#else
   readSmapsFile(smapsFilename, sMaps);
#endif

   // Annotate maps with j9segments
   annotateMapWithSegments(sMaps, segments);
   annotateMapWithThreadStacks(sMaps, threadStacks);

   //======================== Callsites processing =============================
   vector<CallSite> callSites; // must not become out-of-scope until I am done with the maps
   if (callsitesFilename)
      {
      // Read the callsites file
      readCallSitesFile(callsitesFilename, callSites);

      // Annotate the smaps file with callsites
      annotateMapWithSegments(sMaps, callSites);
      }

   if (verbose)
      {
      // print results one by one
      for (vector<MapEntry>::const_iterator map = sMaps.begin(); map != sMaps.end(); ++map)
         map->printEntryWithAnnotations();
      }

   printSpaceKBTakenByVmComponents(sMaps, segments, threadStacks, callSites, pageMapReader);

   // pageMapReader is not needed anymore
   if (pageMapReader)
      delete(pageMapReader);

   return 0;
   }


