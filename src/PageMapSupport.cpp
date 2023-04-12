// Support fo readinf /proc/PID/pagemap file and determining which pages are
// present in physical memory
#include <stdio.h>  // printf, snprintf
#include <fcntl.h> // open, O_RDONLY
#include <errno.h> // errno
#include <inttypes.h> // uint64_t
#include <unistd.h> // sysconf, pread
#include <string.h> // strerror
#include <stdexcept> // runtime_error
#include <iostream> // cerr
#include "PageMapSupport.hpp"


typedef struct __attribute__ ((__packed__)) {
    union {
        uint64_t pmd;
        uint64_t page_frame_number : 55;
        struct {
            uint64_t swap_type: 5;
            uint64_t swap_offset: 50;
            uint64_t soft_dirty: 1;
            uint64_t exclusive: 1;
            uint64_t zero: 4;
            uint64_t file_page: 1;
            uint64_t swapped: 1;
            uint64_t present: 1;
        };
    };
} pmd_t;

PageMapReader::PageMapReader(int pid): _pid(pid)
   {
   // Determine the page size on the system
   _pageSize = sysconf(_SC_PAGE_SIZE);
   if (_pageSize < 0)
      {
      throw std::runtime_error("cannot read page size with sysconf");
      }

   // form filename
   snprintf(_pagemapPath, sizeof(_pagemapPath), "/proc/%d/pagemap", pid);

   int pagemapfd = open(_pagemapPath, O_RDONLY);
   if (pagemapfd < 0)
      {
      std::cerr << "Cannot open pagemap file: " << _pagemapPath << std::endl;
      std::cerr << "Verify that PID exists and that we have read permission on the file." << std::endl;
      std::cerr << "Error: " << strerror(errno) << std::endl;
      throw std::runtime_error("");
      }
   }

PageMapReader::~PageMapReader()
   {
   close(_pagemapfd);
   }

unsigned long long PageMapReader::computeRssForAddrRange(unsigned long long startAddr, unsigned long long endAddr)
   {
   if (startAddr >= endAddr)
      throw std::runtime_error("invalid address range");

   unsigned long long rss = 0;

   // Align the start/end address to the page boundary
   unsigned long long startAligned = startAddr & ~(_pageSize - 1);
   unsigned long long endAligned = (endAddr + _pageSize - 1) & ~(_pageSize - 1);

   // Read the contribution of the first page which may not be entirely used by this AddrRange
   pmd_t pmd;
   if (pread(_pagemapfd, &pmd.pmd, sizeof(pmd.pmd), (off_t)((startAligned / _pageSize) * sizeof(pmd))) != sizeof(pmd))
      {
      // Is the process still alive?
      throw std::runtime_error("cannot read pagemap file: " + std::string(_pagemapPath) + std::string(strerror(errno)));
      }
   if (pmd.pmd != 0 && pmd.present)
      {
      if (endAddr <= startAligned + _pageSize)
         {
         // The range si within the first page
         rss += endAddr - startAddr;
         return rss;
         }
      else
         {
         rss += startAligned + _pageSize - startAddr;
         }
      }
   // Read the contribution of the last page which may not be entirely used by this AddrRange
   if (pread(_pagemapfd, &pmd.pmd, sizeof(pmd.pmd), (off_t)(((endAligned - _pageSize) / _pageSize) * sizeof(pmd))) != sizeof(pmd))
      {
      // Is the process still alive?
      throw std::runtime_error("cannot read pagemap file: " + std::string(_pagemapPath) + std::string(strerror(errno)));
      }
   if (pmd.pmd != 0 && pmd.present)
      {
      rss += endAddr - (endAligned - _pageSize);
      }

   // Read the contribution of all the other pages
   for (unsigned long long i = startAligned + _pageSize; i < endAligned - _pageSize; i += _pageSize)
      {
      pmd_t pmd;
      // read at given offset
      if (pread(_pagemapfd, &pmd.pmd, sizeof(pmd.pmd), (off_t)((i / _pageSize) * sizeof(pmd))) != sizeof(pmd))
         {
         // Is the process still alive?
         throw std::runtime_error("cannot read pagemap file: " + std::string(_pagemapPath) + std::string(strerror(errno)));
         }
      if (pmd.pmd != 0 && pmd.present)
         {
         rss += _pageSize;
         }
      } // end for
   return rss;
   }