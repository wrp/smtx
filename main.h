#include "config.h"
#include <assert.h>
#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <locale.h>
#include <pwd.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/types.h>
#include <unistd.h>
#include <wchar.h>
#include <wctype.h>

#include "vtparser.h"
/* mtm by default will advertise itself as a "screen-bce" terminal.
 * This is the terminal type advertised by such programs as
 * screen(1) and tmux(1) and is a widely-supported terminal type.
 * mtm supports emulating the "screen-bce" terminal very well, and this
 * is a reasonable default.
 *
 * However, you can change the default terminal that mtm will
 * advertise itself as. There's the "mtm" terminal type that is
 * recommended for use if you know it will be available in all the
 * environments in which mtm will be used. It advertises a few
 * features that mtm has that the default "screen-bce" terminfo doesn't
 * list, meaning that terminfo-aware programs may get a small
 * speed boost.
 */
#define DEFAULT_TERMINAL "screen-bce"
#define DEFAULT_COLOR_TERMINAL "screen-256color-bce"

/* The default command prefix key, when modified by cntrl.
 * This can be changed at runtime using the '-c' flag.
 */
#define COMMAND_KEY 'g'


#if HAVE_CURSES_H
# include <curses.h>
#elif HAVE_NCURSESW_CURSES_H
# include <ncursesw/curses.h>
#endif
#if HAVE_PTY_H
# include <pty.h>
#elif HAVE_LIBUTIL_H
# include <libutil.h>
#elif HAVE_UTIL_H
# include <util.h>
#endif

#define MIN(x, y) ((x) < (y)? (x) : (y))
#define MAX(x, y) ((x) > (y)? (x) : (y))
#define CTL(x) ((x) & 0x1f)

struct screen {
	int sy, sx, vis, tos, off;
	short fg, bg, sfg, sbg, sp;
	bool insert, oxenl, xenl, saved;
	attr_t sattr;
	WINDOW *win;
};

struct node {
	int id;
	int split;  /* '|', '-', or '\0' (lateral, transverse, or no split) */
	double split_point; /* percent of window dedicated to c[0] */
	int y, x, h, w, pt, ntabs, tabstop;
	pid_t pid;
	bool *tabs, pnm, decom, am, lnm;
	wchar_t repc;
	struct node *parent;
	struct node *c[2];
	char title[32];
	char putative_cmd[32];
	WINDOW *twin;  /* Window for title, or divider */
	struct screen pri, alt, *s;
	wchar_t *g0, *g1, *g2, *g3, *gc, *gs, *sgc, *sgs;
	VTPARSER vp;
};

typedef int(action)(struct node *n, const char **args);
struct handler {
	action *act;
	const char *args[7];
};

#define MAXMAP 0x7f
extern wchar_t CSET_US[]; /* "USASCII" */
extern wchar_t CSET_UK[]; /* "United Kingdom" */
extern wchar_t CSET_GRAPH[]; /* Graphics Set One */
extern int scrollback_history;
extern int tabstop;
extern int id;
extern int cmd_count;
extern int nfds;
extern fd_set fds;
extern char commandkey;
extern const char *term;
extern struct node *root, *view_root;
extern struct node *focused;

extern void setupevents(struct node *n);
extern void safewrite(int fd, const char *b, size_t n);
extern struct node * find_node(struct node *b, int id);
extern struct node * newnode(int y, int x, int h, int w, int id);
extern void main_loop(void);
extern void build_bindings(void);
extern int new_pty(struct node *n);
extern int new_screens(struct node *n);
extern void focus(struct node *n);
extern void draw(struct node *n);
extern void prune(struct node *c);
extern action transition;
extern action create;
extern action digit;
extern action equalize;
extern action reorient;
extern action equalize;
extern action mov;
extern action reshape_root;
extern action redrawroot;
extern action resize;
extern action swap;
