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
#ifndef _J9_SEGMENT_HPP__
#define _J9_SEGMENT_HPP__
#include <iostream>
#include <vector>
#include <string>
#include "AddrRange.hpp"



class J9Segment : public  AddrRange
   {
   public:
#define MEMORY_TYPE_JIT_SCRATCH_SPACE  0x1000000
#define MEMORY_TYPE_JIT_PERSISTENT      0x800000
#define MEMORY_TYPE_VIRTUAL  0x400

      enum SegmentType
         {
         UNKNOWN = 0,
         HEAP,
         INTERNAL,
         CLASS,
         CODECACHE,
         DATACACHE
         };
   private:
      unsigned long long _id;
      SegmentType        _type;
      unsigned           _flags;
      static const char *_segmentTypes[];
   public:
      J9Segment(unsigned long long id, unsigned long long start, unsigned long long end, SegmentType segType, unsigned flags) :
         AddrRange(start, end),  _id(id), _type(segType), _flags(flags) {}
      const char *getTypeName() const { return _segmentTypes[_type]; }
      SegmentType getSegmentType() const { return _type; }
      unsigned getFlags() const { return _flags; }
      virtual void clear()
         {
         AddrRange::clear();
         _id = 0;
         _type = UNKNOWN;
         _flags = 0;
         }
      virtual int rangeType() const { return J9SEGMENT_RANGE; }
      bool isJITScratch() const { return _type == INTERNAL && (_flags & MEMORY_TYPE_JIT_SCRATCH_SPACE); }
      bool isJITPersistent() const { return _type == INTERNAL && (_flags & MEMORY_TYPE_JIT_PERSISTENT); }
   protected:
      virtual void print(std::ostream& os) const;
   }; // J9Segment


class ThreadStack : public  AddrRange
   {
   private:
      std::string _threadName;

   public:
      ThreadStack(unsigned long long start, unsigned long long end, const std::string& threadName) : AddrRange(start, end), _threadName(threadName) {}
      const std::string& getThreadName() const { return _threadName; }
      virtual void clear()
         {
         AddrRange::clear();
         _threadName.clear();
         }
      virtual int rangeType() const { return THREADSTACK_RANGE; }
   protected:
      virtual void print(std::ostream& os) const;
   }; // J9Segment

J9Segment::SegmentType determineSegmentType(const std::string& line);
void readJavacore(const char * javacoreFilename, std::vector<J9Segment>& segments, std::vector<ThreadStack>& threadStacks);


#endif // _J9_SEGMENT_HPP__