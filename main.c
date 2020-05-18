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
   TODO: (feature requests)
     Register signal handlers for TERM and HUP (at least).  Need to ensure
       that endwin is called.
     Handle SIGWINCH better.
     Handle resizing better in general.  If a user swaps from lateral
       to transverse split and back, the final result should not change.
 */

#include "main.h"

static int id;
static struct handler keys[128];
static struct handler cmd_keys[128];
static struct handler code_keys[KEY_MAX - KEY_MIN + 1];
static struct handler (*binding)[128] = &keys;
static struct node *root, *focused, *lastfocused = NULL;
static struct node *view_root;
static char commandkey = CTL(COMMAND_KEY);
static int nfds = 1; /* stdin */
static fd_set fds;
static unsigned cmd_count = 0;
int scrollback_history = 1024;

static void reshape(struct node *n, int y, int x, int h, int w);
static void draw(struct node *n);
static void reshapechildren(struct node *n);
static const char *term = NULL;
static void freenode(struct node *n);
static struct node * splice(struct node *old, struct node *new, int, double);
static action transition;
static action split;

void
safewrite(int fd, const char *b, size_t n) /* Write with retry on interrupt */
{
	ssize_t s;
	const char *e = b + n;
	while( b < e && ((s = write(fd, b, e - b)) >= 0 || errno == EINTR) ) {
		b += s > 0 ? s : 0;
	}
}

static const char *
getshell(void) /* Get the user's preferred shell. */
{
	const char *shell = getenv("SHELL");
	struct passwd *pwd = shell && *shell ? NULL : getpwuid(geteuid());
	return shell && *shell ? shell : pwd ? pwd->pw_shell : "/bin/sh";
}

static void
extend_tabs(struct node *n, int tabstop)
{
	if( n->ntabs < n->w ) {
		n->tabs = realloc(n->tabs, n->w * sizeof *n->tabs);
		if( n->tabs != NULL ) {
			for( ; n->ntabs < n->w; n->ntabs++ ) {
				n->tabs[n->ntabs] = n->ntabs % tabstop == 0;
			}
		} else {
			n->ntabs = 0;
		}
		assert( n->ntabs == n->w || (n->ntabs == 0 && ! n->tabs) );
	}
}

static struct node *
newnode(int y, int x, int h, int w, int id)
{
	struct node *n = NULL;
	if( (n = calloc(1, sizeof *n)) != NULL ) {
		n->id = id;
		n->w = w;
		n->y = y;
		n->x = x;
		n->h = h;
		n->pt = -1;
		extend_tabs(n, n->tabstop = 8);
		if( h && w ) {
			n->twin = newpad(1, w);
		}
		strncpy(n->title, getshell(), sizeof n->title);
		n->title[sizeof n->title - 1] = '\0';
	}
	return n;
}

static int
delwinnul(WINDOW *w)
{
	return w ? delwin(w) : 0;
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
		delwinnul(n->pri.win);
		delwinnul(n->alt.win);
		if( n->pt >= 0 ) {
			close(n->pt);
			FD_CLR(n->pt, &fds);
		}
		free(n->tabs);
		free(n);
	}
}

static void
fixcursor(void) /* Move the terminal cursor to the active window. */
{
	struct node *f = focused;
	if( f && f->s ) {
		int y, x;
		int show = binding != &cmd_keys && f->s->vis;
		curs_set(f->s->off != f->s->tos ? 0 : show);
		getyx(f->s->win, y, x);
		y = MIN(MAX(y, f->s->tos), f->s->tos + f->h - 1);
		wmove(f->s->win, y, x);
	}
}

static const char *
getterm(void)
{
	const char *t = term ? term : getenv("TERM");
	return t ? t : COLORS > 255 ? DEFAULT_COLOR_TERMINAL : DEFAULT_TERMINAL;
}

static int
new_screens(struct node *n)
{
	int h = n->h > 2 ? n->h - 1 : 24;
	int w = n->w > 1 ? n->w : 80;

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

static int
new_pty(struct node *n)
{
	int h = n->h > 1 ? n->h - 1 : 24;
	int w = n->w ? n->w : 80;
	struct winsize ws = {.ws_row = h, .ws_col = w};
	n->pid = forkpty(&n->pt, NULL, NULL, &ws);
	if( n->pid < 0 ) {
		perror("forkpty");
		return -1;
	} else if( n->pid == 0 ) {
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
	FD_SET(n->pt, &fds);
	fcntl(n->pt, F_SETFL, O_NONBLOCK);
	nfds = n->pt > nfds ? n->pt : nfds;
	return n->pt;
}

static void
focus(struct node *n)
{
	if( n && n->s && n->s->win ) {
		lastfocused = focused;
		focused = n;
	} else if( n ) {
		focus(n->c[0] ? n->c[0] : n->c[1]);
	}
}

static void
reap_dead_window(struct node *c)
{
	struct node *p = c->parent;
	if( p != NULL ) {
		struct node *n = p->parent;
		struct node *sibling = p->c[ c == p->c[0] ];
		sibling->parent = n;
		if( n == NULL ) {
			view_root = root = sibling;
			reshape(root, 0, 0, LINES, COLS);
		} else {
			n->c[ p == n->c[1] ] = sibling;
			reshapechildren(n);
		}
		freenode(p);
		if( c == focused ) {
			focus(sibling);
			lastfocused = NULL;
		}
	} else {
		assert( c == root );
		view_root = root = focused = NULL;
	}
	freenode(c);
}

static void
reshape_window(struct node *n, int d)
{
	int h = n->h > 1 ? n->h - 1 : 24;
	int w = n->w ? n->w : 80;
	int oy, ox;
	struct winsize ws = {.ws_row = h, .ws_col = w}; /* tty(4) */

	extend_tabs(n, n->tabstop);
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
	ioctl(n->pt, TIOCSWINSZ, &ws);
}

static void
reshapechildren(struct node *n)
{
	if( n->twin ) {
		wclear(n->twin);
		wnoutrefresh(n->twin);
	}
	if( n->split == '|' ) {
		int w[2];
		w[0] = n->w * n->split_point;
		if( w[0] && w[0] == n->w ) {
			w[0] -= 1;
		}
		w[1] = n->w - w[0] - 1;
		assert( w[1] >= 0 && w[0] >= 0 );
		assert( n->h >= 0 && n->x >= 0 && n->y >= 0 );
		reshape(n->c[0], n->y, n->x, n->h, w[0]);
		reshape(n->c[1], n->y, n->x + w[0] + 1, n->h, w[1]);
		if( n->w > 0 ) {
			resize_pad(&n->twin, n->h, 1);
		} else {
			delwinnul(n->twin);
		}
	} else if( n->split == '-' ) {
		int h[2];
		h[0] = n->h * n->split_point;
		h[1] = n->h - h[0];
		assert( h[0] <= n->h && h[0] >= 0 && h[1] >= 0 );
		reshape(n->c[0], n->y, n->x, h[0], n->w);
		reshape(n->c[1], n->y + h[0], n->x, h[1], n->w);
		delwinnul(n->twin);
		n->twin = NULL;
	}
}

static void
reshape(struct node *n, int y, int x, int h, int w)
{
	if (n->y == y && n->x == x && n->h == h && n->w == w && ! n->split)
		return;

	int d = n->h - h;
	n->y = y;
	n->x = x;
	n->h = h;
	n->w = w;

	if( n->split == '\0' ) {
		reshape_window(n, d);
		if( n->h ) {
			resize_pad(&n->twin, 1, n->w);
		} else {
			delwinnul(n->twin);
		}
	} else {
		reshapechildren(n);
	}
	draw(n);
	doupdate();
}

static void
draw_title(struct node *n)
{
	char t[128];
	int x = 0;
	size_t s = MAX(n->w - 2, (int)sizeof t);
	if( binding == &cmd_keys && n == focused ) {
		wattron(n->twin, A_REVERSE);
	} else {
		wattroff(n->twin, A_REVERSE);
	}
	snprintf(t, s, "%d (%d) %s ", n->id, (int)n->pid, n->title);
	x += strlen(t);
	if( n->twin ) {
		mvwprintw(n->twin, 0, 0, "%s", t);
		mvwhline(n->twin, 0, x, ACS_HLINE, n->w - x);
		pnoutrefresh(n->twin, 0, 0, n->y + n->h - 1, n->x,
			n->y + n->h - 1, n->x + n->w);
	}
}

static void
drawchildren(const struct node *n)
{
	draw(n->c[0]);
	if (n->split == '|' && n->twin ) {
		assert( n->c[0]->y == n->y );
		mvwvline(n->twin, 0, 0, ACS_VLINE, n->h);
		pnoutrefresh(n->twin, 0, 0, n->y, n->x + n->c[0]->w,
			n->y + n->h, n->x + n->c[0]->w);
	}
	draw(n->c[1]);
}

static void
draw(struct node *n) /* Draw a node. */
{
	if( n != NULL ) {
		if( ! n->split ) {
			draw_title(n);
			if( n->h > 1 && n->w > 0 ) {
				pnoutrefresh(
					n->s->win,  /* pad */
					n->s->off,  /* pminrow */
					0,          /* pmincol */
					n->y,       /* sminrow */
					n->x,       /* smincol */
					n->y + n->h - 2,  /* smaxrow */
					n->x + n->w - 1   /* smaxcol */
				);
			}
		} else {
			assert( strchr("|-", n->split) );
			drawchildren(n);
		}
	}
}

static int
reorient(struct node *n, const char *args[])
{
	if( n && n->split == '\0' ) {
		reorient(n->parent, args);
	} else if( n ) {
		n->split = n->split == '|' ? '-' : '|';
		reshapechildren(n);
		drawchildren(n);
	}
	return 0;
}

static int
split(struct node *n, const char *args[])
{
	assert( n != NULL );
	assert( !n->split );
	assert( n->c[0] == NULL );
	assert( n->c[1] == NULL );
	(void)args;
	int split = n->parent ? n->parent->split : '-';
	for( int count = cmd_count ? cmd_count : 1; n && count; count -= 1 ) {
		struct node *v = newnode(0, 0, n->h, n->w, ++id);
		if( v != NULL && new_screens(v) && new_pty(v) ) {
			splice(n, v, split, 1.0 / ( count + 1));
		}
		n = v;
	}
	return 0;
}

static struct node *
splice(struct node *n, struct node *v, int typ, double sp)
{
	struct node *p = n->parent;
	struct node *c = newnode(n->y, n->x, n->h, n->w, 0);
	if( c != NULL ) {
		c->split = typ;
		c->split_point = sp;
		c->parent = p;
		c->c[0] = n;
		c->c[1] = v;
		n->parent = v->parent = c;
		if( p ) {
			p->c[ p->c[1] == n ] = c;
			reshapechildren(p);
		} else {
			view_root = p = root = c;
			reshape(c, 0, 0, LINES, COLS);
		}
		focus(v);
		draw(p);
	}
	return c;
}

static bool
getinput(struct node *n, fd_set *f) /* check all ptty's for input. */
{
	bool status = true;;
	if( n && n->c[0] && !getinput(n->c[0], f) ) {
		status = false;
	} else if( n && n->c[1] && !getinput(n->c[1], f) ) {
		status = false;
	} else if( n && ! n->split  && n->pt > 0 && FD_ISSET(n->pt, f) ) {
		char iobuf[BUFSIZ];
		ssize_t r = read(n->pt, iobuf, sizeof(iobuf));
		if( r > 0 ) {
			vtwrite(&n->vp, iobuf, r);
		} else if( errno != EINTR && errno != EWOULDBLOCK ) {
			assert(n->c[0] == NULL);
			assert(n->c[1] == NULL);
			reap_dead_window(n);
			status = false;
		}
	}
	return status;
}

static void
scrollbottom(struct node *n)
{
	if( n && n->s ) {
		n->s->off = n->s->tos;
	}
}

static int
digit(struct node *n, const char **args)
{
	(void)n;
	(void)args;
	assert( args && args[0]);

	cmd_count = 10 * cmd_count + args[0][0] - '0';
	return 0;
}

static int
scrolln(struct node *n, const char **args)
{
	if(args[0][0] == '-') {
		n->s->off = MAX(0, n->s->off - n->h / 2);
	} else {
		n->s->off = MIN(n->s->tos, n->s->off + n->h / 2);
	}
	return 0;
}

static int
sendarrow(struct node *n, const char **args)
{
	const char *k = args[0];
    char buf[100] = {0};
    snprintf(buf, sizeof(buf) - 1, "\033%s%s", n->pnm? "O" : "[", k);
    safewrite(n->pt, buf, strlen(buf));
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

static struct node *
find_node(struct node *b, int id)
{
	struct node *r = id ? NULL : root;
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
	return y >= n->y && y < n->y + n->h && x >= n->x && x <= n->x + n->w;
}

struct node *
find_window(struct node *n, int y, int x)
{
	assert( !n || n->split != '-' || n->c[0]->h + n->c[1]->h == n->h );
	assert( !n || n->split != '-' || n->c[0]->y + n->c[0]->h
		== n->c[1]->y );
	assert( !n || n->split != '|' || n->c[0]->w + n->c[1]->w + 1 == n->w );
	assert( !n || n->split != '|' || n->c[0]->x + n->c[0]->w + 1
		== n->c[1]->x );
	if( n != NULL && n->split ) {
		if( n->split == '-' ) {
			n = n->c[ y >= n->c[1]->y ];
		} else if( n->split == '|' ) {
			n = n->c[ x >= n->c[1]->x ];
		}
		n = find_window(n, y, x);
	}
	if( n && ! contains(n, y, x) ) {
		n = NULL;
	}
	return n;
}

int
mov(struct node *n, const char **args)
{
	assert( n == focused && n != NULL );
	char cmd = args[0][0];
	int count = cmd_count == 0 ? 1 : cmd_count;
	int midx = n->x + n->w / 2;
	int midy = n->y + n->h / 2;
	for( struct node *t = n; t && count--; n = t ? t : n ) {
		switch( cmd ) {
		case 'k': /* move up */
			t = find_window(view_root, t->y - 1, midx);
			break;
		case 'j': /* move down */
			t = find_window(view_root, t->y + t->h + 1, midx );
			break;
		case 'l': /* move right */
			t = find_window(view_root, midy, t->x + t->w + 1);
			break;
		case 'h': /* move left */
			t = find_window(view_root, midy, t->x - 1);
			break;
		case 'g':
			t = find_node(root, cmd_count);
			transition(t, NULL);
			break;
		case 'p':
			t = lastfocused;
			break;
		default:
			assert(0);
		}
	}
	focus(n);
	return 0;
}

static int
redrawroot(struct node *n, const char **args)
{
	(void) n;
	(void) args;
	reshapechildren(view_root);
	draw(view_root);
	return 0;
}

int
send(struct node *n, const char **args)
{
	if( n->lnm && args[0][0] == '\r' ) {
		assert( args[0][1] == '\0' );
		assert( args[1] == NULL );
		args[0] = "\r\n";
	}
	size_t len = args[1] ? strtoul(args[1], NULL, 10 ) : strlen(args[0]);
	safewrite(n->pt, args[0], len);
	scrollbottom(n);
	return 0;
}

static int
resize(struct node *n, const char **args)
{
	(void) args;
	if( n->parent ) {
		double factor = cmd_count ? MIN(100, cmd_count) / 100.0 : 1.0;
		n->parent->split_point = factor;
		reshapechildren(view_root);
	}
	return 0;
}

static int
equalize(struct node *n, const char **args)
{
	(void) args;
	assert( n->split == '\0' );
	int split = n->parent ? n->parent->split : '\0';
	int count = 2;
	while( n->parent && n->parent->split == split  ) {
		n = n->parent;
		n->split_point = 1 / (double) count++;
	}
	reshapechildren(n);
	return 0;
}

static int
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

static int
new_tabstop(struct node *n, const char **args)
{
	(void) args;
	n->ntabs = 0;
	extend_tabs(n, n->tabstop = cmd_count ? cmd_count : 8);
	return 0;
}

static int
set_root(struct node *n, const char **args)
{
	if( args[0] ) {
		n = root;
	} else if( cmd_count ) {
		n = find_node(root, cmd_count);
	} else {
		n = n->parent;
	}
	view_root = n ? n : root;
	reshape(view_root, 0, 0, LINES, COLS);
	return 0;
}

static void
build_bindings()
{
	assert( KEY_MAX - KEY_MIN < 2048 ); /* Avoid overly large luts */

	add_key(keys, commandkey, transition, NULL);
	add_key(keys, L'\r', send, "\r",  NULL);
	add_key(keys, L'\n', send, "\n", NULL);
	add_key(keys, 0, send, "\000", "1", NULL);

	add_key(cmd_keys, commandkey, transition, &commandkey, "1", NULL);
	add_key(cmd_keys, L'\r', transition, NULL);
	add_key(cmd_keys, L',', scrolln, "-1", NULL);
	add_key(cmd_keys, L'm', scrolln, "+1", NULL);
	add_key(cmd_keys, L'=', equalize, NULL);
	add_key(cmd_keys, L'>', resize, NULL);
	add_key(cmd_keys, L'c', split, NULL);
	add_key(cmd_keys, L'x', reorient, NULL);
	add_key(cmd_keys, L'r', redrawroot, NULL);
	add_key(cmd_keys, L'g', mov, "g", NULL);
	add_key(cmd_keys, L'j', mov, "j", NULL);
	add_key(cmd_keys, L'k', mov, "k", NULL);
	add_key(cmd_keys, L'l', mov, "l", NULL);
	add_key(cmd_keys, L'h', mov, "h", NULL);
	add_key(cmd_keys, L'p', mov, "p", NULL);
	add_key(cmd_keys, L't', new_tabstop, NULL);
	add_key(cmd_keys, L'v', set_root, NULL);
	add_key(cmd_keys, L'V', set_root, "base");
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
			safewrite(n->pt, c, strlen(c));
		}
		if( binding != &keys ) {
			transition(n, NULL);
		}
	}
	if( !b || !(b->act == digit) ) {
		cmd_count = 0;
	}
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
		while( (r = wget_wch(focused->s->win, &w)) != ERR ) {
			handlechar(r, w);
		}
		getinput(root, &sfds);
		draw(view_root);
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
main(int argc, char **argv)
{
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

	view_root = root = newnode(0, 0, LINES, COLS, ++id);
	if( root == NULL || !new_screens(root) || !new_pty(root) ) {
		err(EXIT_FAILURE, "Unable to create root window");
	}
	focus(view_root);
	draw(view_root);
	run();
	endwin();
	return EXIT_SUCCESS;
}
