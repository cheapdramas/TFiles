/*
 _____   _____   _   _              
|_   _| |  ___| (_) | |   ___   ___ 
  | |   | |_    | | | |  / _ \ / __|
  | |   |  _|   | | | | |  __/ \__ \
  |_|   |_|     |_| |_|  \___| |___/




/* TFiles - Terminal File Manager
 *
 * Copyright (C) 2025  cheapdramas | bileckijrostislav46@gmail.com 
 * written by a 15 y.o at midnight
 *
 _____                                                               
|  ___|__  _ __   _   _  ___  _   _ _ __   _ __ ___   ___  _ __ ___  
| |_ / _ \| '__| | | | |/ _ \| | | | '__| | '_ ` _ \ / _ \| '_ ` _ \ 
|  _| (_) | |    | |_| | (_) | |_| | |    | | | | | | (_) | | | | | |
|_|  \___/|_|     \__, |\___/ \__,_|_|    |_| |_| |_|\___/|_| |_| |_|
                  |___/                                              
 *
 *
 *
 *
*/
//Compile: gcc tfiles-refactored.c -lncursesw
#include "config.h" 
#include <linux/limits.h>
#include <signal.h>
#include <pwd.h> 
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <errno.h>
#include <dirent.h>
#include <unistd.h>
#include <string.h>
#include <locale.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <ftw.h>
#include <ncursesw/ncurses.h>


#define COLOR_PAIR_YELLOW 1
#define COLOR_PAIR_RED 2

#define APP_NAME "tfiles"
#define DATA_DIR ".local/share/" APP_NAME

#define MALLOC_FAIL "Failed to allocate memory"


typedef struct {
  char ** filenames;
  size_t size;
  size_t last_used_index;
}FilesArray;


typedef struct{
  char *filename;
  off_t filesize;
  bool is_regular;
  int dirlen;
}FileInfo;


typedef struct{
  WINDOW *MainPopup_win;
  int MainSizeX, MainSizeY, MainStartY, MainStartX;

  WINDOW *PopupConfirm_win;
  int ConfirmSizeX, ConfirmSizeY, ConfirmStartY, ConfirmStartX;

  int last_used_y;
}Popup;


typedef struct{
  WINDOW *window;
  int height;
  int width;
}StatusLine;


typedef enum {
  SUCCESS = 0,
  ERROR = 1,
  EXISTS = 2
}OperationStatus;


typedef struct{
  char *editor; 
  
}AppState;

typedef struct{
  FilesArray files;
  Popup popup;
  StatusLine status;
  WINDOW *curses_win;
  int startX;
  char pwd[PATH_MAX];
  char previous_path[PATH_MAX];
  int highlight;
}Window;

typedef struct{
  Window *first_window, *second_window, *active_window;
  int stdscrY;
  int stdscrX;
  uint8_t window_counter;
}WindowManager;



typedef struct{
  char cache[PATH_MAX];
  char data[PATH_MAX];
}AppDataPaths;


typedef struct {
  AppState state;
  AppDataPaths data_paths;
  WindowManager winmgr;
}App;

void free_window(Window *window){
  //Free stuff from window
  if (window->curses_win != NULL){
    delwin(window->curses_win);
  }

  free(window);
}


void app_exit(App *app, const char *reason, ...){
  curs_set(1);
  endwin();

  //Free shit from app

  //Free windows
  if (app->winmgr.first_window != NULL){
    free_window(app->winmgr.first_window);
  }
  if (app->winmgr.second_window != NULL){
    free_window(app->winmgr.second_window);
  }

  //Reason is passed
  if (reason!=NULL){
    //Prepare list for arguments
    va_list args;
    va_start(args, reason);
    //Copy of list because vsnprintf will 
    //make args pointing to last argument
    va_list args_copy;
    va_copy(args_copy, args);

    //Calculate required length for reason + arguments
    int needed = vsnprintf(NULL, 0, reason, args);
    va_end(args);
    //Formatting error
    if (needed < 0){
      fprintf(stderr, "Formatting error\n");
      exit(EXIT_FAILURE);
    }

    char *buffer = malloc(needed+1);
    if (buffer == NULL){
      fprintf(stderr, "%s\n",MALLOC_FAIL);
      exit(EXIT_FAILURE);
    }
    vsnprintf(buffer, needed + 1, reason, args_copy);

    fprintf(stderr, "%s\n", buffer);

    free(buffer);
    va_end(args_copy);
  }
  exit(EXIT_SUCCESS);
}


void *malloc_wrap(App *app,size_t size) {
  void *ptr = malloc(size);
  if (ptr == NULL) {
    app_exit(app,"%s for size: %ld", MALLOC_FAIL, size);
  }
  return ptr;
}


void update_windows_size(App *app){
  //Get new window size
  getmaxyx(stdscr, app->winmgr.stdscrY, app->winmgr.stdscrX);



  //If only one window
  if (app->winmgr.window_counter < 1){
    if (app->winmgr.first_window->curses_win != NULL){
      delwin(app->winmgr.first_window->curses_win);
    }

    app->winmgr.first_window->curses_win = newwin(app->winmgr.stdscrY, app->winmgr.stdscrX, 0,0);
    //newwin failed
    if (app->winmgr.first_window->curses_win == NULL){
      app_exit(app,"Failed to create window");
    }
  }
  //If two
  else{
    if (app->winmgr.first_window->curses_win != NULL){
      delwin(app->winmgr.first_window->curses_win);
    } 
    if (app->winmgr.second_window->curses_win != NULL){
      delwin(app->winmgr.second_window->curses_win);
    }

    app->winmgr.first_window->curses_win = newwin(app->winmgr.stdscrY, app->winmgr.stdscrX / 2 - 1, 0, 1);
    if (app->winmgr.first_window->curses_win == NULL){
      app_exit(app, "Failed to create window");
    }

    app->winmgr.second_window->curses_win = newwin(app->winmgr.stdscrY, app->winmgr.stdscrX / 2, 0, app->winmgr.stdscrX/2);
    if (app->winmgr.second_window->curses_win == NULL){
      app_exit(app, "Failed to create window");
    }
  }

}



void *create_window(App *app,char pwd[PATH_MAX]){
  Window *window = malloc_wrap(app,sizeof(Window));

  window->curses_win = newwin(0, 0, 0, 0);
  if (window->curses_win == NULL){
    app_exit(app, "You are ackshually bad at this");
  }
  //First window
  if (app->winmgr.window_counter < 1){
    app->winmgr.first_window = window;
  }
  //Second_window
  else{
    app->winmgr.second_window = window;
  }

  app->winmgr.active_window = window;



  //Setup window
  
  update_windows_size(app);
  
  window->highlight = 0;  
  //If pwd is specified
  if (pwd != NULL){
    snprintf(window->pwd, sizeof(window->pwd), "%s", pwd);
  }
  //Set present working directory to .
  else{
    if (getcwd(window->pwd,sizeof(window->pwd)) == NULL){
      app_exit(app,"getcwd() failed: %s",strerror(errno));
    }
  }
  if (app->winmgr.window_counter < 1){
    app->winmgr.window_counter += 1;
  }
  return window;
}


OperationStatus create_dir(const char *path){
  if (mkdir(path,0700) < 0){
    if (errno==EEXIST){
      return EXISTS;
    }
    return ERROR;
  }
  return SUCCESS;
}


void init_ncurses(App *app){
  initscr();
  curs_set(0);
  noecho();
  keypad(stdscr, true);

  getmaxyx(stdscr, app->winmgr.stdscrY, app->winmgr.stdscrX);

	use_default_colors();
  start_color();
  init_pair(1,COLOR_YELLOW,-1);
  init_pair(2,COLOR_RED, -1);
}


void init_app_folders(App *app){
  //Orginizing tfiles data lezzgo
  const char *home_dir = getenv("HOME");
  //If not HOME value in env
  if (home_dir == NULL){
    struct passwd *pw = getpwuid(getuid());
    home_dir = pw->pw_dir;
  }//Failed to get HOME path
  if (home_dir == NULL){
    app_exit(app,"Unable to determine home directory");
  }

  snprintf(app->data_paths.data, PATH_MAX, "%s/%s", home_dir, DATA_DIR);

  if (create_dir(app->data_paths.data) == ERROR){
    app_exit(app, "Failed to create data folder at %s", app->data_paths.data);
  }
}


void init_app(int argc, char **argv, App *app){
  init_app_folders(app);

  //Set window counter to 0
  app->winmgr.window_counter = 0;


  //Create first window
  Window *first_window = create_window(app, NULL);


  //Set default editor
  app->state.editor = getenv("EDITOR"); 

  if (app->state.editor == NULL){
    app->state.editor = "vim";
  }
  
  //Parsing arguments 
  
  //No arguments passed
  if (argc < 2){
    return;
  }
  
  int argument_idx_start = 0;

  //User want last application state
  if (strcmp(argv[argument_idx_start], "-l") == 0){
    argument_idx_start++;
  }

  //Iterate through arguments 
  for (int i = argument_idx_start; i < argc; i++){
    char *argument = argv[i];
    //User set path 
    if (strcmp(argument, "-path") == 0){
      //Check if next argument avaible
      if (i + 1 < argc){
        char *path = argv[i+1];
        snprintf(first_window->pwd, sizeof(first_window->pwd), "%s", path);

        i++;
        continue;
      }
    }

    //User set editor
    if (strcmp(argument, "-editor") == 0){
      //Check if next argument avaible
      if (i + 1 < argc){
        app->state.editor = argv[i+1];

        i++;
        continue;
      }
    }
  }
}


void draw(App *app){

}


int main(int argc,char **argv){
  App app = {0};
  init_ncurses(&app);
  init_app(argc, argv, &app);


  //test
  create_window(&app, NULL);

  //Main loop

  int user_input;
  while (user_input != 'q'){
    box(app.winmgr.first_window->curses_win,0,0);
    box(app.winmgr.second_window->curses_win,0,0);
    refresh();
    wrefresh(app.winmgr.active_window->curses_win);
    wrefresh(app.winmgr.first_window->curses_win);


    user_input = getch();
    if (user_input == KEY_RESIZE){
      update_windows_size(&app);
    }
    clear();
  }

  app_exit(&app,NULL);
}
