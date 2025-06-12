#include "files.h"
#include "enums.h"
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>

int compare_filenames(const void *a, const void *b) {
  return strcmp(*(char **)a, *(char **)b);
}

extern void FilesArray_free(FilesArray *fa) {
  if (fa->filenames == NULL) {
    return;
  }

  for (int i = 0; i < fa->files_count; i++) {
    free(fa->filenames[i]);
  }
  free(fa->filenames);

  fa->files_count = 0;
  fa->size = 0;
  fa->filenames = NULL;
}

extern int FilesArray_fill(FilesArray *fa, char *pwd) {
	if (fa->filenames != NULL) {
	  FilesArray_free(fa);
	}
  
	fa->files_count = 0;
	fa->size = 2;
	fa->filenames = malloc(fa->size * sizeof(char *));
	if (fa->filenames == NULL){
	  return MALLOC_FAIL;
	}
	memset(fa->filenames, 0, fa->size * sizeof(char *));
  
	DIR *dirp;
	struct dirent *entry;
	dirp = opendir(pwd);
	if (dirp == NULL) {
	  return ERROR;
	}
  
	// Reading directory
	while ((entry = readdir(dirp)) != NULL) {
	  // Skip if filename = "." or ".."
	  if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
		continue;
	  }
  
	  // Realloc if array too small
	  if (fa->files_count >= fa->size) {
		size_t old_size = fa->size;
		fa->size *= 2;
  
		char **new_filenames = realloc(fa->filenames, fa->size * sizeof(char *));
		if (new_filenames == NULL) {
		  return MALLOC_FAIL;
		}
		// Initialize new reallocated empty space,
		// So it won't point to garbage
		memset(new_filenames + old_size, 0, (fa->size - old_size) * sizeof(char *));
  
		// Update array
		fa->filenames = new_filenames;
	  }
  
	  fa->filenames[fa->files_count] = strdup(entry->d_name);
	  if (fa->filenames[fa->files_count] == NULL) {
		return MALLOC_FAIL;
	  }
  
	  fa->files_count += 1;
	}
  
	// If after reading directory, last_index = 0
	// Than directory is empty
	if (fa->files_count == 0) {
	  FilesArray_free(fa);
	}
  
	// Sort filenames
	qsort(fa->filenames, fa->files_count, sizeof(char *), compare_filenames);
  
	closedir(dirp);
	return SUCCESS;
}

extern FileType get_filetype(const char *path) {
	struct stat file_stat;
  if (stat(path, &file_stat) < 0) {
    return -1;
  }
  if (S_ISDIR(file_stat.st_mode) == 1) {
    return DIRECTORY;
  }
  return REGULAR;
}

extern int create_dir(const char *path) {
  if (mkdir(path, 0700) < 0) {
    if (errno == EEXIST) {
      return EXISTS;
    }
    return ERROR;
  }
  return SUCCESS;
}

