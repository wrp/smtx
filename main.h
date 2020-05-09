#include "config.h"
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <locale.h>
#include <pwd.h>
#include <signal.h>
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
#define DEFAULT_256_COLOR_TERMINAL "screen-256color-bce"

/* The default command prefix key, when modified by cntrl.
 * This can be changed at runtime using the '-c' flag.
 */
#define COMMAND_KEY 'g'

/* The change focus keys. */
#define MOVE_UP         CODE(KEY_UP)
#define MOVE_DOWN       CODE(KEY_DOWN)
#define MOVE_RIGHT      CODE(KEY_RIGHT)
#define MOVE_LEFT       CODE(KEY_LEFT)
#define MOVE_OTHER      KEY(L'o')

/* The split terminal keys. */
#define HSPLIT KEY(L'h')
#define VSPLIT KEY(L'v')

/* The delete terminal key. */
#define DELETE_NODE KEY(L'w')

/* The force redraw key. */
#define REDRAW KEY(L'l')

/* The scrollback keys. */
#define SCROLLUP CODE(KEY_PPAGE)
#define SCROLLDOWN CODE(KEY_NPAGE)
#define RECENTER CODE(KEY_END)

#if HAVE_CURSES_H
# include <curses.h>
#elif HAVE_NCURSESW_CURSES_H 1
# include <ncursesw/curses.h>
#endif

/* Includes needed to make forkpty(3) work. */
#ifndef FORKPTY_INCLUDE_H
    #if defined(__APPLE__)
        #define FORKPTY_INCLUDE_H <util.h>
    #elif defined(__FreeBSD__)
        #define FORKPTY_INCLUDE_H <libutil.h>
    #else
        #define FORKPTY_INCLUDE_H <pty.h>
    #endif
#endif

#define MIN(x, y) ((x) < (y)? (x) : (y))
#define MAX(x, y) ((x) > (y)? (x) : (y))
#define CTL(x) ((x) & 0x1f)

#include FORKPTY_INCLUDE_H

typedef struct SCRN SCRN;
struct SCRN{
    int sy, sx, vis, tos, off;
    short fg, bg, sfg, sbg, sp;
    bool insert, oxenl, xenl, saved;
    attr_t sattr;
    WINDOW *win;
};

/*** DATA TYPES */
typedef enum{
    HORIZONTAL,
    VERTICAL,
    VIEW
} Node;

typedef struct NODE NODE;
struct NODE{
    Node t;
    int y, x, h, w, pt, ntabs;
    bool *tabs, pnm, decom, am, lnm;
    wchar_t repc;
    NODE *p, *c1, *c2;
    SCRN pri, alt, *s;
    wchar_t *g0, *g1, *g2, *g3, *gc, *gs, *sgc, *sgs;
    VTPARSER vp;
};

#define MAXMAP 0x7f
extern wchar_t CSET_US[]; /* "USASCII" */
extern wchar_t CSET_UK[]; /* "United Kingdom" */
extern wchar_t CSET_GRAPH[]; /* Graphics Set One */

void setupevents(NODE *n);
#define SENDN(n, s, c) safewrite(n->pt, s, c)
#define SEND(n, s) SENDN(n, s, strlen(s))
void safewrite(int fd, const char *b, size_t n);
typedef void (handler)(VTPARSER *v, void *p, wchar_t w, wchar_t iw,
          int argc, int *argv, const wchar_t *osc);
handler ris;
