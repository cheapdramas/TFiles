#ifndef ENUMS_H
#define ENUMS_H

typedef enum ReturnStatus { 
  SUCCESS     = 0, 
  ERROR       = 1,
  EXISTS      = 2,
  MALLOC_FAIL = 3,
} ReturnStatus;

typedef enum FileType { 
  REGULAR     = 0,
  DIRECTORY   = 1 
} FileType;

#endif
