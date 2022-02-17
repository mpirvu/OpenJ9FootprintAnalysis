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
#include "smap.hpp"
#include "CallSites.hpp"
#include "Util.hpp"
#include "vmmap.hpp"
#include "AddrRange.hpp"
#undef WINDOWS_FOOTPRINT
using namespace std;



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
            map->addCoveringRange(*seg);
         else
            map->addOverlappingRange(*seg);
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
                  ThreadStack *threadStack = new ThreadStack(map->getAddrRange().getStart(), map->getAddrRange().getEnd(), stackRegion->getThreadName());
                  map->addCoveringRange(*threadStack);
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



enum RangeCategories {
   GCHEAP = 0,
   CODECACHE,
   DATACACHE,
   DLL,
   STACK,
   SCC,
   SCRATCH,
   PERSIST,
   OTHER_INTERNAL,
   CLASS,
   CALLSITE,
   UNKNOWN,
   NOTCOVERED,
   NUM_CATEGORIES, // Must be the last one
   };

RangeCategories getJ9SegmentCategory(const J9Segment* j9seg)
   {
   switch (j9seg->getSegmentType())
      {
      case J9Segment::HEAP:
         return GCHEAP;
         break;
      case J9Segment::CODECACHE:
         return CODECACHE;
         break;
      case J9Segment::DATACACHE:
         return DATACACHE;
         break;
      case J9Segment::INTERNAL:
         if (j9seg->isJITScratch())
            return SCRATCH;
         else if (j9seg->isJITPersistent())
            return PERSIST;
         else
            return OTHER_INTERNAL;
         break;
      case J9Segment::CLASS:
         return CLASS;
         break;
      default:
         return UNKNOWN;
      }; // end switch
   }

template <typename MAPENTRY>
void printSpaceKBTakenByVmComponents(const vector<MAPENTRY> &smaps)
   {
   cout << "\nprintSpaceKBTakenByVmComponents...\n";

   const char* RangeCategoryNames[NUM_CATEGORIES] = { "GC heap", "CodeCache", "DataCache", "DLL", "Stack", "SCC", "JITScratch", "JITPersist", "Internal", "Classes", "CallSites", "Unknown", "Not covered" };
   // categories of covering ranges
   unsigned long long virtualSize[NUM_CATEGORIES]; // one entry for each category
   unsigned long long rssSize[NUM_CATEGORIES]; // one entry for each category
   for (int i = 0; i < NUM_CATEGORIES; i++)
      virtualSize[i] = rssSize[i] = 0;
   //unsigned long long rss[NUM_CATEGORIES];


   unsigned long long totalVirtSize = 0;
   unsigned long long totalRssSize = 0;



   TopTen<MAPENTRY, MemoryEntryRssLessThan> topTenDlls;

   TopTen<MAPENTRY, MemoryEntryRssLessThan> topTenNotCovered;

   TopTen<MAPENTRY, MemoryEntryRssLessThan> topTenPartiallyCovered;

   unordered_map<string, unsigned long long> dllCollection;

   // Iterate through all smaps/vmmaps
   for (auto crtMap = smaps.cbegin(); crtMap != smaps.cend(); ++crtMap)
      {
      totalVirtSize += crtMap->size();
      totalRssSize += crtMap->getResidentSize() << 10; // convert to bytes

      // Check if shared library
      if (crtMap->isMapForSharedLibrary())
         {
         virtualSize[DLL] += crtMap->size();
         rssSize[DLL] += crtMap->getResidentSize() << 10;
         topTenDlls.processElement(*crtMap);

         // Note that in Linux a DLL may have 3 smaps. e.g.
         // Size = 11968 rss = 11136 Prot = r-xp / home / jbench / mpirvu / JITDll_gcc / libj9jit28.so
         // Size = 960   rss = 256   Prot = r--p / home / jbench / mpirvu / JITDll_gcc / libj9jit28.so
         // Size = 448   rss = 448   Prot = rw-p / home / jbench / mpirvu / JITDll_gcc / libj9jit28.so
         // We want to sum-up all contributions for the same DLL. Thus let's create a hashtable
         // that accumulates the sums (key is the name of the DLL, value is the total RSS)
         // Then we need to sort by the total RSS
         //
         auto& dllTotalRSSSize = dllCollection[crtMap->getDetailsString()]; // If key does not exist, it will be inserted
         dllTotalRSSSize += crtMap->getResidentSize() << 10;
         continue; // DLL maps are not shared with other categories
         }

      // Check if shared class cache. Find "javasharedresources" in the details string
      if (crtMap->getDetailsString().find("javasharedresources") != string::npos || // found
         crtMap->getDetailsString().find("classCache") != string::npos)
         {
         virtualSize[SCC] += crtMap->size();
         rssSize[SCC] += crtMap->getResidentSize() << 10;
         continue; // SCC maps are not shared with other categories (even though classes can reside here)
         }

      // Thread stacks
      if (crtMap->isMapForThreadStack()) 
         {
         virtualSize[STACK] += crtMap->size();
         rssSize[STACK] += crtMap->getResidentSize() << 10;
         continue; // maps marked as "stack" are not shared with other categories
         }

      // The following sums up the virtual size for each category covering this smap
      unsigned long long sz[NUM_CATEGORIES]; // one entry for each category
      for (int i = 0; i < NUM_CATEGORIES; i++)
         sz[i] = 0;

      // look for any covering segments
      const std::list<const AddrRange*> coveringRanges = crtMap->getCoveringRanges();
      const std::list<const AddrRange*> overlapRanges = crtMap->getOverlappingRanges();
      if (coveringRanges.size() != 0 && overlapRanges.size() != 0)
         {
         cerr << "Warning: smap starting at addr " << crtMap->getAddrRange().getStart() << " has both covering and overlapping ranges\n";
         }
      for (auto seg = coveringRanges.cbegin(); seg != coveringRanges.cend(); ++seg)
         {
         if ((*seg)->rangeType() == AddrRange::J9SEGMENT_RANGE) // exclude callsites
            {
            // Convert the range to a segment
            const J9Segment* j9seg = static_cast<const J9Segment*>(*seg);
            // Determine the type of the segment
            RangeCategories category = getJ9SegmentCategory(j9seg);
            if (category != UNKNOWN)
               {
               virtualSize[category] += j9seg->size();
               sz[category] += j9seg->size();
               }
            }
         else if ((*seg)->rangeType() == AddrRange::CALLSITE_RANGE)// This is a callsite
            {
            // Convert the range to a callsite
            const CallSite* callsite = static_cast<const CallSite*>(*seg);
            virtualSize[CALLSITE] += callsite->size();
            sz[CALLSITE] += callsite->size();
            }
         else if ((*seg)->rangeType() == AddrRange::THREADSTACK_RANGE)
            {
            // Convert the range to a ThreadStack
            const ThreadStack* threadStack = static_cast<const ThreadStack*>(*seg);
            virtualSize[STACK] += threadStack->size();
            sz[STACK] += threadStack->size();
            }
         } // end for
      // Now look whether a map is covered by ranges of different types and assign RSS in proportional values
      unsigned long long totalCoveredSize = 0;
      int numDifferentCategories = 0;
      int lastNonNullCategory = 0;
      for (int i = 0; i < NUM_CATEGORIES; i++)
         {
         if (sz[i] > 0)
            {
            numDifferentCategories++;
            lastNonNullCategory = i;
            }
         totalCoveredSize += sz[i];
         }

      if (totalCoveredSize > 0)
         {
         // If the map is covered by segments of the same type
         // then we can charge the entire RSS to that type
         if (numDifferentCategories == 1)
            {
            rssSize[lastNonNullCategory] += (crtMap->getResidentSize() << 10);
            }
         else // Proportional allocation
            {
            for (int i = 0; i < NUM_CATEGORIES; i++)
               {
               // TODO: must do some rounding
               unsigned long long l = (crtMap->getResidentSize() << 10) * sz[i] / crtMap->size();
               rssSize[i] += l;
               }
            rssSize[UNKNOWN] += (crtMap->getResidentSize() << 10) * (crtMap->size() - totalCoveredSize) / crtMap->size();
            virtualSize[UNKNOWN] += crtMap->size() - totalCoveredSize;
            topTenPartiallyCovered.processElement(*crtMap);
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
            RangeCategories smapCategory = UNKNOWN;
            for (auto seg = overlapRanges.cbegin(); seg != overlapRanges.cend(); ++seg)
               {
               if ((*seg)->rangeType() == AddrRange::J9SEGMENT_RANGE) // exclude callsites
                  {
                  // Convert the range to a segment
                  const J9Segment* j9seg = static_cast<const J9Segment*>(*seg);

                  // Determine the type of the segment
                  RangeCategories segCategory = getJ9SegmentCategory(j9seg);
                  if (segCategory == UNKNOWN)
                     {
                     // Type of segment is not known, so type of smap is unknown
                     smapCategory = UNKNOWN;
                     break;
                     }
                  else
                     {
                     if (smapCategory == UNKNOWN) // Not yet set
                        {
                        smapCategory = segCategory;
                        }
                     else // Different types of segments. Cannot determine the type of smap
                        {
                        smapCategory = UNKNOWN;
                        break;
                        }
                     }
                  }
               else // callsites
                  {
                  // Theoretically we could handle these cases as well, but it's unlikely they occur
                  smapCategory = UNKNOWN; 
                  break;
                  }
               }
            rssSize[smapCategory] += (crtMap->getResidentSize() << 10);
            virtualSize[smapCategory] += crtMap->size();
            if (smapCategory == UNKNOWN)
               {
               cout << "smap with different/unknown segments that are not totaly included in this smap\n";
               topTenPartiallyCovered.processElement(*crtMap);
               }
            }
         else
            {
            rssSize[NOTCOVERED] += (crtMap->getResidentSize() << 10);
            virtualSize[NOTCOVERED] += crtMap->size();
            topTenNotCovered.processElement(*crtMap);
            }
         }
      } // end for (iterate through maps)
   cout << dec << endl;
   cout << "Totals:       Virtual= " << setw(8) << (totalVirtSize >> 10) << " KB; RSS= " << setw(8) << (totalRssSize >> 10) << " KB\n";
   for (int i = 0; i < NUM_CATEGORIES; i++)
      {
      cout << setw(11) << RangeCategoryNames[i] << ":  Virtual= " << setw(8) << (virtualSize[i] >> 10) << " KB; RSS= " << setw(8) << (rssSize[i] >> 10) << " KB\n";
      }

   // Print explanation
   cout << endl;
   cout << "Unknown portion comes from maps that are partially covered by segments and callsites" << endl;
   cout << "'Not covered' are maps that are really not covered by any segment or callsite" << endl;

   //
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

   cout << "\nTop 10 maps partially covered\n";
   topTenPartiallyCovered.print();
   }



#ifdef WINDOWS_FOOTPRINT
typedef VmmapEntry MapEntry; // For Windows
#else
typedef SmapEntry MapEntry;  // For Linux
#endif

int main(int argc, char* argv[])
   {
   // Verify number of arguments
   if (argc < 2)
      error("Need at least one argument: smaps file, javacore and callsites are optional");
   
   // Read the smaps file
   vector<MapEntry> sMaps;
#ifdef WINDOWS_FOOTPRINT
   readVmmapFile(argv[1], sMaps);
#else
   readSmapsFile(argv[1], sMaps);  
#endif
   

   
   //===================== Javacore processing ============================
   vector<J9Segment> segments; // must not become out-of-scope until I am done with the maps
   vector<ThreadStack> threadStacks;
   if (argc >= 3)
      {
      // Read the javacore file
      readJavacore(argv[2], segments, threadStacks);
#ifdef DEBUG   
      // let's print all segments
      cout << "Print segments:\n";
      for (vector<J9Segment>::iterator it = segments.begin(); it != segments.end(); ++it)
         cout << *it << endl;
#endif       
      
      // Annotate maps with j9segments
      annotateMapWithSegments(sMaps, segments);
      annotateMapWithThreadStacks(sMaps, threadStacks);
      }
   
   //======================== Callsites processing =============================
   vector<CallSite> callSites; // must not become out-of-scope until I am done with the maps
   if (argc >= 4)
      {
      // Read the callsites file
      readCallSitesFile(argv[3], callSites);

      // Annotate the smaps file with callsites
      annotateMapWithSegments(sMaps, callSites);
      }
     
   // print results one by one
   for (vector<MapEntry>::const_iterator map = sMaps.begin(); map != sMaps.end(); ++map)
      {
      map->printEntryWithAnnotations();
      }

   printSpaceKBTakenByVmComponents(sMaps);
   return 0;
   }


