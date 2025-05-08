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
  char **filenames;
  size_t size;
  unsigned int last_index;
}FilesArray;


typedef struct{
  FilesArray files;
  WINDOW *curses_win;
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


void destroy_ncurses_window(WINDOW **win){
  if (*win == NULL){
    return;
  }
  werase(*win);
  wrefresh(*win);
  delwin(*win);
  *win = NULL;
}


void free_files_window(Window *window){
  if (window->files.filenames == NULL){
    return;
  }
  
  for (int i = 0; i < window->files.last_index; i++){
    free(window->files.filenames[i]);
  }
  free(window->files.filenames);

  window->files.last_index = 0;
  window->files.size = 0;
  window->files.filenames = NULL;
}


void free_window(Window **window){
  if (*window == NULL){
    return;
  }
  Window *window_ = *window;
  //Free stuff from window
  destroy_ncurses_window(&window_->curses_win);
  free_files_window(*window);

  free(window_);
  *window = NULL;
}


void app_exit(App *app, const char *reason, ...){
  //Free windows
  free_window(&app->winmgr.first_window);
  free_window(&app->winmgr.second_window);


  curs_set(1);
  echo();
  endwin();


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


void chdir_wrap(App *app,const char *path){
  if (chdir(path) < 0){
    app_exit(app, "Failed to change directory");
  }
}


//for qsort
int compare_filenames(const void *a, const void *b) {
  return strcmp(*(char **)a, *(char **)b);
}


void fill_files_window(App *app, Window *window){
  if (window->files.filenames != NULL){
    free_files_window(window);
  }

  window->files.last_index = 0;
  window->files.size = 2;
  window->files.filenames = malloc_wrap(app, window->files.size * sizeof(char *));
  memset(window->files.filenames, 0, window->files.size * sizeof(char *));

  DIR *dirp;
  struct dirent *entry; 
  dirp = opendir(window->pwd); 
  if (dirp == NULL){
    app_exit(app,"Failed to open directory");
    return;
  }

  //Reading directory
  while ((entry = readdir(dirp)) != NULL){
    //Skip if filename = "." or ".."
    if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0){
      continue;
    }

    //Realloc if array too small
    if (window->files.last_index >= window->files.size){
      size_t old_size = window->files.size;
      window->files.size *= 2;

      char ** new_filenames = realloc(window->files.filenames,window->files.size * sizeof(char *));
      if (new_filenames == NULL){
        app_exit(app, MALLOC_FAIL);
      }
      //Initialize new reallocated empty space,
      //So it won't point to garbage
      memset(new_filenames + old_size, 0, (window->files.size - old_size) * sizeof(char *));

      //Update array
      window->files.filenames = new_filenames;
    }
    
    window->files.filenames[window->files.last_index] = strdup(entry->d_name);
    if (window->files.filenames[window->files.last_index] == NULL){
      app_exit(app, MALLOC_FAIL);
    }

    window->files.last_index += 1;
  }

  //If after reading directory, last_index = 0
  //Than directory is empty
  if (window->files.last_index == 0){
    free_files_window(window);
  }
  
  //Sort filenames
  qsort(window->files.filenames, window->files.last_index, sizeof(char *), compare_filenames);

  closedir(dirp);
}


void copy_files_window(App *app, Window *dest, Window *src){
  if (src->files.filenames == NULL){
    return;
  }
  if (dest->files.filenames != NULL){
    free_files_window(dest);
  }

  dest->files.size = src->files.size;
  dest->files.last_index = src->files.last_index;
  dest->files.filenames = malloc_wrap(app, dest->files.size * sizeof(char * ));
  memset(dest->files.filenames, 0, dest->files.size * sizeof(char *));

  for (int i = 0; i < dest->files.last_index; i++){
    char *src_filename = src->files.filenames[i];
    dest->files.filenames[i] = strdup(src_filename);
    if (dest->files.filenames[i] == NULL){
      app_exit(app, MALLOC_FAIL);
    }
  }
} 


void update_windows_size(App *app){
  //Get new window size
  getmaxyx(stdscr, app->winmgr.stdscrY, app->winmgr.stdscrX);



  //If only one window
  if (app->winmgr.window_counter == 1){
    destroy_ncurses_window(&app->winmgr.active_window->curses_win);

    app->winmgr.active_window->curses_win = newwin(app->winmgr.stdscrY, app->winmgr.stdscrX, 0,0);
    //newwin failed
    if (app->winmgr.active_window->curses_win == NULL){
      app_exit(app,"Failed to create window");
    }
  }
  //If two
  else{
    destroy_ncurses_window(&app->winmgr.first_window->curses_win);
    destroy_ncurses_window(&app->winmgr.second_window->curses_win);

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


Window *create_window(App *app,const char *pwd){
  Window *window = malloc_wrap(app,sizeof(Window));

  window->files.filenames = NULL;
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


  //Increasing window counter
  if (app->winmgr.window_counter <= 1){
    app->winmgr.window_counter += 1;
  }

  //First window (only at the start)
  if (app->winmgr.window_counter == 1){
    app->winmgr.first_window = window;
  }
  //Two windows
  else{
    //Check which window to create
    //Assign new window to first
    if (!app->winmgr.first_window){
      copy_files_window(app, window,app->winmgr.second_window);
      app->winmgr.first_window = window;
    } 
    //Assign new window to second
    else{
      copy_files_window(app, window,app->winmgr.first_window);
      app->winmgr.second_window = window;
    }
  }
  app->winmgr.active_window = window;
  
  


  //Create WINDOW* with appropriate size
  window->curses_win = NULL;
  update_windows_size(app);


  return window;
}

void close_window(App *app){
  if (app->winmgr.window_counter < 2){
    return;
  }

    //Active window is first one, remove first
  if (app->winmgr.active_window == app->winmgr.first_window){
    free_window(&app->winmgr.first_window);
    app->winmgr.active_window = app->winmgr.second_window;
  }
  //Active window is second one, remove second
  else{
    free_window(&app->winmgr.second_window);
    app->winmgr.active_window = app->winmgr.first_window;
  } 
  
  
  app->winmgr.window_counter -= 1;

  update_windows_size(app);
}


OperationStatus create_dir(const char *path){
  if (mkdir(path,0700) < 0){
    if (errno == EEXIST){
      return EXISTS;
    }
    return ERROR;
  }
  return SUCCESS;
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


void parse_app_arguments(App *app, int argc, char **argv){
  //Init editor
  //If user passed an editor, it will change later
  if ((app->state.editor = getenv("EDITOR")) == NULL){
    app->state.editor = "vim";
  }

  //Check if arguments passed
  if (argc < 2){return;}
  
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
        chdir_wrap(app, path);

        i++;
        continue;
      }
    }

    //User set editor
    if (strcmp(argument, "-editor") == 0){
      //Check if next argument avaible
      if (i + 1 < argc){
        app->state.editor= argv[i+1];

        i++;
        continue;
      }
    }
  }
}


void init_app(App *app, int argc, char **argv){
  //Init data folders
  init_app_folders(app);

  //Parsing arguments
  parse_app_arguments(app, argc, argv);

  //Create first window
  app->winmgr.window_counter = 0;
  Window *first_window = create_window(app, NULL);

  //Fill window with files
  fill_files_window(app, first_window);
}


void draw_window(App *app, Window *window){
  if (window == NULL){
    return;
  }

  //Draw box around window
  box(window->curses_win, 0, 0);
  
  //Draw files
  if (window->files.filenames == NULL){
    return;
  }
  
  //Get window size
  int window_size_y, window_size_x;
  getmaxyx(window->curses_win, window_size_y, window_size_x);

  int filename_index = 0;
  int filename_draw_y = 1;

  if (window->highlight > window_size_y){
    filename_index = window->highlight;
  }
  
  

  for (int i = filename_index; i < window->files.last_index; i++){
    char *filename = window->files.filenames[i];

    if (window->highlight == i){
      wattron(window->curses_win, A_REVERSE);
    }
    mvwprintw(window->curses_win,filename_draw_y,1,"%s",filename);
    wattroff(window->curses_win, A_REVERSE);


    filename_draw_y ++;
  }

  wrefresh(window->curses_win);
}


void draw(App *app){
  refresh();
  draw_window(app,app->winmgr.first_window);
  draw_window(app,app->winmgr.second_window);
}




int main(int argc,char **argv){
  App app = {0};
  init_ncurses(&app);
  init_app(&app,argc, argv);



  //Main loop

  int user_input;
  while (user_input != 'q'){
    draw(&app);

    user_input = getch();
    if (user_input == 'c'){
      if (app.winmgr.window_counter == 1){
        create_window(&app, NULL);
      }
    }


    if (user_input == KEY_RIGHT && app.winmgr.window_counter > 1 && app.winmgr.active_window == app.winmgr.first_window){
      werase(app.winmgr.first_window->curses_win);
      app.winmgr.active_window = app.winmgr.second_window;
    }
    if (user_input == KEY_LEFT && app.winmgr.window_counter > 1 && app.winmgr.active_window == app.winmgr.second_window){
      werase(app.winmgr.second_window->curses_win);
      app.winmgr.active_window = app.winmgr.first_window;
    }
    if (user_input == KEY_DOWN && 
        app.winmgr.active_window->files.filenames &&
        app.winmgr.active_window->files.last_index -1 > app.winmgr.active_window->highlight
    ){
      app.winmgr.active_window->highlight += 1;
    }
    if (user_input == KEY_UP && 
        app.winmgr.active_window->files.filenames &&
        app.winmgr.active_window->highlight >= 1
    ){
      app.winmgr.active_window->highlight -= 1;
    }



    if (user_input == 'd'){
      close_window(&app);
    }
   
    if (user_input == KEY_RESIZE){
      update_windows_size(&app);
    }
  }

  app_exit(&app,NULL);
}
