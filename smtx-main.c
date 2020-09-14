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

#include "smtx.h"

static char *term = "smtx";
static struct handler keys[128];
static struct handler cmd_keys[128];
static struct handler code_keys[KEY_MAX - KEY_MIN + 1];

struct state S = {
	.commandkey = CTL('g'),
	.width = 80,
	.binding = &keys,
	.maps = { &keys, &cmd_keys },
	.history = 1024,
	.count = -1,
};

static void
set_errmsg(int rv, const char *fmt, va_list ap)
{
	(void)rv;
	int e = errno;
	wmove(S.werr, 0, 0);
	vw_printw(S.werr, fmt, ap);
	if( e ) {
		wprintw(S.werr, ": %s", strerror(e));
	}
	wclrtoeol(S.werr);
}

int
err_check(int rv, const char *fmt, ...)
{
	if( rv ) {
		int e = errno;
		va_list ap;
		va_start(ap, fmt);
		errno = e;
		( S.werr ? set_errmsg : verrx )(rv, fmt, ap);
		va_end(ap);
		errno = e;
	}
	return rv;
}

int
rewrite(int fd, const char *b, size_t n)
{
	const char *e = b + n;
	ssize_t s = 0;
	int rv = 0;
	if( n > 0 ) do {
		s = write(fd, b, e - b);
		rv = err_check(s < 0 && errno != EINTR, "write to fd %d", fd);
		b += s < 0 ? 0 : s;
	} while( b < e && rv == 0 );
	return rv;
}

static const char *
getshell(void)
{
	const char *s = getenv("SHELL");
	struct passwd *pwd = s ? NULL : getpwuid(geteuid());
	return s ? s : pwd ? pwd->pw_shell : "/bin/sh";
}

void
pty_size(struct pty *p)
{
	err_check(p->fd != -1 && ioctl(p->fd, TIOCGWINSZ, &p->ws),
		"ioctl error getting size of pty %d", p->id);
}

void
extend_tabs(struct pty *p, int tabstop)
{
	int w = p->ws.ws_col;
	typeof(*p->tabs) *n;
	if( p->ntabs < w && ( n = realloc(p->tabs, w * sizeof *n)) != NULL ) {
		for( p->tabs = n; p->ntabs < w; p->ntabs++ ) {
			p->tabs[p->ntabs] = tabstop && p->ntabs % tabstop == 0;
		}
	}
}

static void
delwinnul(WINDOW **w)
{
	err_check((*w ? delwin(*w) : OK) != OK, "Error deleting window");
	*w = NULL;
}

static void
free_proc(struct pty **pv)
{
	struct pty *p = *pv;
	if( p != NULL ) {
		free(p->tabs);
		delwinnul(&p->pri.win);
		delwinnul(&p->alt.win);
		struct pty *t, *prev = NULL;
		for( t = S.p; t; prev = t, t = t->next ) {
			if( t == p ) {
				*(prev ? &prev->next : &S.p) = t->next;
			}
		}
		free(p);
		*pv = NULL;
	}
}

void
resize_pad(WINDOW **p, int h, int w)
{
	if( *p ) {
		err_check(wresize(*p, h, w ) != OK, "Error resizing window");
	} else if( (*p = newpad(h, w)) != NULL ) {
		err_check(nodelay(*p, TRUE) != OK, "Error setting delay");
	}
}

static int
new_screens(struct pty *p)
{
	int rows, cols;

	getmaxyx(stdscr, rows, cols);
	rows = MAX(rows, S.history);
	cols = MAX(cols, S.width);

	resize_pad(&p->pri.win, rows, cols);
	resize_pad(&p->alt.win, rows, cols);
	if( ! p->pri.win || !p->alt.win ) {
		return -1;
	}
	scrollok(p->pri.win, TRUE);
	scrollok(p->alt.win, TRUE);
	keypad(p->pri.win, TRUE);
	keypad(p->alt.win, TRUE);
	p->s = &p->pri;
	p->vp.p = p;
	setupevents(&p->vp);
	return 0;
}

static struct pty *
new_pty(int rows, int cols)
{
	struct pty *p = calloc(1, sizeof *p);
	if( p != NULL ) {
		const char *sh = getshell();
		const char *bname = strrchr(sh, '/');
		bname = bname ? bname + 1 : sh;
		p->ws.ws_row = rows - 1;
		p->ws.ws_col = cols;
		p->pid = forkpty(&p->fd, NULL, NULL, &p->ws);
		if( p->pid == 0 ) {
			setsid();
			signal(SIGCHLD, SIG_DFL);
			execl(sh, sh, NULL);
			err(EXIT_FAILURE, "exec SHELL='%s'", sh);
		} else if( p->pid < 0 || new_screens(p) == -1 ) {
			err_check(1, "new_pty");
			free_proc(&p);
		} else if( p->pid > 0 ) {
			FD_SET(p->fd, &S.fds);
			fcntl(p->fd, F_SETFL, O_NONBLOCK);
			extend_tabs(p, p->tabstop = 8);
			p->id = p->fd - 2;
			struct pty **t = &S.p;
			/* Insert into ordered list */
			while( *t && (*t)->next && (*t)->next->fd > p->fd ) {
				t = &(*t)->next;
			}
			p->next = *t;
			*t = p;
			strncpy(p->status, bname, sizeof p->status - 1);
		}
	}
	return p;
}

struct canvas *
newcanvas(void)
{
	struct canvas *n = calloc(1, sizeof *n);
	int y, x;
	getmaxyx(stdscr, y, x);
	if( !n ) {
		err_check(1, "calloc");
	} else if( ( n->p = new_pty(y, MAX(x, S.width))) == NULL ) {
		free(n);
		n = NULL;
	} else {
		n->split_point[0] = 1.0;
		n->split_point[1] = 1.0;
	}
	return n;
}

void focus(struct canvas *n) { S.f = n ? n : S.v; }

static void
freecanvas(struct canvas *n)
{
	if( n ) {
		delwinnul(&n->wtit);
		delwinnul(&n->wdiv);
		delwinnul(&n->bkg);
		free(n);
	}
}

static int
winpos(WINDOW *w, int dir)
{
	int y = 0, x = 0;
	assert(w);
	getyx(w, y, x);
	return dir ? x : y;
}

static void
draw_window(struct canvas *n)
{
	struct point o = n->origin;
	struct point e = { o.y + n->extent.y - 1, o.x + n->extent.x - 1 };
	if( n->p && e.y > 0 && e.x > 0 ) {
		struct point off = n->offset;
		if( ! n->manualscroll ) {
			int x = winpos(n->p->s->win, 1);
			if( x < n->extent.x - 1 ) {
				n->offset.x = 0;
			} else if( n->offset.x + n->extent.x < x + 1 ) {
				n->offset.x = x - n->extent.x + 1;
			}
		}
		pnoutrefresh(n->p->s->win, off.y, off.x, o.y, o.x, e.y, e.x);
	}
}

void
fixcursor(void) /* Move the terminal cursor to the active window. */
{
	struct canvas *f = S.f;
	int x = 0, y = 0;
	int show = S.binding != &cmd_keys && f->p->s->vis;
	if( f->p && f->extent.y && show ) {
		assert( f->p->s );
		int top = S.history - f->extent.y;
		getyx(f->p->s->win, y, x);
		if( 0
			|| x < f->offset.x
			|| x > f->offset.x + f->extent.x - 1
			/* || y < f->offset.y (1) */
			|| y > f->offset.y + f->extent.y - 1
			|| f->offset.y < top /* (1) */
		) {
			show = false;
		} else {
			y = MIN( MAX(y, top), top + f->extent.y);
			wmove(f->p->s->win, y, x);
			draw_window(f);
		}
	}
	curs_set(show);
}
/* (1) Checking f->offset.y < top really does not make any
 * sense.  This test *should* be y < f->offset.y, but
 * changing it breaks the current tests...which
 * are not reliable! TODO: figure this out
 */

void
reshape_window(struct pty *p)
{
	err_check(ioctl(p->fd, TIOCSWINSZ, &p->ws), "ioctl on %d", p->id);
	err_check(kill(p->pid, SIGWINCH), "send WINCH to %d", (int)p->pid);
}

static void
set_height(struct canvas *n)
{
	struct pty *p = n->p;
	assert( S.history >= n->extent.y );
	p->ws.ws_row = n->extent.y;
	wsetscrreg(p->pri.win, 0, S.history - 1);
	wsetscrreg(p->alt.win, 0, n->extent.y - 1);
	wrefresh(p->s->win);
	reshape_window(p);
}

void
scrollbottom(struct canvas *n)
{
	if( n && n->p && n->p->s && n->extent.y ) {
		assert( S.history >= n->extent.y );
		n->offset.y = S.history - n->extent.y;
	}
}

void
reshape(struct canvas *n, int y, int x, int h, int w)
{
	if( n ) {
		struct pty *p = n->p;
		n->origin.y = y;
		n->origin.x = x;
		int h1 = h * n->split_point[0];
		int w1 = w * n->split_point[1];
		int have_title = h1 > 0 && w1 > 0;
		int have_div = h1 > 0 && w1 > 0 && n->c[1];

		assert(n->split_point[0] >= 0.0);
		assert(n->split_point[0] <= 1.0);
		assert(n->split_point[1] >= 0.0);
		assert(n->split_point[1] <= 1.0);
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
		n->extent.y = h1 - have_title;
		n->extent.x = w1;
		if( p ) {
			bool changed = n->extent.y > p->ws.ws_row;
			if( p->fd >= 0 && changed ) {
				set_height(n);
			}
			if( n->extent.x > p->ws.ws_col ) {
				int d = n->extent.x - p->ws.ws_col;
				resize_pad(&n->bkg, n->extent.y, d);
				wbkgd(n->bkg, ACS_CKBOARD);
				pnoutrefresh(n->bkg, 0, 0,
					n->origin.y,
					n->origin.x + p->ws.ws_col,
					n->origin.y + n->extent.y - 1,
					n->origin.x + n->extent.x - 1
				);
			}
			scrollbottom(n);
		} else if( w1 && h1 > 1 ) {
			resize_pad(&n->bkg, n->extent.y, n->extent.x);
		}
	}
}

static void
prune(const char *arg)
{
	struct canvas *x = S.f;
	struct canvas *p = x->parent;
	int d = x->typ;
	struct canvas *n = x->c[d];
	(void)arg;

	if( n ) {
		n->parent = p;
		n->origin = x->origin;
		*(p ? &p->c[d] : &S.c) = n;
	} else if( p ) {
		p->split_point[d] = 1.0;
		p->c[d] = NULL;
	} else {
		S.c = NULL;
	}
	if( x == S.f ) {
		focus(n ? n : p);
	}
	if( S.v == x ) {
		S.v = n ? n : p;
	}
	for( ; x; x = n ) {
		n = x->c[!d];
		freecanvas(x);
	}
	S.reshape = 1;
}

static void
draw_pane(WINDOW *w, int y, int x)
{
	int wy, wx;
	getmaxyx(w, wy, wx);
	pnoutrefresh(w, 0, 0, y, x, y + wy - 1, x + wx - 1);
}

static void
draw_title(struct canvas *n, int r)
{
	if( n->wtit ) {
		assert( n->p != NULL );
		wattrset(n->wtit, r ? A_REVERSE : A_NORMAL);
		mvwprintw(n->wtit, 0, 0, "%d %s %d-%d/%d",
			n->p->id,
			n->p->status,
			n->offset.x + 1,
			n->offset.x + n->extent.x,
			n->p->ws.ws_col
		);
		whline(n->wtit, ACS_HLINE, n->extent.x);
		struct point o = n->origin;
		draw_pane(n->wtit, o.y + n->extent.y, o.x);
	}
}

static void
draw_div(struct canvas *n, int rev)
{
	if( n->wdiv ) {
		( rev ? &wattron : &wattroff )(n->wdiv, A_REVERSE);
		mvwvline(n->wdiv, 0, 0, ACS_VLINE, INT_MAX);
		draw_pane(n->wdiv, n->origin.y, n->origin.x + n->extent.x);
	}
}

void
draw(struct canvas *n) /* Draw a canvas. */
{
	if( n != NULL && n->extent.y > 0 ) {
		int rev = S.binding == &cmd_keys && n == S.f;
		draw(n->c[0]);
		draw(n->c[1]);
		draw_div(n, rev && !n->extent.x);
		draw_window(n);
		draw_title(n, rev);
	}
}

void
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
}

static void
wait_child(struct pty *p)
{
	int status, k = 0;
	const char *fmt = "exited %d";
	switch( waitpid(p->pid, &status, WNOHANG) ) {
	case -1: err_check(1, "waitpid %d", p->pid);
	case 0: break;
	default:
		if( WIFEXITED(status) ) {
			k = WEXITSTATUS(status);
		} else {
			assert( WIFSIGNALED(status) );
			fmt = "caught signal %d";
			k = WTERMSIG(status);
		}
		FD_CLR(p->fd, &S.fds);
		err_check(close(p->fd) ,"close fd %d", p->fd);
		p->fd = -1;
		struct pty *t, *prev = NULL;
		for( t = S.p; t; prev = t, t = t->next ) {
			if( t == p ) {
				*(prev ? &prev->next : &S.p) = p->next;
			}
		}
		*(prev ? &prev->next : &S.p) = p;
		p->next = NULL;
		snprintf(p->status, sizeof p->status, fmt, k);
		p->id = p->pid;
		p->pid = -1;
		S.reshape = 1;
	}
}

static void
getinput(fd_set *f) /* check all pty's for input. */
{
	for( struct pty *t = S.p; t; t = t->next ) {
		if( t->fd > 0 && FD_ISSET(t->fd, f) ) {
			char iobuf[BUFSIZ];
			ssize_t r = read(t->fd, iobuf, sizeof iobuf);
			if( r > 0 ) {
				vtwrite(&t->vp, iobuf, r);
			} else if( errno != EINTR && errno != EWOULDBLOCK ) {
				wait_child(t);
			}
		}
	}
}

static void
digit(const char *arg)
{
	S.count = 10 * (S.count == -1 ? 0 : S.count) + *arg - '0';
}

static void
set_view_count(const char *arg)
{
	(void)arg;
	struct canvas *n = S.f;
	S.v = n;
	switch( S.count ) {
	case 0:
		S.v = S.c;
		break;
	}
	S.reshape = 1;
}

static void
sendarrow(const char *k)
{
	struct canvas *n = S.f;
	char buf[3] = { '\033', n->p->pnm ? 'O' : '[', *k };
	rewrite(n->p->fd, buf, 3);
}

struct canvas *
find_window(struct canvas *n, int y, int x)
{
	struct canvas *r = n;
	if( n && ( y < n->origin.y || y > n->origin.y + n->extent.y
			|| x < n->origin.x || x > n->origin.x + n->extent.x) ) {
		if( (r = find_window(n->c[0], y, x)) == NULL ) {
			r = find_window(n->c[1], y, x);
		}
	}
	return r;
}

enum direction {nil, up, down, left, right};

static void
navigate(enum direction dir, int count, int type)
{
	struct canvas *n = S.f;
	struct canvas *t = S.f;
	struct canvas *q = NULL;
	int startx = t->origin.x;
	int starty = t->origin.y + t->extent.y;
	while( t && count-- && dir != nil ) {
		struct point target = {starty, startx};
		switch( dir ) {
		case up:
			target.y = t->origin.y - 1;
			q = t->parent;
			break;
		case down:
			target.y = t->origin.y + t->extent.y + 1;
			q = t->c[0];
			break;
		case right:
			target.x = t->origin.x + t->extent.x + 1;
			q = t->c[1];
			break;
		case left:
			target.x = t->origin.x - 1;
			q = t->parent;
			break;
		case nil:
			;
		}
		t = type ? q : find_window(S.v, target.y, target.x);
		n = t ? t : n;
	}
	focus(n);
}

void
mov(const char *arg)
{
	struct canvas *n = S.f;
	assert( n == S.f && n != NULL );
	int count = S.count < 1 ? 1 : S.count;
	enum direction dir = *arg == 'k' ? up : *arg == 'j' ? down :
			*arg == 'h' ? left : *arg == 'l' ? right : nil;
	navigate(dir, count, 0);
}

static void
add_key(struct handler *b, wchar_t k, action act, const char *arg)
{
	if( b == code_keys ) {
		assert( k >= KEY_MIN && k <= KEY_MAX );
		k -= KEY_MIN;
	}
	b[k].act = act;
	b[k].arg = arg;
}

static size_t describe_layout(char *d, ptrdiff_t siz, const struct canvas *c,
	unsigned flags);
static void
show_layout(const char *arg)
{
	char buf[1024] = "layout: ";
	int flag = S.count == -1 ? 1 : S.count;
	struct canvas *c = flag & 0x20 ? S.f : S.c;
	(void)arg;
	size_t s = describe_layout(buf + 8, sizeof buf - 8, c, flag);
	buf[8 + s] = ':';
	rewrite(1, buf, s + 9);
}

static size_t describe_row(char *desc, size_t siz, int row);
static void
show_row(const char *arg)
{
	(void)arg;
	int row = S.count == -1 ? 1 : S.count;
	char buf[1024];
	int k = sprintf(buf, "row %d: ", row);
	size_t s = describe_row(buf + k, sizeof buf - k, row);
	rewrite(1, buf, s + k);
}


void
build_bindings()
{
	assert( KEY_MAX - KEY_MIN < 2048 ); /* Avoid overly large luts */

	add_key(keys, S.commandkey, transition, NULL);
	add_key(keys, L'\r', send, "\r");
	add_key(keys, L'\n', send, "\n");

	add_key(cmd_keys, CTL('e'), show_layout, NULL);
	add_key(cmd_keys, CTL('f'), show_row, NULL);

	add_key(cmd_keys, S.commandkey, transition, &S.commandkey);
	add_key(cmd_keys, L'\r', transition, NULL);
	add_key(cmd_keys, L'\n', transition, NULL);
	/* TODO: rebind b,f,<,> to hjkl in different binding */
	add_key(cmd_keys, L'a', attach, NULL);
	add_key(cmd_keys, L'b', scrolln, "-");
	add_key(cmd_keys, L'f', scrolln, "+");
	/* If default bindings for scrollh are changed, edit README.rst */
	add_key(cmd_keys, L'>', scrollh, ">");
	add_key(cmd_keys, L'<', scrollh, "<");

	add_key(cmd_keys, L'=', equalize, NULL);
	add_key(cmd_keys, L'c', create, NULL);
	add_key(cmd_keys, L'C', create, "C");
	add_key(cmd_keys, L'j', mov, "j");
	add_key(cmd_keys, L'k', mov, "k");
	add_key(cmd_keys, L'l', mov, "l");
	add_key(cmd_keys, L'h', mov, "h");
	add_key(cmd_keys, L'J', resize, "J");
	add_key(cmd_keys, L'K', resize, "K");
	add_key(cmd_keys, L'L', resize, "L");
	add_key(cmd_keys, L'H', resize, "H");
	add_key(cmd_keys, L'q', quit, NULL);
	add_key(cmd_keys, L's', swap, NULL);
	add_key(cmd_keys, L't', new_tabstop, NULL);
	add_key(cmd_keys, L'W', set_width, NULL);
	add_key(cmd_keys, L'v', set_view_count, NULL);
	add_key(cmd_keys, L'x', prune, NULL);
	add_key(cmd_keys, L'0', digit, "0");
	add_key(cmd_keys, L'1', digit, "1");
	add_key(cmd_keys, L'2', digit, "2");
	add_key(cmd_keys, L'3', digit, "3");
	add_key(cmd_keys, L'4', digit, "4");
	add_key(cmd_keys, L'5', digit, "5");
	add_key(cmd_keys, L'6', digit, "6");
	add_key(cmd_keys, L'7', digit, "7");
	add_key(cmd_keys, L'8', digit, "8");
	add_key(cmd_keys, L'9', digit, "9");
	add_key(code_keys, KEY_RESIZE, reshape_root, NULL);
	add_key(code_keys, KEY_F(1), send, "\033OP");
	add_key(code_keys, KEY_F(2), send, "\033OQ");
	add_key(code_keys, KEY_F(3), send, "\033OR");
	add_key(code_keys, KEY_F(4), send, "\033OS");
	add_key(code_keys, KEY_F(5), send, "\033[15~");
	add_key(code_keys, KEY_F(6), send, "\033[17~");
	add_key(code_keys, KEY_F(7), send, "\033[18~");
	add_key(code_keys, KEY_F(8), send, "\033[19~");
	add_key(code_keys, KEY_F(9), send, "\033[20~");
	add_key(code_keys, KEY_F(10), send, "\033[21~");
	add_key(code_keys, KEY_F(11), send, "\033[23~");
	add_key(code_keys, KEY_F(12), send, "\033[24~");
	add_key(code_keys, KEY_HOME, send, "\033[1~");
	add_key(code_keys, KEY_END, send, "\033[4~");
	add_key(code_keys, KEY_PPAGE, send, "\033[5~");
	add_key(code_keys, KEY_NPAGE, send, "\033[6~");
	add_key(code_keys, KEY_BACKSPACE, send, "\177");
	add_key(code_keys, KEY_DC, send, "\033[3~");
	add_key(code_keys, KEY_IC, send, "\033[2~");
	add_key(code_keys, KEY_BTAB, send, "\033[Z");
	add_key(code_keys, KEY_ENTER, send, "\r");
	add_key(code_keys, KEY_UP, sendarrow, "A");
	add_key(code_keys, KEY_DOWN, sendarrow, "B");
	add_key(code_keys, KEY_RIGHT, sendarrow, "C");
	add_key(code_keys, KEY_LEFT, sendarrow, "D");
}

static void
handlechar(int r, int k) /* Handle a single input character. */
{
	struct handler *b = NULL;
	struct canvas *n = S.f;

	if( r == OK && k > 0 && k < (int)sizeof *S.binding ) {
		b = &(*S.binding)[k];
	} else if( r == KEY_CODE_YES && k >= KEY_MIN && k <= KEY_MAX ) {
		b = &code_keys[k - KEY_MIN];
	}
	if( b && b->act ) {
		b->act(b->arg);
		if( b->act != digit ) {
			S.count = -1;
		}
	} else if( S.mode == passthru && n->p && n->p->fd > 0 ) {
		char c[MB_LEN_MAX + 1];
		if( ( r = wctomb(c, k)) > 0 ) {
			scrollbottom(n);
			rewrite(n->p->fd, c, r);
		}
	}
}

static void handle_term(int s) { (void) s; exit(0); }

static void
main_loop(void)
{
	while( S.c != NULL && S.p && S.p->fd > 0 ) {
		int r;
		wint_t w = 0;
		fd_set sfds = S.fds;

		if( S.reshape ) {
			reshape_root(NULL);
		}
		draw(S.v);
		if( winpos(S.werr, 1) ) {
			int y, x;
			getmaxyx(stdscr, y, x);
			pnoutrefresh(S.werr, 0, 0, y - 1, 0, y - 1, x);
		}
		fixcursor();
		doupdate();
		if( select(S.p->fd + 1, &sfds, NULL, NULL, NULL) < 0 ) {
			err_check(errno != EINTR , "select");
			FD_ZERO(&sfds);
		}
		while( (r = get_wch(&w)) != ERR ) {
			handlechar(r, w);
		}
		getinput(&sfds);
	}
}

static void
parse_args(int argc, char *const*argv)
{
	int c;
	char *name = strrchr(argv[0], '/');
	while( (c = getopt(argc, argv, ":c:hs:t:vw:")) != -1 ) {
		switch( c ) {
		case 'h':
			printf("usage: %s"
				" [-c ctrl-key]"
				" [-s history-size]"
				" [-t terminal-type]"
				" [-v]"
				" [-w width]"
				"\n",
				name ? name + 1 : argv[0]);
			exit(0);
		case 'c':
			S.commandkey = CTL(optarg[0]);
			break;
		case 's':
			S.history = strtol(optarg, NULL, 10);
			break;
		case 't':
			term = optarg;
			break;
		case 'v':
			printf("%s-%s\n", PACKAGE_NAME, PACKAGE_VERSION);
			exit(EXIT_SUCCESS);
		case 'w':
			S.width = strtol(optarg, NULL, 10);
			break;
		default:
			errno = 0;
			err_check(1, "Unknown option: %c", optopt);
			exit(EXIT_FAILURE);
		}
	}
}

static void endwin_wrap(void) { (void) endwin(); }

static void
init(void)
{
	char buf[16];
	struct sigaction sa;
	int y, x;
	SCREEN *new;
	memset(&sa, 0, sizeof sa);
	sa.sa_flags = 0;
	sigemptyset(&sa.sa_mask);
	sa.sa_handler = handle_term;
	sigaction(SIGTERM, &sa, NULL);
	FD_ZERO(&S.fds);
	FD_SET(STDIN_FILENO, &S.fds);
	snprintf(buf, sizeof buf - 1, "%d", getpid());
	setenv("SMTX", buf, 1);
	setenv("SMTX_VERSION", VERSION, 1);
	setlocale(LC_ALL, "");
	build_bindings();
	if( (new = newterm(term, stdin, stdout)) == NULL ) {
		initscr();
	} else {
		set_term(new);
		setenv("TERM", term, 1);
	}
	getmaxyx(stdscr, y, x);
	S.history = MAX(y, S.history);
	atexit(endwin_wrap);
	raw();
	noecho();
	nonl();
	timeout(0);
	intrflush(NULL, FALSE);
	start_color();
	use_default_colors();
	resize_pad(&S.werr, 1, x);
	err_check(S.werr == NULL, "Unable to create error window");
	wattron(S.werr, A_REVERSE);
	create(NULL);
	err_check( ( S.f = S.v = S.c ) == NULL, "Unable to create root window");
}

int
smtx_main(int argc, char *argv[])
{
	parse_args(argc, argv);
	init();
	main_loop();
	return EXIT_SUCCESS;
}

/* Descriptive functions used by test suite */

/* Describe a layout. This is called in a signal handler by the tests. */
static size_t
describe_layout(char *d, ptrdiff_t siz, const struct canvas *c, unsigned flags)
{
	const char * const e = d + siz;
	int recurse = flags & 0x1;
	int show_id = flags & 0x4;
	int show_pid = flags & 0x8;
	int show_pos = flags & 0x10;

	char *isfocus = recurse && c == S.f ? "*" : "";
	d += snprintf(d, e - d, "%s%dx%d", isfocus, c->extent.y, c->extent.x);

	if( show_pos) {
		d += snprintf(d, e - d, "@%d,%d", c->origin.y, c->origin.x);
	}
	if( show_pid && c->p ) {
		d += snprintf(d, e - d, "(pid=%d)", c->p->pid);
	}
	if( show_id && c->p ) {
		d += snprintf(d, e - d, "(id=%d)", c->p->id);
	}
	if( c->p->s ) {
		if( ! c->p->s->vis ) {
			d += snprintf(d, e - d, "!"); /* Cursor hidden */
		}
	}
	if( ! c->p->pnm ) {
		d += snprintf(d, e - d, "#"); /* Numeric keypad  */
	}
	for( int i = 0; i < 2; i ++ ) {
		if( recurse && e - d > 3 && c->c[i] ) {
			*d++ = ';';
			*d++ = ' ';
			d += describe_layout(d, e - d, c->c[i], flags);
		}
	}
	return siz - ( e - d );
}

size_t
describe_state(char *desc, size_t siz)
{
	size_t len = 0;
	int y, x;

	getmaxyx(stdscr, y, x);
	len += snprintf(desc, siz, "history=%d, ", S.history);
	len += snprintf(desc + len, siz - len, "y=%d, x=%d, ", y, x);
	len += snprintf(desc + len, siz - len, "w=%d", S.width);
	if( len < siz ) {
		desc[len++] = '\n';
		desc[len] = '\0';
	}
	return len;
}

static size_t
describe_row(char *desc, size_t siz, int row)
{
	size_t len = 0;
	int y, x, mx;

	const struct canvas *c = S.c;
	assert( c->offset.x >= 0 );

	if( row < c->extent.y ) {
		WINDOW *w = c->p->s->win;
		getmaxyx(w, y, mx);
		mx = MIN3(mx, c->extent.x + c->offset.x, (int)siz - 1);
		mx -= c->offset.x;
		getyx(w, y, x);
		len = mx;
		row += c->offset.y;
		for( char *d = desc + len; mx >= 0; mx-- ) {
			*d-- = mvwinch(w, row, mx + c->offset.x) & A_CHARTEXT;
		}
		wmove(w, y, x);
	} else if( row == c->extent.y ) {
		WINDOW *w = c->wtit;
		len = c->extent.x;
		for( x = 0; x < c->extent.x; x++ ) {
			desc[x] = mvwinch(w, 0, x) & A_CHARTEXT;
		}
	}
	return len;
}
