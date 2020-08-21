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

#define CTL(x) ((x) & 0x1f)

#include "vtparser.h"

#define DEFAULT_TERMINAL "screen-bce"
#define DEFAULT_COLOR_TERMINAL "screen-256color-bce"

#define MIN(x, y) ((x) < (y) ? (x) : (y))
#define MIN3(a, b, c) ((a) < (b) ? MIN(a, c) : MIN(b, c))
#define MAX(x, y) ((x) > (y) ? (x) : (y))

struct canvas;
struct screen {
	int sy, sx, vis;
	short fg, bg, sfg, sbg, sp;
	bool insert, oxenl, xenl, saved;
	attr_t sattr;
	WINDOW *win;
};
struct pty {
	int fd, ntabs, tabstop, id;
	struct winsize ws;
	pid_t pid;
	bool *tabs, pnm, decom, am, lnm;
	wchar_t repc;
	struct screen pri, alt, *s;
	wchar_t *g0, *g1, *g2, *g3, *gc, *gs, *sgc, *sgs;
	struct pty *next;
	char status[32];
	struct vtp vp;
};

typedef void(action)(const char *arg);
struct handler {
	action *act;
	const char *arg;
};

enum mode {
	passthru, /* Unbound keystrokes are passed to focused window */
	sink      /* Unbound keystrokes are discarded */
};
extern int smtx_main(int, char *const*);
extern struct state S;

struct state {
	char commandkey;
	int width;
	int history;      /* Number of lines retained */
	int count;
	enum mode mode;
	struct handler (*binding)[128];
	void *maps[2];
	struct canvas *v; /* Root canvas currently displayed */
	struct canvas *c; /* Root of tree of all canvas */
	struct canvas *f; /* Currently focused canvas */;
	struct pty *p;    /* Head of list of all pty */
	fd_set fds;
	WINDOW *werr;
	int reshape;
};

struct point { int y, x; };
struct canvas {
	struct point origin; /* position of upper left corner */
	struct point extent; /* relative position of lower right corner */
	int typ; /* 0: c[0] is full width, 1: c[1] is full height */
	struct point offset; /* Number of lines window is scrolled */
	struct pty *p;
	struct canvas *parent;
	/*
	A canvas contains both c[0] and c[1], and shows only the upper
	left corner.  eg: if extent = {8, 42}, p->s.win = {3, 13}, typ = 0
	    split_point = { 0.5, 0.333 }

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
	double split_point[2]; /* percent of screen dedicated to window */
	int manualscroll;
	WINDOW *bkg;   /* Display hash if pty too small */
	WINDOW *wtit;  /* Window for title */
	WINDOW *wdiv;  /* Window for divider */
};

extern struct canvas * get_focus(void);
extern unsigned describe_layout(char *, ptrdiff_t, unsigned);
extern unsigned describe_row(char *, size_t, int);

#define MAXMAP 0x7f
extern wchar_t CSET_US[]; /* "USASCII" */
extern wchar_t CSET_UK[]; /* "United Kingdom" */
extern wchar_t CSET_GRAPH[]; /* Graphics Set One */
extern int tabstop;
extern int id;
extern const char *term;
extern void fixcursor(void);
extern void focus(struct canvas *n);

extern struct canvas * init(void);
extern void balance(struct canvas *);
extern void draw(struct canvas *);
extern void setupevents(struct vtp *);
extern int rewrite(int fd, const char *b, size_t n);
extern void build_bindings(void);
extern void draw(struct canvas *n);
extern void scrollbottom(struct canvas *n);
extern void show_err(const char *fmt, ...);

extern action attach;
extern action transition;
extern action create;
extern action equalize;
extern action mov;
extern action new_tabstop;
extern action reorient;
extern action resize;
extern action scrolln;
extern action scrollh;
extern action send;
