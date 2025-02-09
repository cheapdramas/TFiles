//compile: gcc tfiles.c $(pkg-config ncursesw --libs --cflags)
#include <linux/limits.h>
#include <stdio.h>
#include <ncurses.h>
#include <dirent.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <locale.h>
#include <sys/stat.h>
#include <sys/types.h>
#include "config.h" 




//global scope variables
int dirlen = 0;
int stdscrY;
int stdscrX;
int highlight;
char pwd[PATH_MAX];
DIR *dirp;

WINDOW *status_line;
int status_line_height = 3;

//Minus one because we want to display all of the files at the start
int dont_draw_file_index = -1; 
int last_visible_file_index;
int first_visible_file_index;





void init_ncurses(){
  initscr();
  getmaxyx(stdscr, stdscrY, stdscrX);
  curs_set(0);
  noecho();
  keypad(stdscr, true);
  start_color();
  init_pair(1,COLOR_YELLOW,COLOR_BLACK);
}

void opendir_wrap(char *path){
  dirp = opendir(path);
  if (dirp == NULL){
    endwin();
    perror("Unable to open this directory");
    exit(1);
  }
}
void status_line_newwin(){
  status_line = newwin(status_line_height,stdscrX-1,stdscrY - status_line_height,0);
}

void init(int argc,char **argv){
  opendir_wrap(".");
  //if path was specified
  if (argc == 2){
    opendir_wrap(argv[1]);  
    chdir(argv[1]);
  }
  if (argc > 2){
    endwin();
    printf("%s\n","Maximum amount of arguments: 2");
    exit(1);
  }
  //initializing an status_line
  status_line_newwin(); 
}

typedef struct {
  char ** filenames;
  char ** filetypes;
  size_t size;
  size_t last_used_index;
}FilesArray;


void FilesArray_init(FilesArray *fa,int start_size){
  fa->filenames = malloc(start_size * sizeof(char *));
  fa->filetypes = malloc(start_size * sizeof(char *));
  if (fa->filenames  == NULL || fa->filetypes == NULL){
    printf("%s\n","Couldn`t allocate memory!");
    exit(1);
  }

  fa->last_used_index = 0;
  fa->size = start_size;
}

void FilesArray_append(FilesArray *fa,char *filename, char * filetype){
  if (fa->last_used_index >= fa->size){
    fa->size*=2;
    fa->filenames = realloc(fa->filenames,fa->size* sizeof(char *));
    fa->filetypes = realloc(fa->filetypes,fa->size*sizeof(char *));
    if (fa->filenames  == NULL || fa->filetypes == NULL){
      printf("%s\n","Couldn`t reallocate memory!");
      endwin();
      exit(1);
    }
  }
  fa->filenames[fa->last_used_index] = strdup(filename);
  fa->filetypes[fa->last_used_index] = strdup(filetype);
  fa->last_used_index += 1;
}

void FilesArray_free(FilesArray *fa){
  //go through array elements and free every allocated memmory we did
  for (size_t i = 0;i<fa->last_used_index;i++){
    free(fa->filenames[i]);
    free(fa->filetypes[i]);
  }
  free(fa->filenames);
  free(fa->filetypes);
  fa->last_used_index = 0;
}

//fills FilesArray with files from given directory
void FilesArray_fill(DIR * dir,FilesArray *fa){
  dirlen = 0;
  struct dirent *entry;
  char * filetype;
  char *filename;
  while ((entry = readdir(dir)) != NULL){
    //no . and .. dirs
    if (strcmp(entry->d_name,".") != 0 && strcmp(entry->d_name,"..")!= 0 ){
      switch(entry->d_type){
        case DT_REG:
          filetype = "F";
          break;
        case DT_DIR:
          filetype = "D";
          break;
        case DT_LNK:
          filetype = "D";
          break;
        default:
          filetype="F";
          break;
      }
      filename = entry->d_name;
      FilesArray_append(fa,filename,filetype);
      dirlen ++;
    }
  }
}

//sort filename order by name O(n²)
void sort_files(FilesArray *fa){
  char ** filenames = fa->filenames;
  char ** filetypes = fa->filetypes;

  //main algorithm
  for (int i=0;i<dirlen;i++){
    int j_min = i;
    for (int j = i + 1;j<dirlen;j++){
      if (strcmp(filenames[j],filenames[j_min]) < 0){
        j_min = j;
      }
    }
    if (i!=j_min){
      char *temp_filename = filenames[i];
      filenames[i] = filenames[j_min];
      filenames[j_min] = temp_filename;
     
      //reindexing filetypes
      char *temp_filetype = filetypes[i];
      filetypes[i] = filetypes[j_min];
      filetypes[j_min] = temp_filetype;
    }
  }
}

void clear_and_recreate(){
  clear();
  werase(status_line);
  status_line_newwin();
}

void update_values(){
  last_visible_file_index = stdscrY- status_line_height + dont_draw_file_index - 1;
  first_visible_file_index = dont_draw_file_index;
}


void handle_user_input(int user_input){
  //for detecting key arrows
  switch(user_input){
    case KEY_DOWN:
    case KEY_NAVDOWN:
       if (highlight+1 < dirlen){
        highlight ++;
        if (highlight > last_visible_file_index){
          dont_draw_file_index ++;
        }
      }
      break;


    case KEY_NAVUP:
    case KEY_UP:
      if (highlight > 0){
        highlight --;
        
        if (highlight <first_visible_file_index){
          dont_draw_file_index --;
        }
      }
      break;


    case KEY_RESIZE:
      clear_and_recreate();
      break;
    default:
      break;
  }
}


void draw_files(FilesArray filesArray){
  for (int i = 0;i<dirlen;i++){
    //if file is visible
    if (i <= last_visible_file_index){

      if (i == highlight){
        attron(A_REVERSE);
      }
      if (strcmp(filesArray.filetypes[i],"D") == 0){
        attron(COLOR_PAIR(1));
      }
      mvprintw(i + (-1 * dont_draw_file_index),0," %s\n",filesArray.filenames[i]);
      attroff(A_REVERSE);
      attroff(COLOR_PAIR(1));
    }
  }
}

void draw_status_line(){
  getcwd(pwd,sizeof(pwd)); 
  refresh();
  mvprintw(stdscrY-(status_line_height-1),1,"%s",pwd);

  box(status_line,0,0);
  wrefresh(status_line);
}


int main(int argc,char **argv){
  //SETLOCALE out of init function, idk how to fix this
  setlocale(LC_CTYPE, "");
  init_ncurses();
  init(argc,argv);

  FilesArray filesArray;
  FilesArray_init(&filesArray,1);
  FilesArray_fill(dirp,&filesArray);
  sort_files(&filesArray);  



  int user_input;

  // Main loop 
  while (user_input != 'q'){
    //getting X,Y to draw files correctly
    getmaxyx(stdscr, stdscrY, stdscrX);
    update_values();
    draw_files(filesArray);
    draw_status_line();

    user_input = getch();
    //Getting new X,Y if user resized window
    getmaxyx(stdscr, stdscrY, stdscrX);
    handle_user_input(user_input);
    refresh();
  }


  endwin();
  return 0;
}
