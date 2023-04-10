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
#include <iomanip>
#include <fstream>
#include <regex>
#include "Util.hpp"
#include "Javacore.hpp"

using namespace std;

void J9Segment::print(std::ostream& os) const
   {
   os << std::hex << getTypeName() << " ID=" << setfill('0') << setw(16) << _id <<
      " Start=" << setfill('0') << setw(16) << getStart() << " End=" << setfill('0') << setw(16) << getEnd() <<
      " size=" << setfill(' ') << std::dec << setw(5) << sizeKB() << " KB flags=" << std::hex << setfill('0') << setw(8) << getFlags();
   }

const char * J9Segment::_segmentTypes[] = { "UNKNOWN", "HEAP", "INTERNAL", "CLASS", "CODECACHE", "DATACACHE" };

// Determine the segment type from the line in the javacore
// samples:
// 1STSEGTYPE     Internal Memory
// 1STSEGTYPE     Class Memory
// 1STSEGTYPE     JIT Code Cache
// 1STSEGTYPE     JIT Data Cache
// 1STHEAPTYPE    Object Memory
J9Segment::SegmentType determineSegmentType(const string& line)
   {
   vector<string> tokens;
   tokenize(line, tokens);

   if (tokens.size() >= 3)
      {
      if (tokens[1] == "JIT")
         {
         if (tokens[2] == "Code")
            return J9Segment::CODECACHE;
         if (tokens[2] == "Data")
            return J9Segment::DATACACHE;
         }
      else if (tokens[1] == "Internal" && tokens[2] == "Memory")
         {
         return J9Segment::INTERNAL;
         }
      else if (tokens[1] == "Class" && tokens[2] == "Memory")
         {
         return J9Segment::CLASS;
         }
      }
   return J9Segment::UNKNOWN;
   }

void ThreadStack::print(std::ostream& os) const
   {
   os << " ThreadName=" << setfill(' ') << setw(16) << _threadName << std::hex <<
      " Start=" << setfill('0') << setw(16) << getStart() << " End=" << setfill('0') << setw(16) << getEnd() <<
      " size=" << setfill(' ') << std::dec << setw(5) << sizeKB() << " KB";
   }

void javacoreParseStack(ifstream& myfile, int& lineNo, vector<ThreadStack>& threadStacks)
   {
   string line;
   bool foundThreadDetailsSection = false;
   while (myfile.good() && !foundThreadDetailsSection)
      {
      // Search for 1XMTHDINFO     Thread Details
      getline(myfile, line);
      lineNo++;
      if (line.find("1XMTHDINFO     Thread Details") != std::string::npos)
         foundThreadDetailsSection = true;
      }
   if (!foundThreadDetailsSection)
      {
      cerr << "WARNING: thread section was not found in the javacore\n";
      return;
      }
   string threadName;

   const size_t hdrLen = strlen("3XMTHREADINFO ");

   while (myfile.good())
      {
      getline(myfile, line);
      lineNo++;
      if (line.find("1XMTHDSUMMARY  Threads CPU Usage Summary") != std::string::npos)
         return; // found the end of the thread section

      // If the line starts with "3XMTHREADINFO " then it contains the thread name
      if (line.compare(0, hdrLen, "3XMTHREADINFO ") == 0)
         {
         // 3XMTHREADINFO      "main" J9VMThread:0x00000000022D7700, omrthread_t:0x00007F17B00078D0, java/lang/Thread:0x00000000F0039278, state:CW, prio=5
         // 3XMTHREADINFO      Anonymous native thread
         // 3XMTHREADINFO      "GC Slave" J9VMThread:0x00000000023B9300, omrthread_t:0x00007F17B04A3FB8, java/lang/Thread:0x00000000F004C628, state:R, prio=5
         // 3XMTHREADINFO      "JIT Compilation Thread-000" J9VMThread:0x00000000022DB300, omrthread_t:0x00007F17B01B6720, java/lang/Thread:0x00000000F0042B78, state:R, prio=10
         if (line.find("Anonymous native thread") != std::string::npos)
            {
            threadName.assign("Anonymous");
            }
         else
            {
            size_t start = line.find_first_of('"', hdrLen);
            if (start != std::string::npos)
               {
               size_t end = line.find_first_of('"', start + 1);
               if (end != std::string::npos)
                  {
                  threadName.assign(line.substr(start, end - start + 1));
                  }
               }
            }
         }
      else
         {
         // Search for "3XMTHREADINFO2            (native stack address range from:0x00007F17035D8000, to:0x00007F1703619000, size:0x41000)"
         std::cmatch result;
         std::regex  pattern("3XMTHREADINFO2\\s+\\(native stack address range from:0x([0-9A-F]+), to:0x([0-9A-F]+), size:0x([0-9A-F]+)");
         if (std::regex_search(line.c_str(), result, pattern))
            {
            unsigned long long startAddr = hex2ull(result[1]);
            unsigned long long endAddr = hex2ull(result[2]);
            unsigned long long blockSize = hex2ull(result[3]);
            if (endAddr - startAddr != blockSize)
               cerr << "Error for thread stack size in line " << lineNo << endl;
            threadStacks.push_back(ThreadStack(startAddr, endAddr, threadName));
            }
         }
      }
   }

/**
 * Read the javacore file and extract the memory segments and thread stacks
 * The output is stored in the segments and threadStacks vectors
*/
void readJavacore(const char * javacoreFilename, vector<J9Segment>& segments, vector<ThreadStack>& threadStacks)
   {
   cout << "Reading javacore file: " << string(javacoreFilename) << endl;
   // Open the file
   ifstream myfile(javacoreFilename);

   // check if successfull
   if (!myfile.is_open())
      {
      cerr << "Cannot open " << javacoreFilename << endl;
      exit(-1);
      }

   // read each line from file
   string line;
   int lineNo = 0;
   bool memInfoFound = false;
   J9Segment::SegmentType segmentType = J9Segment::UNKNOWN;
   while (myfile.good())
      {
      getline(myfile, line);
      lineNo++;

      if (!memInfoFound)
         {
         // search for:0SECTION       MEMINFO subcomponent dump routine
         if (line.find("0SECTION       MEMINFO subcomponent dump routine") != std::string::npos)
            {
            memInfoFound = true;
            }
         }
      else // process segments
         {
         if (line.find("1STHEAPTYPE", 0) != std::string::npos)
            {
            segmentType = J9Segment::HEAP;
            }
         else if (line.find("1STHEAPREGION", 0) != std::string::npos ||
                  line.find("1STHEAPSPACE", 0) != std::string::npos)
            {
            // 1STHEAPSPACE   0x00007FD4F4151E00         --                 --                 --         Generational
            // 1STHEAPREGION  0x00007FD4F41522F0 0x00000000F0000000 0x00000000F2400000 0x0000000002400000 Generational/Tenured Region
            // 1STHEAPREGION  0x00007FD4F41520E0 0x00000000FDBA0000 0x00000000FDF50000 0x00000000003B0000 Generational/Nursery Region
            // 1STHEAPREGION  0x00007FD4F4151ED0 0x00000000FDF50000 0x0000000100000000 0x00000000020B0000 Generational/Nursery Region
            // or
            // 1STHEAPSPACE   0x000002302F2E94A0 0x00000000BFF80000 0x00000000FFF80000 0x0000000040000000 Flat
            vector<string> tokens;
            tokenize(line, tokens);
            if (tokens.size() < 6)
               {
               cerr << "Have found " << tokens.size() << " instead of 6-7 at line " << lineNo << endl; exit(-1);
               }
            if (tokens[0] == "1STHEAPSPACE" && tokens[5] == "Generational")
               {
               // Skip this line because it has no address information
               continue;
               }
            unsigned long long id = hex2ull(tokens[1]);
            unsigned long long startAddr = hex2ull(tokens[2]);
            unsigned long long endAddr = hex2ull(tokens[3]);
            if (id == HEX_CONVERT_ERROR || startAddr == HEX_CONVERT_ERROR || endAddr == HEX_CONVERT_ERROR)
               {
               cerr << "HEX_CONVERT_ERROR in javacore at line:" << lineNo << " : " << line << std::endl;
               exit(-1);
               }
            segments.push_back(J9Segment(id, startAddr, endAddr, segmentType, 0));
            }
         else if (line.find("1STSEGTYPE", 0) != std::string::npos)
            {
            segmentType = determineSegmentType(line);
            if (segmentType == J9Segment::UNKNOWN)
               {
               cerr << "Unknown segment type at line " << lineNo << endl;  exit(-1);
               }
            }
         else if (line.find("1STSEGMENT", 0) != std::string::npos)
            {
            // Process one segment
            //NULL           segment            start              alloc              end                type       size
            //1STSEGMENT     0x00007FBE13B616C0 0x00007FBE07CFB030 0x00007FBE07EF7EA0 0x00007FBE07EFB030 0x00000048 0x0000000000200000
            // Scratch segment
            //1STSEGMENT     0x000002304491E730 0x00007FF68A7C0000 0x00007FF68B260000 0x00007FF68B7C0000 0x01000440 0x0000000001000000
            // JIT Persistent Memory Segment
            // 1STSEGMENT     0x000002304491E668 0x0000023048623060 0x00000230486953D0 0x0000023048723060 0x00800040 0x0000000000100000

            vector<string> tokens;
            tokenize(line, tokens);
            if (tokens.size() != 7)
               {
               cerr << "Have found " << tokens.size() << " instead of 7 at line " << lineNo << endl; exit(-1);
               }
            unsigned long long id        = hex2ull(tokens[1]);
            unsigned long long startAddr = hex2ull(tokens[2]);
            unsigned long long endAddr   = hex2ull(tokens[4]);
            if (id == HEX_CONVERT_ERROR || startAddr == HEX_CONVERT_ERROR || endAddr == HEX_CONVERT_ERROR)
               {
               cerr << "HEX_CONVERT_ERROR in javacore at line:" << lineNo << " : " << line << std::endl; exit(-1);
               }
            unsigned flags = strtoul(tokens[5].c_str(), NULL, 16);
            segments.push_back(J9Segment(id, startAddr, endAddr, segmentType, flags));
            }
         else if (line.find("1STGCHTYPE", 0) != std::string::npos)
            {
            // Stop when reaching GC history
            break;
            }
         }
      } // end while
   javacoreParseStack(myfile, lineNo, threadStacks);
   myfile.close();
   cout << "Reading of segments from javacore file finished\n";
   }

