/*
 * Copyright 2017 - 2019 Rob King <jking@deadpixi.com>
 * Copyright 2020 - William Pursell <william.r.pursell@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include "config.h"
#include <assert.h>
#include <ctype.h>
#if HAVE_CURSES_H
# include <curses.h>
#elif HAVE_NCURSESW_CURSES_H
# include <ncursesw/curses.h>
#endif
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <locale.h>
#if HAVE_PTY_H
# include <pty.h>
# include <utmp.h>
#elif HAVE_LIBUTIL_H
# include <libutil.h>
#elif HAVE_UTIL_H
# include <util.h>
#endif
#include <pwd.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <wchar.h>
#include <wctype.h>

#include "vtparser.h"

extern int smtx_main(void);

#define MIN(x, y) ((x) < (y) ? (x) : (y))
#define MAX(x, y) ((x) > (y) ? (x) : (y))
#define Kase break; case

struct canvas;
struct screen {
	int vis;
	int tos; /* top of screen */
	int maxy; /* highest row in which the cursor has ever been */
	int rows; /* Number of rows in the window */
	int delta; /* Number of lines written by a vtwrite */
	struct { int top; int bot; } scroll;
	struct {
		int y, x, xenl;
		cchar_t bkg;
		short color[2]; /* [0] == foreground, [1] == background */
		wchar_t *gc, *gs;
	} c, sc; /* cursor/save cursor */
	bool insert;
	wchar_t repc; /* character to be repeated */
	int decawm; /* Wrap-around mode */
	attr_t sattr;
	WINDOW *win;
};
struct pty {
	int fd, tabstop, count;
	struct winsize ws;
	pid_t pid;
	bool *tabs, pnm, decom, lnm;
	/* DECOM: When set, cursor addressing is relative to the upper left
	 * corner of the scrolling region instead of top of screen. */
	struct screen scr[2], *s;
	wchar_t *g[4];
	char status[32];
	struct vtp vp;
	char secondary[PATH_MAX];
	struct pty *next;
};

typedef void(action)(const char *arg);
typedef void(action0)(void);
struct handler {
	union {
		action *a;
		action0 *v;
	} act;
	const char *arg;
};

extern struct state S;
struct mode {
	struct handler keys[128];
};
extern struct handler k1[128];
extern struct handler ctl[128];
extern struct handler code_keys[KEY_MAX - KEY_MIN + 1];

struct state {
	unsigned char ctlkey;
	int width;   /* Columns in newly created ptys */
	int history; /* Rows in newly created windows */
	int count;   /* User entered count in command mode */
	const char *term; /* Name of the terminal type */
	struct handler *binding; /* Current key binding */
	struct canvas *c;  /* Root of all canvasses in use */
	struct canvas *f;  /* Currently focused canvas */;
	struct pty *p;     /* List of all pty in use */
	struct pty *tail;  /* Last in the list of p */
	struct canvas *unused; /* Unused canvasses */
	fd_set fds;
	int maxfd;
	WINDOW *werr;
	WINDOW *wbkg;
	int reshape;
	char errmsg[256];
};

struct point { int y, x; };
struct canvas {
	struct point origin; /* position of upper left corner */
	struct point extent; /* relative position of lower right corner */
	/* extent.y is the actual number of rows visible in the window */
	int typ; /* 0: c[0] is full width, 1: c[1] is full height */
	struct point offset; /* Number of lines window is scrolled */
	struct pty *p;
	struct canvas *parent;
	/*
	A canvas contains both c[0] and c[1], and shows only the upper
	left corner.  eg: if extent = {3, 13}, typ = 0, split = { 0.5, 0.333 }

	             |<-wdiv
	  p->s.win   |              c[1]
	             |
	----wtit-----x-------c[1]->wtit-----------

	               c[0]

	-------------c[0]->wtit-------------------
	c[0] is the window below this, c[1] is the window to the right
	(in a typ==1 window, c1 is full height and c0 is partial width)
	(Note that w.x + c1->w.x == extent.x - 1, subtracting 1 for wdiv)
	*/
	struct canvas *c[2];
	struct { double y, x; } split;
	int manualscroll;
	WINDOW *wtit;  /* Window for title */
	WINDOW *wdiv;  /* Window for divider */
};

extern wchar_t CSET_US[0x7f]; /* "USASCII" */
extern wchar_t CSET_UK[0x7f]; /* "United Kingdom" */
extern wchar_t CSET_GRAPH[0x7f]; /* Graphics Set One */
extern int tabstop;
extern int id;
extern void fixcursor(void);

extern struct canvas * newcanvas(struct pty *, struct canvas *);
extern void draw(struct canvas *);
extern void setupevents(struct pty *);
extern void rewrite(int fd, const char *b, size_t n);
extern void build_bindings(void);
extern void draw(struct canvas *n);
extern void scrollbottom(struct canvas *n);
extern int check(int, int, const char *, ...);
extern void set_tabs(struct pty *p, int tabstop);
extern int resize_pad(WINDOW **, int, int);
extern void reshape_window(struct pty *);
extern void reshape(struct canvas *n, int y, int x, int h, int w);
void set_scroll(struct screen *s, int top, int bottom);
extern void change_count(struct canvas * n, int, int);
extern int build_layout(const char *);

extern action0 attach;
extern action balance;
extern action create;
extern action digit;
extern action0 focus;
extern action mov;
extern action0 new_tabstop;
extern action0 next;
extern action0 prune;
extern action reorient;
extern action0 reshape_root;
extern action resize;
extern action scrollh;
extern action scrolln;
extern action send;
extern action0 send_cr;
extern action sendarrow;
extern action0 set_history;
extern action0 set_layout;
extern action set_width;
extern action0 swap;
extern action transition;
extern action0 transpose;
extern action0 vbeep;

/* Debugging/test harness */
#ifndef NDEBUG
extern action show_status;
#endif
