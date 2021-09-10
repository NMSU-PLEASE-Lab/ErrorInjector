//
// print result of bit and size operations
//
#include <stdio.h>

int main (int argc, char **argv)
{
   unsigned long v;
   fprintf(stderr, "Sizes of data types in bytes\n\n");
   fprintf(stderr, "char       = %d\n", sizeof(char));
   fprintf(stderr, "short      = %d\n", sizeof(short));
   fprintf(stderr, "int        = %d\n", sizeof(int));
   fprintf(stderr, "long       = %d\n", sizeof(long));
   fprintf(stderr, "long long  = %d\n", sizeof(long long));
   fprintf(stderr, "float      = %d\n", sizeof(float));
   fprintf(stderr, "double     = %d\n", sizeof(double));
   fprintf(stderr, "long double= %d\n", sizeof(long double));
   fprintf(stderr, "void *     = %d\n", sizeof(void*));
   v = (~0)^0x7;
   fprintf(stderr, "~0     == %lx\n", v);
   fprintf(stderr, "~0^0x7 == %lx\n", v^0x7);
   fprintf(stderr, "~0-7   == %lx\n", v-7);
   return 0;
}
