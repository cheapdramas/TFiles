//compile: gcc tfiles.c $(pkg-config ncursesw --libs --cflags)
#define _XOPEN_SOURCE 500
#include <linux/limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
// #include <ncurses.h>
#include <dirent.h>
#include <unistd.h>
#include <string.h>
#include <locale.h>
#include <sys/stat.h>
#include <sys/types.h>
#include "config.h" 
#include <sys/wait.h>
#include <fcntl.h>
#include <ftw.h>
#include <ncursesw/ncurses.h>

#define PAIR_COLOR_YELLOW 1
#define PAIR_COLOR_RED 2



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

//Popup dialog window
typedef struct{
  WINDOW *MainPopup_win;
  int MainSizeX,MainSizeY,MainStartY,MainStartX;

  WINDOW *PopupConfirm_win;
  int ConfirmSizeX,ConfirmSizeY,ConfirmStartY,ConfirmStartX;
}Popup;




//Minus one because we want to display all of the files at the start
int dont_draw_file_index = -1; 
int last_visible_file_index;
int first_visible_file_index;

//To prevent blocking highlight variable in loop, so it doesn`t get stuck only on previous_path and could go on
//If we showed previous_path, move highlight, and turn this to 0 so it won`t stuck
//If we cd .. than this is again 1
int show_previous_path = 1;

//fzf
//flag for fzf and draw_files funcs
//in fzf we found file and put this flag to 1
//in draw_files in flag is 1 we search for that file and put highlight on it
//after we displayed file = flag is 0 again
int show_found_file= 0;
char found_filename[NAME_MAX];
char found_dir[PATH_MAX];

time_t last_mtime = 0;


char *editor = NULL;


typedef enum {
  SUCCESS = 0,
  ERROR = 1,
  FILE_EXISTS = 2
}FileStatus;




void init_ncurses(){
  initscr();
	use_default_colors();
  getmaxyx(stdscr, stdscrY, stdscrX);
  curs_set(0);
  noecho();
  keypad(stdscr, true);
  start_color();
  init_pair(1,COLOR_YELLOW,-1);
  init_pair(2,COLOR_RED, -1);
}

void opendir_wrap(DIR **dirp,char *path){
  *dirp = opendir(path);
  if (*dirp == NULL){
    endwin();
    perror("Unable to open this directory");
    exit(1);
  }
}
void chdir_wrap(char *path){
  if (  (chdir(path)) != 0){
    endwin();
    perror("Unable to change directory");
    exit(EXIT_SUCCESS);
  }
}


void draw_border(WINDOW *w){
  int left,right,top,bottom,tlc,trc,blc,brc;
  left = right = ' ';
  top = 0;
  bottom = blc =brc = ' ';
  tlc = trc = blc= brc = ' ';
  wborder(w,left,right,top,bottom, tlc,trc,blc,brc);
  wrefresh(w);
}

void status_line_newwin(){
  status_line = newwin(status_line_height,stdscrX-1,stdscrY - status_line_height,0);
}
void draw_status_line(){
  refresh();
  draw_border(status_line);
  // mvprintw(stdscrY-(status_line_height-1),1,"%s",pwd);
  mvwprintw(status_line,1,2,"%s",pwd);
  wrefresh(status_line);
}
void update_status_line(){
  werase(status_line);
  wrefresh(status_line);
  delwin(status_line);
  status_line_newwin();
  draw_status_line();
}

void clear_screen(){
  clear();
  
  //immediately update status_line, as we try to keep it static, and 
  //not updating it every main cycle
  update_status_line();
}




void init(int argc,char **argv){
  //argv[0] = always program name
  //argv[1] = must be a start path
  
  if ((editor = getenv("EDITOR")) == NULL){
    editor = "vim";
  }

  if (argc >=  2){
    //start iterating through arguments 
    for (int i = 1;i < argc;i++){
      char *argument = argv[i];

      //if we found "-path" argument
      if (strcmp(argument,"-path") == 0){
        if (i + 1 <= argc - 1){
          //Changing directory
          chdir_wrap(argv[i+1]); 
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
  if ( (dirp = opendir(".")) !=NULL){
    while ((entry = readdir(dirp)) != NULL){
      //no . and .. dirs
      if (strcmp(entry->d_name,".") != 0 && strcmp(entry->d_name,"..")!= 0 ){
        filename = entry->d_name;
        FilesArray_append(fa,filename);
        dirlen ++;
      }
    }
  }
  else{
    attron(COLOR_PAIR(PAIR_COLOR_RED));
    mvprintw(0,0,"Directory opening failed | Check your permissions or path validity");
    attroff(COLOR_PAIR(PAIR_COLOR_RED)); 
    getch();

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


void update_values();
void draw_files(FilesArray filesArray);

// Mark







Popup popup;
// Those functions described below main function
void PopupDelete(FilesArray *fa,char *filename);
void PopupRenameFile(char *filename,FilesArray *fa);
void PopupCreateFile(FilesArray *fa);

void Popup_update_size_and_pos(){
  popup.MainSizeY = 13;
  if (popup.MainSizeY > stdscrY){
    popup.MainSizeY = stdscrY;
  }
  popup.MainSizeX = 62;
  if (popup.MainSizeX > stdscrX){
    popup.MainSizeX= stdscrX;
  }
  popup.MainStartY = (stdscrY / 2) - popup.MainSizeY/2;
  popup.MainStartX = (stdscrX / 2) - popup.MainSizeX / 2;


  //Can someone explain to me why it doesn`t shows up
  //at 35x9?
  //pls god
  //chatgpt help me
	//my math sucks
	//я сраний двоющнік(токо по фізікі ;) )
  popup.ConfirmSizeY  = popup.MainSizeY / 5;
  popup.ConfirmSizeX  = popup.MainSizeX - 2;
  popup.ConfirmStartY = popup.MainStartY + popup.MainSizeY-3 ;
  popup.ConfirmStartX = popup.MainStartX+1;
}

void Popup_draw_error(char *ErrorText){
  werase(popup.MainPopup_win);
  box(popup.MainPopup_win,0,0);
  mvwprintw(popup.MainPopup_win,popup.MainSizeY - 2,2,"%s",ErrorText);
  mvwprintw(popup.MainPopup_win,0,1,"Press any button to continue");
  wrefresh(popup.MainPopup_win);
}
//0 == yes  || 1 == no
void Popup_draw_confirm_buttons(int which_button){
	
  if (which_button == 0)
	{
    wattron(popup.PopupConfirm_win,A_REVERSE);
    mvwprintw(popup.PopupConfirm_win,1,1,"yes");
    wattroff(popup.PopupConfirm_win,A_REVERSE);
    mvwprintw(popup.PopupConfirm_win,1,popup.ConfirmSizeX - 3,"no");
  }else{
    wattron(popup.PopupConfirm_win,A_REVERSE);
    mvwprintw(popup.PopupConfirm_win,1,popup.ConfirmSizeX - 3,"no");
    wattroff(popup.PopupConfirm_win,A_REVERSE);
    mvwprintw(popup.PopupConfirm_win,1,1,"yes");
  }

  wrefresh(popup.PopupConfirm_win);
}
void Popup_draw_base(int draw_confirm){
  refresh();
  Popup_update_size_and_pos(); 


  popup.MainPopup_win = newwin(
    popup.MainSizeY,
    popup.MainSizeX,
    popup.MainStartY,
    popup.MainStartX
  );
  box(popup.MainPopup_win,0,0);
  wrefresh(popup.MainPopup_win);

	if (draw_confirm){
		popup.PopupConfirm_win = newwin(
			 popup.ConfirmSizeY, 
			 popup.ConfirmSizeX,
			 popup.ConfirmStartY,  
			 popup.ConfirmStartX
		);
		draw_border(popup.PopupConfirm_win);
		wrefresh(popup.PopupConfirm_win);
	}
}

void PopupPrintInside(char *text,int consider_confirm){
  int y = 1;
  int x = 1;
  char *old_text= text;
  if (!consider_confirm){
    popup.ConfirmSizeY = 0;
  }
  for (int i = 0;old_text[i] != '\0';i++){
    // if (y < popup.MainSizeY - popup.ConfirmSizeY - 2 + consider_confirm){
    //Out of border
    if (x > popup.MainSizeX - 2){
      x=1;
      y+=1;
      if (y > popup.MainSizeY - 2){return;}
    }
    wchar_t wc;        
    mbtowc(&wc, text, MB_CUR_MAX);
    mvwprintw(popup.MainPopup_win, y, x, "%lc", wc);  // Print first character
    text+=1;
    x+=1;
  }
}



int PopupInput(
	char *input_from_start,
	char **final_input,
  size_t final_input_size,
	void (*draw_func)(),
	FilesArray *fa
){
	curs_set(1);	
	int user_input;
	int text_len =0;

  char text_dummy[final_input_size];

	int cursor_x= 1,cursor_y = 1;
	int cursor_to_word = 0;
	int confirm;

	if (input_from_start!=NULL){
    // Set final_input to input_from_start, so we could start editing the right
		// thing
		text_len = strlen(input_from_start); 		
		cursor_to_word = text_len;
    strcpy(text_dummy,input_from_start);
		// This will set our cursor into the last character of input_from_start
		PopupPrintInside(text_dummy,0);
    
		// Updating cursor position
		getyx(popup.MainPopup_win,cursor_y,cursor_x);
	}
	strcpy(*final_input,text_dummy);


	wmove(popup.MainPopup_win,cursor_y,cursor_x);
	wrefresh(popup.MainPopup_win);

	

	while (true){
		user_input = getch();
		if (user_input == '\n'){
			confirm = 1;
			break;
		}
		if (user_input == 27){
			confirm = 0;
			break;
		}
	
    // Bro is typing characters
    if (user_input != 127 && user_input != '\b'&& user_input != KEY_BACKSPACE && user_input != KEY_LEFT && user_input != KEY_RIGHT && user_input != KEY_UP && user_input != KEY_DOWN && user_input != '\t' && user_input != KEY_RESIZE)
		{
      if (text_len + 1 <= final_input_size){
				// If not on the end of input
        if (cursor_to_word < text_len){
          char replaced_char = text_dummy[cursor_to_word];
          text_dummy[cursor_to_word] = user_input; 
          for (int i = cursor_to_word + 1; i < text_len+1;i++){
            char temp = text_dummy[i];
            text_dummy[i] = replaced_char;
            replaced_char = temp;
          }
          text_dummy[text_len += 1] = '\0';
          cursor_x += 1;
          if (cursor_x > popup.MainSizeX - 2){
            if (cursor_y < popup.MainSizeY - 1){
              cursor_x = 1;
              cursor_y += 1;
            }
          }
        }else{
          text_dummy[text_len] = user_input;
          text_dummy[text_len+= 1] = '\0';
        }

        cursor_to_word += 1;




        strcpy(*final_input,text_dummy);
        PopupPrintInside(*final_input,0);

				if (cursor_to_word < text_len){
					wmove(popup.MainPopup_win,cursor_y,cursor_x);
				}
				else
				{
					getyx(popup.MainPopup_win,cursor_y,cursor_x);
				}


				wrefresh(popup.MainPopup_win);
		}		
  }	
		if (user_input == '\b' || user_input == KEY_BACKSPACE){
			for (int i = cursor_to_word - 1;i<text_len;i++){
				text_dummy[i] = text_dummy[i+1];
			}
			if (cursor_to_word > 0){
				cursor_to_word -= 1;
			}
			if (text_len > 0){
				text_len -= 1;
				text_dummy[text_len] = '\0';
			}

			if (cursor_y > 1){
				if (cursor_x - 1 < 1){
					cursor_x = popup.MainSizeX - 2;
					cursor_y -= 1;
				}
				// Free move
				else{
					cursor_x -= 1;
				}
			}
			// We are on first Y
			else
			{
				if (cursor_x > 1){
					cursor_x -= 1;
				}
			}

			delwin(popup.MainPopup_win);
			draw_func(); 
			strcpy(*final_input,text_dummy);
			PopupPrintInside(*final_input, 0);
			wmove(popup.MainPopup_win,cursor_y,cursor_x);

			wrefresh(popup.MainPopup_win);
		}
 
		if (user_input == KEY_LEFT){
			if (cursor_to_word > 0){cursor_to_word -= 1;}


			// Check X limitations, based on which Y level we are
			if (cursor_y > 1){
				if (cursor_x - 1 < 1){
					cursor_y -= 1;
					cursor_x = popup.MainSizeX - 2;
					wmove(popup.MainPopup_win,cursor_y,cursor_x);
				}
				// Free move
				else{
					wmove(popup.MainPopup_win,cursor_y,cursor_x-=1);
				}
			}
			// cursor_y = 1
			else{
				if (cursor_x > 1){
					wmove(popup.MainPopup_win,cursor_y,cursor_x-=1);
				}
			}
			wrefresh(popup.MainPopup_win);
		}

		if (user_input == KEY_RIGHT){
      if (cursor_to_word < text_len){
        cursor_to_word += 1;
        if (cursor_x + 1 > popup.MainSizeX - 2){
          if (cursor_y < popup.MainSizeY - 1){
            cursor_x = 1;
            cursor_y += 1;
            wmove(popup.MainPopup_win,cursor_y,cursor_x);
          }
        }
        // Free move
        else{
          wmove(popup.MainPopup_win,cursor_y,cursor_x += 1);
        }
      }
      wrefresh(popup.MainPopup_win);
		}

    if (user_input == KEY_RESIZE){
      clear_screen();
      getmaxyx(stdscr, stdscrY,stdscrX);
      
      int x,y;
      cursor_x = cursor_y = 1;

      delwin(popup.MainPopup_win);
      
      //update popup.Main... size and pos values
      Popup_update_size_and_pos();
      update_values();
      draw_files(*fa);
      refresh();
			draw_func();
      PopupPrintInside(*final_input, 0);
      getyx(popup.MainPopup_win,y,x);
      for (int i = 0; i < cursor_to_word;i++){
        if (cursor_x > popup.MainSizeX - 2){
          cursor_x = 1;
          // if (cursor_y < MainSizeY - 2){}
          cursor_y += 1;
        }
        cursor_x += 1;
      }
      wmove(popup.MainPopup_win,cursor_y,cursor_x); 
      wrefresh(popup.MainPopup_win);
    }
	}
	curs_set(0);
  refresh();
	werase(popup.MainPopup_win);
	wrefresh(popup.MainPopup_win);
	return confirm;
}






void PopupDelete_draw(char *filename){
  int confirm= 1;
  Popup_draw_base(confirm);
  mvwprintw(popup.MainPopup_win,0,popup.MainSizeX / 2-5 ,"Delete files?");
  PopupPrintInside(filename,confirm);
  wrefresh(popup.MainPopup_win);
}
void PopupCreateFile_draw(){
	Popup_draw_base(0);
  mvwprintw(popup.MainPopup_win,0,popup.MainSizeX / 2-5 ,"Create new file");
  wrefresh(popup.MainPopup_win);
}
void PopupRenameFile_draw(){
	Popup_draw_base(0);
  mvwprintw(popup.MainPopup_win,0,popup.MainSizeX / 2-5 ,"Rename files?");
  wrefresh(popup.MainPopup_win);
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



void resize_event(){
  // updating values (for new screen size)
  update_values();
  //if highlight is trying to get out of a user view
  if (highlight > last_visible_file_index){
    if (last_visible_file_index >= 0){
      highlight = last_visible_file_index;
    }
  }
  clear_screen();
}

//Getch wrap to hold unexpected resize signal
int getch_wrap(){
  int input = getch();
  if (input == KEY_RESIZE){
    resize_event();
  }  
  return input;
}



//calls 'file <filename>' to get file info and writes it to filecmdresult
void cmdFile(char *filename,char filecmdresult[64]){
  int size_cmd = strlen(filename) + 6;
  char cmd[size_cmd];
  snprintf(cmd,size_cmd,"file %s",filename);
  FILE *stream;
  if ( (stream = popen(cmd,"r")) ){
    fgets(filecmdresult,64,stream);
  }
  pclose(stream);
}






// /If <filename> type is a text like, open it in current terminal session (vim or whatever)
//Else: let the xdg-open command work
void open_file(char *filename){
  char filecmdresult[64];
  cmdFile(filename,filecmdresult);
  if (strstr(filecmdresult,"text") || strstr(filecmdresult,"empty")){
		endwin();
	
		
		sigset_t new_mask, old_mask;
		//block resize signal, so our app wont get crazy
		sigemptyset(&new_mask);
		sigaddset(&new_mask, SIGWINCH);
		sigprocmask(SIG_BLOCK, &new_mask, &old_mask);


		pid_t pid = fork();
		//spawn new process
		if (pid == 0){
			//replace new process with vim or whatever editor user put in argv
			execlp(editor,editor,filename,NULL);
			//if execlp failed
			exit(EXIT_SUCCESS);
		}else{
			//parent process
			int status;
			//waiting for child process(file editing) to finish
			waitpid(pid,&status,0);
		}
		//restores signal mask, now SIGWINCH can be delivered
		sigprocmask(SIG_SETMASK, &old_mask, NULL);

		refresh();
		clear_screen();
		init_ncurses();
		//force send resize signal so our app will process it again after blocking
		raise(SIGWINCH);  
  }
  
  //if file is not text related or empty
  else{
    pid_t pid;
    //spawning new process
    pid = fork();
    if (pid == 0){
      //change stdout descriptor so we wont get any warning and shit from xdg-open
      int null_fd = open("/dev/null",O_WRONLY);
      dup2(null_fd,2);
      //replaces process with xdg-open 
      execlp("xdg-open","xdg-open",filename,NULL); 
    }
  }
}

FileStatus create_file(char filename[NAME_MAX]){
  int filename_len = strlen(filename);
  if (filename[filename_len- 1] == '/'){
    filename[filename_len - 1] = '\0';



    if (mkdir(filename, 0755) == 0) {
      return SUCCESS;
    } 
    else {
      return ERROR;
    }
  }

  else
  {
    struct stat buffer;

    // If file exists
    if (stat(filename,&buffer) == 0){
      return FILE_EXISTS;
    }
    // Try to create file
    FILE *file =fopen(filename,"w");
    // Error appiered
    if (!file){
      return ERROR;
    }


    fclose(file);
    return SUCCESS;
  }
}


FileStatus delete_file(char *filename){
  if (remove(filename) != 0){
    return ERROR;
  }
  return SUCCESS;
}

FileStatus rename_file(char *old_filename,char *new_filename){
	if (rename(old_filename,new_filename) == 0){
		return SUCCESS;
	}
	
	return ERROR;
}

//for remove_directory func
int unlink_cb(const char *fpath, const struct stat *sb, int typeflag, struct FTW *ftwbuf){
  int rv = remove(fpath);

  return rv;
}

int remove_directory(char *path)
{
  return nftw(path, unlink_cb, 64, FTW_DEPTH | FTW_PHYS);
}


void *fzf(char *mode){
  endwin();

	sigset_t new_mask, old_mask;
	//block resize signal
	sigemptyset(&new_mask);
	sigaddset(&new_mask, SIGWINCH);
	sigprocmask(SIG_BLOCK, &new_mask, &old_mask);



  if (strcmp(mode,"DEFAULT") == 0){
		FILE *stream;
    const char *cmd = "(find -maxdepth 1 ! -name '.') | sed 's|^\\./||' | fzf";
    if (  (stream = popen(cmd,"r"))  ){
      fgets(found_filename,NAME_MAX,stream);
    }

    found_filename[strlen(found_filename) - 1] = '\0';

    show_found_file = 1;
    pclose(stream);
    sigprocmask(SIG_SETMASK, &old_mask, NULL);
		raise(SIGWINCH);
	}
  
  if (strcmp(mode,"DIRS") == 0){
    FILE *stream;
    const char *cmd = "find / -type d 2>/dev/null | fzf";

    if (  (stream = popen(cmd,"r"))  ){
      fgets(found_dir,PATH_MAX,stream);
    }
    if (strcmp(found_dir,pwd) !=0){
      found_dir[strlen(found_dir) - 1] = '\0';
    } 
    chdir(found_dir);

    sigprocmask(SIG_SETMASK, &old_mask, NULL);
		raise(SIGWINCH);

  }
  
  init_ncurses();
}





void handle_user_input(FilesArray *fa,int user_input){
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
        update_values();

        clear_screen();
      }
      break;
    //selected file
    case KEY_RIGHT:
    case KEY_SELECT_FILE:
    case KEY_SELECT_FILE1:
      if (dirlen > 0){
        char *filename = fa->filenames[highlight];
        

        //if current item(file) is a directory, than chdir
        if (is_dir(filename)){
          chdir(filename);
          //updating current directory
          update_values();

          clear_screen();

          FilesArray_free(fa);
          FilesArray_new(fa);
          //reset highlight
          highlight = 0;
          //reset dont_draw_file_index
          dont_draw_file_index = -1;
          //just to update last_mtime for new directory
          //to not compare with old last_mtime for previous directory
          update_last_mtime();
        }
        //FILE
        else{
          open_file(filename);
        }
      }

      break;
    
    case KEY_FIND_FILE:
      int second_input = getch_wrap();
      //ff
      if (second_input == 'f'){
        fzf("DEFAULT");
      }
      //fc - start of possible "fcd"
      if (second_input == 'c'){
        int third_input = getch_wrap();
        //"fcd" - fast change directory
        if (third_input == 'd'){
          clear_screen();


          fzf("DIRS");

          //reseting app variables
          FilesArray_free(fa);
          FilesArray_new(fa);
          if (strcmp(pwd,found_dir) != 0){
            highlight = 0;
            dont_draw_file_index = -1;
          } 
          
          update_last_mtime();
        }

      }
      break;


    case KEY_DELETE_FILE:
      if (dirlen > 0){
        char *filename = fa->filenames[highlight];
        char message[11 + strlen(filename)];
        snprintf(message, 11+ strlen(filename), "Delete %s ?",filename);

        char *message_p = &message[0];  

        PopupDelete(fa,filename);
      }

      break; 

		case KEY_CREATE_FILE:
			PopupCreateFile(fa);
			break;
		case KEY_RENAME_FILE:
			if (dirlen > 0){
        char *filename = fa->filenames[highlight];
				PopupRenameFile(filename,fa);
			}
	
       
    default:
      break;
  }
}


void draw_files(FilesArray filesArray){
  nodelay(stdscr, false);
  char fullpath[PATH_MAX];
  char *filename;
  for (int i = 0;i<dirlen;i++){
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






    //if file is visible (or somewhere above)
    if (i <= last_visible_file_index){



      //if we just got out of fzf
      char *chr_ptr_found_filename = &found_filename[0];
      if (show_found_file == 1){
        if (strcmp(filename, chr_ptr_found_filename) == 0){
          highlight = i;
          if (i < dont_draw_file_index){
            dont_draw_file_index = i;
          }
          nodelay(stdscr,true);
        }      
      }


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
        attron(COLOR_PAIR(PAIR_COLOR_YELLOW));
      }
      //Printing the filename (-1 * dont_draw_file_index creates the scroll effect)
      mvprintw(i + (-1 * dont_draw_file_index),0," %s\n",filesArray.filenames[i]);
      attroff(A_REVERSE);
      attroff(COLOR_PAIR(PAIR_COLOR_YELLOW));
    }
    //if file is beyond the screen
    else{
      //if we just got out of fzf
      char *chr_ptr_found_filename = &found_filename[0];
      if (show_found_file == 1){
        if (strcmp(filename, chr_ptr_found_filename) == 0){
          highlight = i;
          dont_draw_file_index = i - (stdscrY - 4);
          nodelay(stdscr,true);
          clear_screen();
        }      
      }
  



      //if we found previous_path that is beyond the screen
      if (strcmp(previous_path,fullpath) == 0){
        //do not let highlight get stuck on previous_path
        if (show_previous_path == 1){
          attroff(A_REVERSE); //just in case (may be useless)
          // setting dont_draw_file_index to new value, so we can see the previous_path that were beyond the screen
          highlight = i;
          dont_draw_file_index = i - (stdscrY - 4);
          show_previous_path = 0;
          //To ingore getch() in current iteration, so we can straight jump into new draw_files() function with new dont_draw_file_index
          nodelay(stdscr,true);
          clear_screen(); //just in case (works)
        }
      }

    }
  }
  show_found_file = 0;
}




int main(int argc,char **argv){
  //SETLOCALE out of init function, idk how to fix this
  setlocale(LC_CTYPE, "");
  init_ncurses();
  init(argc,argv);

  FilesArray filesArray;
  FilesArray_new(&filesArray);
  update_last_mtime();



  int user_input;

  draw_status_line(); 
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
      clear_screen();
    }
    //getting X,Y to draw files correctly
    getmaxyx(stdscr, stdscrY, stdscrX);
    update_values();
    draw_files(filesArray);
    /* draw_status_line(); */

    user_input = getch_wrap();
    handle_user_input(&filesArray,user_input);
    refresh();

  }


  endwin();
  return 0;
}

void PopupDelete(FilesArray *fa,char *filename){
  PopupDelete_draw(filename);
  Popup_draw_confirm_buttons(0);

  int user_input = 1;
  int which_button = 0;
  while(user_input){
    user_input = getch();
    switch (user_input) {
      //Escape button (why is it so slow)
      case 27:
        clear_screen();

        break;
  
      //Move to "no"
      case KEY_RIGHT:
      case KEY_SELECT_FILE:
        which_button = 1;
        Popup_draw_confirm_buttons(which_button);
        break;
      //Move to "yes"
      case KEY_LEFT:
      case KEY_NAV_PARENTDIR:
        which_button = 0;
        Popup_draw_confirm_buttons(which_button);
        break;


      //YES
      case 'y':
      case 'Y':
        if (is_dir(filename)){
            if (remove_directory(filename) != 0){
              Popup_draw_error("Unable to delete this file");
              getch_wrap();
            }
          }
        else{
            if (delete_file(filename)!= 0){
              Popup_draw_error("Unable to delete this file");
              getch_wrap();
            }
          }

        
        delwin(popup.MainPopup_win);
        delwin(popup.PopupConfirm_win);
        FilesArray_free(fa);
        FilesArray_new(fa);
        return;
      //NO
      case 'n':
      case 'N':
        werase(popup.MainPopup_win);
        werase(popup.PopupConfirm_win);
        wrefresh(popup.MainPopup_win);
        wrefresh(popup.PopupConfirm_win);
        delwin(popup.MainPopup_win);
        delwin(popup.PopupConfirm_win);
        return;

      case '\n':
        //YES
        if (which_button == 0){
          if (is_dir(filename)){
            if (remove_directory(filename) != 0){
              Popup_draw_error("Unable to delete this file");
              getch_wrap();
            }
          }
          else{
            if (delete_file(filename)!= 0){
              Popup_draw_error("Unable to delete this file");
              getch_wrap();
            }
          }

          
          clear_screen();
          delwin(popup.MainPopup_win);          
          delwin(popup.PopupConfirm_win);
          FilesArray_free(fa);
          FilesArray_new(fa);
          return;
        }
        //NO
        else{
          werase(popup.MainPopup_win);
          werase(popup.PopupConfirm_win);
          wrefresh(popup.MainPopup_win);
          wrefresh(popup.PopupConfirm_win);
          delwin(popup.MainPopup_win);          
          delwin(popup.PopupConfirm_win);
          return;
        }





      case KEY_RESIZE:
        update_values();
        clear_screen();
        getmaxyx(stdscr, stdscrY,stdscrX);
        
        werase(popup.MainPopup_win);
        wrefresh(popup.MainPopup_win);
        werase(popup.PopupConfirm_win);
        wrefresh(popup.PopupConfirm_win);



        delwin(popup.MainPopup_win);
        delwin(popup.PopupConfirm_win);
        //update popup.Main... size and pos values
        Popup_update_size_and_pos();
        draw_files(*fa);
        refresh();
        PopupDelete_draw(filename);
        Popup_draw_confirm_buttons(which_button);
        break;
    }
  }
  delwin(popup.MainPopup_win);
  delwin(popup.PopupConfirm_win);
}
void PopupCreateFile(FilesArray *fa){
  PopupCreateFile_draw();
	int confirm;
	char *filename = malloc(NAME_MAX);
  if (filename == NULL){
    endwin();
    printf("Memory allocation failed: filename for PopupCreateFile");
    exit(1);
  }


	confirm = PopupInput(NULL,&filename,NAME_MAX,&PopupCreateFile_draw,fa);

	if (confirm){
		if (strlen(filename) > 0)
		{
		 	create_file(filename);
		 	show_found_file = 1;
		 	strcpy(found_filename,filename);
		}
	}

	
	delwin(popup.MainPopup_win);
	free(filename);
}
void PopupRenameFile (char *old_filename,FilesArray *fa){
	PopupRenameFile_draw();
	int confirm_rename;
	char *new_filename= malloc(NAME_MAX);
  if (new_filename == NULL){
    endwin();
    printf("Memory allocation failed: new_filename for PopupRenameFile");
    exit(1);
  }


	confirm_rename = PopupInput(old_filename,&new_filename,NAME_MAX,&PopupRenameFile_draw,fa);
	if (confirm_rename){
		if (rename_file(old_filename,new_filename) != SUCCESS){
			Popup_draw_error("Can`t rename this file");
			getch_wrap();
		}
	}

  werase(popup.MainPopup_win);
  wrefresh(popup.MainPopup_win);
	delwin(popup.MainPopup_win);
  free(new_filename);
}
