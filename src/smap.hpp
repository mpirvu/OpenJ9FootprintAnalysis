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
#ifndef _SMAP_HPP__
#define _SMAP_HPP__
#include <iostream>
#include <vector>
#include "MemoryEntry.hpp"
/*
7fffbbdff000-7fffbbe00000 r-xp 00000000 00:00 0                          [vdso]
Size:                  4 kB
Rss:                   4 kB
Pss:                   0 kB
Shared_Clean:          4 kB
Shared_Dirty:          0 kB
Private_Clean:         0 kB
Private_Dirty:         0 kB
Referenced:            4 kB
Swap:                  0 kB
KernelPageSize:        4 kB
MMUPageSize:           4 kB
*/
class SmapEntry : public MemoryEntry
   {
   public:
      int _pss;
      int _sharedClean;
      int _sharedDirty;
      int _privateClean;
      int _privateDirty;
      int _swap;
      int _kernelPageSize;
      int _mmuPageSize;
   public:
      SmapEntry() { clear(); }
      virtual void clear()
         {
         MemoryEntry::clear();
         _pss = 0; _sharedClean = 0; _sharedDirty = 0;
         _privateClean = 0; _privateDirty = 0; _swap = 0; _kernelPageSize = 0; _mmuPageSize = 0;
         }
      bool isMapForSharedLibrary() const;
      bool isMapForThreadStack() const;
   protected:
      virtual void print(std::ostream& os) const;
   private:
      inline static bool endsWith(const std::string & str, const std::string & suffix)
         {
         return suffix.size() <= str.size() && std::equal(suffix.rbegin(), suffix.rend(), str.rbegin());
         }
   };


void readSmapsFile(const char *smapsFilename, std::vector<SmapEntry>& smaps);
void readMapsFile(const char *smapsFilename, std::vector<SmapEntry>& smaps);
void printLargestUnallocatedBlocks(const std::vector<SmapEntry> &smaps);
unsigned long long printSpaceKBTakenBySharedLibraries(const std::vector<SmapEntry> &smaps);
unsigned long long computeReservedSpaceKB(const std::vector<SmapEntry> &smaps);
void printTopTenReservedSpaceKB(const std::vector<SmapEntry> &smaps);
#endif // _SMAP_HPP__