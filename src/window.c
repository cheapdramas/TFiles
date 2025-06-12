#include "window.h"
#include "enums.h"
#include "files.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wchar.h>
#include <math.h>


extern void trim_text(bool is_pwd, wchar_t *dest, const char *src, int sizeX) {
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

extern void free_ncurses_window(WINDOW **win) {
  if (*win == NULL) {
    return;
  }
  werase(*win);
  wrefresh(*win);
  delwin(*win);
  *win = NULL;
}

extern int Window_copy(Window *dest, Window *src) {
  if (src->files.filenames == NULL) {
    return ERROR;
  }
  if (dest->files.filenames != NULL) {
    FilesArray_free(&dest->files);
  }

  strncpy(dest->pwd, src->pwd, sizeof(src->pwd));
  dest->files.size = src->files.size;
  dest->files.files_count = src->files.files_count;
  dest->files.filenames = malloc(dest->files.size * sizeof(char *));
  if (dest->files.filenames == NULL) {
    return MALLOC_FAIL;
  }
  memset(dest->files.filenames, 0, dest->files.size * sizeof(char *));

  for (int i = 0; i < dest->files.files_count; i++) {
    char *src_filename = src->files.filenames[i];
    dest->files.filenames[i] = strdup(src_filename);
    if (dest->files.filenames[i] == NULL) {
      return MALLOC_FAIL;
    }
  }
  return SUCCESS;
}

extern int Window_update_size(WindowManager *wm) {
  int stdscrY, stdscrX;
  getmaxyx(stdscr, stdscrY, stdscrX);

  if (wm->window_counter == 1) {
    free_ncurses_window(&wm->active_window->curses_win);
    wm->active_window->curses_win = newwin(stdscrY, stdscrX, 0, 0);
    if (wm->active_window->curses_win == NULL) {
      return ERROR;
    }

    if (stdscrY - 5 < 0) {
      return SUCCESS;
    }
    if (wm->active_window->highlight >
        (stdscrY - 5) + wm->active_window->scroll) {
      wm->active_window->scroll += wm->active_window->highlight -
                                   ((stdscrY - 5) + wm->active_window->scroll);
    }
  } else {
    free_ncurses_window(&wm->first_window->curses_win);
    free_ncurses_window(&wm->second_window->curses_win);

    wm->first_window->curses_win = newwin(stdscrY, stdscrX / 2 - 1, 0, 1);
    wm->second_window->curses_win =
        newwin(stdscrY, stdscrX / 2, 0, stdscrX / 2);

    if (!wm->first_window || !wm->second_window) {
      return ERROR;
    }

    int first_bottom_border = (stdscrY - 5) + wm->first_window->scroll;
    int second_bottom_border = (stdscrY - 5) + wm->second_window->scroll;

    if (wm->first_window->highlight > first_bottom_border) {
      wm->first_window->scroll +=
          wm->first_window->highlight - first_bottom_border;
    }

    if (wm->second_window->highlight > second_bottom_border) {
      wm->first_window->scroll +=
          wm->first_window->highlight - first_bottom_border;
    }
  }
  return SUCCESS;
}

extern int Window_create(Window *win, WindowManager *wm, const char *pwd) {
  win->files.filenames = NULL;
  win->highlight = 0;
  win->curses_win = NULL;
  win->scroll = 0;

  if (pwd != NULL) {
    snprintf(win->pwd, sizeof(win->pwd), "%s", pwd);
  } else {
    if (getcwd(win->pwd, sizeof(win->pwd)) == NULL) {
      return ERROR;
    }
  }

  if (wm->window_counter <= 1) {
    wm->window_counter += 1;
  }

  if (wm->window_counter == 1) {
    wm->first_window = win;
  } else {
    if (!wm->first_window) {
      Window_copy(win, wm->second_window);
      wm->first_window = win;
    } else {
      Window_copy(win, wm->first_window);
      wm->second_window = win;
    }
  }
  wm->active_window = win;

  Window_update_size(wm);
	return SUCCESS;
}

extern int Window_chdir(const char *path, Window *win) {
	if (strlen(win->pwd) == 1 && strcmp(path, "..") == 0) { // If at '/' and trying to cd ..
    return ERROR;
  }
	//Restore current window path
	if (chdir(win->pwd) < 0) {
		return ERROR;
	}

	if (chdir(path) < 0) {
		return ERROR;
	}

	//Getting full path into win->pwd
	if (getcwd(win->pwd, sizeof(win->pwd)) == NULL) {
		return ERROR;
  }

	win->highlight = 0;
	win->scroll = 0;
	FilesArray_free(&win->files);
	int fill_res = FilesArray_fill(&win->files, win->pwd);
	if (fill_res > 0) { 
		return fill_res;
	}
	Window_clear(win);
	return SUCCESS;
}

extern void Window_draw(WindowManager wm, Window *win) {
  if (win == NULL) {
    return;
  }

  // Get win size
  int win_size_y, win_size_x;
  getmaxyx(win->curses_win, win_size_y, win_size_x);

  // Draw box around win
  if (wm.active_window == win) {
    box(win->curses_win, 0, 0);
  } else {
    Window_draw_inactive_box(win, win_size_y, win_size_x);
  }

  // Draw line for status_line;
  int status_line_y = getmaxy(win->curses_win) - STATUSLINE_HEIGHT;
  int status_line_x = getmaxx(win->curses_win) - 2;
  mvwhline(win->curses_win, status_line_y, 1, '-', status_line_x);

  // Display pwd
  wchar_t pwd[win_size_x];
  trim_text(true, pwd, win->pwd, win_size_x - 2);
  mvwaddwstr(win->curses_win, status_line_y + 1, 1, pwd);

  wrefresh(win->curses_win);
  // Draw files
  if (win->files.filenames == NULL) {
    return;
  }

  int win_limit = win_size_y - STATUSLINE_HEIGHT;
  int filename_draw_y = 1;
  int filename_draw_x = (int)(log10(win->files.files_count)) + 3;

	//If user just changed dir to ..
	//Seek for previous 

  for (int i = win->scroll; i < win->files.files_count; i++) {
    // Leave from loop if on win limit
    if (filename_draw_y >= win_limit) {
      break;
    }

    char *filename = win->files.filenames[i];
    wchar_t filename_trimmed[win_size_x];
    trim_text(false, filename_trimmed, filename, win_size_x - filename_draw_x);

    // Getting file type
    // TODO. can you cache smth here? (at least, with cache values, check if dir
    // is changed, it`s gonna be faster)
    char filepath[PATH_MAX];
    strcpy(filepath, win->pwd);
    strcat(filepath, "/");
    strcat(filepath, filename);
    FileType filetype = get_filetype(filepath);

    mvwhline(win->curses_win, filename_draw_y, 1, ' ', win_size_x - 2);

    if (filetype == DIRECTORY) {
      wattron(win->curses_win, COLOR_PAIR(COLOR_PAIR_RED));
    }
    if (win->highlight == i) {
      wattron(win->curses_win, A_REVERSE);
    }
    // Draw file index
    if (!win->relative_number || win->highlight == i) { // Draw absolute numbers
      mvwprintw(win->curses_win, filename_draw_y, 1, "%d", i);
    } else { // Draw relative numbers */
      mvwprintw(win->curses_win, filename_draw_y, 1, "%d", abs(win->highlight - i));
    }
		//Display file
    mvwaddwstr(win->curses_win, filename_draw_y, filename_draw_x, filename_trimmed);

    wattroff(win->curses_win, COLOR_PAIR(COLOR_PAIR_RED));
    wattroff(win->curses_win, A_REVERSE);

    filename_draw_y++;
  }
  wrefresh(win->curses_win);
}

extern void Window_draw_inactive_box(Window *win, int sizeY, int sizeX) {
  WINDOW *curs_win = win->curses_win;
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

extern void Window_clear(Window *win) {
  int maxY, maxX;
  getmaxyx(win->curses_win, maxY, maxX);
  // Clear files
  for (int y = 1; y < maxY; y++) {
    mvwhline(win->curses_win, y, 1, ' ', maxX - 2);
  }
	//Clear pwd
	mvwhline(win->curses_win, 1 + maxY - STATUSLINE_HEIGHT, 1, ' ', maxX - 2);
	wrefresh(win->curses_win);
}

extern void Window_close(WindowManager *wm) {
	if (wm->window_counter < 2) {
    return;
  }
  // Active window is first one, remove first
  if (wm->active_window == wm->first_window) {
    Window_free(&wm->first_window);
    wm->active_window = wm->second_window;
  }
  // Active window is second one, remove second
  else {
    Window_free(&wm->second_window);
    wm->active_window = wm->first_window;
  }
  wm->window_counter -= 1;
  Window_update_size(wm);
}

extern void Window_free(Window **win) {
	if (*win == NULL) {
    return;
  }
  Window *win_ = *win;
  // Free stuff from win
  free_ncurses_window(&win_->curses_win);
  FilesArray_free(&win_->files);

  free(win_);
  *win = NULL;
}


