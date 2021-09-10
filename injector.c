/** 
* @file
* @author Jonathan Cook
* @brief Bit error injector
*
* @details This library will inject random bit errors into a
* running application. 
* USAGE:
* - set environment variable SDC_DELAY to the integer # of seconds
*   of wall-time to wait before injecting the error (default: 3 seconds)
* - set environment variable SDC_MPIONLY if you want to only inject an
*   MPI process (this checks for env vars and allows you to avoid injecting 
*   into mpirun/mpiexec)
* - set environment variable SDC_MPIRANK to an integer if you want an error 
*   only injected to that particular MPI process; otherwise, all processes get
*   injected.
* - set environment variable SDC_OUTFILE to the name of the desired output
*   file (defaults to sdcout-PID.log, where PID is the process PID for this run)
*   Use one '%d' in the output filename if you want the PID embedded in the name.
* - set environment variable SDC_MEMTYPE to one of the following:
*   -- 'all' -- any memory in the application space (and its DSO libraries) may be 
*               injected with an error
*   -- 'data' -- any data memory in the application space may be injected with an error
*   -- 'code' -- any program memory in the application space may be injected with an error
*   -- 'appdata' -- any data memory in the application space, but not data memory in the
*                   DSO libraries, may be injected with an error (excl heap and stack, bad!)
*   -- 'heap' -- any memory in the application's heap may be injected with an error
*   -- 'stack' -- any memory in the application's stack may be injected with an error
*   -- default is 'data'
* - load this library into app space using LD_PRELOAD
* - run the application
*
* TODO:
* - use env var for bit range for errors (i.e., limit to exponent?)
* - decide on 32 or 64 bit base (effects address alignment and bit range)
* - need some sort of process selection capability (extern random # set into env var?)
* - allow double bit errors (ecc will correct single bit, but single models other problems too)
* - allow OR in of different memory types
*
* Copyright (C) 2021 Jonathan Cook
* All rights reserved.
**/
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <fcntl.h>
//#include <sys/time.h>
//#include <sys/resource.h>
#define __USE_GNU
#include <dlfcn.h>
#undef __USE_GNU
#include <pthread.h>
#include <sys/mman.h> // for mprotect()
#include "sdc.h"

int sdcDebug = 0;

MapSegment *memoryMap = 0;
unsigned long totalMemory = 0;
unsigned long totalReadMemory = 0;
unsigned long totalWriteMemory = 0;
unsigned long totalCodeMemory = 0;
unsigned long totalHeapMemory = 0;
unsigned long totalStackMemory = 0;
unsigned long totalAppDataMemory = 0;
static int myMPIRank = -1;
static pthread_t sdcInjectorThread = 0;
static int waitSecondsUntilInject = 3;
static unsigned long systemPageSize = 0;
static char logFilename[128];

static enum {injectALL=1, injectDATA, injectCODE, injectAPPDATA,
             injectHEAP, injectSTACK} injectMemoryType = injectALL;
static char* injectMTName[] = {"Unknown", "All", "Data", "Code", "AppData",
                               "Heap", "Stack", "Unusable"};

// routines from readsmaps.c
void dumpMemoryMap(int level);
int readProcSmaps(int pid);

/**
* @brief Thread routine for injecting SDC error(s)
*
* @param p is required pthread start-function parameter, not used
* @return NULL always
* @details This function is started in its own thread, and its job
* is to simply sleep for the specified number of seconds, then
* generate a random address (8-byte aligned) and inject a random
* bit error. Once an error is injected, this function returns and
* the thread dies.
**/
void* sdcInjectorStart(void *p)
{
   FILE *logf;
   unsigned long randomSize, addressMask;
   uintptr_t randomAddress;
   uint64_t injectVal;
   uint64_t *injectPtr;
   unsigned int randomBit, seed; int randDev;
   char* pagePtr; unsigned int pagePerms;
   MapSegment *map;
   if (sdcDebug>1)
      fprintf(stderr, "In SDC thread, waiting %d seconds\n", waitSecondsUntilInject);
   // go to sleep for awhile
   sleep(waitSecondsUntilInject);
   // awake, now inject a bit error
   
   // make address mask
   addressMask = (~0)^0x7; // all ones except lower three bits
   
   // read O/S memory map for this process
   readProcSmaps(0);
   if (sdcDebug > 2) 
      dumpMemoryMap(1);
   // choose whether injecting into data (write) memory or all memory
   if (injectMemoryType == injectDATA)
      randomSize = totalWriteMemory;
   else if (injectMemoryType == injectCODE)
      randomSize = totalCodeMemory;
   else if (injectMemoryType == injectHEAP)
      randomSize = totalHeapMemory;
   else if (injectMemoryType == injectSTACK)
      randomSize = totalStackMemory;
   else if (injectMemoryType == injectAPPDATA)
      randomSize = totalAppDataMemory;
   else
      randomSize = totalMemory;
   // re-seed with a 'random' seed
   randDev = open("/dev/random", O_RDONLY);
   if (randDev != -1) {
      read(randDev, &seed, sizeof(seed));
      srandom(seed);
      close(randDev);
   } else {
      srandom(time(0)+clock());
   }
   // choose an 8-byte aligned address (offset)
   randomAddress = (random() % randomSize) & addressMask;
   // choose an 8-byte bit number
   randomBit = (random() >> 2) % 64;
   if (sdcDebug)
      fprintf(stderr, "SDC: Injecting error at %lx (%lx), bit %d!\n", randomAddress,
              randomSize, randomBit);
   // now must map chosen address (offset) onto a real address in
   // one of the mapped sections (not ELF sections)
   randomSize = 0;
   map = memoryMap;
   // find map where this address offset falls into
   while (map && randomSize < randomAddress) {
      if ((injectMemoryType == injectALL) ||
          (injectMemoryType == injectDATA && map->permissions & PERM_WRITE) ||
          (injectMemoryType == injectCODE && map->permissions & PERM_EXEC) ||
          (injectMemoryType == injectAPPDATA && map->permissions & PERM_WRITE) ||
          (injectMemoryType == injectSTACK && !strcmp(map->name,"[stack]")) ||
          (injectMemoryType == injectHEAP && !strcmp(map->name,"[heap]"))) {
         randomSize += (map->endAddress - map->beginAddress);
         if (randomSize > randomAddress) break;
      }
      map = map->next;
   }
   if (!map) {
      if (sdcDebug) fprintf(stderr, "SDC: failed to find map for address %lx\n", randomAddress);
      return NULL;
   }
   if (sdcDebug)
      fprintf(stderr, "SDC: Injecting into (%s), (%lx - %lx)\n", map->name,
              map->beginAddress, map->endAddress);
   // re-map randomAddress to a real address
   randomSize -= (map->endAddress - map->beginAddress); // remove last map size
   randomAddress -= randomSize; // remove previous sizes to create offset
   randomAddress += map->beginAddress; // create actual address in this map section
   injectPtr = (unsigned long *) (randomAddress & addressMask); // need to realign after map base?
   // if address is on read-only page, make it writeable
   if (!(map->permissions & PERM_WRITE)) {
      pagePtr = (char *)(((unsigned long)injectPtr) & ~(systemPageSize-1));
      pagePerms = 0;
      if (map->permissions & PERM_READ)
         pagePerms |= PROT_READ;
      if (map->permissions & PERM_EXEC)
         pagePerms |= PROT_EXEC;
      mprotect(pagePtr, systemPageSize, pagePerms | PROT_WRITE);
   }
   // generate bit to flip
   injectVal = 0x1L << randomBit;
   if (sdcDebug)
      fprintf(stderr, "SDC: Injecting %lx at %p\n", injectVal, injectPtr);
   // log info to log file
   logf = fopen(logFilename,"a");
   if (logf) {
      fprintf(logf, "SDC Configuration:\nDelay %d\n", waitSecondsUntilInject);
      fprintf(logf, "MPI Rank: %d\n", myMPIRank);
      fprintf(logf, "Memory Type: %s\n", injectMTName[injectMemoryType]);
      fprintf(logf, "Total (Write) Memory: %ld %ld\n", totalMemory, totalWriteMemory);
      fprintf(logf, "Injected error info:\nAddress: %p\n", injectPtr);
      fprintf(logf, "Bit number: %d\nBit mask: %lx\n", randomBit, injectVal);
      fprintf(logf, "Map: %lx - %lx %x\nName: %s", map->beginAddress, map->endAddress, 
              map->permissions, map->name);
      // if code, try to report what function was effected
      if (map->permissions & PERM_EXEC) {
         Dl_info dlinfo;
         if (dladdr(injectPtr, &dlinfo)) {
            fprintf(logf, " (%s,%p)", dlinfo.dli_sname, dlinfo.dli_saddr);
         }
      }
      fflush(logf);
      fclose(logf);
      logf = fopen(logFilename,"a");
      fprintf(logf, "\n");         
      fprintf(logf, "Current value: %lx\n", *injectPtr);
      fflush(logf);
   }   
   // XOR the chosen bit into the value at the chosen address
   *injectPtr = (*injectPtr ^ injectVal); // flip the chosen injection bit
   // if address is on read-only page, remove write permissions
   if (!(map->permissions & PERM_WRITE)) {
      mprotect(pagePtr, systemPageSize, pagePerms);
   }
   // log info to log file
   if (logf) {
      fprintf(logf, "New value: %lx\n", *injectPtr);
   }
   fflush(logf);
   fclose(logf);
   return NULL;
}

/**
* @brief sdcTesterFinalize(): finalization routine for SDC Tester.
*
* It writes the general statistics out to stderr (logfile?),
**/
#ifdef TESTING
void sdcTesterFinalize(void)
#else
void __attribute__((destructor)) sdcTesterFinalize(void)
#endif
{
   FILE *logf;
   if (sdcDebug)
      fprintf(stderr, "SDC Tester Finished\n");;
   logf = fopen(logFilename,"r+"); // don't create if injection never occurred
   if (!logf)
      return;
   fseek(logf, 0, SEEK_END);
   fprintf(logf,"Application finished\n");
   fflush(logf);
   fclose(logf);
   return;
}
/* for non-gnu compilers */
//#pragma fini sdcTesterFinalize

/**
* @brief sdcTesterInitialize(): initialization routine for SDC Tester.
*
* Initialize the SDC tester program
**/
#ifdef TESTING
void sdcTesterInitialize(void)
#else
void __attribute__((constructor)) sdcTesterInitialize(void)
#endif
{
   char* enval;
   long ival;
   unsigned int myPid;
   
   if (sdcDebug) 
      fprintf(stderr, "SDC Tester Initializing\n");;
      
   systemPageSize = getpagesize();
   myPid = getpid();
   
   // check if we are injecting into an MPI program
   enval = getenv("SDC_MPIONLY");
   if (enval) {
      enval = getenv("OMPI_COMM_WORLD_RANK");
      if (!enval)
         enval = getenv("OMPI_MCA_ns_nds_vpid");
      if (!enval) {
         // then we are not one of the MPI processes, but are probably
         // mpirun, so we should not inject an error
         return;
      }
   }
   // check if we are injecting into one MPI process
   enval = getenv("SDC_MPIRANK");
   if (enval) {
      int actualRank = -1;
      int desiredRank = atoi(enval);
      enval = getenv("OMPI_COMM_WORLD_RANK");
      if (!enval)
         enval = getenv("OMPI_MCA_ns_nds_vpid");
      if (!enval) {
         // then no rank determined, so don't do anything
         return;
      }
      actualRank = atoi(enval);
      if (desiredRank != actualRank) {
         // then not correct rank, so don't do anything
         return;
      }
      myMPIRank = actualRank;
      //fprintf(stderr,"%d: Correct rank %d, injecting error\n", myPid, actualRank);
   }
   
   enval = getenv("SDC_DELAY");
   if (enval) {
      ival = strtol(enval,0,0);
      if (ival >= 0 || ival <= 9999999)
         waitSecondsUntilInject = ival;
      else
         fprintf(stderr, "SDC: Bad value (%s) for SDC_DELAY!\n", enval);
   }
   enval = getenv("SDC_MEMTYPE");
   if (enval) {
      if (!strcasecmp(enval, "all"))
         injectMemoryType = injectALL;
      else if (!strcasecmp(enval, "data"))
         injectMemoryType = injectDATA;
      else if (!strcasecmp(enval, "code"))
         injectMemoryType = injectCODE;
      else if (!strcasecmp(enval, "appdata"))
         injectMemoryType = injectAPPDATA;
      else if (!strcasecmp(enval, "heap"))
         injectMemoryType = injectHEAP;
      else if (!strcasecmp(enval, "stack"))
         injectMemoryType = injectSTACK;
      else
         fprintf(stderr, "SDC: Bad value (%s) for SDC_MEMTYPE\n", enval);
   } else
      injectMemoryType = injectDATA;

   enval = getenv("SDC_OUTFILE");
   if (enval) {
      sprintf(logFilename,enval,myPid,0,0,0,0,0,0); // extra 0's for safety
   } else {
      sprintf(logFilename,"./sdc-%d.log",myPid);
   }

   // should read memory map only when woken up, not at beginning
   // readProcSmaps(); // read in application memory map (done yet?)
   // dumpMemoryMap(0);
   
#ifndef TESTING
   // create bit error injector thread
   pthread_create(&sdcInjectorThread, NULL, sdcInjectorStart, NULL);
#endif
}
/* for non-gnu compilers */
//#pragma init sdcTesterInitialize

#ifdef TESTING
int main(int argc, char **argv)
{
   //char *m, p[655360];
   sdcDebug = 2;
   //m = malloc(6553600);
   sdcTesterInitialize();
   sdcInjectorStart(0);
   sdcTesterFinalize();
   return 0;
}
#endif
