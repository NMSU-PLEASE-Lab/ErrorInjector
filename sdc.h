/** 
* @file
* @author Jonathan Cook
* @brief Error injector header
*
* @details Define a few things for the bit error injector code to share.
*
* Copyright (C) 2021 Jonathan Cook
*
**/

#define PERM_READ 0x1
#define PERM_WRITE 0x2
#define PERM_EXEC 0x4
#define PERM_SHARED 0x20
#define PERM_PRIVATE 0x10

typedef struct map_struct {
   unsigned long beginAddress;
   unsigned long endAddress;
   int  permissions;
   char *name;
   struct map_struct *next;
} MapSegment;
