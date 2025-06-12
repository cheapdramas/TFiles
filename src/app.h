#ifndef APP_H 
#define APP_H

#include "config.h"
#include <linux/limits.h>
#include "window.h"
#include <string.h>
#include <pwd.h>



typedef struct AppState{
  char *editor;
  bool debug;
} AppState;

typedef struct AppDataPaths{
  char cache[PATH_MAX];
  char data[PATH_MAX];
} AppDataPaths;

typedef struct App{
  AppState state;
  AppDataPaths data_paths;
  WindowManager winmgr;
} App;

extern void App_exit(App *app, const char *reason, ...);

extern void App_init_folders(App *app);

extern int App_parse_arguments(App *app, int argc, char **argv);

extern void App_init(App *app, int argc, char **argv);

#endif
