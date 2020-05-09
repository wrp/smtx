/* Copyright 2017 - 2019 Rob King <jking@deadpixi.com>
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

/*
   BUGS:
     If you build 3 windows, then close one, ctrl-G o does
       not change windows.  (lastfocus gets lost)
     SIGWINCH is not handled at all
 */

#include "main.h"

static NODE *root, *focused, *lastfocused = NULL;
static int commandkey = CTL(COMMAND_KEY);
static int nfds = 1; /* stdin */
static fd_set fds;
static char iobuf[BUFSIZ];

static void reshape(NODE *n, int y, int x, int h, int w);
static void draw(NODE *n);
static void reshapechildren(NODE *n);
static const char *term = NULL;
static void freenode(NODE *n, bool recursive);

static void
quit(int rc, const char *m) /* Shut down MTM. */
{
    if (m)
        fprintf(stderr, "%s\n", m);
    exit(rc);
}

void
safewrite(int fd, const char *b, size_t n) /* Write, retry on interrupt */
{
	ssize_t s;
	while( n > 0 && ((s = write(fd, b, n)) >= 0 || errno == EINTR)) {
		if( s > 0 ) {
			b += s;
			n -= s;
		}
	}
}

static const char *
getshell(void) /* Get the user's preferred shell. */
{
	const char *shell = getenv("SHELL");
	struct passwd *pwd = shell ? NULL : getpwuid(getuid());
	return shell ? shell : pwd ? pwd->pw_shell : "/bin/sh";
}

static bool *
newtabs(int w, int ow, bool *oldtabs) /* Initialize default tabstops. */
{
	bool *tabs = calloc(w, sizeof *tabs);
	for( int i = 0; tabs != NULL && i < w; i++ ) {
		tabs[i] = i < ow ? oldtabs[i] : i % 8 == 0;
	}
	return tabs;
}

static NODE *
newnode(Node t, NODE *p, int y, int x, int h, int w) /* Create a new node. */
{
    NODE *n = calloc(1, sizeof(NODE));
    bool *tabs = newtabs(w, 0, NULL);
    if (!n || h < 2 || w < 2 || !tabs)
        return free(n), free(tabs), NULL;

    n->t = t;
    n->pt = -1;
    n->p = p;
    n->y = y;
    n->x = x;
    n->h = h;
    n->w = w;
    n->tabs = tabs;
    n->ntabs = w;

    return n;
}

static void
freenode(NODE *n, bool recurse) /* Free a node. */
{
    if (n){
        if (lastfocused == n)
            lastfocused = NULL;
        if (n->pri.win)
            delwin(n->pri.win);
        if (n->alt.win)
            delwin(n->alt.win);
        if (recurse)
            freenode(n->c1, true);
        if (recurse)
            freenode(n->c2, true);
        if (n->pt >= 0){
            close(n->pt);
            FD_CLR(n->pt, &fds);
        }
        free(n->tabs);
        free(n);
    }
}

static void
fixcursor(void) /* Move the terminal cursor to the active view. */
{
    if (focused){
        int y, x;
        curs_set(focused->s->off != focused->s->tos? 0 : focused->s->vis);
        getyx(focused->s->win, y, x);
        y = MIN(MAX(y, focused->s->tos), focused->s->tos + focused->h - 1);
        wmove(focused->s->win, y, x);
    }
}

static const char *
getterm(void)
{
    const char *envterm = getenv("TERM");
    if (term)
        return term;
    if (envterm && COLORS >= 256 && !strstr(DEFAULT_TERMINAL, "-256color"))
        return DEFAULT_256_COLOR_TERMINAL;
    return DEFAULT_TERMINAL;
}

static NODE *
newview(NODE *p, int y, int x, int h, int w) /* Open a new view. */
{
    struct winsize ws = {.ws_row = h, .ws_col = w};
    NODE *n = newnode(VIEW, p, y, x, h, w);
    if (!n)
        return NULL;

    SCRN *pri = &n->pri, *alt = &n->alt;
    pri->win = newpad(MAX(h, SCROLLBACK), w);
    alt->win = newpad(h, w);
    if (!pri->win || !alt->win)
        return freenode(n, false), NULL;
    pri->tos = pri->off = MAX(0, SCROLLBACK - h);
    n->s = pri;

    nodelay(pri->win, TRUE); nodelay(alt->win, TRUE);
    scrollok(pri->win, TRUE); scrollok(alt->win, TRUE);
    keypad(pri->win, TRUE); keypad(alt->win, TRUE);

    setupevents(n);

    pid_t pid = forkpty(&n->pt, NULL, NULL, &ws);
    if (pid < 0){
        if (!p)
            perror("forkpty");
        return freenode(n, false), NULL;
    } else if (pid == 0){
        char buf[100] = {0};
        snprintf(buf, sizeof(buf) - 1, "%lu", (unsigned long)getppid());
        setsid();
        setenv("MTM", buf, 1);
        setenv("TERM", getterm(), 1);
        signal(SIGCHLD, SIG_DFL);
        execl(getshell(), getshell(), NULL);
        return NULL;
    }

    FD_SET(n->pt, &fds);
    fcntl(n->pt, F_SETFL, O_NONBLOCK);
    nfds = n->pt > nfds? n->pt : nfds;
    return n;
}

static NODE *
newcontainer(Node t, NODE *p, int y, int x, int h, int w,
             NODE *c1, NODE *c2) /* Create a new container */
{
    NODE *n = newnode(t, p, y, x, h, w);
    if (!n)
        return NULL;

    n->c1 = c1;
    n->c2 = c2;
    c1->p = c2->p = n;

    reshapechildren(n);
    return n;
}

static void
focus(NODE *n) /* Focus a node. */
{
    if (!n)
        return;
    else if (n->t == VIEW){
        lastfocused = focused;
        focused = n;
    } else
        focus(n->c1? n->c1 : n->c2);
}

#define ABOVE(n) n->y - 2, n->x + n->w / 2
#define BELOW(n) n->y + n->h + 2, n->x + n->w / 2
#define LEFT(n)  n->y + n->h / 2, n->x - 2
#define RIGHT(n) n->y + n->h / 2, n->x + n->w + 2

static NODE *
findnode(NODE *n, int y, int x) /* Find the node enclosing y,x. */
{
    #define IN(n, y, x) (y >= n->y && y <= n->y + n->h && \
                         x >= n->x && x <= n->x + n->w)
    if (IN(n, y, x)){
        if (n->c1 && IN(n->c1, y, x))
            return findnode(n->c1, y, x);
        if (n->c2 && IN(n->c2, y, x))
            return findnode(n->c2, y, x);
        return n;
    }
    return NULL;
}

static void
replacechild(NODE *n, NODE *c1, NODE *c2) /* Replace c1 of n with c2. */
{
    c2->p = n;
    if (!n){
        root = c2;
        reshape(c2, 0, 0, LINES, COLS);
    } else if (n->c1 == c1)
        n->c1 = c2;
    else if (n->c2 == c1)
        n->c2 = c2;

    n = n? n : root;
    reshape(n, n->y, n->x, n->h, n->w);
    draw(n);
}

static void
removechild(NODE *p, const NODE *c) /* Replace p with other child. */
{
    replacechild(p->p, p, c == p->c1? p->c2 : p->c1);
    freenode(p, false);
}

static void
deletenode(NODE *n) /* Delete a node. */
{
    if (!n || !n->p)
        quit(EXIT_SUCCESS, NULL);
    if (n == focused)
        focus(n->p->c1 == n? n->p->c2 : n->p->c1);
    removechild(n->p, n);
    freenode(n, true);
}

static void
reshapeview(NODE *n, int d, int ow) /* Reshape a view. */
{
    int oy, ox;
    bool *tabs = newtabs(n->w, ow, n->tabs);
    struct winsize ws = {.ws_row = n->h, .ws_col = n->w};

    if (tabs){
        free(n->tabs);
        n->tabs = tabs;
        n->ntabs = n->w;
    }

    getyx(n->s->win, oy, ox);
    wresize(n->pri.win, MAX(n->h, SCROLLBACK), MAX(n->w, 2));
    wresize(n->alt.win, MAX(n->h, 2), MAX(n->w, 2));
    n->pri.tos = n->pri.off = MAX(0, SCROLLBACK - n->h);
    n->alt.tos = n->alt.off = 0;
    wsetscrreg(n->pri.win, 0, MAX(SCROLLBACK, n->h) - 1);
    wsetscrreg(n->alt.win, 0, n->h - 1);
    if (d > 0){ /* make sure the new top line syncs up after reshape */
        wmove(n->s->win, oy + d, ox);
        wscrl(n->s->win, -d);
    }
    doupdate();
    refresh();
    ioctl(n->pt, TIOCSWINSZ, &ws);
}

static void
reshapechildren(NODE *n) /* Reshape all children of a view. */
{
    if (n->t == HORIZONTAL){
        int i = n->w % 2? 0 : 1;
        reshape(n->c1, n->y, n->x, n->h, n->w / 2);
        reshape(n->c2, n->y, n->x + n->w / 2 + 1, n->h, n->w / 2 - i);
    } else if (n->t == VERTICAL){
        int i = n->h % 2? 0 : 1;
        reshape(n->c1, n->y, n->x, n->h / 2, n->w);
        reshape(n->c2, n->y + n->h / 2 + 1, n->x, n->h / 2 - i, n->w);
    }
}

static void
reshape(NODE *n, int y, int x, int h, int w) /* Reshape a node. */
{
    if (n->y == y && n->x == x && n->h == h && n->w == w && n->t == VIEW)
        return;

    int d = n->h - h;
    int ow = n->w;
    n->y = y;
    n->x = x;
    n->h = MAX(h, 1);
    n->w = MAX(w, 1);

    if (n->t == VIEW)
        reshapeview(n, d, ow);
    else
        reshapechildren(n);
    draw(n);
}

static void
drawchildren(const NODE *n) /* Draw all children of n. */
{
    draw(n->c1);
    if (n->t == HORIZONTAL)
        mvvline(n->y, n->x + n->w / 2, ACS_VLINE, n->h);
    else
        mvhline(n->y + n->h / 2, n->x, ACS_HLINE, n->w);
    wnoutrefresh(stdscr);
    draw(n->c2);
}

static void
draw(NODE *n) /* Draw a node. */
{
	if( n->t == VIEW ) {
		pnoutrefresh(
			n->s->win,  /* pad */
			n->s->off,  /* pminrow */
			0,          /* pmincol */
			n->y,       /* sminrow */
			n->x,       /* smincol */
			n->y + n->h - 1,  /* smaxrow */
			n->x + n->w - 1   /* smaxcol */
		);
	} else {
		drawchildren(n);
	}
}

static void
split(NODE *n, Node t) /* Split a node. */
{
    int nh = t == VERTICAL? (n->h - 1) / 2 : n->h;
    int nw = t == HORIZONTAL? (n->w) / 2 : n->w;
    NODE *p = n->p;
    NODE *v = newview(NULL, 0, 0, MAX(0, nh), MAX(0, nw));
    if (!v)
        return;

    NODE *c = newcontainer(t, n->p, n->y, n->x, n->h, n->w, n, v);
    if (!c){
        freenode(v, false);
        return;
    }

    replacechild(p, n, c);
    focus(v);
    draw(p? p : root);
}

static bool
getinput(NODE *n, fd_set *f) /* Recursively check all ptty's for input. */
{
    if (n && n->c1 && !getinput(n->c1, f))
        return false;

    if (n && n->c2 && !getinput(n->c2, f))
        return false;

    if (n && n->t == VIEW && n->pt > 0 && FD_ISSET(n->pt, f)){
        ssize_t r = read(n->pt, iobuf, sizeof(iobuf));
        if (r > 0)
            vtwrite(&n->vp, iobuf, r);
        if (r <= 0 && errno != EINTR && errno != EWOULDBLOCK)
            return deletenode(n), false;
    }

    return true;
}

static void
scrollback(NODE *n)
{
    n->s->off = MAX(0, n->s->off - n->h / 2);
}

static void
scrollforward(NODE *n)
{
    n->s->off = MIN(n->s->tos, n->s->off + n->h / 2);
}

static void
scrollbottom(NODE *n)
{
    n->s->off = n->s->tos;
}

static void
sendarrow(const NODE *n, const char *k)
{
    char buf[100] = {0};
    snprintf(buf, sizeof(buf) - 1, "\033%s%s", n->pnm? "O" : "[", k);
    SEND(n, buf);
}

static bool
handlechar(int r, int k) /* Handle a single input character. */
{
    const char cmdstr[] = {commandkey, 0};
    static bool cmd = false;
    NODE *n = focused;
    #define KERR(i) (r == ERR && (i) == k)
    #define KEY(i)  (r == OK  && (i) == k)
    #define CODE(i) (r == KEY_CODE_YES && (i) == k)
    #define INSCR (n->s->tos != n->s->off)
    #define SB scrollbottom(n)
    #define DO(s, t, a) \
        if (s == cmd && (t)) { a ; cmd = false; return true; }

    DO(cmd,   KERR(k),             return false)
    DO(cmd,   CODE(KEY_RESIZE),    reshape(root, 0, 0, LINES, COLS); SB)
    DO(false, KEY(commandkey),     return cmd = true)
    DO(false, KEY(0),              SENDN(n, "\000", 1); SB)
    DO(false, KEY(L'\n'),          SEND(n, "\n"); SB)
    DO(false, KEY(L'\r'),          SEND(n, n->lnm? "\r\n" : "\r"); SB)
    DO(false, SCROLLUP && INSCR,   scrollback(n))
    DO(false, SCROLLDOWN && INSCR, scrollforward(n))
    DO(false, RECENTER && INSCR,   scrollbottom(n))
    DO(false, CODE(KEY_ENTER),     SEND(n, n->lnm? "\r\n" : "\r"); SB)
    DO(false, CODE(KEY_UP),        sendarrow(n, "A"); SB);
    DO(false, CODE(KEY_DOWN),      sendarrow(n, "B"); SB);
    DO(false, CODE(KEY_RIGHT),     sendarrow(n, "C"); SB);
    DO(false, CODE(KEY_LEFT),      sendarrow(n, "D"); SB);
    DO(false, CODE(KEY_HOME),      SEND(n, "\033[1~"); SB)
    DO(false, CODE(KEY_END),       SEND(n, "\033[4~"); SB)
    DO(false, CODE(KEY_PPAGE),     SEND(n, "\033[5~"); SB)
    DO(false, CODE(KEY_NPAGE),     SEND(n, "\033[6~"); SB)
    DO(false, CODE(KEY_BACKSPACE), SEND(n, "\177"); SB)
    DO(false, CODE(KEY_DC),        SEND(n, "\033[3~"); SB)
    DO(false, CODE(KEY_IC),        SEND(n, "\033[2~"); SB)
    DO(false, CODE(KEY_BTAB),      SEND(n, "\033[Z"); SB)
    DO(false, CODE(KEY_F(1)),      SEND(n, "\033OP"); SB)
    DO(false, CODE(KEY_F(2)),      SEND(n, "\033OQ"); SB)
    DO(false, CODE(KEY_F(3)),      SEND(n, "\033OR"); SB)
    DO(false, CODE(KEY_F(4)),      SEND(n, "\033OS"); SB)
    DO(false, CODE(KEY_F(5)),      SEND(n, "\033[15~"); SB)
    DO(false, CODE(KEY_F(6)),      SEND(n, "\033[17~"); SB)
    DO(false, CODE(KEY_F(7)),      SEND(n, "\033[18~"); SB)
    DO(false, CODE(KEY_F(8)),      SEND(n, "\033[19~"); SB)
    DO(false, CODE(KEY_F(9)),      SEND(n, "\033[20~"); SB)
    DO(false, CODE(KEY_F(10)),     SEND(n, "\033[21~"); SB)
    DO(false, CODE(KEY_F(11)),     SEND(n, "\033[23~"); SB)
    DO(false, CODE(KEY_F(12)),     SEND(n, "\033[24~"); SB)
    DO(true,  MOVE_UP,             focus(findnode(root, ABOVE(n))))
    DO(true,  MOVE_DOWN,           focus(findnode(root, BELOW(n))))
    DO(true,  MOVE_LEFT,           focus(findnode(root, LEFT(n))))
    DO(true,  MOVE_RIGHT,          focus(findnode(root, RIGHT(n))))
    DO(true,  MOVE_OTHER,          focus(lastfocused))
    DO(true,  HSPLIT,              split(n, HORIZONTAL))
    DO(true,  VSPLIT,              split(n, VERTICAL))
    DO(true,  DELETE_NODE,         deletenode(n))
    DO(true,  REDRAW,              touchwin(stdscr); draw(root); redrawwin(stdscr))
    DO(true,  SCROLLUP,            scrollback(n))
    DO(true,  SCROLLDOWN,          scrollforward(n))
    DO(true,  RECENTER,            scrollbottom(n))
    DO(true,  KEY(commandkey),     SENDN(n, cmdstr, 1));
    char c[MB_LEN_MAX + 1] = {0};
    if (wctomb(c, k) > 0){
        scrollbottom(n);
        SEND(n, c);
    }
    return cmd = false, true;
}

static void
run(void)
{
	while( root != NULL ) {
		int r;
		wint_t w = 0;
		fd_set sfds = fds;
		if( select(nfds + 1, &sfds, NULL, NULL, NULL) < 0 ) {
			FD_ZERO(&sfds);
		}
		do {
			r = wget_wch(focused->s->win, &w);
		} while( handlechar(r, w) );
		getinput(root, &sfds);
		draw(root);
		doupdate();
		fixcursor();
		draw(focused);
		doupdate();
	}
}

static void
parse_args(int argc, char **argv)
{
	int c;
	char *name = strrchr(argv[0], '/');
	while( (c = getopt(argc, argv, ":hc:T:t:")) != -1 ) {
		switch (c) {
		case 'h':
			printf("usage: %s [-T NAME] [-t NAME] [-c KEY]\n",
				name ? name + 1 : argv[0]);
			exit(0);
		case 'c':
			commandkey = CTL(optarg[0]);
			break;
		case 'T':
			setenv("TERM", optarg, 1);
			break;
		case 't':
			term = optarg;
			break;
		default:
			fprintf(stderr, "Unkown option: %c\n", optopt);
			exit(EXIT_FAILURE);
		}
	}
}


void
cleanup(void)
{
	if (root) {
		freenode(root, true);
	}
	endwin();
}

int
main(int argc, char **argv)
{
	FD_SET(STDIN_FILENO, &fds);
	setlocale(LC_ALL, "");
	signal(SIGCHLD, SIG_IGN); /* automatically reap children */
	parse_args(argc, argv);

	if( initscr() == NULL ) {
		exit(EXIT_FAILURE);
	}
	atexit(cleanup);
	raw();
	noecho();
	nonl();
	intrflush(stdscr, FALSE);
	start_color();
	use_default_colors();

	root = newview(NULL, 0, 0, LINES, COLS);
	if( root == NULL ) {
		quit(EXIT_FAILURE, "Unable to create root window");
	}
	focus(root);
	draw(root);
	run();
	return EXIT_SUCCESS;
}
