#include "app.h"
#include "enums.h"
#include "window.h"
#include <stdlib.h>
#include <unistd.h>


extern void App_exit(App *app, const char *reason, ...) {
  // Free windows
  Window_free(&app->winmgr.first_window);
  Window_free(&app->winmgr.second_window);

  curs_set(1);
  echo();
  endwin();

  // Reason is passed
  if (reason != NULL) {
    // Prepare list for arguments
    va_list args;
    va_start(args, reason);
    // Copy of list because vsnprintf will
    // make args pointing to last argument
    va_list args_copy;
    va_copy(args_copy, args);

    // Calculate required length for reason + arguments
    int needed = vsnprintf(NULL, 0, reason, args);
    va_end(args);
    // Formatting error
    if (needed < 0) {
      fprintf(stderr, "Formatting error\n");
      exit(EXIT_FAILURE);
    }

    char *buffer = malloc(needed + 1);
    if (buffer == NULL) {
      fprintf(stderr, "%s\n", MALLOC_FAIL_MSG);
      exit(EXIT_FAILURE);
    }
    vsnprintf(buffer, needed + 1, reason, args_copy);

    fprintf(stderr, "%s\n", buffer);

    free(buffer);
    va_end(args_copy);
  }
  exit(EXIT_SUCCESS);
}

extern void App_init_folders(App *app) { 
  const char *home_dir = getenv("HOME");
  // If not HOME value in env
  if (home_dir == NULL) {
    struct passwd *pw = getpwuid(getuid());
    home_dir = pw->pw_dir;
  } // Failed to get HOME path
  if (home_dir == NULL) {
    App_exit(app, "Unable to determine home directory");
  }

  snprintf(app->data_paths.data, PATH_MAX, "%s/%s", home_dir, DATA_DIR);

  if (create_dir(app->data_paths.data) == ERROR) {
    App_exit(app, "Failed to create data folder at %s", app->data_paths.data);
  }
}

extern int App_parse_arguments(App *app, int argc, char **argv) {
	// Init editor
  // If user passed an editor, it will change later
  if ((app->state.editor = getenv("EDITOR")) == NULL) {
    app->state.editor = "vim";
  }
  // Check if arguments passed
  if (argc < 2) {
    return SUCCESS;
  }
  int argument_idx_start = 0;
  // User want last application state
  if (strcmp(argv[argument_idx_start], "-l") == 0) {
    argument_idx_start++;
  }
  // Iterate through arguments
  for (int i = argument_idx_start; i < argc; i++) {
    char *argument = argv[i];
    // User set path
    if (strcmp(argument, "-path") == 0) {
      // Check if next argument avaible
      if (i + 1 < argc) {
        char *path = argv[i + 1];
        if (chdir(path) < 0) return ERROR;

        i++;
        continue;
      }
    }

    // User set editor
    if (strcmp(argument, "-editor") == 0) {
      // Check if next argument avaible
      if (i + 1 < argc) {
        app->state.editor = argv[i + 1];

        i++;
        continue;
      }
    }
    // If user want debug mode (for me mostly)
    if (strcmp(argument, "-debug") == 0) {
      app->state.debug = true;
    }
  }
	return SUCCESS;
}

extern void App_init(App *app, int argc, char **argv) {	
	// Init data folders
  App_init_folders(app);

  // Parsing arguments
  App_parse_arguments(app, argc, argv);

  // Create first window
  app->winmgr.window_counter = 0;

  Window *first_window = malloc(sizeof(Window));
	if (first_window == NULL) {
		App_exit(app, MALLOC_FAIL_MSG);
	}
	Window_create(first_window,&app->winmgr, NULL);

  // Fill window with files
  int fill_res = FilesArray_fill(&first_window->files, first_window->pwd); 
  if (fill_res > 0) {
    if (fill_res == MALLOC_FAIL){
      App_exit(app, MALLOC_FAIL_MSG);
    }
    else {
      App_exit(app, NULL);
    }
  }
}
	



