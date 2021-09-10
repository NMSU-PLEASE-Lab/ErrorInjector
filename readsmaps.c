/** 
* @file
* @author Jonathan Cook
* @brief Read and record information on application memory map
*
* @details This code builds a data structure that represents the
* running application's memory map. It can be compiled into a stand-alone
* test using -DTESTING
*
* Copyright (C) 2021 Jonathan Cook
*
**/
#include <stdio.h>
#include <unistd.h>
#include <malloc.h>
#include <string.h>
#include "sdc.h"

#ifdef TESTING
#define EXTERN 
#else
#define EXTERN extern
#endif

EXTERN MapSegment *memoryMap;
EXTERN unsigned long totalMemory;
EXTERN unsigned long totalReadMemory;
EXTERN unsigned long totalWriteMemory;
EXTERN unsigned long totalCodeMemory;
EXTERN unsigned long totalHeapMemory;
EXTERN unsigned long totalStackMemory;
EXTERN unsigned long totalAppDataMemory;
EXTERN int sdcDebug;

/**
* @brief Read and parse /proc/[pid]/maps file for memory map
**/
int readProcSmaps(int pid)
{
   FILE *f;
   char line[256];
   int myPid, matched;
   unsigned int absSize, rssSize; // segment sizes from file
   unsigned long beginAddr, endAddr, offset, inode;
   char perms[5], dev[4];
   char name[128], appName[128];
   MapSegment *newSeg, *tailSeg;
   if (pid <= 0)
      myPid = getpid();
   else
      myPid = pid;
   sprintf(line, "/proc/%d/smaps", myPid); // maybe use smaps?
   f = fopen(line, "r");
   if (!f) 
      return -1;
   strcpy(name,"none");
   appName[0] = '\0';
   // if re-reading, then clear old map
   while (memoryMap) {
      newSeg = memoryMap->next;
      if (memoryMap->name) {
         free(memoryMap->name);
         memoryMap->name = 0;
      }
      memoryMap->next = 0;
      free(memoryMap);
      memoryMap = newSeg;
   }
   newSeg = tailSeg = 0;
   totalMemory = totalWriteMemory = 0;
   // walk through file lines and extract memory map info
   while (fgets(line, sizeof(line), f) != NULL) {
      line[strlen(line)-1] = '\0';
      if (sdcDebug>1)
         fprintf(stderr, "(%s)\n",line);
      matched = sscanf(line, "%lx-%lx %c%c%c%c %lx %c%c:%c%c %ld %127s", &beginAddr, 
                       &endAddr, &perms[0], &perms[1], &perms[2], &perms[3], 
                       &offset, &dev[0], &dev[1], &dev[2], &dev[3], &inode, &name);
      if (matched < 7) 
         continue;
      // if map is of the injector library, skip (since it should be invisible)
      if (strstr(name,"libsdc.so"))
         continue;
      // skip if this segment has no rwx permissions (empty?)
      if (perms[0]=='-' && perms[1]=='-' && perms[2]=='-')
         continue;
      // first time through, set application name
      if (!appName[0] && strcmp(name,"none")) {
         strcpy(appName, name);
      }
      // read next two file lines for size and rss size (in KB)
      if (fgets(line, sizeof(line), f) == NULL) break;
      sscanf(line, "Size: %u", &absSize);
      if (fgets(line, sizeof(line), f) == NULL) break;
      sscanf(line, "Rss: %u", &rssSize);
      if (sdcDebug>1)
         fprintf(stderr, "absSize = %d  rssSize = %d\n", absSize, rssSize);
      // adjust map size to more closely match resident set size
      if (rssSize < absSize) {
         if (strstr(name,"[stack]"))
            beginAddr = endAddr - (rssSize * 1024); // stack grows downward
         else if (perms[2]=='x')
            ; // is a code segment, so no idea what pages are out, just leave as is
         else if (strstr(name, "[heap]"))
            endAddr = beginAddr + (rssSize * 1024); // heap grows upward
         else if (rssSize < absSize/4) {
            // only adjust unknown segments if the rss is less than 1/4 of the whole
            // this will handle the worst cases, like openmpi's huge but 
            // little used shared memory pool segment
            endAddr = beginAddr + (rssSize * 1024); // assume this segment grows up
         }
      }
      // make perms a string
      perms[4] = '\0';
      if (sdcDebug>1)
         fprintf(stderr, "num matches: %d (%s) (%lx %lx %s)\n", matched, name, 
                 beginAddr, endAddr, perms);
      // set up new map record
      newSeg = (MapSegment*) malloc(sizeof(MapSegment));
      newSeg->beginAddress = beginAddr;
      newSeg->endAddress = endAddr;
      newSeg->permissions  = (perms[0]=='r'? PERM_READ : 0);
      newSeg->permissions |= (perms[1]=='w'? PERM_WRITE : 0);
      newSeg->permissions |= (perms[2]=='x'? PERM_EXEC : 0);
      newSeg->permissions |= (perms[3]=='s'? PERM_SHARED : 0);
      newSeg->permissions |= (perms[3]=='p'? PERM_PRIVATE : 0);
      newSeg->name = strdup(name);
      newSeg->next = 0;
      if (!memoryMap) {
         memoryMap = tailSeg = newSeg;
      } else {
         tailSeg->next = newSeg;
         tailSeg = newSeg;
      }
      // if no access permissions then don't count in totals
      if (!(newSeg->permissions & (PERM_READ|PERM_WRITE|PERM_EXEC)))
         continue;
      // increment the appropriate total memory counts
      totalMemory += (endAddr - beginAddr);
      if (newSeg->permissions & PERM_READ) 
         totalReadMemory += (endAddr - beginAddr);
      if (newSeg->permissions & PERM_WRITE) 
         totalWriteMemory += (endAddr - beginAddr);
      if (newSeg->permissions & PERM_EXEC) 
         totalCodeMemory += (endAddr - beginAddr);
      if (!strcmp(name,appName) && newSeg->permissions & PERM_WRITE) 
         totalAppDataMemory += (endAddr - beginAddr);
      if (!strcmp(name,"[heap]") && newSeg->permissions & PERM_WRITE) 
         totalHeapMemory += (endAddr - beginAddr);
      if (!strcmp(name,"[stack]") && newSeg->permissions & PERM_WRITE) 
         totalStackMemory += (endAddr - beginAddr);
   }
   fclose(f);
   return 0;
}

/**
* @brief Dump a human readable view of memory map info to stderr
**/
void dumpMemoryMap(int level)
{
   MapSegment *seg = memoryMap;
   while (level > 0 && seg) {
      fprintf(stderr, "segment: %lx - %lx   %x   (%s)\n", seg->beginAddress, seg->endAddress,
              seg->permissions, seg->name);
      seg = seg->next;
   }
   fprintf(stderr, "Total overall memory: %ld bytes (%.2f MB)\n", totalMemory, 
          ((double) totalMemory) / (1024*1024));
   fprintf(stderr, "Total read    memory: %ld bytes (%.2f MB)\n", totalReadMemory, 
          ((double) totalReadMemory) / (1024*1024));
   fprintf(stderr, "Total write   memory: %ld bytes (%.2f MB)\n", totalWriteMemory, 
          ((double) totalWriteMemory) / (1024*1024));
   fprintf(stderr, "Total code    memory: %ld bytes (%.2f MB)\n", totalCodeMemory, 
          ((double) totalCodeMemory) / (1024*1024));
   fprintf(stderr, "Total appdata memory: %ld bytes (%.2f MB)\n", totalAppDataMemory, 
          ((double) totalAppDataMemory) / (1024*1024));
   fprintf(stderr, "Total heap    memory: %ld bytes (%.2f MB)\n", totalHeapMemory, 
          ((double) totalHeapMemory) / (1024*1024));
   fprintf(stderr, "Total stack   memory: %ld bytes (%.2f MB)\n", totalStackMemory, 
          ((double) totalStackMemory) / (1024*1024));
}

#ifdef TESTING
int main(int argc, char **argv)
{
   sdcDebug = 2;
   if (argc > 1)
      readProcSmaps(atoi(argv[1]));
   else
      readProcSmaps(0);
   dumpMemoryMap(1);
}
#endif
