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
#include <vector>
#include <iostream>
#include <unordered_map>
#include <utility> // for std::pair
#include <algorithm> // for sort
#include <type_traits> // for is_same_v<>
#include <unistd.h> // for getopt
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
            // For segments, lets identify the maps that are covered by Java heap segments or CODECACHES
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
         // A stackRegion usually spans two smaps: one for the stack guard
         // which is protected to R/W and one for the stack itself
         // We want to cover the entire stack guard with part of the thread stack and
         // cover entirely or partially the next smap with the remaining of the thread
         // stack
         if (map->getAddrRange().includes(*stackRegion))
            {
            map->addCoveringRange(*stackRegion);
            map->setPurpose(SmapEntry::STACK);
            break; // go to next smap
            }
         else
            {
            // This could be our stack guard
            // Typically the start of the smap is the same as the start of the thread stack
            if (stackRegion->includes(map->getAddrRange()))
               {
               if (stackRegion->getStart() == map->getAddrRange().getStart())
                  {
                  // Create a new threadStack just for the size of this smap
                  // This is not technically a memory leak because we need it till the end of the program
                  ThreadStack *threadStack = new ThreadStack(map->getAddrRange().getStart(), map->getAddrRange().getEnd(), stackRegion->getThreadName(), 0 /*rss*/);
                  map->addCoveringRange(*threadStack);
                  map->setPurpose(SmapEntry::STACK);
                  // Substract the size of the stack guard from the ThreadStack
                  // This adjusted ThreadStack will be attributed to the next map
                  stackRegion->setStart(map->getAddrRange().getEnd());
                  }
               }
            else
               {
               cout << "Unexpected situation with ThreadStack " << *stackRegion << " and smap " << *map;
               map->addOverlappingRange(*stackRegion);
               }
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


template <typename MAPENTRY>
void computeProportionalRssContribution(const MAPENTRY &crtMap, bool usePageMap,
                                        unsigned long long virtualSize[], // output
                                        unsigned long long rssSize[]) // output
   {
   const list<const AddrRange*> coveringRanges = crtMap.getCoveringRanges();
   const list<const AddrRange*> overlapRanges = crtMap.getOverlappingRanges();
   if (coveringRanges.size() != 0 && overlapRanges.size() != 0)
      {
      cerr << "Warning: smap starting at addr " << crtMap.getAddrRange().getStart() << " has both covering and overlapping ranges\n";
      }

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
      if (usePageMap)
         rssSize[category] += (*seg)->getRSS(); // rssSize[] sums up the RSS of covering ranges for all smaps
      } // end for
   // When using the pageMap we already have the RSS for each category,
   // so we can skip estimating the RSS using the proportional scheme based on virtual size
   if (usePageMap)
      return;

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
         for (int i = 0; i < AddrRange::NUM_CATEGORIES; i++)
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
      else // This smap is not covered by anything and it does not overlap anything
         {
         rssSize[AddrRange::NOTCOVERED] += (crtMap.getResidentSizeKB() << 10);
         virtualSize[AddrRange::NOTCOVERED] += crtMap.size();
         }
      }
   }


template <typename MAPENTRY>
void printSpaceKBTakenByVmComponents(const vector<MAPENTRY> &smaps, bool usePageMap)
   {
   cout << "\nprintSpaceKBTakenByVmComponents...\n";

   // categories of covering ranges
   unsigned long long virtualSize[AddrRange::NUM_CATEGORIES] = {0}; // one entry for each category
   unsigned long long rssSize[AddrRange::NUM_CATEGORIES] = {0}; // one entry for each category

   TopTen<MAPENTRY, MemoryEntryRssLessThan> topTenDlls;

   TopTen<MAPENTRY, MemoryEntryRssLessThan> topTenNotCovered;

   unordered_map<string, unsigned long long> dllCollection; // maps dll name to size (RSS)

   unsigned long long totalVirtSize = 0;
   unsigned long long totalRssSize = 0;

   // Iterate through all smaps/vmmaps
   for (auto crtMap = smaps.cbegin(); crtMap != smaps.cend(); ++crtMap)
      {
      totalVirtSize += crtMap->size();
      totalRssSize += crtMap->getResidentSizeKB() << 10; // convert to bytes

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
          continue; // These smaps are not shared with other categories
          }

      // Determine whether a map is covered by ranges of different types and assign RSS in proportional values
      // We can do a better job is we know for each page of the smap whether it is in RSS or not
      computeProportionalRssContribution(*crtMap, usePageMap, virtualSize, rssSize);

      if (crtMap->getCoveringRanges().size() == 0 &&
          crtMap->getOverlappingRanges().size() == 0 &&
          crtMap->getResidentSizeKB() != 0)
         topTenNotCovered.processElement(*crtMap);
      } // end for (iterate through smaps)
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
   int pid = 0;
   bool verbose = false;
   while ((opt = getopt(argc, argv, "c:j:ps:v")) != -1)
      {
      switch (opt)
         {
         case 's':
            smapsFilename = optarg;
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

   // Read the smaps file
   vector<MapEntry> sMaps;
#ifdef WINDOWS_FOOTPRINT
   readVmmapFile(smapsFilename, sMaps);
#else
   readSmapsFile(smapsFilename, sMaps);
#endif



   //===================== Javacore processing ============================
   vector<J9Segment> segments; // must not become out-of-scope until I am done with the maps
   vector<ThreadStack> threadStacks;

   readJavacore(javacoreFilename, segments, threadStacks, pageMapReader);
#ifdef DEBUG
   // let's print all segments
   cout << "Print segments:\n";
   for (vector<J9Segment>::iterator it = segments.begin(); it != segments.end(); ++it)
      cout << *it << endl;
#endif
   // Annotate maps with j9segments
   annotateMapWithSegments(sMaps, segments);
   annotateMapWithThreadStacks(sMaps, threadStacks);

   //======================== Callsites processing =============================
   vector<CallSite> callSites; // must not become out-of-scope until I am done with the maps
   if (callsitesFilename)
      {
      // Read the callsites file
      readCallSitesFile(callsitesFilename, callSites, pageMapReader);

      // Annotate the smaps file with callsites
      annotateMapWithSegments(sMaps, callSites);
      }

   if (verbose)
      {
      // print results one by one
      for (vector<MapEntry>::const_iterator map = sMaps.begin(); map != sMaps.end(); ++map)
         map->printEntryWithAnnotations();
      }

   bool usePageMap = pageMapReader != nullptr;
   printSpaceKBTakenByVmComponents(sMaps, usePageMap);

   // pageMapReader is not needed anymore
   if (pageMapReader)
      delete(pageMapReader);

   return 0;
   }


