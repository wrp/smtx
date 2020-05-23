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
     Copy-mode, with stack of registers and ability to edit.
     Ability to set titles.
     Needs to be easier to navigate in full screen mode.   Perhaps
       title bar would show title of windows above, below, to the
       left, and right.  Then hjkl navigation would stay in full
       screen mode while going between them.
     Register signal handlers for TERM and HUP.  Need to ensure
       that endwin is called.
     Handle SIGWINCH better.
 */

#include "sttm.h"

int id;
static struct handler keys[128];
static struct handler cmd_keys[128];
static struct handler code_keys[KEY_MAX - KEY_MIN + 1];
static struct handler (*binding)[128] = &keys;
struct node *focused, *lastfocused = NULL;
struct node root, *view_root;
char commandkey = CTL(COMMAND_KEY);
int nfds = 1; /* stdin */
fd_set fds;
int cmd_count = -1;
int scrollback_history = 1024;

static void reshape(struct node *n, int y, int x, int h, int w);
const char *term = NULL;
static void freenode(struct node *n);

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
extend_tabs(struct node *n, int tabstop)
{
	struct proc *p = &n->p;
	int d = n->c[1] != NULL;
	assert(p->ws.ws_row == n->h1 - 1 || (p->ws.ws_row == 24 && n->h1 < 2 ));
	assert(p->ws.ws_col == n->w1 - d || (p->ws.ws_col == 80 ) );

	int w = p->ws.ws_col;;
	if( p->ntabs < w ) {
		typeof(*p->tabs) *new;
		if( (new = realloc(p->tabs, w * sizeof *p->tabs)) != NULL ) {
			for( p->tabs = new; p->ntabs < w; p->ntabs++ ) {
				p->tabs[p->ntabs] = p->ntabs % tabstop == 0;
			}
		}
	}
}

struct node *
newnode(int y, int x, int h, int w, int id)
{
	struct node *n = NULL;
	if( (n = calloc(1, sizeof *n)) != NULL ) {
		n->id = id;
		n->y = y;
		n->x = x;
		n->h = n->h1 = h;
		n->w = n->w1 = w;
		n->split_point[0] = 1.0;
		n->split_point[1] = 1.0;
		n->p.pt = -1;
		if( h && w ) {
			n->wtit = newpad(1, w);
		}
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
	return *p ? wresize(*p, h, w ) : (*p = newpad(h, w)) ? OK : ERR;
}

static void
freenode(struct node *n)
{
	if( n ) {
		delwinnul(&n->p.pri.win);
		delwinnul(&n->p.alt.win);
		if( n->p.pt >= 0 ) {
			close(n->p.pt);
			FD_CLR(n->p.pt, &fds);
		}
		free(n->p.tabs);
		free(n);
	}
}

static void
fixcursor(void) /* Move the terminal cursor to the active window. */
{
	struct node *fo = focused;
	struct proc *f = &fo->p;  /* Odd name for backwards churn avoidance */
	assert( f && f->s );
	int y, x;
	int show = binding != &cmd_keys && f->s->vis;
	curs_set(f->s->off != f->s->tos ? 0 : show);
	getyx(f->s->win, y, x);
	y = MIN(MAX(y, f->s->tos), f->s->tos + fo->h - 1);
	wmove(f->s->win, y, x);
}

static const char *
getterm(void)
{
	const char *t = term ? term : getenv("TERM");
	return t ? t : COLORS > 255 ? DEFAULT_COLOR_TERMINAL : DEFAULT_TERMINAL;
}

int
new_screens(struct node *N)
{
	int h = N->h1 > 2 ? N->h1 - 1 : 24;
	int w = N->w1 > 1 ? N->w1 : 80;
	struct proc *n = &N->p;

	resize_pad(&n->pri.win, MAX(h, scrollback_history), w);
	resize_pad(&n->alt.win, h, w);
	if( n->pri.win == NULL || n->alt.win == NULL ) {
		return 0;
	}
	n->pri.tos = n->pri.off = MAX(0, scrollback_history - h);
	n->s = &n->pri;

	nodelay(n->pri.win, TRUE);
	nodelay(n->alt.win, TRUE);
	scrollok(n->pri.win, TRUE);
	scrollok(n->alt.win, TRUE);
	keypad(n->pri.win, TRUE);
	keypad(n->alt.win, TRUE);

	setupevents(n);
	return 1;
}

int
new_pty(struct node *n)
{
	int h = n->h1 > 1 ? n->h1 - 1 : 24;
	int w = n->w1 > 0 ? n->w1 : 80;
	struct proc *p = &n->p;
	p->ws = (struct winsize) {.ws_row = h, .ws_col = w};
	p->pid = forkpty(&p->pt, NULL, NULL, &p->ws);
	if( p->pid < 0 ) {
		perror("forkpty");
		return -1;
	} else if( p->pid == 0 ) {
		char buf[64];
		snprintf(buf, sizeof buf - 1, "%lu", (unsigned long)getppid());
		setsid();
		setenv("STTM", buf, 1);
		setenv("STTM_VERSION", VERSION, 1);
		setenv("TERM", getterm(), 1);
		signal(SIGCHLD, SIG_DFL);
		execl(n->title, n->title, NULL);
		perror("execl");
		_exit(EXIT_FAILURE);
	}
	FD_SET(p->pt, &fds);
	fcntl(p->pt, F_SETFL, O_NONBLOCK);
	nfds = p->pt > nfds ? p->pt : nfds;
	extend_tabs(n, p->tabstop = 8);
	return p->pt;
}

void
focus(struct node *n)
{
	if( n && n != focused && n->p.s && n->p.s->win ) {
		lastfocused = focused;
		focused = n;
	} else {
		focused = root.c[0];
	}
}

void
prune(struct node *x)
{
	struct node *p = x->parent;
	assert( p != NULL );
	int d = x == p->c[1];
	struct node *n = x->c[d];
	struct node *o = x->c[!d];
	if( o && o->c[d] ) {
		x->split_point[!d] = 0.0;
	} else if( o ) {
		assert( o->c[d] == NULL );
		assert( o->parent == x );
		p->c[d] = o;
		o->parent = p;
		o->c[d] = n;
		if( n ) {
			assert( n->parent == x );
			n->parent = o;
		}
		equalize(o, NULL);
		equalize(p, NULL);
		freenode(x);
	} else {
		assert( o == NULL );
		p->c[d] = n;
		p->split_point[d] = 1.0; /* Cheesy.  Need to fix equalize() */
		if( n ) {
			n->parent = p;
		}
		if( p != &root ) {
			equalize(p, NULL);
		}
		freenode(x);
	}
	reshape(root.c[0], 0, 0, LINES, COLS);
	if( view_root == x ) {
		view_root = root.c[0];
	}
	if( x == focused ) {
		focus(p->c[d]);
	}
}

static void
reshape_window(struct node *N, int d)
{
	int need_div = N->c[1] != NULL;
	int h = N->h1 > 1 ? N->h1 - 1 : 24;
	int w = N->w1 > need_div ? N->w1 - need_div : 80;
	int oy, ox;
	struct proc *n = &N->p;
	n->ws = (struct winsize) {.ws_row = h, .ws_col = w};

	getyx(n->s->win, oy, ox);

	resize_pad(&n->pri.win, MAX(h, scrollback_history), w);
	resize_pad(&n->alt.win, h, w);

	n->pri.tos = n->pri.off = MAX(0, scrollback_history - h);
	n->alt.tos = n->alt.off = 0;
	wsetscrreg(n->pri.win, 0, MAX(scrollback_history, h) - 1);
	wsetscrreg(n->alt.win, 0, h - 1);
	if( d != 0 ) {
		wmove(n->s->win, oy + d, ox);
		wscrl(n->s->win, -d);
	}
	wrefresh(n->s->win);
	ioctl(n->pt, TIOCSWINSZ, &n->ws);
	extend_tabs(N, n->tabstop);
}

static void
reshape(struct node *n, int y, int x, int h, int w)
{
	if( n ) {
		int d = n->h1 - h * n->split_point[0];
		n->y = y;
		n->x = x;
		n->h = h;
		n->w = w;
		n->h1 = h * n->split_point[0];
		n->w1 = w * n->split_point[1];

		reshape_window(n, d);
		if( n->h1 && ! n->hide_title ) {
			resize_pad(&n->wtit, 1, n->w1 - (n->c[1] != NULL));
		} else {
			delwinnul(&n->wtit);
		}
		if( n->w1 && ! n->hide_div && (n->c[1] || n->parent->dir) ) {
			resize_pad(&n->wdiv, n->h1, 1);
		}

		y = n->y + n->h1;
		if( n->dir ) {
		reshape(n->c[0], n->y + n->h1, n->x, n->h - n->h1, n->w - n->w1);
		reshape(n->c[1], n->y, n->x + n->w1, n->h, n->w - n->w1);
		} else {
		reshape(n->c[0], n->y + n->h1, n->x, n->h - n->h1, n->w);
		reshape(n->c[1], n->y, n->x + n->w1, n->h1, n->w - n->w1);
		}
		draw(n);
		doupdate();
	}
}

static void
draw_title(struct node *n)
{
	if( n->wtit ) {
		char t[128];
		int x = 0;
		size_t s = MAX(n->w1 - 2, (int)sizeof t);
		if( binding == &cmd_keys && n == focused ) {
			wattron(n->wtit, A_REVERSE);
		} else {
			wattroff(n->wtit, A_REVERSE);
		}
		snprintf(t, s, "%d (%d) %s ", n->id, (int)n->p.pid, n->title);
		x += strlen(t);
		int glyph = ACS_HLINE;
		mvwprintw(n->wtit, 0, 0, "%s", t);
		mvwhline(n->wtit, 0, x, glyph, n->w - x);
		pnoutrefresh(n->wtit, 0, 0, n->y + n->h1 - 1, n->x,
			n->y + n->h1 - 1, n->x + n->w1 - (n->c[1] ? 1 : 0));
	}
}


void
draw(struct node *n) /* Draw a node. */
{
	if( n != NULL ) {
		assert( n->c[0] == NULL || n->c[0]->x == n->x );
		assert( n->c[1] == NULL || n->c[1]->y == n->y );
		draw_title(n);
		if( n->h1 > 1 && n->w1 > 0 ) {
			pnoutrefresh(
				n->p.s->win,
				n->p.s->off,
				0,
				n->y,
				n->x,
				n->y + n->h1 - 2,
				n->x + n->w1 - 1
			);
		}
		draw(n->c[0]);
		if( n->wdiv ) {
			mvwvline(n->wdiv, 0, 0, ACS_VLINE, n->h1);
			pnoutrefresh(n->wdiv, 0, 0, n->y, n->x + n->w1 - 1,
				n->y + n->h1, n->x + n->w1 - 1);
		}
		draw(n->c[1]);
	}
}

#if 0
int
reorient(struct node *n, const char *args[])
{
	if( n && n->split == '\0' ) {
		reorient(n->parent, args);
	} else if( n ) {
		n->split = n->split == '|' ? '-' : '|';
		reshape(n);
		draw(n);
	}
	return 0;
}
#endif

int
create(struct node *n, const char *args[])
{
	assert( n != NULL );
	int dir = *args && **args == 'C' ? 1 : 0;
	/* Always split last window in a chain */
	while( n->c[dir] != NULL ) {
		n = n->c[dir];
	}
	assert( n->c[dir] == NULL );
	int y = ( dir == 0 ) ? n->y + n->h / 2 : n->y;
	int h = ( dir == 0 ) ? n->h / 2 : n->h1;
	int x = ( dir == 1 ) ? n->x + n->w / 2 : n->x;
	int w = ( dir == 1 ) ? n->w / 2 : n->w1;
	n->split_point[dir] = 0.5;
	struct node *v = newnode(y, x, h, w, ++id);
	if( v != NULL && new_screens(v) && new_pty(v) ) {
		n->c[dir] = v;
		v->parent = n;
		v->dir = dir;
	}
	equalize(v, NULL);
	return 0;
}

static bool
getinput(struct node *n, fd_set *f) /* check all ptty's for input. */
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
			prune(n);
			status = false;
		}
	}
	return status;
}

static void
scrollbottom(struct node *n)
{
	if( n && n->p.s ) {
		n->p.s->off = n->p.s->tos;
	}
}

int
digit(struct node *n, const char **args)
{
	(void)n;
	cmd_count = 10 * (cmd_count == -1 ? 0 : cmd_count) + args[0][0] - '0';
	return 0;
}

static int
scrolln(struct node *n, const char **args)
{
	int count = cmd_count == -1 ? n->h / 2 : cmd_count;
	if(args[0][0] == '-') {
		n->p.s->off = MAX(0, n->p.s->off - count);
	} else {
		n->p.s->off = MIN(n->p.s->tos, n->p.s->off + count);
	}
	return 0;
}

static int
sendarrow(struct node *n, const char **args)
{
	const char *k = args[0];
    char buf[100] = {0};
    snprintf(buf, sizeof(buf) - 1, "\033%s%s", n->p.pnm? "O" : "[", k);
    safewrite(n->p.pt, buf, strlen(buf));
    return 0;
}

int
reshape_root(struct node *n, const char **args)
{
	(void)args;
	reshape(view_root, 0, 0, LINES, COLS);
	scrollbottom(n);
	return 0;
}

struct node *
find_node(struct node *b, int id)
{
	struct node *r = id ? NULL : root.c[0];
	if( id && b != NULL ) {
		if( b->id == id ) {
			r = b;
		} else if( ( r = find_node(b->c[0], id)) == NULL ) {
			r = find_node(b->c[1], id);
		}
	}
	return r;
}

int
contains(struct node *n, int y, int x)
{
	return y >= n->y && y < n->y + n->h1 && x >= n->x && x <= n->x + n->w1;
}

struct node *
find_window(struct node *n, int y, int x)
{
	struct node *r = n;
	if( n && !contains(n, y, x) ) {
		if( ( r = find_window(n->c[0], y, x)) == NULL ) {
			r = find_window(n->c[1], y, x);
		}
	}
	return r;
}

int
mov(struct node *n, const char **args)
{
	assert( n == focused && n != NULL );
	char cmd = args[0][0];
	int count = cmd_count < 1 ? 1 : cmd_count;
	int startx = n->x + n->w1 / 2;
	int starty = n->y + n->h1 - 1;
	struct node *t = n;
	switch( cmd ) {
	case 'p':
		n = lastfocused;
		break;
	default:
		for( ; t && count--; n = t ? t : n ) switch( cmd ) {
		case 'k': /* move up */
			t = find_window(view_root, t->y - 1, startx);
			break;
		case 'j': /* move down */
			t = find_window(view_root, t->y + t->h1, startx );
			break;
		case 'l': /* move right */
			t = find_window(view_root, starty, t->x + t->w1 + 1);
			break;
		case 'h': /* move left */
			t = find_window(view_root, starty, t->x - 1);
			break;
		}
	}
	focus(n);
	return 0;
}

int
send(struct node *n, const char **args)
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

int
equalize(struct node *n, const char **args)
{
	(void) args;
	assert( n != NULL );

	int dir = n->parent ? n == n->parent->c[1] : 0;
	int count = 2;
	while( n->c[dir] != NULL ) {
		n = n->c[dir];
	}
	while( n != view_root && n->parent && n == n->parent->c[dir] ) {
		n = n->parent;
		n->split_point[dir] = 1 / (double) count++;
	}
	reshape(n, n->y, n->x, n->h, n->w);
	return 0;
}

int
transition(struct node *n, const char **args)
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
new_tabstop(struct node *n, const char **args)
{
	(void) args;
	n->p.ntabs = 0;
	extend_tabs(n, n->p.tabstop = cmd_count > -1 ? cmd_count : 8);
	return 0;
}

#if 0
int
swap(struct node *a, const char **args)
{
	int rv = -1;
	(void) args;
	if( cmd_count > 0 && a->parent ) {
		struct node *b = find_node(root, cmd_count);
		int ca = a == a->parent->c[1];
		int cb = b == b->parent->c[1];
		struct node *siba = sibling(a);
		struct node *sibb = sibling(b);
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
	add_key(cmd_keys, L',', scrolln, "-", NULL);
	add_key(cmd_keys, L'm', scrolln, "+", NULL);
	add_key(cmd_keys, L'=', equalize, NULL);
	add_key(cmd_keys, L'c', create, NULL);
	add_key(cmd_keys, L'C', create, "C", NULL);
	add_key(cmd_keys, L'j', mov, "j", NULL);
	add_key(cmd_keys, L'k', mov, "k", NULL);
	add_key(cmd_keys, L'l', mov, "l", NULL);
	add_key(cmd_keys, L'h', mov, "h", NULL);
	add_key(cmd_keys, L'p', mov, "p", NULL);
	add_key(cmd_keys, L't', new_tabstop, NULL);
	for( int i=0; i < 10; i++ ) {
		char *buf = calloc(2, 1);
		if( buf == NULL ) {
			err(EXIT_FAILURE, "out of memory");
		}
		buf[0] = '0' + i;
		add_key(cmd_keys, '0' + i, digit, buf, NULL);
	}
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
	struct node *n = focused;

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
	while( root.c[0] != NULL ) {
		int r;
		wint_t w = 0;
		fd_set sfds = fds;

		draw(view_root);
		doupdate();
		fixcursor();
		draw(focused);
		doupdate();
		if( select(nfds + 1, &sfds, NULL, NULL, NULL) < 0 ) {
			FD_ZERO(&sfds);
		}
		while( (r = wget_wch(focused->p.s->win, &w)) != ERR ) {
			handlechar(r, w);
		}
		getinput(root.c[0], &sfds);
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
	struct node *r;
	FD_SET(STDIN_FILENO, &fds);
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

	r = view_root = root.c[0] = newnode(0, 0, LINES, COLS, ++id);
	r->parent = &root;
	if( r == NULL || !new_screens(r) || !new_pty(r) ) {
		err(EXIT_FAILURE, "Unable to create root window");
	}
	focus(view_root);
	draw(view_root);
	main_loop();
	endwin();
	return EXIT_SUCCESS;
}