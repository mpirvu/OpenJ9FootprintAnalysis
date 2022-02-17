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
#ifndef _VMMAP_HPP__
#define _VMMAP_HPP__
#include <iostream>
#include <vector>
#include "MemoryEntry.hpp"
/*
"Address","Type","Size","Committed","Private","Total WS","Private WS","Shareable WS","Shared WS","Locked WS","Blocks","Protection","Details",
"0000000000970000","Heap (Private Data)","1,024","1,020","1,020","1,004","1,004","","","","2","Read/Write","Heap ID: 1 [LOW FRAGMENTATION]"
"  0000000000970000","Heap (Private Data)","1,020","1,020","1,020","1,004","1,004","","","","","Read/Write","Heap ID: 1 [LOW FRAGMENTATION]"
"  0000000000A6F000","Heap (Private Data)","4","","","","","","","","","Reserved","Heap ID: 1 [LOW FRAGMENTATION]"
"0000000000AC0000","Shareable","8","8","","8","","8","8","","1","Read",""
"  0000000000AC0000","Shareable","8","8","","8","","8","8","","","Read",""
"00000000149B0000","Thread Stack","1,024","268","268","12","12","","","","3","Read/Write/Guard","Thread ID: 2508"
"  00000000149B0000","Thread Stack","756","","","","","","","","","Reserved",""
"  0000000014A6D000","Thread Stack","12","12","12","","","","","","","Read/Write/Guard",""
"  0000000014A70000","Thread Stack","256","256","256","12","12","","","","","Read/Write",""
"0000000015BD0000","Heap (Private Data)","16,192","1,596","1,596","748","748","","","","10","Read/Write","Heap ID: 1 [LOW FRAGMENTATION]"
"  0000000015BD0000","Heap (Private Data)","68","68","68","68","68","","","","","Read/Write","Heap ID: 1 [LOW FRAGMENTATION]"
"  0000000015BE1000","Heap (Private Data)","60","","","","","","","","","Reserved","Heap ID: 1 [LOW FRAGMENTATION]"
"  0000000015BF0000","Heap (Private Data)","324","324","324","224","224","","","","","Read/Write","Heap ID: 1 [LOW FRAGMENTATION]"
"  0000000015C41000","Heap (Private Data)","4","4","4","","","","","","","No access","Heap ID: 1 [LOW FRAGMENTATION]"
"  0000000015C42000","Heap (Private Data)","1,128","1,128","1,128","384","384","","","","","Read/Write","Heap ID: 1 [LOW FRAGMENTATION]"
"  0000000015D5C000","Heap (Private Data)","508","","","","","","","","","Reserved","Heap ID: 1 [LOW FRAGMENTATION]"
"  0000000015DDB000","Heap (Private Data)","68","68","68","68","68","","","","","Read/Write","Heap ID: 1 [LOW FRAGMENTATION]"
"  0000000015DEC000","Heap (Private Data)","484","","","","","","","","","Reserved","Heap ID: 1 [LOW FRAGMENTATION]"
"  0000000015E65000","Heap (Private Data)","4","4","4","4","4","","","","","Read/Write","Heap ID: 1 [LOW FRAGMENTATION]"
"  0000000015E66000","Heap (Private Data)","13,544","","","","","","","","","Reserved","Heap ID: 1 [LOW FRAGMENTATION]"
*/
class VmmapEntry : public MemoryEntry
   {
   public:
      unsigned long long _committed;
      unsigned long long _privateWS;  
      unsigned long long _shareableWS;
      unsigned long long _sharedWS;
      unsigned long long _lockedWS;
      unsigned long long _numBlocks;
      std::string _type; // "Heap (Private Data)" "Shareable" "Thread Stack"

   public:
      VmmapEntry() { clear(); }
      virtual void clear()
         {
         MemoryEntry::clear();
         _committed = _privateWS = _shareableWS = _sharedWS = _lockedWS = _numBlocks = 0;
         _type.clear();
         }
      const std::string& getTypeString() const { return _type; }
      bool isMapForSharedLibrary() const { return (getTypeString().find("Image") == 0); } // The type must start with Image
      bool isMapForThreadStack() const { return getTypeString().find("Thread Stack") == 0; } // Must start with Thread Stack}
   protected:
      virtual void print(std::ostream& os) const;
   }; // VmmapEntry




void readVmmapFile(const char *vmmapFilename, std::vector<VmmapEntry>& vmmap);
//void printLargestUnallocatedBlocks(const std::vector<SmapEntry> &smaps);
//unsigned long long computeReservedSpaceKB(const std::vector<SmapEntry> &smaps);
//void printTopTenReservedSpaceKB(const std::vector<SmapEntry> &smaps);

#endif // _VMMAP_HPP__