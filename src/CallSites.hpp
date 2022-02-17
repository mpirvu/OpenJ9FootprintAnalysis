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
#ifndef _CALLSITE_HPP__
#define _CALLSITE_HPP__
#include "AddrRange.hpp"

class CallSite : public AddrRange
   {
   std::string        _filename;
   unsigned           _lineNo;
   public:
      CallSite(unsigned long long startAddr, unsigned long long endAddr, const std::string& filename, int lineNo) :
         AddrRange(startAddr, endAddr), _filename(filename), _lineNo(lineNo){}
      virtual void clear()
         {
         AddrRange::clear();
         _filename.clear();
         _lineNo = 0;
         }
      virtual int rangeType() const { return CALLSITE_RANGE; }
   protected:
      virtual void print(std::ostream& os) const;
   }; //  AddrRange

void readCallSitesFile(const char *filename, std::vector<CallSite>& callSites);


#endif // _CALLSITE_HPP__