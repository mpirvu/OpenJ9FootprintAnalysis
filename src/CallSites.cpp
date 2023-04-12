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
#include <string>
#include <iostream>
#include <fstream>
#include <iomanip>
#include <regex>
#include "CallSites.hpp"
#include "Util.hpp"
#include "PageMapSupport.hpp"

using namespace std;

/*
 !j9x 0x004FA4C0,0x000001D4	LargeObjectAllocateStats.cpp:31
 !j9x 0x004FA6D0,0x000001D4	LargeObjectAllocateStats.cpp:39
 !j9x 0x004FA8E0,0x000001E8	LargeObjectAllocateStats.cpp:45
 !j9x 0x004FAB00,0x000000A4	TLHAllocationInterface.cpp:53
*/
void readCallSitesFile(const char *filename, vector<CallSite>& callSites, PageMapReader *pageMapReader)
   {
   cout << "\nReading callSites file: " << string(filename) << endl;
   // Open the file
   ifstream myfile(filename);
   // check if successfull
   if (!myfile.is_open())
      {
      cerr << "Cannot open " << filename << endl;
      exit(-1);
      }
   string line;
   unsigned long long totalSize = 0;
   while (myfile.good())
      {
      getline(myfile, line);
      // skip empty lines
      size_t pos;
      if ((pos = line.find_first_not_of(" \t\n")) == string::npos)
         continue;
      // Skip lines that do not start with "!j9x"
      if (line.find("!j9x", pos) == string::npos)
         continue;

      std::cmatch result;       //!j9x 0xstart,0xsize	               filename:lineNo
      std::regex  pattern1("\\s*\\!j9x 0x([0-9A-F]+),0x([0-9A-F]+)\\s+(\\S+):(\\d+)");
      std::regex  pattern2("\\s*\\!j9x 0x([0-9A-F]+),0x([0-9A-F]+)\\s+(\\S+)");
      bool match1, match2;
      if ((match1 = std::regex_search(line.c_str(), result, pattern1)) ||
          (match2 = std::regex_search(line.c_str(), result, pattern2)))
         {
         unsigned long long startAddr = hex2ull(result[1]);
         unsigned long long blockSize = hex2ull(result[2]);
         unsigned long long endAddr = startAddr + blockSize;
         unsigned lineNo = match1 ? (unsigned)a2ull(result[4]) : 0;
         //cerr << "Match found: start=" << hex << startAddr << " blockSize=" << blockSize << " " << result[3] << ":" << lineNo << endl;
         unsigned long long rss = pageMapReader ? pageMapReader->computeRssForAddrRange(startAddr, endAddr) : 0;
         callSites.push_back(CallSite(startAddr, startAddr+blockSize, result[3], lineNo, rss));
         totalSize += blockSize;
         }
      else // try another pattern
         {
         cerr << "No match for:" << line << endl;
         exit(-1);
         }
      }
   myfile.close();
   cout << "Total size of call sites: " << (totalSize >> 10) << " KB"<< endl;
   }


void CallSite::print(std::ostream& os) const
   {
   os << hex << "Start=" << setfill('0') << setw(16) << getStart() <<
      " End=" << setfill('0') << setw(16) << getEnd() << dec <<
      " Size=" << setfill(' ') << std::dec << setw(5) << sizeKB()  << " KB @" << _filename << ":" << _lineNo;
   }
