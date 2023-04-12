#ifndef PAGEMAPSUPPORT_HPP_
#define PAGEMAPSUPPORT_HPP_

class PageMapReader
   {
   int _pid; // PID of the process for which we want to rea the pagemap
   long _pageSize; // page size of the system
   char _pagemapPath[64]; // buffer for holding the path to the pagemap file
   int _pagemapfd; // file descriptor for the pagemap file

   public:
   PageMapReader(int pid);
   ~PageMapReader();
   unsigned long long computeRssForAddrRange(unsigned long long startAddr, unsigned long long endAddr);
   };

#endif /* PAGEMAPSUPPORT_HPP_ */