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
#include <iostream>
#include <iomanip>
#include <string>
#include "MemoryEntry.hpp"

using namespace std;

void MemoryEntry::addCoveringRange(const AddrRange& seg)
   {
   // insert based on start address
   std::list<const AddrRange*>::iterator s = _coveringRanges.begin();
   for (; s != _coveringRanges.end(); ++s)
      {
      if (seg == **s) // duplicate; segment and call site mapping to the same address
         return;
      if (seg > **s)
         break; // I found my insertion point
      }
   // We must insert before segment s
   _coveringRanges.insert(s, &seg);
   }

void MemoryEntry::print(std::ostream& os) const
   {
   os << std::hex << "Start=" << setfill('0') << setw(16) << getStart() <<
      " End=" << setfill('0') << setw(16) << getEnd() << std::dec <<
      " Size=" << setfill(' ') << setw(6) << sizeKB() << " rss=" << setfill(' ') << setw(6) << _rss << " Prot=" << getProtectionString();
   if (_details.length() > 0)
      os << " " << _details;
   }

// Print entry with annotations
void MemoryEntry::printEntryWithAnnotations() const
   {
   // Print the entry first
   cout << "MemEntry: " << *this << endl;
   // Check whether I need to print any covering segments/call-sites
   const list<const AddrRange*> coveringRanges = getCoveringRanges();
   if (coveringRanges.size() != 0)
      {
      cout << "\tCovering segments/call-sites:\n";
      // Go through the list of covering ranges
      for (list<const AddrRange*>::const_iterator range = coveringRanges.begin(); range != coveringRanges.end(); ++range)
         {
         cout << "\t\t" << **range << endl;
         }
      }
   const list<const AddrRange*> overlappingRanges = getOverlappingRanges();
   if (overlappingRanges.size() != 0)
      {
      cout << "\tOverlapping segments/call-sites:\n";
      for (list<const AddrRange*>::const_iterator range = overlappingRanges.begin(); range != overlappingRanges.end(); ++range)
         {
         cout << "\t\t" << **range << endl;
         }
      }
   }

