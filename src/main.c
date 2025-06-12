#include "app.h"
#include "config.h"
#include "enums.h"
#include "files.h"
#include "window.h"
#include <ctype.h>
#include <limits.h>
#include <locale.h>
#include <ncursesw/ncurses.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <wchar.h>

void init_ncurses() {
  initscr();
  curs_set(0);
  noecho();
  keypad(stdscr, true);

  use_default_colors();
  start_color();
  init_pair(1, COLOR_YELLOW, -1);
  init_pair(2, COLOR_RED, -1);
}

void *malloc_wrap(App *app, size_t size) {
  void *ptr = malloc(size);
  if (ptr == NULL) {
    App_exit(app, "%s for size: %ld", MALLOC_FAIL, size);
  }
  return ptr;
}

void chdir_wrap(App *app, const char *path) {
  if (chdir(path) < 0) {
    App_exit(app, "Failed to change directory");
  }
}
// Char pointer array find element
int charp_arr_find_element(char *target, char **arr) {
  int search_index = 0;
  while (arr[search_index] != NULL) {
    if (strcmp(target, arr[search_index]) == 0) {
      return search_index;
    }
    search_index++;
  }
  return -1;
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
  int first_win_sizeX, first_win_sizeY;
  getmaxyx(first_window->curses_win, first_win_sizeY, first_win_sizeX);
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

  Window_draw(app->winmgr, app->winmgr.first_window);
  Window_draw(app->winmgr, app->winmgr.second_window);
}

void move_highlight(Window *window, int jump_counter, bool move_down) {
  if (!window->files.filenames) {
    return;
  }

  int files_count = window->files.files_count;
  int window_height = getmaxy(window->curses_win) - STATUSLINE_HEIGHT;

  if (move_down) {
    if (window->highlight + 1 >= window->files.files_count) {
      return;
    }
    // If try to jump to unexisting file position
    if (jump_counter >= files_count ||
        window->highlight + jump_counter + 1 > files_count) {
      // Set highlight to last file index
      window->highlight = files_count - 1; //-1 because we want last index
    } else {
      // if: jump_number != 0, add it
      // else: add 1
      window->highlight += jump_counter;
    }

    // Scroll
    int last_visible_file_idx = window_height + window->scroll;

    if (window->highlight + 1 >= last_visible_file_idx) {

      if (window->highlight > files_count - window_height) {
        window->scroll = files_count - window_height + 1;
      } else {
        window->scroll += jump_counter ? jump_counter : 1;
      }
    }
    return;
  }

  // Move up
  if (window->highlight - 1 < 0) {
    return;
  }

  if (jump_counter >= files_count || window->highlight - jump_counter < 0) {
    window->highlight = 0;
  } else {
    window->highlight -= jump_counter ? jump_counter : 1;
  }
  // Scroll
  if (window->highlight + 1 <= window->scroll) {
    if (window->scroll - jump_counter < 0) {
      window->scroll = 0;
    } else {
      window->scroll -= jump_counter ? jump_counter : 1;
    }
  }
  return;
}

void input_handler(App *app, int user_input) {
  int jump_counter = 0;

  while (isdigit(user_input)) {
    int digit = user_input - '0';
    if (jump_counter > (INT_MAX - digit) / 10) {
      jump_counter = INT_MAX;
      break;
    }

    jump_counter = jump_counter * 10 + (user_input - '0');
    user_input = getch();
  }

  switch (user_input) {
  // Create window
  case KEY_CREATE_WINDOW:
    if (app->winmgr.window_counter == 1) {
      Window *win = malloc_wrap(app, sizeof(Window));
      Window_create(win, &app->winmgr, NULL);
    }
    return;

  // Switch beetween windows
  case KEY_SWITCH_WINDOWS:
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
  case KEY_SWITCH_NUMBERS:
    if (app->winmgr.active_window->relative_number) {
      app->winmgr.active_window->relative_number = false;
    } else {
      app->winmgr.active_window->relative_number = true;
    }
    return;

  // Movement
  case KEY_DOWN:
  case KEY_NAVDOWN:
    move_highlight(app->winmgr.active_window, jump_counter ? jump_counter : 1, true);
    return;

  case KEY_UP:
  case KEY_NAVUP:
    move_highlight(app->winmgr.active_window, jump_counter ? jump_counter : 1, false);
    return;
	case KEY_GOTO_FILE:
		if (app->winmgr.active_window->files.files_count >= jump_counter) {
			app->winmgr.active_window->highlight = 0;
			app->winmgr.active_window->scroll = 0;
			move_highlight(app->winmgr.active_window, jump_counter, true);
		}
		return;
  // CD ..
  case KEY_LEFT:
  case KEY_NAV_PARENTDIR:
  case KEY_NAV_PARENTDIR1: {
		//e.g pwd = '/home/prog/rostik',
		//we will get here '/rostik'
		char *previous_pwd_dirname = strdup(strrchr(app->winmgr.active_window->pwd,'/'));
    int wchdir_res = Window_chdir("..", app->winmgr.active_window);
		
    if (wchdir_res == SUCCESS) {
			if (!previous_pwd_dirname) {
				return;
			}
      // Find previous directory name in current one
			int found_file_index = charp_arr_find_element(previous_pwd_dirname + 1,app->winmgr.active_window->files.filenames);
      if (found_file_index >= 0) {
				move_highlight(app->winmgr.active_window, found_file_index, true);
			}
    } else if (wchdir_res == MALLOC_FAIL) {
      App_exit(app, MALLOC_FAIL_MSG);
    }
		free(previous_pwd_dirname);

    return;
	}
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
      Window_chdir(filepath, app->winmgr.active_window);
    } else { // TODO
    }

    return;

  case KEY_CLOSE_WINDOW:
    Window_close(&app->winmgr);
    return;
  case KEY_RESIZE:
    Window_update_size(&app->winmgr);
    return;
  default:
    return;
  }
}

int main(int argc, char **argv) {
  setlocale(LC_CTYPE, "");

  App app = {0};
  init_ncurses();
  App_init(&app, argc, argv);

  // Main loop

  int user_input;
  while (user_input != 'q') {
    draw(&app);

    user_input = getch();
    input_handler(&app, user_input);
  }

  App_exit(&app, NULL);
}
