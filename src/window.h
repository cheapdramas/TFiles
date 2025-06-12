#ifndef WINDOW_H 
#define WINDOW_H

#include "config.h"
#include "files.h"
#include <ncursesw/ncurses.h>
#include <linux/limits.h>



typedef struct Window {
  FilesArray files;
  WINDOW *curses_win;
	bool relative_number;
  char pwd[PATH_MAX];
  int highlight;
  int scroll;
} Window;

typedef struct WindowManager {
  Window *first_window, *second_window, *active_window;
  uint8_t window_counter;
} WindowManager;

typedef struct Popup{
  WINDOW *MainPopup_win;
  int MainSizeX, MainSizeY, MainStartY, MainStartX;
  WINDOW *PopupConfirm_win;
  int ConfirmSizeX, ConfirmSizeY, ConfirmStartY, ConfirmStartX;
  int last_used_y;
} Popup;

extern void trim_text(bool is_pwd, wchar_t *dest, const char *src, int sizeX);

extern void free_ncurses_window(WINDOW **win);

extern int Window_copy(Window *dest, Window *src);

extern int Window_update_size(WindowManager *wm);

extern int Window_create(Window *win, WindowManager *wm, const char *pwd);

extern int Window_chdir(const char *path, Window *win);

extern void Window_draw(WindowManager wm, Window *win);

extern void Window_draw_inactive_box(Window *win, int sizeY, int sizeX);

extern void Window_clear(Window *win);

extern void Window_close(WindowManager *wm);

extern void Window_free(Window **win);



#endif

