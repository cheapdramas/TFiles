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
//Keeps pwd before cd ..
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

time_t last_mtime = 0;


char *editor = NULL;



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
  //argv[0] = always program name
  //argv[1] = must be a start path

  if (argc >=  2){

    //start iterating through arguments 
    for (int i = 1;i < argc;i++){
      char *argument = argv[i];

      //if we found "-path" argument
      if (strcmp(argument,"-path") == 0){
        if (i + 1 <= argc - 1){
          //Changing directory
          if (chdir(argv[i+1]) == -1){
            endwin();
            perror("Bad argument(path)! Make sure that first argument is a valid path\n");
            exit(1);
          }
        }
      }

      
      //if we found "-editor" argument
      if (strcmp(argument,"-editor") == 0){
        //We make sure that we have our next argument as editor name
        //i + 1 as next index from editor
        //argc - 1 as we comparing indexes(start from 0)
        if (i + 1<= argc - 1){
          editor = argv[i+1];
        }
        else
        {
          endwin();
          printf("Bad argument(editor)! Make sure to provide editor name after '-editor' argument \n");
          exit(1);
        }

      }


      


    }
  }
  // initializing an status_line
  getcwd(pwd,sizeof(pwd));
  status_line_newwin(); 
}

typedef struct {
  char ** filenames;
  size_t size;
  size_t last_used_index;
}FilesArray;


void FilesArray_init(FilesArray *fa,int start_size){
  fa->filenames = malloc(start_size * sizeof(char *));
  if (fa->filenames  == NULL){
    printf("%s\n","Couldn`t allocate memory!");
    endwin();
    exit(1);
  }

  fa->last_used_index = 0;
  fa->size = start_size;
}

void FilesArray_append(FilesArray *fa,char *filename){
  if (fa->last_used_index >= fa->size){
    fa->size*=2;
    fa->filenames = realloc(fa->filenames,fa->size* sizeof(char *));
    if (fa->filenames  == NULL){
      printf("%s\n","Couldn`t reallocate memory!");
      endwin();
      exit(1);
    }
  }
  fa->filenames[fa->last_used_index] = strdup(filename);
  //if memory allocation for filename or filetype is failed
  if (fa->filenames[fa->last_used_index] == NULL){
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
  }
  free(fa->filenames);
  fa->last_used_index = 0;
  fa->size = 1;
}

//fills FilesArray with files from given directory
void FilesArray_fill(FilesArray *fa){
  dirlen = 0;
  struct dirent *entry;
  char *filename;
  DIR *dirp;
  opendir_wrap(&dirp,".");


  while ((entry = readdir(dirp)) != NULL){
    //no . and .. dirs
    if (strcmp(entry->d_name,".") != 0 && strcmp(entry->d_name,"..")!= 0 ){
      filename = entry->d_name;
      FilesArray_append(fa,filename);
      dirlen ++;
    }
  }
  closedir(dirp);
}

//for qsort
int compare_filenames(const void *a, const void *b) {
  return strcmp(*(char **)a, *(char **)b);
}

void FilesArray_sort(FilesArray *fa) {
  qsort(fa->filenames, dirlen, sizeof(char *), compare_filenames);
}

//sort filename order by name O(n²)

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

//We can use this function to check for changes in dir
//Or we can use this function to update last_mtime
bool dir_have_changes(){
  struct stat dir_stat;
  if (stat(pwd,&dir_stat) == 0){
    if (dir_stat.st_mtime != last_mtime){
      last_mtime = dir_stat.st_mtime;
      return true;
    }
  }
  return false;
}

//Just for clarity
void update_last_mtime(){
  struct stat dir_stat;
  if (stat(pwd,&dir_stat) == 0){
    if (dir_stat.st_mtime != last_mtime){
      last_mtime = dir_stat.st_mtime;
    }
  }
}



bool is_dir(char *filename){
  struct stat file_stat;
  if (stat(filename,&file_stat) != 0){
    return false;
  }
  return S_ISDIR(file_stat.st_mode);
}

//If <filename> type is a text like, open it in current terminal session (vim or whatever)
//Else: let the xdg-open command work
void open_file(char *filename){
  //here we will keep the result that 'file' command gave us
  //I do not know why 64 and why = {0};
  char buf[64] = {0};
 
  //here we will keep the command
  //I do not know why 64 and why = {0};
  char cmd[64]= {0};

  FILE *stream;
  
  //creating cmd string
  snprintf(cmd,64,"%s %s","file",filename);




  //Here we call 'file' to filename
  //At one we will check that call is successfull
  if ( (stream = popen(cmd,"r")) ){
    if ( fgets(buf,64,stream) ){

        pclose(stream);

      if (strstr(buf,"text") || strstr(buf,"empty")){
        
        if (editor != NULL) {
          char syscall[strlen(editor) + strlen(filename) + 3];
          snprintf(syscall,strlen(editor) + strlen(filename) + 3,"%s %s", editor,filename);
          def_prog_mode();
          endwin();
          system(syscall);
         
        }
        else
        {
          char syscall[strlen(filename) + 5];
          snprintf(syscall,strlen(filename) + 5,"%s %s","vim",filename);
          def_prog_mode();
          endwin();
          system(syscall); 
        }
      }
      //xdg-open || open  
      else
      {
        char syscall[strlen(filename) + 8];
        snprintf(syscall,strlen(filename) + 8,"open \"%s\"",filename);
        system(syscall);
        clear();
        refresh();


        


      }
    }
  }
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
        //-1 As non existing index
        //If 0: we would leave the mark of 'selected' on the first file, is file is visible
        //But as we go to parent dir, we want to our highlight point to previous_path
        highlight = -1;
        //-1 As non existing index
        //(This would change when we find previous_path in draw_files)
        dont_draw_file_index = -1;

        update_last_mtime();

        clear_and_recreate();
      }
      break;
    //selected file
    case KEY_RIGHT:
    case KEY_SELECT_FILE:
    case KEY_SELECT_FILE1:
      if (dirlen >= 1){
        char *filename = fa->filenames[highlight];
        

        //if current item(file) is a directory, than chdir
        if (is_dir(filename)){
          chdir(filename);
          FilesArray_free(fa);
          FilesArray_new(fa);
          //reset highlight
          highlight = 0;
          //reset dont_draw_file_index
          dont_draw_file_index = -1;
          //just to update last_mtime for new directory
          //to not compare with old last_mtime for previous directory
          update_last_mtime();

          clear_and_recreate();
        }
        //FILE
        else{
          open_file(filename);
        }


      }
      break;



    case KEY_RESIZE:
      //updating values (for new screen size)
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
        //getting full path, length of filename + 2 ('/0' and '/')
        snprintf(fullpath,strlen(filename) + 2,"/%s",filename);
      }
      else{
        snprintf(fullpath, strlen(pwd) + strlen(filename) + 2, "%s/%s", pwd,filename);
      }
      


      //current file highlighting      
      if (strcmp(previous_path,fullpath) == 0){
        //If we just cd ..
        if (show_previous_path == 1){
          attroff(A_REVERSE); //CHECK may be useless here
          attron(A_REVERSE);
          //setting highlight to found previous_path
          highlight = i;
          //setting this to 0 so we won`t get highlight stuck on previous_path
          show_previous_path = 0;
        }
      }
      if (i == highlight){
        attron(A_REVERSE);
      }  
    

      if (is_dir(filename)){
        attron(COLOR_PAIR(1));
      }
      //Printing the filename (-1 * dont_draw_file_index creates the scroll effect)
      mvprintw(i + (-1 * dont_draw_file_index),0," %s\n",filesArray.filenames[i]);
      attroff(A_REVERSE);
      attroff(COLOR_PAIR(1));
    }
    //if file is beyond the screen
    else{
      filename = filesArray.filenames[i];
      snprintf(fullpath, strlen(pwd) + strlen(filename) + 2, "%s/%s", pwd,filename);
      //if we found previous_path that is beyond the screen
      if (strcmp(previous_path,fullpath) == 0){
        //do not let highlight get stuck on previous_path
        if (show_previous_path == 1){
          attroff(A_REVERSE); //just in case (may be useless)
          // setting dont_draw_file_index to new value, so we can see the previous_path that were beyond the screen
          dont_draw_file_index = i - last_visible_file_index + 1;
          highlight = i;
          show_previous_path = 0;
          //To ingore getch() in current iteration, so we can straight jump into new draw_files() function with new dont_draw_file_index
          nodelay(stdscr,true);
          clear(); //just in case (works)
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
    if (dir_have_changes()){
      FilesArray_free(&filesArray);
      FilesArray_new(&filesArray);
      //if highlighted was the last file, that got deleted
      //highlight is on 1 bigger than dirlen, as it`s start from 0 (==)
      if (highlight == dirlen){
        mvprintw(0,0,"%d   %d", highlight,dirlen);
        //put highlight on the last file in directory
        highlight = dirlen - 1;
      }
      //clear so we can draw new changes in directory
      clear();
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
