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
#ifndef _MEMENTRY_HPP__
#define _MEMENTRY_HPP__

#include <string>
#include <iostream>
#include <list>
#include <functional> // for binary predicates. Want to sort entries by size
#include <algorithm>
#include "AddrRange.hpp"
#include "Javacore.hpp"

class MemoryEntry
   {
   public:
      AddrRange            _addrRange;
      unsigned long long   _rss;
      std::string          _details;
      std::string          _protection;
      // The following two fields are collections of heterogenous objects derived from AddrRange
      // To be able to call the correct 'print' function based on the type of the object
      // we must store pointers 
      std::list<const AddrRange*> _coveringRanges;    // address ranges that are included of this smap;
      std::list<const AddrRange*> _overlappingRanges; // address ranges that overlap or are bigger than this smap
   public:
      MemoryEntry() { clear(); }
      virtual void clear()
         {
         _addrRange.clear();
         _details.clear();
         _rss = 0;
         _coveringRanges.clear();
         _overlappingRanges.clear();
         }
      const AddrRange& getAddrRange() const { return _addrRange; }
      void addCoveringRange(const AddrRange& seg);
      void addOverlappingRange(const AddrRange& seg) { _overlappingRanges.push_back(&seg); }
 
      const std::list<const AddrRange*> getCoveringRanges() const { return _coveringRanges; }
      const std::list<const AddrRange*> getOverlappingRanges() const { return _overlappingRanges; }
      unsigned long long sizeKB() const { return _addrRange.sizeKB(); } // !! result in KB
      unsigned long long size() const { return _addrRange.size(); }
      unsigned long long getResidentSize() const { return _rss; } // result in KB
      unsigned long long gapKB(const MemoryEntry& toOther) const { return _addrRange.gapKB(toOther.getAddrRange()); }
      const std::string& getDetailsString() const { return _details; }
      const std::string& getProtectionString() const { return _protection; }
      unsigned long long getStart() const { return _addrRange.getStart(); }
      unsigned long long getEnd() const { return _addrRange.getEnd(); }
      void setStart(unsigned long long a){ _addrRange.setStart(a); }
      void setEnd(unsigned long long a) { _addrRange.setEnd(a); }
      //bool includes(const AddrRange& other) const { return other._startAddr >= _startAddr && other._startAddr < _endAddr && other._endAddr <= _endAddr; }
      //bool disjoint(const AddrRange& other) const { return _endAddr <= other._startAddr || other._endAddr <= _startAddr; }
      bool operator <(const MemoryEntry& other) const { return this->getAddrRange() < other.getAddrRange(); }
      bool operator >(const MemoryEntry& other) const { return this->getAddrRange() > other.getAddrRange(); }
      virtual void printEntryWithAnnotations() const;
      friend std::ostream& operator<<(std::ostream& os, const MemoryEntry& ar);
   protected:
      virtual void print(std::ostream& os) const;
   };

inline std::ostream& operator<< (std::ostream& os, const MemoryEntry& me)
   {
   me.print(os);
   return os;
   }

// Define our binary function object class that will be used to order MemoryEntry by size
struct MemoryEntrySizeLessThan : public std::binary_function<MemoryEntry, MemoryEntry, bool>
   {
   bool operator() (const MemoryEntry& m1, const MemoryEntry& m2) const
      {
      return (m1.size() < m2.size());
      }
   };

struct MemoryEntryRssLessThan : public std::binary_function<MemoryEntry, MemoryEntry, bool>
   {
   bool operator() (const MemoryEntry& m1, const MemoryEntry& m2) const
      {
      return (m1.getResidentSize() < m2.getResidentSize());
      }
   };

#endif // _MEMENTRY_HPP__
