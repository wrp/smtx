/* Old versions of ncurses don't support A_ITALIC.
 * Define this to disable it if the situation isn't automatically detected.
#define NO_ITALICS
 */

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

/* mtm supports a scrollback buffer, allowing users to scroll back
 * through the output history of a virtual terminal. The SCROLLBACK
 * knob controls how many lines are saved (minus however many are
 * currently displayed). 1000 seems like a good number.
 *
 * Note that every virtual terminal is sized to be at least this big,
 * so setting a huge number here might waste memory. It is recommended
 * that this number be at least as large as the largest terminal you
 * expect to use is tall.
 */
#define SCROLLBACK 1000

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

/* The path for the wide-character curses library. */
#ifndef NCURSESW_INCLUDE_H
    #if defined(__APPLE__) || !defined(__linux__) || defined(__FreeBSD__)
        #define NCURSESW_INCLUDE_H <curses.h>
    #else
        #define NCURSESW_INCLUDE_H <ncursesw/curses.h>
    #endif
#endif
#include NCURSESW_INCLUDE_H

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
#include FORKPTY_INCLUDE_H

/* You probably don't need to alter these much, but if you do,
 * here is where you can define alternate character sets.
 *
 * Note that if your system's wide-character implementation
 * maps directly to Unicode, the preferred Unicode characters
 * will be used automatically if your system declares such
 * support. If it doesn't declare it, define WCHAR_IS_UNICODE to
 * force Unicode to be used.
 */
#define MAXMAP 0x7f
extern wchar_t CSET_US[]; /* "USASCII" */
extern wchar_t CSET_UK[]; /* "United Kingdom" */
extern wchar_t CSET_GRAPH[]; /* Graphics Set One */
