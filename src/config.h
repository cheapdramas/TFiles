#ifndef config 
#define config 


#define MAX_ARGC = 2
#define FILE_OPENER "xdg-open"
#define STATUSLINE_HEIGHT 3
#define COLOR_PAIR_YELLOW 1
#define COLOR_PAIR_RED 2
#define APP_NAME "tfiles"
#define DATA_DIR ".local/share/" APP_NAME
#define MALLOC_FAIL_MSG "Failed to allocate memory"



//Select upper file
#define KEY_NAVUP 'k'

//Select lower file
#define KEY_NAVDOWN 'j'

//Change current directory to parent
#define KEY_NAV_PARENTDIR 'h'
#define KEY_NAV_PARENTDIR1 'b'
//Select current file | Entry in directory
#define KEY_SELECT_FILE 'l'
#define KEY_SELECT_FILE1 '\n'

#define KEY_GOTO_FILE 'g'

#define KEY_CREATE_WINDOW 'c'
#define KEY_CLOSE_WINDOW 'd'
#define KEY_SWITCH_WINDOWS '\t'
#define KEY_SWITCH_NUMBERS 'n'

//Search file/directory && Start button for find shortcuts
#define KEY_FIND_FILE 'f'
//Create new file
#define KEY_CREATE_FILE 'a'
//Delete current file
#define KEY_DELETE_FILE 'd'
//Rename current file
#define KEY_RENAME_FILE 'r'
//Get info about current file
#define KEY_FILE_INFO 'i'



#endif
