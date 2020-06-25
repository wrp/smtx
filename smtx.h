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
#if 0
#include <sys/ioctl.h>
#endif
#include <sys/select.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <wchar.h>
#include <wctype.h>

#include "vtparser.h"
#define DEFAULT_TERMINAL "screen-bce"
#define DEFAULT_COLOR_TERMINAL "screen-256color-bce"

#define MIN(x, y) ((x) < (y)? (x) : (y))
#define MAX(x, y) ((x) > (y)? (x) : (y))
#define CTL(x) ((x) & 0x1f)

struct state {
	char commandkey;
	int width;
};

struct screen {
	int sy, sx, vis, tos;
	short fg, bg, sfg, sbg, sp;
	bool insert, oxenl, xenl, saved;
	attr_t sattr;
	WINDOW *win;
};

struct canvas;
struct proc {
	int pt, ntabs, tabstop; /* Should tabs be in struct screen ? */
	pid_t pid;
	bool *tabs, pnm, decom, am, lnm;
	wchar_t repc;
	struct screen pri, alt, *s;
	wchar_t *g0, *g1, *g2, *g3, *gc, *gs, *sgc, *sgs;
	struct winsize ws;
	VTPARSER vp;
	int canvas_count;
	struct canvas *c[];
};

struct point { int y, x; };

struct canvas {
	struct point origin; /* position of upper left corner */
	struct point extent; /* relative position of lower right corner */
	int typ; /* 0: c[0] is full width, 1: c[1] is full height */
	struct point offset; /* Number of lines window is scrolled */
	struct proc *p;
	struct canvas *parent;
	/*
	A canvas contains both c[0] and c[1], and shows only the upper
	left corner.  eg: siz.y=8, siz.x=42, win.y=3, win.x=13, typ=0
	    split_point = { 0.5, 0.333 }

	             |<-wdiv
	    win      |              c[1]
	             |
	----wtit-----x-------c[1]->wtit-----------

	               c[0]

	-------------c[0]->wtit-------------------
	c[0] is the window below this, c[1] is the window to the right
	(in a typ==1 window, c1 is full height and c0 is partial width)
	(Note that win.x + c1->win.x == siz.x - 1, subtracting 1 for wdiv)
	*/
	struct canvas *c[2];
	double split_point[2]; /* percent of window dedicated to win */
	char title[64];
	int no_prune;
	char putative_cmd[64];
	WINDOW *input; /* one of win, wtit, wdiv, or p->s.win */
	WINDOW *win;
	WINDOW *wtit;  /* Window for title */
	WINDOW *wdiv;  /* Window for divider */
};

typedef int(action)(struct canvas *n, const char *arg);
struct handler {
	action *act;
	const char *arg;
};

#define MAXMAP 0x7f
extern wchar_t CSET_US[]; /* "USASCII" */
extern wchar_t CSET_UK[]; /* "United Kingdom" */
extern wchar_t CSET_GRAPH[]; /* Graphics Set One */
extern int scrollback_history;
extern int tabstop;
extern int id;
extern int cmd_count;
extern fd_set fds;
extern const char *term;
extern struct canvas *focused;
extern void focus(struct canvas *, int);
extern void fixcursor(void);

extern struct canvas * init(void);
extern void draw(struct canvas *);
extern void setupevents(VTPARSER *);
extern void safewrite(int fd, const char *b, size_t n);
extern void main_loop(void);
extern void build_bindings(void);
extern void draw(struct canvas *n);
extern int smtx_main(int, char *const*);
extern action transition;
extern action create;
extern action digit;
extern action equalize;
extern action mov;
extern action new_tabstop;
extern action reorient;
extern action reshape_root;
extern action redrawroot;
extern action resize;
extern action swap;
extern action scrolln;
extern action scrollh;
