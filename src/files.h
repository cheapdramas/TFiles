#ifndef FILES
#define FILES 

#include <stddef.h>
#include "enums.h"
#include <sys/types.h>
#include <stdbool.h>

typedef struct FileInfo {
  char *filename;
  off_t filesize;
  bool is_regular;
  int dirlen;
} FileInfo;



typedef struct FilesArray {
  char **filenames;
  size_t size;
  unsigned int files_count;
} FilesArray;


extern int FilesArray_fill(FilesArray *fa, char *pwd);

extern void FilesArray_free(FilesArray *fa);

extern FileType get_filetype(const char *path);

extern int create_dir(const char *path);


#endif
