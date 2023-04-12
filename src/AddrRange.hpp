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
#ifndef _ADDRRANGE_HPP__
#define _ADDRRANGE_HPP__
#include <iostream>
#include <iomanip>
#include <functional> // for binary predicates. Want to sort entries by size
#include <algorithm>

class AddrRange
   {
   public:
   enum RangeCategories
      {
      JAVAHEAP = 0,
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
      }; // enum RangeCategories
   static constexpr const char* const RangeCategoryNames[NUM_CATEGORIES] = { "GC heap", "CodeCache", "DataCache", "DLL", "Stack", "SCC", "JITScratch", "JITPersist", "Internal", "Classes", "CallSites", "Unknown", "Not covered" };
   static_assert(NUM_CATEGORIES == sizeof(RangeCategoryNames)/sizeof(RangeCategoryNames[0]), "RangeCategoryNames array size mismatch");

   private:
   unsigned long long _startAddr;
   unsigned long long _endAddr;
   unsigned long long _rss = 0;
   public:
      AddrRange() : _startAddr(0), _endAddr(0), _rss(0) {}
      AddrRange(unsigned long long start, unsigned long long end, unsigned long long _rss) : _startAddr(start), _endAddr(end), _rss(_rss)
         {
         if (end <= start && !(start == 0 && end == 0))
            {
            std::cerr << std::hex << "Range error: start=" << start << " end=" << end << std::endl;
            }
         }
      unsigned long long getStart() const { return _startAddr; }
      unsigned long long getEnd() const { return _endAddr; }
      unsigned long long getRSS() const { return _rss; }
      void setStart(unsigned long long a){ _startAddr = a; }
      void setEnd(unsigned long long a) { _endAddr = a; }
      void setRSS(unsigned long long rss) { _rss = rss; }
      virtual RangeCategories getRangeCategory() const { return UNKNOWN; }
      virtual void clear() { _startAddr = _endAddr = 0; _rss = 0; }
      bool includes(const AddrRange& other) const { return other._startAddr >= _startAddr && other._endAddr <= _endAddr; }
      bool disjoint(const AddrRange& other) const { return _endAddr <= other._startAddr || other._endAddr <= _startAddr; }
      unsigned long long size() const { return _endAddr - _startAddr; }
      // Measure the size (KB) between the end of this segment and the beginning of the next (toOther)
      // The two segments must be disjoint
      unsigned long long gapKB(const AddrRange& toOther) const { return (toOther._startAddr - _endAddr) >> 10; }
      unsigned long long sizeKB() const { return (_endAddr - _startAddr) >> 10; }
      bool operator <(const AddrRange& other) const { return this->getStart() < other.getStart(); }
      bool operator >(const AddrRange& other) const { return this->getStart() > other.getStart(); }
      virtual bool operator == (const AddrRange& other) const { return this->getStart() == other.getStart() && this->getEnd() == other.getEnd(); }
      friend std::ostream& operator<<(std::ostream& os, const AddrRange& ar);
      enum { SIMPLE_RANGE = 0, CALLSITE_RANGE, J9SEGMENT_RANGE, THREADSTACK_RANGE };
      virtual int rangeType() const { return SIMPLE_RANGE; }

   protected:
      virtual void print(std::ostream& os) const
         {
         os << std::hex << "Start=" << std::setfill('0') << std::setw(16) << getStart() <<
            " End=" << std::setfill('0') << std::setw(16) << getEnd() << std::dec <<
            " Size=" << std::setfill(' ') << std::setw(6) << sizeKB();
         }
   }; //  AddrRange

inline std::ostream& operator<< (std::ostream& os, const AddrRange& ar)
   {
   ar.print(os);
   return os;
   }

// Define our binary function object class that will be used to order AddrRange by size
struct AddrRangeSizeLessThan : public std::binary_function<AddrRange, AddrRange, bool>
   {
   bool operator() (const AddrRange& m1, const AddrRange& m2) const
      {
      return (m1.size() < m2.size());
      }
   };


#endif // _ADDRRANGE_HPP__