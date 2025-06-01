#ifndef FILES
#define FILES 

#include <stddef.h>
#include "enums.h"

typedef struct FilesArray {
  char **filenames;
  size_t size;
  unsigned int files_count;
} FilesArray;


extern int FilesArray_fill(FilesArray *fa, char *pwd);
extern void FilesArray_free(FilesArray *fa);


#endif
