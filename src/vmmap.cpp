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
#include <regex>
#include <iostream>
#include <fstream>
#include <iomanip>
#include <cassert>
//#include <ctype> // isdigit
#include "vmmap.hpp"
#include "CallSites.hpp"
#include "Util.hpp"

using namespace std;
//#define DEBUG

bool tokenizeVmmapLine(const string& line, vector<string>& tokens)
   {
   size_t lastPos = 0;
   size_t lineSize = line.size();
   while(lastPos < lineSize)
      {
      // Find the starting quote
      size_t startOfToken = line.find_first_of('"', lastPos);
      if (startOfToken == string::npos)
         {
         cerr << "Cannot find starting quote" << endl;
         return false;
         }
      startOfToken++; // move past the quote
      // Find the ending quote
      size_t endOfToken = line.find_first_of('"', startOfToken);
      if (endOfToken == string::npos)
         {
         cerr << "Cannot find ending quote while searching from position" << startOfToken << endl;
         return false;
         }
      // Extract token
      tokens.push_back(line.substr(startOfToken, endOfToken - startOfToken));
      // Move past quote and comma
      lastPos = endOfToken + 2;
      }
   return true;
   }

// This is used for vmmap files in text format where fields start at fixed positions
// The positions of these fields can be given by the postion of the headings
// We use an array of these postions as input
bool tokenizeVmmapTextLine(const string& line, vector<string>& tokens, const vector<size_t>& fieldPositions)
   {
   size_t lineSize = line.size();
#ifdef DEBUG
   cout << "tokenizeVmmapTextLine(" << lineSize << "):" << line << endl;
#endif

   size_t numFields = fieldPositions.size();
   for (size_t fieldNo = 0; fieldNo < numFields-1; fieldNo++) // going up to numFields-1 because the last one is a dummy one
      {
#ifdef DEBUG
      cout << "fieldNo=" << fieldNo << endl;
#endif
      size_t startPosInLine = fieldPositions.at(fieldNo);
      size_t endPosInLine = fieldPositions.at(fieldNo + 1); // No overflow due to how fieldPositions is constructed (one extra element which could be npos)
#ifdef DEBUG
      cout << "  startPosInLine=" << startPosInLine << endl;
      cout << "  endPosInLine=" << endPosInLine << endl;
#endif
      // The last field (Description) could be completely missing
      if (startPosInLine > lineSize)
         {
         tokens.push_back("");
#ifdef DEBUG
         cout << "    Field=" << "" << endl;
#endif
         continue;
         }

      // Read field by extracting substring bewteen startPos and endPos
      // and strip leading and trailing spaces
      string token = line.substr(startPosInLine, endPosInLine - startPosInLine);
#ifdef DEBUG
      cout << "    Field(" << token.size() << ")=" << token << endl;
#endif

      size_t startToken = token.find_first_not_of(" \t\n\r");
      if (startToken != string::npos) // if my string has some non-white chars
         {
         size_t endToken = token.find_last_not_of(" \t\n\r");
         //assert(endToken != string::npos);
         tokens.push_back(token.substr(startToken, endToken - startToken + 1));
         }
      else
         {
#ifdef DEBUG
         cout << "Pushing empty string\n";
#endif
         tokens.push_back("");
         }
      }
   // TODO should return false, if the line is too short or too long than assumed
#ifdef DEBUG
   cout << "tokenizeVmmapTextLine ending" << line << endl;
#endif
   return true;
   }

void readVmmapTextFile(const char *vmmapFilename, vector<VmmapEntry>& vmmaps)
   {
   unsigned long long virtSize = 0;
   unsigned long long rssSize = 0;
   cout << "Reading map file: " << string(vmmapFilename) << endl;
   // Open the file
   ifstream myfile(vmmapFilename);
   // check if successfull
   if (!myfile.is_open())
      {
      cerr << "Cannot open " << vmmapFilename << endl;
      exit(-1);
      }
   // First we need to get to the header that looks like this
   // "Address","Type","Size","Committed","Private","Total WS","Private WS","Shareable WS","Shared WS","Locked WS","Blocks","Protection","Details",
   //Address    Type                  Size        Committed  Private    Total WS   Private WS  Shareable WS  Shared WS  Locked WS  Blocks  Protection           Details
   int lineNo = 0;
   string line;
   bool headerFound = false;
   vector<size_t> fieldPositions;
   while (myfile.good())
      {
      getline(myfile, line); lineNo++;
#ifdef DEBUG
      cout << "readVmmapFile looking at :" << line << endl;
#endif
      size_t startPos = line.find("Address");

      if (std::string::npos != startPos)
         {
         fieldPositions.push_back(startPos);
         // Now find all the other fields memorizing their start position
         startPos = line.find("Type");
         if (std::string::npos != startPos)
            fieldPositions.push_back(startPos);
         else
            break; // will signal error

         startPos = line.find("Size");
         if (std::string::npos != startPos)
            fieldPositions.push_back(startPos);
         else
            break; // will signal error

         startPos = line.find("Committed");
         if (std::string::npos != startPos)
            fieldPositions.push_back(startPos);
         else
            break; // will signal error

         startPos = line.find("Private");
         if (std::string::npos != startPos)
            fieldPositions.push_back(startPos);
         else
            break; // will signal error

         startPos = line.find("Total WS");
         if (std::string::npos != startPos)
            fieldPositions.push_back(startPos);
         else
            break; // will signal error

         startPos = line.find("Private WS");
         if (std::string::npos != startPos)
            fieldPositions.push_back(startPos);
         else
            break; // will signal error

         startPos = line.find("Shareable WS");
         if (std::string::npos != startPos)
            fieldPositions.push_back(startPos);
         else
            break; // will signal error

         startPos = line.find("Shared WS");
         if (std::string::npos != startPos)
            fieldPositions.push_back(startPos);
         else
            break; // will signal error

         startPos = line.find("Locked WS");
         if (std::string::npos != startPos)
            fieldPositions.push_back(startPos);
         else
            break; // will signal error

         startPos = line.find("Blocks");
         if (std::string::npos != startPos)
            fieldPositions.push_back(startPos);
         else
            break; // will signal error

         startPos = line.find("Protection");
         if (std::string::npos != startPos)
            fieldPositions.push_back(startPos);
         else
            break; // will signal error

         startPos = line.find("Details");
         if (std::string::npos != startPos)
            fieldPositions.push_back(startPos);
         else
            break; // will signal error

         //fieldPositions.push_back(line.find_last_not_of(" \t\n\r")); // could be npos
         fieldPositions.push_back(string::npos);

         headerFound = true;
         break;
         }
      }
   if (!headerFound)
      {
      cerr << "Cannot find header line in expected format\n" << endl;
      exit(-1);
      }

   // Now read all entries
   VmmapEntry entry;
   while (myfile.good())
      {
      getline(myfile, line); lineNo++;
      // skip empty lines or lines that do not start with some character (subblock start with ampty space)
      if (line.find_first_not_of(" \t\n\r") != 0)
         continue;

#ifdef DEBUG
      cout << "readVmmapEntry looking at line " << lineNo << " with " << line.size() << " characters: " << line << endl;
#endif

      std::vector<std::string> tokens; // we should do tokens(13)

      if (!tokenizeVmmapTextLine(line, tokens, fieldPositions))
         exit(-1); // some error occurred
      // We must find exactly 13 items
      if (tokens.size() != 13)
         {
         cerr << "Must find exactly 13 tokens for vmmap line: " << line << endl;
         exit(-1);
         }

      // If the start address starts with a number it's a main entry, otherwise it's a subblock
      if (tokens[0].at(0) == ' ')
         {
         // sub-block
         continue;
         }
      // TODO: process sub-blocks as well

      unsigned long long start = hex2ull(tokens[0]);
      if (start == HEX_CONVERT_ERROR)
         {
         cerr << "Error with start address on line: " << line << endl;
         exit(-1);
         }
      else
         {
#ifdef DEBUG
         cout << "Tokens: ";
         for (vector<string>::iterator it = tokens.begin(); it != tokens.end(); ++it)
            {
            cout << *it << " ";
            }
         cout << endl;
#endif
         }
      // "Address","Type","Size","Committed","Private","Total WS","Private WS","Shareable WS","Shared WS","Locked WS","Blocks","Protection","Details",
      entry.clear();
      entry.setStart(start);
      entry._type = tokens[1];
      unsigned long long size = a2ull(tokens[2]);
      entry.setEnd(start + (size << 10));
      entry._committed = a2ull(tokens[3]);
      entry._rss = a2ull(tokens[5]);
      entry._privateWS = a2ull(tokens[6]);
      entry._shareableWS = a2ull(tokens[7]);
      entry._sharedWS = a2ull(tokens[8]);
      entry._lockedWS = a2ull(tokens[9]);
      entry._numBlocks = a2ull(tokens[10]);
      entry._protection = tokens[11];
      entry._details = tokens[12];

      vmmaps.push_back(entry);

      virtSize += entry.sizeKB();
      rssSize += entry.getResidentSizeKB();
      }
   myfile.close();
   cout << std::dec << "Total virtual size: " << virtSize << " kB. Total rss:" << rssSize << " kB." << endl;
   }



void readVmmapCsvFile(const char *vmmapFilename, vector<VmmapEntry>& vmmaps)
   {
   unsigned long long virtSize = 0;
   unsigned long long rssSize = 0;
   cout << "Reading map file: " << string(vmmapFilename) << endl;
   // Open the file
   ifstream myfile(vmmapFilename);
   // check if successfull
   if (!myfile.is_open())
      {
      cerr << "Cannot open " << vmmapFilename << endl;
      exit(-1);
      }
   // First we need to get to the header that looks like this
   // "Address","Type","Size","Committed","Private","Total WS","Private WS","Shareable WS","Shared WS","Locked WS","Blocks","Protection","Details",
   int lineNo = 0;
   string line;
   bool headerFound = false;
   while (myfile.good())
      {
      getline(myfile, line); lineNo++;
#ifdef DEBUG
      cout << "readVmmapFile looking at :" << line << endl;
#endif
      if (std::string::npos != line.find("\"Address\",\"Type\",\"Size\",\"Committed\",\"Private\",\"Total WS\",\"Private WS\",\"Shareable WS\",\"Shared WS\",\"Locked WS\",\"Blocks\",\"Protection\",\"Details\","))
         {
         headerFound = true;
         break;
         }
      }
   if (!headerFound)
      {
      cerr << "Cannot find header line in expected format\n" << endl;
      exit(-1);
      }

   // Now read all entries
   VmmapEntry entry;
   while (myfile.good())
      {
      getline(myfile, line); lineNo++;
      // skip empty lines
      if (line.find_first_not_of(" \t\n\r") == string::npos)
         continue;
#ifdef DEBUG
      cout << "readVmmapEntry looking at line " << lineNo << " with " << line.size() << " characters: " << line << endl;
#endif
      // All tokens are separated by ',' and sorounded by quotes
      vector<string> tokens; // we should do tokens(13)
      if (!tokenizeVmmapLine(line, tokens))
         exit(-1); // some error occurred
      // We must find exactly 13 items
      if (tokens.size() != 13)
         {
         cerr << "Must find exactly 13 tokens for vmmap line: " << line << endl;
         exit(-1);
         }

      // If the start address starts with a number it's a main entry, otherwise it's a subblock
      if (tokens[0].at(0) == ' ')
         {
         // sub-block
         continue;
         }
      // TODO: process sub-blocks as well

      unsigned long long start = hex2ull(tokens[0]);
      if (start == HEX_CONVERT_ERROR)
         {
         cerr << "Error with start address on line: " << line << endl;
         exit(-1);
         }
      else
         {
#ifdef DEBUG
         cout << "Tokens: ";
         for (vector<string>::iterator it = tokens.begin(); it != tokens.end(); ++it)
            {
            cout << *it << " ";
            }
         cout << endl;
#endif
         }
      // "Address","Type","Size","Committed","Private","Total WS","Private WS","Shareable WS","Shared WS","Locked WS","Blocks","Protection","Details",
      entry.clear();
      entry.setStart(start);
      entry._type = tokens[1];
      unsigned long long size = a2ull(tokens[2]);
      entry.setEnd(start + (size << 10));
      entry._committed = a2ull(tokens[3]);
      entry._rss = a2ull(tokens[5]);
      entry._privateWS = a2ull(tokens[6]);
      entry._shareableWS = a2ull(tokens[7]);
      entry._sharedWS = a2ull(tokens[8]);
      entry._lockedWS = a2ull(tokens[9]);
      entry._numBlocks = a2ull(tokens[10]);
      entry._protection = tokens[11];
      entry._details = tokens[12];

      vmmaps.push_back(entry);

      virtSize += entry.sizeKB();
      rssSize += entry.getResidentSizeKB();
      }
   myfile.close();
   cout << std::dec << "Total virtual size: " << virtSize << " kB. Total rss:" << rssSize << " kB." << endl;
   }

void readVmmapFile(const char *vmmapFilename, vector<VmmapEntry>& vmmaps)
   {
//#ifdef DEBUG
   cout << "Determining if vmmapFile is text or csv\n";
//#endif
   // Determine whether the file is of type csv or txt
   string filename(vmmapFilename);
   // TODO: make sure the filename ends with .csv or .txt
   bool isCsv = filename.find(".csv") != string::npos; // It's  csv
   bool isTxt = filename.find(".txt") != string::npos; // It's  txt

   if (isCsv)
      {
      readVmmapCsvFile(vmmapFilename, vmmaps);
      }
   else if (isTxt)
      {
      readVmmapTextFile(vmmapFilename, vmmaps);
      }
   else
      {
      cerr << "vmmap file must be have the extension cvs or txt\n";
      exit(-1);
      }

   }

void VmmapEntry::print(std::ostream& os) const
   {
   os << std::hex << "Start=" << setfill('0') << setw(16) << getStart() <<
      " End=" << setfill('0') << setw(16) << getEnd() << std::dec <<
      " Size=" << setfill(' ') << setw(6) << sizeKB() << " rss=" << setfill(' ') << setw(6) << _rss << " Prot=" << getProtectionString();
   if (_details.length() > 0)
      os << " " << _details;
   }



