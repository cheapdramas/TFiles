//compile: gcc tfiles.c $(pkg-config ncursesw --libs --cflags)
#include <linux/limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <ncurses.h>
#include <dirent.h>
#include <unistd.h>
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
//Keeps track of the highlighted file in parent dir before we cd
int highlight_parrent_dir;
//present working directory (updates every iteration)
char pwd[PATH_MAX];
//previous path(updates only when..)
char previous_path[PATH_MAX];

WINDOW *status_line;
int status_line_height = 3;

//Minus one because we want to display all of the files at the start
int dont_draw_file_index = -1; 
int last_visible_file_index;
int first_visible_file_index;

//To prevent blocking highlight variable in loop, so it doesn`t get stuck only on previous_path and could go on
//If we showed previous_path, move highlight, and turn this to 0 so it won`t stuck
//If we cd .. than this is again 1
int show_previous_path = 1;

struct stat dir_stat;
time_t last_mtime = 0;





void init_ncurses(){
  initscr();
  getmaxyx(stdscr, stdscrY, stdscrX);
  curs_set(0);
  noecho();
  keypad(stdscr, true);
  start_color();
  init_pair(1,COLOR_YELLOW,COLOR_BLACK);
}

void opendir_wrap(DIR **dirp,char *path){
  *dirp = opendir(path);
  if (*dirp == NULL){
    endwin();
    perror("Unable to open this directory");
    exit(1);
  }
}
void status_line_newwin(){
  status_line = newwin(status_line_height,stdscrX-1,stdscrY - status_line_height,0);
}

void init(int argc,char **argv){
  //if path was specified
  if (argc == 2){
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
    endwin();
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
  //if memory allocation for filename or filetype is failed
  if (fa->filenames[fa->last_used_index] == NULL || fa->filetypes[fa->last_used_index] == NULL){
    printf("%s\n","Memory allocation failed!");
    endwin();
    exit(1);
  }
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
  fa->size = 1;
}

//fills FilesArray with files from given directory
void FilesArray_fill(FilesArray *fa){
  dirlen = 0;
  struct dirent *entry;
  char * filetype;
  char *filename;
  DIR *dirp;
  opendir_wrap(&dirp,".");


  while ((entry = readdir(dirp)) != NULL){
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
  closedir(dirp);
}



//sort filename order by name O(n²)
void FilesArray_sort(FilesArray *fa){
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

void FilesArray_new(FilesArray *filesArray){
  FilesArray_init(filesArray,1);
  FilesArray_fill(filesArray);
  FilesArray_sort(filesArray);  
}



void clear_and_recreate(){
  clear();
  werase(status_line);
  status_line_newwin();
}

void update_values(){
  getmaxyx(stdscr, stdscrY, stdscrX);
  last_visible_file_index = stdscrY- status_line_height + dont_draw_file_index - 1;
  getcwd(pwd,sizeof(pwd));
  first_visible_file_index = dont_draw_file_index;
}

bool dir_changed(){
  if (stat(pwd,&dir_stat) == 0){
    if (dir_stat.st_mtime != last_mtime){
      last_mtime = dir_stat.st_mtime;
      return true;
    }
  }

  return false;
}



void handle_user_input(FilesArray *fa,int user_input){
  //for detecting key arrows
  switch(user_input){
    //Navigating backwards
    case KEY_DOWN:
    case KEY_NAVDOWN:
       if (highlight+1 < dirlen){
        highlight ++;
        if (highlight > last_visible_file_index){
          dont_draw_file_index ++;
        }
      }
      break;

    //Navigating upwards
    case KEY_UP:
    case KEY_NAVUP:
      if (highlight > 0){
        highlight --;
        if (highlight <first_visible_file_index){
          dont_draw_file_index --;
        }
      }
      break;
    //Goes back to parent dir
    case KEY_LEFT:
    case KEY_NAV_PARENTDIR:
    case KEY_NAV_PARENTDIR1:
      //if we are not in single-dashed path 
      if (strcmp(pwd,"/") != 0){
        chdir("..");
        FilesArray_free(fa);
        FilesArray_new(fa);
        show_previous_path = 1;
        strcpy(previous_path,pwd);
        highlight = -1;
        dont_draw_file_index = -1;
        clear_and_recreate();
      }
      break;
    //selected file
    case KEY_RIGHT:
    case KEY_SELECT_FILE:
    case KEY_SELECT_FILE1:
      char *filetype = fa->filetypes[highlight];
      char *filename = fa->filenames[highlight];
      

      //if current item(file) is a directory, than chdir
      if (strcmp(filetype,"D")==0){
        chdir(filename);
        FilesArray_free(fa);
        FilesArray_new(fa);
        highlight = 0;
        dont_draw_file_index = -1;
        clear_and_recreate();
      }
      break;



    case KEY_RESIZE:
      update_values();
    
      if (highlight > last_visible_file_index){
        highlight = last_visible_file_index;
      }

      clear_and_recreate();

      break;
    default:
      break;
  }
}


void draw_files(FilesArray filesArray){
  nodelay(stdscr, false);
  char fullpath[PATH_MAX];
  char *filename;
  for (int i = 0;i<dirlen;i++){
    //if file is visible
    if (i <= last_visible_file_index){
      filename = filesArray.filenames[i];
      //getting file full path
      //if our path is single-dashed (/home,/lib,/usr)
      if (strcmp(pwd,"/") == 0){
        snprintf(fullpath,strlen(filename) + 2,"/%s",filename);
      }
      else{
        snprintf(fullpath, strlen(pwd) + strlen(filename) + 2, "%s/%s", pwd,filename);
      }
      


      //current file highlighting      
      if (strcmp(previous_path,fullpath) == 0){
        if (show_previous_path == 1){
          attroff(A_REVERSE);
          attron(A_REVERSE);
          highlight = i;
          show_previous_path = 0;
        }
      }
      if (i == highlight){
        attron(A_REVERSE);
      }  
    

      if (strcmp(filesArray.filetypes[i],"D") == 0){
        attron(COLOR_PAIR(1));
      }
      mvprintw(i + (-1 * dont_draw_file_index),0," %s\n",filesArray.filenames[i]);
      // mvprintw(i + (-1 * dont_draw_file_index),0," %s\n",fullpath);
      attroff(A_REVERSE);
      attroff(COLOR_PAIR(1));
    }
    //if file is beyond the screen
    else{
      filename = filesArray.filenames[i];
      //getting file full path
      snprintf(fullpath, strlen(pwd) + strlen(filename) + 2, "%s/%s", pwd,filename);
      //if we found previous_path that is beyond the screen
      if (strcmp(previous_path,fullpath) == 0){
        //do not let highlight get stuck on previous_path
        if (show_previous_path == 1){
          attroff(A_REVERSE);
          dont_draw_file_index = i - last_visible_file_index + 1;
          highlight = i;
          show_previous_path = 0;
          nodelay(stdscr,true);
          clear();

        }
      }

    }
  }
}

void draw_status_line(){
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

  FilesArray_new(&filesArray);



  int user_input;

  // Main loop 
  while (user_input != 'q'){
    if (dir_changed()){
      FilesArray_free(&filesArray);
      FilesArray_new(&filesArray);
    }
    //getting X,Y to draw files correctly
    getmaxyx(stdscr, stdscrY, stdscrX);
    update_values();
    draw_files(filesArray);
    draw_status_line();

    user_input = getch();
    handle_user_input(&filesArray,user_input);
    refresh();

  }


  endwin();
  return 0;
}
