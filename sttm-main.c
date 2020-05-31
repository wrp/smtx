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
/*
   TODO:
     name change? smtx (simple modal terminal multi-plexor)
     Copy-mode, with stack of registers and ability to edit.
     Ability to set titles.
     Needs to be easier to navigate in full screen mode.   Perhaps
       title bar would show title of windows above, below, to the
       left, and right.  Then hjkl navigation would stay in full
       screen mode while going between them.
     Register signal handlers for TERM and HUP.  Need to ensure
       that endwin is called.
 */

#include "sttm.h"

static struct handler keys[128];
static struct handler cmd_keys[128];
static struct handler code_keys[KEY_MAX - KEY_MIN + 1];
static struct handler (*binding)[128] = &keys;
struct canvas *focused, *lastfocused = NULL;
struct canvas *root, *view_root;
char commandkey = CTL('g'); /* Change with -c flag */
static int maxfd = STDIN_FILENO;
fd_set fds;
int cmd_count = -1;
int scrollback_history = 1024;

static struct canvas * balance(struct canvas *n);
static void freecanvas(struct canvas *n);
static void draw_window(struct screen *s, const struct point *);
static void reshape(struct canvas *n, int y, int x, int h, int w);

const char *term = NULL;

void
safewrite(int fd, const char *b, size_t n)
{
	ssize_t s;
	const char *e = b + n;
	while( b < e && ((s = write(fd, b, e - b)) >= 0 || errno == EINTR) ) {
		b += s > 0 ? s : 0;
	}
}

static const char *
getshell(void)
{
	const char *shell = getenv("SHELL");
	struct passwd *pwd = shell && *shell ? NULL : getpwuid(geteuid());
	return shell && *shell ? shell : pwd ? pwd->pw_shell : "/bin/sh";
}

static void
extend_tabs(struct proc *p, int tabstop)
{
	int w = p->ws.ws_col;
	if( p->ntabs < w ) {
		typeof(*p->tabs) *new = realloc(p->tabs, w * sizeof *new);
		if( new != NULL ) {
			for( p->tabs = new; p->ntabs < w; p->ntabs++ ) {
				p->tabs[p->ntabs] = p->ntabs % tabstop == 0;
			}
		}
	}
}

static struct canvas *
newcanvas()
{
	struct canvas *n = calloc(1, sizeof *n);
	if( n != NULL ) {
		n->split_point[0] = 1.0;
		n->split_point[1] = 1.0;
		n->p.pt = -1;
		strncpy(n->title, getshell(), sizeof n->title);
		n->title[sizeof n->title - 1] = '\0';
	}
	return n;
}

static int
delwinnul(WINDOW **w)
{
	int rv = *w ? delwin(*w) : OK;
	*w = NULL;
	return rv;
}

static int
resize_pad(WINDOW **p, int h, int w)
{
	return *p ? wresize(*p, h, w ) == OK : (*p = newpad(h, w)) != NULL;
}

static void
free_proc(struct proc *p)
{
	if( p != NULL ) {
		if( p->pt >= 0 ) {
			close(p->pt);
			FD_CLR(p->pt, &fds);
		}
		p->pt = -1;
		delwinnul(&p->pri.win);
		delwinnul(&p->alt.win);
	}
}

static void
freecanvas(struct canvas *n)
{
	if( n ) {
		delwinnul(&n->wtit);
		delwinnul(&n->wdiv);
		free_proc(&n->p);
		free(n->p.tabs);
		free(n);
	}
}

static int
winsiz(WINDOW *w, int dir)
{
	int y = 0, x = 0;
	if( w ) {
		getmaxyx(w, y, x);
	}
	return dir ? x : y;
}

static void
fixcursor(void) /* Move the terminal cursor to the active window. */
{
	struct proc *p = &focused->p;
	assert( p && p->s );
	int show = binding != &cmd_keys && p->s->vis;
	draw_window(p->s, &focused->origin);
	curs_set(p->s->off != p->s->tos ? 0 : show);

	int x, y;
	getyx(p->s->win, y, x);
	y = MIN(MAX(y, p->s->tos), winsiz(p->s->win, 0) + 1);
	wmove(p->s->win, y, x);
}

static const char *
getterm(void)
{
	const char *t = term ? term : getenv("TERM");
	return t ? t : COLORS > 255 ? DEFAULT_COLOR_TERMINAL : DEFAULT_TERMINAL;
}

static int
new_screens(struct proc *n)
{
	struct screen *ss[] = { &n->pri, &n->alt, NULL };
	for( struct screen **t = ss; *t; t += 1 ) {
		struct screen *s = *t;
		if( ! resize_pad(&s->win, 24, 80) ) {
			return 0;
		}
		s->tos = s->off = 0;
		nodelay(s->win, TRUE);
		scrollok(s->win, TRUE);
		keypad(s->win, TRUE);
	}
	n->s = &n->pri;
	setupevents(n);
	return 1;
}

static int
new_pty(struct proc *p)
{
	p->ws = (struct winsize) {.ws_row = 24, .ws_col = 80};
	p->pid = forkpty(&p->pt, NULL, NULL, &p->ws);
	if( p->pid < 0 ) {
		perror("forkpty");
		return -1;
	} else if( p->pid == 0 ) {
		const char *sh = getshell();
		setsid();
		signal(SIGCHLD, SIG_DFL);
		execl(sh, sh, NULL);
		perror("execl");
		_exit(EXIT_FAILURE);
	}
	FD_SET(p->pt, &fds);
	maxfd = p->pt > maxfd ? p->pt : maxfd;
	fcntl(p->pt, F_SETFL, O_NONBLOCK);
	extend_tabs(p, p->tabstop = 8);
	return p->pt;
}

void
focus(struct canvas *n, int reset)
{
	if( n && n->p.s && n->p.s->win ) {
		if( n != focused && reset ) {
			lastfocused = focused;
		}
		focused = n;
	} else {
		focused = root;
	}
}

static void
canvas_yx(const struct canvas *n, int *rows, int *cols)
{
	int y, x;
	*rows = winsiz(n->p.s->win, 0) - n->p.s->tos + 1;
	*cols = winsiz(n->p.s->win, 1);;
	for( int i = 0; i < 2; i++ ) {
		for( const struct canvas *c = n->c[i]; c; c = c->c[i] ) {
			if( c->p.s && c->p.s->win ) {
				/* int k = winsiz(n->wpty, 0 ); */
				getmaxyx(c->p.s->win, y, x);
				assert( y == winsiz(c->p.s->win, 0 ));
				assert( x == winsiz(c->p.s->win, 1 ));
				/* Sometimes this is off by one.
				this needs to be tracked down, and is
				probably the cause of much confusion.*/
				/* assert( y - c->p.s->tos == k ); */
				if( i == 0 ) {
					*rows += y - c->p.s->tos + 1;
				} else {
					*cols += 1 + x;
				}
			}
		}
	}
}

void
prune(struct canvas *x)
{
	struct canvas *p = x->parent;
	struct canvas *dummy;
	int d = x->typ;
	struct canvas *n = x->c[d];
	struct canvas *o = x->c[!d];
	if( o && o->c[d] ) {
		x->split_point[!d] = 0.0;
	} else if( o ) {
		assert( o->c[d] == NULL );
		assert( o->parent == x );
		o->parent = p;
		*(p ? &p->c[d] : &root) = o;
		o->c[d] = n;
		*(n ? &n->parent : &dummy) = o;
		o->origin = x->origin;
		equalize(o, NULL);
		freecanvas(x);
	} else if( n ) {
		n->parent = p;
		n->origin = x->origin;
		*(p ? &p->c[d] : &root) = n;
		freecanvas(x);
	} else if( p ) {
		p->split_point[d] = 1.0;
		p->c[d] = NULL;
		freecanvas(x);
	} else {
		root = NULL;
	}
	if( view_root == x ) {
		view_root = root;
	}
	if( x == focused ) {
		focus(p, 0);
	}
	reshape(root, 0, 0, LINES, COLS);
}

static void
reshape_window(struct canvas *N, int h, int w, int d)
{
	struct proc *n = &N->p;
	if( n->pt >= 0 ) {
		h = h > 1 ? h - 1 : 24;
		w = w > 0 ? w : 80;
		int oy, ox;
		n->ws = (struct winsize) {.ws_row = h, .ws_col = w};
		getyx(n->s->win, oy, ox);
		resize_pad(&n->pri.win, MAX(h, scrollback_history), w);
		resize_pad(&n->alt.win, h, w);
		resize_pad(&N->wpty, h, w);
		n->pri.tos = n->pri.off = MAX(0, scrollback_history - h);
		n->alt.tos = n->alt.off = 0;
		wsetscrreg(n->pri.win, 0, MAX(scrollback_history, h) - 1);
		wsetscrreg(n->alt.win, 0, h - 1);
		if( d != 0 ) {
			wmove(n->s->win, oy + d, ox);
			wscrl(n->s->win, -d);
		}
		wrefresh(n->s->win);
		if( ioctl(n->pt, TIOCSWINSZ, &n->ws) ) {
			perror("ioctl");
		}
		extend_tabs(n, n->tabstop);
		if( kill(n->pid, SIGWINCH) ) {
			perror("kill");
		}
	}
}

static void
reshape(struct canvas *n, int y, int x, int h, int w)
{
	if( n ) {
		int k = winsiz(n->wpty, 0);
		int d = k - h * n->split_point[0]; /* TODO */

		n->origin.y = y;
		n->origin.x = x;
		int h1 = h * n->split_point[0];
		int w1 = w * n->split_point[1];
		int have_title = h1 && w1;
		int have_div = h && w && n->c[1];

		if( have_div ) {
			resize_pad(&n->wdiv, n->typ ? h : h1, 1);
		} else {
			delwinnul(&n->wdiv);
		}
		if( have_title ) {
			resize_pad(&n->wtit, 1, w1);
		} else {
			delwinnul(&n->wtit);
		}

		reshape(n->c[0], y + h1, x, h - h1, n->typ ? w1 : w);
		reshape(n->c[1], y, x + w1 + have_div,
			n->typ ? h : h1, w - w1 - have_div);
		reshape_window(n, h1, w1, d);
	}
}

static void
draw_pane(WINDOW *w, int y, int x, int offset, int r)
{
	int rows, cols;
	getmaxyx(w, rows, cols);
	assert( rows == winsiz(w, 0) );
	assert( cols == winsiz(w, 1) );
	pnoutrefresh(w, offset, 0, y, x, y + (r ? r : rows) - 1, x + cols - 1);
}

static void
draw_title(struct canvas *n)
{
	if( n->wtit ) {
		char t[128];
		int y, x;
		getmaxyx(n->p.s->win, y, x);
		size_t s = MAX(x - 1, (int)sizeof t);
		if( binding == &cmd_keys && n == focused ) {
			wattron(n->wtit, A_REVERSE);
		} else {
			wattroff(n->wtit, A_REVERSE);
		}
		snprintf(t, s, "%d: %s ", (int)n->p.pid, n->title);
		/*
		snprintf(t, s, "%d: %d,%d %d,%d %d,%d", (int)n->p.pid,
			n->origin.y, n->origin.x,
			n->x.y, n->x.x, n->siz.y, n->siz.x);
		*/
		int glyph = ACS_HLINE;
		mvwprintw(n->wtit, 0, 0, "%s", t);
		int len = strlen(t);
		if( x - len > 0 ) {
			mvwhline(n->wtit, 0, len, glyph, x - len);
		}
		draw_pane(n->wtit, n->origin.y + y - n->p.s->tos,
			n->origin.x, 0, 0);
	}
}

static void
draw_window(struct screen *s, const struct point *a)
{
	int y, x;
	getmaxyx(s->win, y, x);
	if( y > s->tos && x > 0 ) {
		draw_pane(s->win, a->y, a->x, s->off, y - s->tos);
	}
}

static void
draw(struct canvas *n) /* Draw a canvas. */
{
	if( n != NULL ) {
		draw(n->c[0]);
		draw(n->c[1]);
		if( n->wdiv ) {
			int y, x;
			getmaxyx(n->p.s->win, y, x);
			mvwvline(n->wdiv, 0, 0, ACS_VLINE, y);
			draw_pane(n->wdiv, n->origin.y, n->origin.x + x, 0, 0);
		}
		draw_title(n);
		draw_window(n->p.s, &n->origin);
	}
}

int
create(struct canvas *n, const char *args[])
{
	assert( n != NULL );
	int dir = *args && **args == 'C' ? 1 : 0;
	int y, x;
	/* Always split last window in a chain */
	while( n->c[dir] != NULL ) {
		n = n->c[dir];
	}
	assert( n->c[dir] == NULL );
	struct canvas *v = n->c[dir] = newcanvas();
	if( v != NULL ) {
		v->typ = dir;
		v->parent = n;
		n = balance(v);
		canvas_yx(n, &y, &x);
		new_screens(&v->p);
		new_pty(&v->p);
	}
	reshape(n, n->origin.y, n->origin.x, y, x);
	return 0;
}

static bool
getinput(struct canvas *n, fd_set *f) /* check all ptty's for input. */
{
	bool status = true;;
	if( n && n->c[0] && !getinput(n->c[0], f) ) {
		status = false;
	} else if( n && n->c[1] && !getinput(n->c[1], f) ) {
		status = false;
	} else if( n && n->p.pt > 0 && FD_ISSET(n->p.pt, f) ) {
		char iobuf[BUFSIZ];
		ssize_t r = read(n->p.pt, iobuf, sizeof(iobuf));
		if( r > 0 ) {
			vtwrite(&n->p.vp, iobuf, r);
		} else if( errno != EINTR && errno != EWOULDBLOCK ) {
			free_proc(&n->p);
			prune(n);
			status = false;
		}
	}
	return status;
}

static void
scrollbottom(struct canvas *n)
{
	if( n && n->p.s ) {
		n->p.s->off = n->p.s->tos;
	}
}

int
digit(struct canvas *n, const char **args)
{
	(void)n;
	cmd_count = 10 * (cmd_count == -1 ? 0 : cmd_count) + args[0][0] - '0';
	return 0;
}

static int
scrolln(struct canvas *n, const char **args)
{
	/* TODO: enable srolling left/right */
	if( n && n->p.s && n->p.s->win ) {
		int y, x;
		getmaxyx(n->p.s->win, y, x);
		(void) x;
		int count = cmd_count == -1 ? (y - n->p.s->tos) - 1 : cmd_count;
		if( args[0][0] == '-' ) {
			n->p.s->off = MAX(0, n->p.s->off - count);
		} else {
			n->p.s->off = MIN(n->p.s->tos, n->p.s->off + count);
		}
	}
	return 0;
}

static int
sendarrow(struct canvas *n, const char **args)
{
	const char *k = args[0];
    char buf[100] = {0};
    snprintf(buf, sizeof(buf) - 1, "\033%s%s", n->p.pnm? "O" : "[", k);
    safewrite(n->p.pt, buf, strlen(buf));
    return 0;
}

int
reshape_root(struct canvas *n, const char **args)
{
	(void)args;
	reshape(view_root, 0, 0, LINES, COLS);
	scrollbottom(n);
	return 0;
}

#if 0
struct canvas *
find_canvas(struct canvas *b, int id)
{
	struct canvas *r = id ? NULL : root;
	if( id && b != NULL ) {
		if( b->id == id ) {
			r = b;
		} else if( ( r = find_canvas(b->c[0], id)) == NULL ) {
			r = find_canvas(b->c[1], id);
		}
	}
	return r;
}
#endif

int
contains(struct canvas *n, int y, int x)
{
	int y1, x1;
	getmaxyx(n->p.s->win, y1, x1);
	return
		y >= n->origin.y && y <= n->origin.y + y1 - n->p.s->tos + 1 &&
		x >= n->origin.x && x <= n->origin.x + x1;
}

struct canvas *
find_window(struct canvas *n, int y, int x)
{
	struct canvas *r = n;
	if( n && !contains(n, y, x) ) {
		if( ( r = find_window(n->c[0], y, x)) == NULL ) {
			r = find_window(n->c[1], y, x);
		}
	}
	return r;
}

int
mov(struct canvas *n, const char **args)
{
	assert( n == focused && n != NULL );
	char cmd = args[0][0];
	int y, x;
	(void) x;
	getmaxyx(n->p.s->win, y, x);
	int count = cmd_count < 1 ? 1 : cmd_count;
	int startx = n->origin.x;
	int starty = n->origin.y + y - n->p.s->tos;
	struct canvas *t = n;
	if( cmd == 'p' ) {
		n = lastfocused;
	} else for( ; t && count--; n = t ? t : n ) {
		int y, x;
		getmaxyx(t->p.s->win, y, x);
		switch( cmd ) {
		case 'k': /* move up */
			t = find_window(view_root, t->origin.y - 1,
				startx);
			break;
		case 'j': /* move down */
			t = find_window(view_root,
				t->origin.y + y - t->p.s->tos + 2,
					startx);
			break;
		case 'l': /* move right */
			t = find_window(view_root, starty,
				t->origin.x + x + 1);
			break;
		case 'h': /* move left */
			t = find_window(view_root, starty,
				t->origin.x - 1);
			break;
		}
	}
	focus(n, 1);
	return 0;
}

int
send(struct canvas *n, const char **args)
{
	if( n->p.lnm && args[0][0] == '\r' ) {
		assert( args[0][1] == '\0' );
		assert( args[1] == NULL );
		args[0] = "\r\n";
	}
	size_t len = args[1] ? strtoul(args[1], NULL, 10 ) : strlen(args[0]);
	safewrite(n->p.pt, args[0], len);
	scrollbottom(n);
	return 0;
}

static struct canvas *
balance(struct canvas *n)
{
	int dir = n->typ;
	while( n->c[dir] != NULL ) {
		n = n->c[dir];
	}
	for(int count = 1; n; n = n->parent ) {
		n->split_point[dir] = 1.0 / count++;
		if( n->parent && n->parent->c[dir] != n ) {
			break;
		}
		if( n->typ != dir ) {
			break;
		}
	}
	return n ? n : root;
}

int
equalize(struct canvas *n, const char **args)
{
	(void) args;
	assert( n != NULL );
	int y, x;
	canvas_yx(n, &y, &x);
	n = balance(n);
	reshape(n, n->origin.y, n->origin.x, y, x);
	return 0;
}

int
transition(struct canvas *n, const char **args)
{
	binding = binding == &keys ? &cmd_keys : &keys;
	if( args && args[0] ) {
		send(n, args);
	}
	if( binding == &keys ) {
		scrollbottom(n);
	}
	return 0;
}

static void
add_key(struct handler *b, wchar_t k, action act, ...)
{
	if( b == code_keys ) {
		assert( k >= KEY_MIN && k <= KEY_MAX );
		k -= KEY_MIN;
	}
	int i = 0;
	b[k].act = act;
	va_list ap;
	va_start(ap, act);
	do b[k].args[i] = va_arg(ap, const char *);
	while( b[k].args[i++] != NULL );
	va_end(ap);
}

int
new_tabstop(struct canvas *n, const char **args)
{
	(void) args;
	n->p.ntabs = 0;
	extend_tabs(&n->p, n->p.tabstop = cmd_count > -1 ? cmd_count : 8);
	return 0;
}

#if 0
int
swap(struct canvas *a, const char **args)
{
	int rv = -1;
	(void) args;
	if( cmd_count > 0 && a->parent ) {
		struct canvas *b = find_canvas(root, cmd_count);
		int ca = a == a->parent->c[1];
		int cb = b == b->parent->c[1];
		struct canvas *siba = sibling(a);
		struct canvas *sibb = sibling(b);
		b->parent->c[cb] = a;
		a->parent->c[ca] = b;
		a->parent = b->parent;
		b->parent = siba->parent;
		reshapechildren(siba->parent);
		reshapechildren(sibb->parent);
		rv = 0;
	}
	return rv;
}
#endif

void
build_bindings()
{
	assert( KEY_MAX - KEY_MIN < 2048 ); /* Avoid overly large luts */

	add_key(keys, commandkey, transition, NULL);
	add_key(keys, L'\r', send, "\r",  NULL);
	add_key(keys, L'\n', send, "\n", NULL);
	add_key(keys, 0, send, "\000", "1", NULL);

	add_key(cmd_keys, commandkey, transition, &commandkey, "1", NULL);
	add_key(cmd_keys, L'\r', transition, NULL);
	add_key(cmd_keys, L'b', scrolln, "-", NULL);
	add_key(cmd_keys, L'f', scrolln, "+", NULL);
	add_key(cmd_keys, L'=', equalize, NULL);
	add_key(cmd_keys, L'c', create, NULL);
	add_key(cmd_keys, L'C', create, "C", NULL);
	add_key(cmd_keys, L'j', mov, "j", NULL);
	add_key(cmd_keys, L'k', mov, "k", NULL);
	add_key(cmd_keys, L'l', mov, "l", NULL);
	add_key(cmd_keys, L'h', mov, "h", NULL);
	add_key(cmd_keys, L'p', mov, "p", NULL);
	add_key(cmd_keys, L't', new_tabstop, NULL);
	add_key(cmd_keys, L'0', digit, "0", NULL);
	add_key(cmd_keys, L'1', digit, "1", NULL);
	add_key(cmd_keys, L'2', digit, "2", NULL);
	add_key(cmd_keys, L'3', digit, "3", NULL);
	add_key(cmd_keys, L'4', digit, "4", NULL);
	add_key(cmd_keys, L'5', digit, "5", NULL);
	add_key(cmd_keys, L'6', digit, "6", NULL);
	add_key(cmd_keys, L'7', digit, "7", NULL);
	add_key(cmd_keys, L'8', digit, "8", NULL);
	add_key(cmd_keys, L'9', digit, "9", NULL);
	add_key(code_keys, KEY_RESIZE, reshape_root, NULL);
	add_key(code_keys, KEY_F(1), send, "\033OP", NULL);
	add_key(code_keys, KEY_F(2), send, "\033OQ", NULL);
	add_key(code_keys, KEY_F(3), send, "\033OR", NULL);
	add_key(code_keys, KEY_F(4), send, "\033OS", NULL);
	add_key(code_keys, KEY_F(5), send, "\033[15~", NULL);
	add_key(code_keys, KEY_F(6), send, "\033[17~", NULL);
	add_key(code_keys, KEY_F(7), send, "\033[18~", NULL);
	add_key(code_keys, KEY_F(8), send, "\033[19~", NULL);
	add_key(code_keys, KEY_F(9), send, "\033[20~", NULL);
	add_key(code_keys, KEY_F(10), send, "\033[21~", NULL);
	add_key(code_keys, KEY_F(11), send, "\033[23~", NULL);
	add_key(code_keys, KEY_F(12), send, "\033[24~", NULL);
	add_key(code_keys, KEY_HOME, send, "\033[1~", NULL);
	add_key(code_keys, KEY_END, send, "\033[4~", NULL);
	add_key(code_keys, KEY_PPAGE, send, "\033[5~", NULL);
	add_key(code_keys, KEY_NPAGE, send, "\033[6~", NULL);
	add_key(code_keys, KEY_BACKSPACE, send, "\177", NULL);
	add_key(code_keys, KEY_DC, send, "\033[3~", NULL);
	add_key(code_keys, KEY_IC, send, "\033[2~", NULL);
	add_key(code_keys, KEY_BTAB, send, "\033[Z", NULL);
	add_key(code_keys, KEY_ENTER, send, "\r", NULL);
	add_key(code_keys, KEY_UP, sendarrow, "A", NULL);
	add_key(code_keys, KEY_DOWN, sendarrow, "B", NULL);
	add_key(code_keys, KEY_RIGHT, sendarrow, "C", NULL);
	add_key(code_keys, KEY_LEFT, sendarrow, "D", NULL);
}

/* Naive test to determine if k "looks like" a command */
static int
is_command(const char *k)
{
	const char *space = strchr(k, ' ');
	char *c, *path = getenv("PATH");
	char name[PATH_MAX];
	size_t len = space ? (size_t)(space - k) : strlen(k);
	if( (c = strchr(k, '/' )) && c < k + len ) {
		memcpy(name, k, len);
		name[len] = '\0';
		return access(k, X_OK) == 0;
	} else for( c = strchr(path, ':'); c; c = strchr(path, ':') ) {
		size_t n = c - path;
		if( n > sizeof name - 2 - len ) {
			/* horribly ill-formed PATH */
			return 0;
		}
		memcpy(name, path, n);
		name[n] = '/';
		memcpy(name + n + 1, k, len);
		name[n + len + 1] = '\0';
		if( access(name, X_OK) == 0 ) {
			return 1;
		}
		path = c + 1;
	}
	return 0;
}

static void
handlechar(int r, int k) /* Handle a single input character. */
{
	struct handler *b = NULL;
	struct canvas *n = focused;

	assert( r != ERR );
	if( r == OK && k > 0 && k < (int)sizeof *binding ) {
		unsigned len = strlen(n->putative_cmd);
		if( k == '\r' ) {
			if( is_command(n->putative_cmd) ) {
				strcpy(n->title, n->putative_cmd);
			}
			len = 0;
		} else if( len < sizeof n->putative_cmd - 1 && isprint(k) ) {
			n->putative_cmd[len++] = k;
		}
		n->putative_cmd[len] = '\0';
		b = &(*binding)[k];

	} else if( r == KEY_CODE_YES ) {
		assert( k >= KEY_MIN && k <= KEY_MAX );
		b = &code_keys[k - KEY_MIN];
	}

	if( b && b->act ) {
		b->act(n, b->args);
	} else {
		char c[MB_LEN_MAX + 1] = {0};
		if( wctomb(c, k) > 0 ) {
			scrollbottom(n);
			safewrite(n->p.pt, c, strlen(c));
		}
		if( binding != &keys ) {
			transition(n, NULL);
		}
	}
	if( !b || !(b->act == digit) ) {
		cmd_count = -1;
	}
}

void
main_loop(void)
{
	while( root != NULL ) {
		int r;
		wint_t w = 0;
		fd_set sfds = fds;

		draw(view_root);
		fixcursor();
		doupdate();
		if( select(maxfd + 1, &sfds, NULL, NULL, NULL) < 0 ) {
			FD_ZERO(&sfds);
		}
		while( (r = wget_wch(focused->p.s->win, &w)) != ERR ) {
			handlechar(r, w);
		}
		getinput(root, &sfds);
	}
}

static void
parse_args(int argc, char *const*argv)
{
	int c;
	char *name = strrchr(argv[0], '/');
	while( (c = getopt(argc, argv, ":hc:s:T:t:")) != -1 ) {
		switch (c) {
		case 'h':
			printf("usage: %s [-s history-size] [-T NAME]"
				" [-t NAME] [-c KEY]\n",
				name ? name + 1 : argv[0]);
			exit(0);
		case 'c':
			commandkey = CTL(optarg[0]);
			break;
		case 's':
			scrollback_history = strtol(optarg, NULL, 10);
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

int
sttm_main(int argc, char *const*argv)
{
	struct canvas *r;
	char buf[32];
	FD_SET(maxfd, &fds);
	snprintf(buf, sizeof buf - 1, "%lu", (unsigned long)getpid());
	setenv("STTM", buf, 1);
	setenv("TERM", getterm(), 1);
	setenv("STTM_VERSION", VERSION, 1);
	unsetenv("COLUMNS");
	unsetenv("LINES");
	setlocale(LC_ALL, "");
	signal(SIGCHLD, SIG_IGN); /* automatically reap children */
	parse_args(argc, argv);
	build_bindings();

	if( initscr() == NULL ) {
		exit(EXIT_FAILURE);
	}
	raw();
	noecho();
	nonl();
	intrflush(NULL, FALSE);
	start_color();
	use_default_colors();

	r = view_root = root = newcanvas();
	if( r == NULL || !new_screens(&r->p) || !new_pty(&r->p) ) {
		err(EXIT_FAILURE, "Unable to create root window");
	}
	reshape(view_root, 0, 0, LINES, COLS);
	focus(view_root, 0);
	main_loop();
	endwin();
	return EXIT_SUCCESS;
}
