//
// Some test program, not part of error injector
// - tests exception on trying to write a write-protected memory page
//
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/mman.h>

#include <limits.h>    /* for PAGESIZE */
#ifndef PAGESIZE
#define PAGESIZE 4096
#endif

int
main(void)
{
   char *p, *p1;
   char c;

   /* Allocate a buffer; it will have the default
      protection of PROT_READ|PROT_WRITE. */
   p = malloc(1024+PAGESIZE-1);
   if (!p) {
      perror("Couldn’t malloc(1024)");
      exit(errno);
   }
   printf("p = %p, p[666] = %p, pagep = %p\n", p, &p[666], 
          (char *)(((int) p + PAGESIZE-1) & ~(PAGESIZE-1)));

   /* Align to a multiple of PAGESIZE, assumed to be a power of two */
   p1 = (char *)(((int) p + PAGESIZE-1) & ~(PAGESIZE-1));

   c = p[666];         /* Read; ok */
   p[666] = 42;        /* Write; ok */

   /* Mark the buffer read-only. */
   if (mprotect(p1, 1024, PROT_READ)) {
      perror("Couldn’t mprotect");
      exit(errno);
   }

   c = p[666];         /* Read; ok */
   p[666] = 42;        /* Write; program dies on SIGSEGV */

   exit(0);
}

