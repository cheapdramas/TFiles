//gcc tfiles.c $(pkg-config ncursesw --libs --cflags) -lm
#include "config.h"
#include "enums.h"
#include "files.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <ftw.h>
#include <linux/limits.h>
#include <locale.h>
#include <ncursesw/ncurses.h>
#include <pwd.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <wchar.h>
#include <math.h>
#include <ctype.h>

#define COLOR_PAIR_YELLOW 1
#define COLOR_PAIR_RED 2

#define APP_NAME "tfiles"
#define DATA_DIR ".local/share/" APP_NAME

#define MALLOC_FAIL_MSG "Failed to allocate memory"

#define STATUSLINE_HEIGHT 3

typedef struct {
  char *filename;
  off_t filesize;
  bool is_regular;
  int dirlen;
} FileInfo;

typedef struct {
  WINDOW *MainPopup_win;
  int MainSizeX, MainSizeY, MainStartY, MainStartX;

  WINDOW *PopupConfirm_win;
  int ConfirmSizeX, ConfirmSizeY, ConfirmStartY, ConfirmStartX;

  int last_used_y;
} Popup;


typedef struct {
  char *editor;
  bool debug;
} AppState;


typedef struct {
  FilesArray files;
  WINDOW *curses_win;
	bool relative_number;
  char pwd[PATH_MAX];
  char previous_path[PATH_MAX];
  int highlight;
  int scroll;
} Window;

typedef struct {
  Window *first_window, *second_window, *active_window;

  uint8_t window_counter;
} WindowManager;

typedef struct {
  char cache[PATH_MAX];
  char data[PATH_MAX];
} AppDataPaths;

typedef struct {
  AppState state;
  AppDataPaths data_paths;
  WindowManager winmgr;
} App;

int number_length(int n) {
	//e.g n = 100
	if (n == 0) return 1;
	//log10(100) = 2 + 1 = 3
	return (int)log10(n) + 1;
}

void init_ncurses(App *app) {
  initscr();
  curs_set(0);
  noecho();
  keypad(stdscr, true);

  use_default_colors();
  start_color();
  init_pair(1, COLOR_YELLOW, -1);
  init_pair(2, COLOR_RED, -1);
}

void destroy_ncurses_window(WINDOW **win) {
  if (*win == NULL) {
    return;
  }
  werase(*win);
  wrefresh(*win);
  delwin(*win);
  *win = NULL;
}


void free_window(Window **window) {
  if (*window == NULL) {
    return;
  }
  Window *window_ = *window;
  // Free stuff from window
  destroy_ncurses_window(&window_->curses_win);
  FilesArray_free(&window_->files);

  free(window_);
  *window = NULL;
}

void app_exit(App *app, const char *reason, ...) {
  // Free windows
  free_window(&app->winmgr.first_window);
  free_window(&app->winmgr.second_window);

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

FileType get_filetype(char *filepath) {
  struct stat file_stat;
  if (stat(filepath, &file_stat) < 0) {
    return -1;
  }

  if (S_ISDIR(file_stat.st_mode) == 1) {
    return DIRECTORY;
  }
  return REGULAR;
}

void *malloc_wrap(App *app, size_t size) {
  void *ptr = malloc(size);
  if (ptr == NULL) {
    app_exit(app, "%s for size: %ld", MALLOC_FAIL, size);
  }
  return ptr;
}

void chdir_wrap(App *app, const char *path) {
  if (chdir(path) < 0) {
    app_exit(app, "Failed to change directory");
  }
}

void clear_window(Window *window) {
  // Clears only what inside in window (pwd, files)
  int maxY, maxX;
  getmaxyx(window->curses_win, maxY, maxX);
	

  // Clear files
  for (int y = 1; y < maxY; y++) {
    mvwhline(window->curses_win, y, 1, ' ', maxX - 2);
  }
	//Clear pwd
	mvwhline(window->curses_win, 1 + maxY - STATUSLINE_HEIGHT, 1, ' ', maxX - 2);

	wrefresh(window->curses_win);
}

void chdir_window(App *app, const char *path, Window *window) {
  if (strlen(window->pwd) == 1 &&
      strcmp(path, "..") == 0) { // If at '/' and trying to cd ..
    return;
  }
  chdir_wrap(app, window->pwd);
  chdir_wrap(app, path);
  if (getcwd(window->pwd, sizeof(window->pwd)) == NULL) {
    app_exit(app, "You are akshually bad at this");
  }
  window->highlight = 0; // Потом так не делать
  window->scroll = 0;
  FilesArray_free(&window->files);
  if (FilesArray_fill(&window->files, window->pwd) > 0)
  {
    app_exit(app, NULL);
  }
	clear_window(window);
}

void copy_window(App *app, Window *dest, Window *src) {
  if (src->files.filenames == NULL) {
    return;
  }
  if (dest->files.filenames != NULL) {
    FilesArray_free(&dest->files);
  }

  strncpy(dest->pwd, src->pwd, sizeof(src->pwd));
  dest->files.size = src->files.size;
  dest->files.files_count = src->files.files_count;
  dest->files.filenames = malloc_wrap(app, dest->files.size * sizeof(char *));
  memset(dest->files.filenames, 0, dest->files.size * sizeof(char *));

  for (int i = 0; i < dest->files.files_count; i++) {
    char *src_filename = src->files.filenames[i];
    dest->files.filenames[i] = strdup(src_filename);
    if (dest->files.filenames[i] == NULL) {
      app_exit(app, MALLOC_FAIL_MSG);
    }
  }
}

void update_windows_size(App *app) {
  // Get new window size
  int stdscrY, stdscrX;
  getmaxyx(stdscr, stdscrY, stdscrX);

  // If only one window
  if (app->winmgr.window_counter == 1) {
    destroy_ncurses_window(&app->winmgr.active_window->curses_win);

    app->winmgr.active_window->curses_win = newwin(stdscrY, stdscrX, 0, 0);
    // newwin failed
    if (app->winmgr.active_window->curses_win == NULL) {
      app_exit(app, "Failed to create window");
    }

    // If highlight is not in range of current window size
    if (stdscrY - 5 < 0) {
      return;
    }
    if (app->winmgr.active_window->highlight >
        (stdscrY - 5) + app->winmgr.active_window->scroll) {
      // Set new scroll, so highlight would stay on the last file
      app->winmgr.active_window->scroll +=
          app->winmgr.active_window->highlight -
          ((stdscrY - 5) + app->winmgr.active_window->scroll);
    }
  }
  // If two
  else {
    destroy_ncurses_window(&app->winmgr.first_window->curses_win);
    destroy_ncurses_window(&app->winmgr.second_window->curses_win);

    app->winmgr.first_window->curses_win =
        newwin(stdscrY, stdscrX / 2 - 1, 0, 1);
    if (app->winmgr.first_window->curses_win == NULL) {
      app_exit(app, "Failed to create window");
    }

    app->winmgr.second_window->curses_win =
        newwin(stdscrY, stdscrX / 2, 0, stdscrX / 2);
    if (app->winmgr.second_window->curses_win == NULL) {
      app_exit(app, "Failed to create window");
    }

    // If highlight is not in range of current windows size
    // Nothing to scroll
    if (stdscrY - 5 < 0) {
      return;
    }

    if (app->winmgr.first_window->highlight >
        (stdscrY - 5) + app->winmgr.first_window->scroll) {
      // Set new scroll, so highlight would stay on the last file
      app->winmgr.first_window->scroll +=
          app->winmgr.first_window->highlight -
          ((stdscrY - 5) + app->winmgr.first_window->scroll);
    }
    if (app->winmgr.second_window->highlight >
        (stdscrY - 5) + app->winmgr.second_window->scroll) {
      app->winmgr.second_window->scroll +=
          app->winmgr.second_window->highlight -
          ((stdscrY - 5) + app->winmgr.second_window->scroll);
    }
  }
}

Window *create_window(App *app, const char *pwd) {
  Window *window = malloc_wrap(app, sizeof(Window));

  window->files.filenames = NULL;
  window->highlight = 0;
  window->curses_win = NULL;
  window->scroll = 0;

  // If pwd is specified
  if (pwd != NULL) {
    snprintf(window->pwd, sizeof(window->pwd), "%s", pwd);
  }
  // Set present working directory to .
  else {
    if (getcwd(window->pwd, sizeof(window->pwd)) == NULL) {
      app_exit(app, "getcwd() failed: %s", strerror(errno));
    }
  }

  // Increasing window counter
  if (app->winmgr.window_counter <= 1) {
    app->winmgr.window_counter += 1;
  }

  // First window (only at the start)
  if (app->winmgr.window_counter == 1) {
    app->winmgr.first_window = window;
  }
  // Two windows
  else {
    // Check which window to create
    // Assign new window to first
    if (!app->winmgr.first_window) {
      copy_window(app, window, app->winmgr.second_window);
      app->winmgr.first_window = window;
    }
    // Assign new window to second
    else {
      copy_window(app, window, app->winmgr.first_window);
      app->winmgr.second_window = window;
    }
  }
  app->winmgr.active_window = window;

  // Create WINDOW* with appropriate size
  update_windows_size(app);

  return window;
}

void close_window(App *app) {
  if (app->winmgr.window_counter < 2) {
    return;
  }

  // Active window is first one, remove first
  if (app->winmgr.active_window == app->winmgr.first_window) {
    free_window(&app->winmgr.first_window);
    app->winmgr.active_window = app->winmgr.second_window;
  }
  // Active window is second one, remove second
  else {
    free_window(&app->winmgr.second_window);
    app->winmgr.active_window = app->winmgr.first_window;
  }

  app->winmgr.window_counter -= 1;

  update_windows_size(app);
}

int create_dir(const char *path) {
  if (mkdir(path, 0700) < 0) {
    if (errno == EEXIST) {
      return EXISTS;
    }
    return ERROR;
  }
  return SUCCESS;
}

void init_app_folders(App *app) {
  // Orginizing tfiles data lezzgo
  const char *home_dir = getenv("HOME");
  // If not HOME value in env
  if (home_dir == NULL) {
    struct passwd *pw = getpwuid(getuid());
    home_dir = pw->pw_dir;
  } // Failed to get HOME path
  if (home_dir == NULL) {
    app_exit(app, "Unable to determine home directory");
  }

  snprintf(app->data_paths.data, PATH_MAX, "%s/%s", home_dir, DATA_DIR);

  if (create_dir(app->data_paths.data) == ERROR) {
    app_exit(app, "Failed to create data folder at %s", app->data_paths.data);
  }
}

void parse_app_arguments(App *app, int argc, char **argv) {
  // Init editor
  // If user passed an editor, it will change later
  if ((app->state.editor = getenv("EDITOR")) == NULL) {
    app->state.editor = "vim";
  }

  // Check if arguments passed
  if (argc < 2) {
    return;
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
        chdir_wrap(app, path);

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
}

void init_app(App *app, int argc, char **argv) {
  // Init data folders
  init_app_folders(app);

  // Parsing arguments
  parse_app_arguments(app, argc, argv);

  // Create first window
  app->winmgr.window_counter = 0;
  Window *first_window = create_window(app, NULL);

  // Fill window with files
  int fill_res = FilesArray_fill(&first_window->files, first_window->pwd); 
  if (fill_res > 0) {
    if (fill_res == MALLOC_FAIL){
      app_exit(app, MALLOC_FAIL_MSG);
    }
    else {
      app_exit(app, NULL);
    }
  }


}

// If text is bigger than window width,
//"Trim" or split it with '~'
// e.g plank-reloaded = plank~oaded or pla~ed
// With pwd=true we gonna chop only the start
void trim_text(bool is_pwd, wchar_t *dest, const char *src, int sizeX) {
  // Convert src to wide char
  wchar_t wsrc[1024];
  mbstowcs(wsrc, src, 1024);

  int src_len = wcswidth(wsrc, -1);
  // Nothing to trim
  if (src_len < sizeX) {
    wcsncpy(dest, wsrc, sizeX);
    dest[sizeX] = L'\0';
    return;
  }

  // Trim pwd
  if (is_pwd) {
    // src: /home/rostik/folder1/folder2
    // trimmed: ~/rostik/folder1/folder2
    // OR       ~/folder1/folder2  etc

    // How much to trim from start
    int trim_count = src_len - sizeX;
    // First char is ~
    dest[0] = L'~';
    // Copy the rest of the src
    wcsncpy(dest + 1, 1 + wsrc + trim_count, sizeX);
    dest[sizeX] = L'\0';

    return;
  }

  // Trim any other text
  // But somewhere in middle

  int mid_index = sizeX / 2 / wcwidth(wsrc[0]);

  wcsncpy(dest, wsrc, mid_index);
  dest[mid_index] = L'~';
  wcsncpy(dest + mid_index + 1, wsrc + mid_index + 2, mid_index);
  dest[sizeX / wcwidth(wsrc[0])] = '\0';
}

void draw_inactive_window_box(Window *window, int sizeY, int sizeX) {
  WINDOW *curs_win = window->curses_win;

  // Corners
  mvwaddch(curs_win, 0, 0, '+');
  mvwaddch(curs_win, 0, sizeX - 1, '+');
  mvwaddch(curs_win, sizeY - 1, 0, '+');
  mvwaddch(curs_win, sizeY - 1, sizeX - 1, '+');

  // Left & right lines
  mvwvline(curs_win, 1, 0, '|', sizeY - 2);
  mvwvline(curs_win, 1, sizeX - 1, '|', sizeY - 2);

  // Top & bottom lines
  mvwhline(curs_win, 0, 1, '-', sizeX - 2);
  mvwhline(curs_win, sizeY - 1, 1, '-', sizeX - 2);

  wrefresh(curs_win);
}


void draw_window(App *app, Window *window) {
  if (window == NULL) {
    return;
  }

  // Get window size
  int window_size_y, window_size_x;
  getmaxyx(window->curses_win, window_size_y, window_size_x);

  // Draw box around window
  if (app->winmgr.active_window == window) {
    box(window->curses_win, 0, 0);
  } else {
    draw_inactive_window_box(window, window_size_y, window_size_x);
  }

  // Draw line for status_line;
  int status_line_y = getmaxy(window->curses_win) - STATUSLINE_HEIGHT;
  int status_line_x = getmaxx(window->curses_win) - 2;
  mvwhline(window->curses_win, status_line_y, 1, '-', status_line_x);

  // Display pwd
  wchar_t pwd[window_size_x];
  trim_text(true, pwd, window->pwd, window_size_x - 2);
  mvwaddwstr(window->curses_win, status_line_y + 1, 1, pwd);

  wrefresh(window->curses_win);
  // Draw files
  if (window->files.filenames == NULL) {
    return;
  }

  int window_limit = window_size_y - STATUSLINE_HEIGHT;
  int filename_draw_y = 1;
  int filename_draw_x = number_length(window->files.files_count) + 2;

  for (int i = window->scroll; i < window->files.files_count; i++) {
    // Leave from loop if on window limit
    if (filename_draw_y >= window_limit) {
      break;
    }

    char *filename = window->files.filenames[i];
    wchar_t filename_trimmed[window_size_x];
    trim_text(false, filename_trimmed, filename, window_size_x - filename_draw_x);

    // Getting file type
    // TODO. can you cache smth here? (at least, with cache values, check if dir
    // is changed, it`s gonna be faster)
    char filepath[PATH_MAX];
    strcpy(filepath, window->pwd);
    strcat(filepath, "/");
    strcat(filepath, filename);
    FileType filetype = get_filetype(filepath);

    mvwhline(window->curses_win, filename_draw_y, 1, ' ', window_size_x - 2);

    if (filetype == DIRECTORY) {
      wattron(window->curses_win, COLOR_PAIR(COLOR_PAIR_RED));
    }
    if (window->highlight == i) {
      wattron(window->curses_win, A_REVERSE);
    }
    // Draw file index
    if (!window->relative_number || window->highlight == i) { // Draw absolute numbers
      mvwprintw(window->curses_win, filename_draw_y, 1, "%d", i);
    } else { // Draw relative numbers */
      mvwprintw(window->curses_win, filename_draw_y, 1, "%d", abs(window->highlight - i));
    }
		//Display file
    mvwaddwstr(window->curses_win, filename_draw_y, filename_draw_x, filename_trimmed);

    wattroff(window->curses_win, COLOR_PAIR(COLOR_PAIR_RED));
    wattroff(window->curses_win, A_REVERSE);

    filename_draw_y++;
  }

  wrefresh(window->curses_win);
}

void draw_debug(App *app) {

	Window *first_window = app->winmgr.first_window;
	Window *second_window = app->winmgr.second_window;


  int terminal_size_y, terminal_size_x;
  getmaxyx(stdscr, terminal_size_y, terminal_size_x);

  mvwprintw(stdscr, 0, 0, "Terminal size: {\n X: %d;\n Y: %d;\n}",
            terminal_size_x, terminal_size_y);
  // mvwprintw(stdscr, 3, 0, "Splitted window size: {\n X: %d;\n Y: %d;\n}",
  //           getmaxx(second_window->curses_win),
  //           getmaxy(second_window->curses_win));
  //
  // draw_window(app, app->winmgr.first_window);
	int first_win_sizeX,first_win_sizeY;
  getmaxyx(first_window->curses_win, first_win_sizeY,first_win_sizeX);
	first_win_sizeY -= STATUSLINE_HEIGHT;
  mvwprintw(stdscr, 4, 0, "Window size: {\n X: %d;\n Y: %d;\n}",
            first_win_sizeX, first_win_sizeY);



  refresh();
}

void draw(App *app) {
  refresh();
  // If debug mode is on
  if (app->state.debug) {
    draw_debug(app);
    return;
  }

  draw_window(app, app->winmgr.first_window);
  draw_window(app, app->winmgr.second_window);
}

void move_highlight(Window *window, int jump_counter, bool move_down){
	if (!window->files.filenames){
		return;
	}

	int files_count = window->files.files_count;
	int window_height = getmaxy(window->curses_win) - STATUSLINE_HEIGHT;

	if (move_down){
    if (window->highlight + 1 >= window->files.files_count) {
      return;
    }
		//If try to jump to unexisting file position
		if (jump_counter >= files_count || window->highlight + jump_counter > files_count){
			//Set highlight to last file index 
			window->highlight = files_count - 1; //-1 because we want last index 
		}
		else{
			//if: jump_number != 0, add it
			//else: add 1
			window->highlight += jump_counter ? jump_counter : 1;
		}


    // Scroll
    int last_visible_file_idx = window_height + window->scroll;

    if (window->highlight + 1 >= last_visible_file_idx) {

			if (window->highlight > files_count - window_height) {
				window->scroll = files_count - window_height + 1;
			}
			else{
				window->scroll += jump_counter ? jump_counter : 1;
			}

		}
    return;
	}

	//Move up
	if (window->highlight - 1 < 0) {
		return;
	}
	

	if (jump_counter >= files_count || window->highlight - jump_counter < 0){
		window->highlight = 0;
	}
	else{
		window->highlight -= jump_counter ? jump_counter : 1;
	}
	// Scroll
	if (window->highlight + 1 <= window->scroll) {
		if (window->scroll - jump_counter < 0){
			window->scroll = 0;
		}
		else{
			window->scroll -= jump_counter ? jump_counter : 1;
		}
	}
	return;
}

void input_handler(App *app, int user_input) {
	int jump_counter = 0;

	while (isdigit(user_input)){
		jump_counter = jump_counter * 10 + (user_input - '0'); 
		user_input = getch();
	}

  switch (user_input) {
  // Create window
  case 'c':
    if (app->winmgr.window_counter == 1) {
      create_window(app, NULL);
    }
    return;

  // Switch beetween windows
  case '\t':
    if (app->winmgr.window_counter < 2) {
      return;
    }
    if (app->winmgr.active_window == app->winmgr.first_window) {
      app->winmgr.active_window = app->winmgr.second_window; // Switch to second
    } else {
      app->winmgr.active_window = app->winmgr.first_window; // Switch to first
    }

    return;
	// Switch between numbers mode: relative / absolute
	case 'n':
		if (app->winmgr.active_window->relative_number){
			app->winmgr.active_window->relative_number = false;
		}	
		else{
			app->winmgr.active_window->relative_number = true;
		}
		return;
		
  // Movement
  case KEY_DOWN:
  case KEY_NAVDOWN:
		move_highlight(app->winmgr.active_window,jump_counter,true);
		return;

  case KEY_UP:
  case KEY_NAVUP:
		move_highlight(app->winmgr.active_window,jump_counter,false);
		return;
  // CD ..
  case KEY_LEFT:
  case KEY_NAV_PARENTDIR:
  case KEY_NAV_PARENTDIR1:
    chdir_window(app, "..", app->winmgr.active_window);
    return;
  // cd - if file is dir
  // edit if file is text
  // execute if file is executable
  case KEY_RIGHT:
  case KEY_SELECT_FILE:
  case KEY_SELECT_FILE1:
    if (!app->winmgr.active_window->files.filenames) {
      return;
    }
    // Determine file type
    int highlight = app->winmgr.active_window->highlight;
    char *filename = app->winmgr.active_window->files.filenames[highlight];
    char *win_pwd = app->winmgr.active_window->pwd;
    char filepath[PATH_MAX];
    strcpy(filepath, win_pwd);
    strcat(filepath, "/");
    strcpy(filepath, filename);

    FileType filetype = get_filetype(filepath);
    if (filetype == DIRECTORY) {
      chdir_window(app, filepath, app->winmgr.active_window);
    } else { // TODO
    }

    return;

  case 'd':
    close_window(app);
    return;
  case KEY_RESIZE:
    update_windows_size(app);
    return;

  default:
    return;
  }
}

int main(int argc, char **argv) {
  setlocale(LC_CTYPE, "");

  App app = {0};
  init_ncurses(&app);
  init_app(&app, argc, argv);

  // Main loop

  int user_input;
  while (user_input != 'q') {
    draw(&app);

    user_input = getch();
    input_handler(&app, user_input);
  }

  app_exit(&app, NULL);
}

