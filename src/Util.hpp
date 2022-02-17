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
#ifndef _UTIL_HPP__
#define _UTIL_HPP__
#include <string>
#include <vector>
#include <list>
#include <iostream>

static const unsigned long long HEX_CONVERT_ERROR = 0xffffffffffffffff;
static const unsigned long long INT_CONVERT_ERROR = 0xffffffffffffffff;

void error(const char * msg);
void tokenize(const std::string& str, std::vector<std::string>& tokens, const char* delim = " \t\n");
unsigned long long hex2ull(const std::string& hexNumber);
unsigned long long a2ull(const std::string& decimalNumber);

template<typename T, typename C>
class TopTen
   {
   std::list<T> _sortedList;
   C _comparator;
public:
   TopTen() {}
   void processElement(const T& newElem)
      {
      if (_sortedList.size() == 10)
         {
         if (_comparator(_sortedList.back(), newElem)) // need some specialized comparison
            {
            // Take the last element out
            _sortedList.pop_back();
            }
         }
      if (_sortedList.size() < 10)
         {
         // Insert new element
         bool inserted = false;
         for (typename std::list<T>::iterator it = _sortedList.begin(); it != _sortedList.end(); ++it)
            {
            // Must go past the 
            if (_comparator(*it, newElem))
               {
               _sortedList.insert(it, newElem);
               inserted = true;
               break;
               }
            } // end for
         if (!inserted)
            {
            _sortedList.push_back(newElem);
            }
         }
      } // processElement

   void print()
      {
      std::cout << "Top ten:\n";
      for (typename std::list<T>::const_iterator it = _sortedList.cbegin(); it != _sortedList.cend(); ++it)
         std::cout << *it << std::endl;
      }
   };
#endif