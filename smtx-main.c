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
static struct handler code_keys[KEY_MAX - KEY_MIN + 1];

struct state S = {
	.ctlkey = CTL('g'),
	.width = 80,
	.mode = enter,
	.history = 1024,
	.count = -1,
};

int
check(int rv, const char *fmt, ...)
{
	if( !rv ) {
		int e = errno;
		va_list ap;
		va_start(ap, fmt);
		if( S.werr ) {
			size_t len = sizeof S.errmsg;
			int n = vsnprintf(S.errmsg, len, fmt, ap);
			if( e && n + 3 < (int)len ) {
				strncat(S.errmsg, ": ", len - n);
				strncat(S.errmsg, strerror(e), len - n - 2);
			}
		} else {
			errno = e;
			verrx(!rv, fmt, ap);
		}
		va_end(ap);
		errno = e;
	}
	return !!rv;
}

void
rewrite(int fd, const char *b, size_t n)
{
	const char *e = b + n;
	ssize_t s;
	if( n > 0 ) do {
		s = write(fd, b, e - b);
		b += s < 0 ? 0 : s;
	} while( b < e && check(s >= 0 || errno == EINTR, "write fd %d", fd) );
}

static const char *
getshell(void)
{
	const char *s = getenv("SHELL");
	struct passwd *pwd = s ? NULL : getpwuid(geteuid());
	return s ? s : pwd ? pwd->pw_shell : "/bin/sh";
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

void
free_proc(struct pty *p)
{
	assert( p != NULL );

	if( --p->count == 0 ) {
		struct pty *t = S.p, *prev = NULL;
		while( t && t != p ) {
			prev = t;
			t = t->next;
		}
		*(prev ? &prev->next : &S.p) = p->next;
		p->next = S.free.p;
		S.free.p = p;
	}
}

int
resize_pad(WINDOW **p, int h, int w)
{
	int rv = -1;
	if( *p ) {
		check(wresize(*p, h, w ) == OK, "Error creating window");
	} else if( (*p = newpad(h, w)) == NULL ) {
		errno = ENOMEM;
		check(0, "Error resizing window");
	} else {
		wtimeout(*p, 0);
		scrollok(*p, TRUE);
		keypad(*p, TRUE);
		rv = 0;
	}
	return rv;
}

static struct pty *
new_pty(int rows, int cols)
{
	struct pty *p = S.free.p ? S.free.p : calloc(1, sizeof *p);
	if( ! check(p != NULL, "calloc") ) {
		return NULL;
	}
	if( S.free.p ) {
		S.free.p = p->next;
	}
	const char *sh = getshell();
	if( p->fd < 1 ) {
		p->ws.ws_row = rows - 1;
		p->ws.ws_col = cols;
		p->pid = forkpty(&p->fd, p->secondary, NULL, &p->ws);
		if( check(p->pid != -1, "forkpty")) switch(p->pid) {
		case 0:
			setsid();
			signal(SIGCHLD, SIG_DFL);
			execl(sh, sh, NULL);
			err(EXIT_FAILURE, "exec SHELL='%s'", sh);
		default:
			if( ! p->pri.win ) {
				resize_pad(&p->pri.win, S.history, cols);
			}
			if( ! p->alt.win ) {
				resize_pad(&p->alt.win, S.history, cols);
			}
		}
	}
	if( p->pri.win && p->alt.win ) {
		p->s = &p->pri;
		p->vp.p = p;
		extend_tabs(p, p->tabstop = 8);
		setupevents(&p->vp);

		FD_SET(p->fd, &S.fds);
		S.maxfd = p->fd > S.maxfd ? p->fd : S.maxfd;
		fcntl(p->fd, F_SETFL, O_NONBLOCK);
		p->id = p->fd - 2;
		p->next = S.p;
		S.p = p;
		const char *bname = strrchr(sh, '/');
		bname = bname ? bname + 1 : sh;
		strncpy(p->status, bname, sizeof p->status - 1);
	} else {
		free(p->tabs);
		delwin(p->pri.win);
		delwin(p->alt.win);
		free(p);
		p = NULL;
	}
	return p;
}

struct canvas *
newcanvas(struct pty *p)
{
	struct canvas *n = NULL;
	p = p ? p : new_pty(LINES, MAX(COLS, S.width));
	if( p != NULL ) {
		if( (n = S.free.c) != NULL ) {
			S.free.c = n->c[0];
		} else {
			check((n = calloc(1 , sizeof *n)) != NULL, "calloc");
		}
		if( n ) {
			n->c[0] = n->c[1] = NULL;
			n->manualscroll = n->offset.y = n->offset.x = 0;
			n->p = p;
			p->count += 1;
			n->split.y = 1.0;
			n->split.x = 1.0;
		}
	}
	return n;
}

void
focus(struct canvas *n)
{
	S.f = n ? n : S.c;
}

static void
draw_window(struct canvas *n)
{
	struct point o = n->origin;
	struct point e = { o.y + n->extent.y - 1, o.x + n->extent.x - 1 };
	if( n->p && e.y > 0 && e.x > 0 ) {
		struct point off = n->offset;
		if( ! n->manualscroll ) {
			int x, y;
			getyx(n->p->s->win, y, x);
			(void)y;
			if( x < n->extent.x - 1 ) {
				n->offset.x = 0;
			} else if( n->offset.x + n->extent.x < x + 1 ) {
				n->offset.x = x - n->extent.x + 1;
			}
		}
		pnoutrefresh(n->p->s->win, off.y, off.x, o.y, o.x, e.y, e.x);
		if( n->p->ws.ws_col < n->extent.x ) {
			assert( n->offset.x == 0 );
			pnoutrefresh(S.wbkg, 0, 0, o.y, o.x + n->p->ws.ws_col,
				e.y, e.x);
		}

	}
}

void
fixcursor(void) /* Move the terminal cursor to the active window. */
{
	struct canvas *f = S.f;
	int show = S.mode == enter && f->p->s->vis;
	if( f->p && f->p->s && f->extent.y ) {
		struct screen *s = f->p->s;
		int y, x;
		getmaxyx(s->win, y, x);
		int top = y - f->extent.y;
		getyx(s->win, y, x);
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
			wmove(s->win, s->c.y = y, s->c.x = x);
		}
		draw_window(f);
	}
	curs_set(show);
}
/* (1) Checking f->offset.y < top really does not make any
 * sense.  This test *should* be y < f->offset.y, but
 * changing it breaks the current tests...which
 * are not reliable! TODO: figure this out (hint, the
 * problem probably stems from f->offset.y == 0 for a
 * new canvas which does not yet have any data written to it.
 * To clean this up, we just need to clean up all of the new_pty/new_canvas
 * cruft.
 */

void
reshape_window(struct pty *p)
{
	check(ioctl(p->fd, TIOCSWINSZ, &p->ws) == 0, "ioctl on %d", p->id);
	check(kill(p->pid, SIGWINCH) == 0, "send WINCH to %d", (int)p->pid);
}

static void
set_height(struct canvas *n)
{
	int y, x;
	struct pty *p = n->p;
	getmaxyx(p->pri.win, y, x);
	(void)x;

	assert( y >= n->extent.y );
	p->ws.ws_row = n->extent.y;
	wsetscrreg(p->pri.win, 0, y - 1);
	wsetscrreg(p->alt.win, 0, n->extent.y - 1);
	wrefresh(p->s->win);
	reshape_window(p);
}

void
scrollbottom(struct canvas *n)
{
	if( n && n->p && n->p->s && n->extent.y ) {
		int y, x;
		getmaxyx(n->p->s->win, y, x);
		(void)x;
		assert( y >= n->extent.y );
		n->offset.y = y - n->extent.y;
	}
}

void
reshape(struct canvas *n, int y, int x, int h, int w)
{
	if( n ) {
		struct pty *p = n->p;
		n->origin.y = y;
		n->origin.x = x;
		int h1 = h * n->split.y;
		int w1 = w * n->split.x;
		int have_title = h1 > 0 && w1 > 0;
		int have_div = h1 > 0 && w1 > 0 && n->c[1];

		assert(n->split.y >= 0.0);
		assert(n->split.y <= 1.0);
		assert(n->split.x >= 0.0);
		assert(n->split.x <= 1.0);
		if( have_div ) {
			resize_pad(&n->wdiv, n->typ ? h : h1, 1);
		}
		if( have_title ) {
			resize_pad(&n->wtit, 1, w1);
		}

		reshape(n->c[0], y + h1, x, h - h1, n->typ ? w1 : w);
		reshape(n->c[1], y, x + w1 + have_div,
			n->typ ? h : h1, w - w1 - have_div);
		n->extent.y = h1 - have_title;
		n->extent.x = w1;
		if( p ) {
			if( p->fd >= 0 && n->extent.y > p->ws.ws_row ) {
				set_height(n);
			}
			scrollbottom(n);
		}
	}
}

static void
freecanvas(struct canvas * n)
{
	if(n) {
		free_proc(n->p);
		freecanvas(n->c[0]);
		freecanvas(n->c[1]);
		n->c[0] = S.free.c;
		S.free.c = n;
	}
}

void
prune_canvas(struct canvas *f)
{
	struct canvas *parent = f->parent;
	int d = !f->c[f->typ] ? !f->typ : f->typ;
	int c = parent ? f == parent->c[1] : d;
	struct canvas *child = f->c[d];

	f->c[d] = NULL;
	if( child ) {
		child->parent = parent;
		child->origin = f->origin;
		*(parent ? &parent->c[c] : &S.c) = child;
	} else if( parent ) {
		*(c ? &parent->split.x : &parent->split.y) = 1.0;
		parent->c[c] = NULL;
	} else {
		S.c = NULL;
	}
	focus(child ? child : parent);
	freecanvas(f);
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
	assert( n->wtit );
	assert( n->p );
	wattrset(n->wtit, r ? A_REVERSE : A_NORMAL);
	mvwprintw(n->wtit, 0, 0, "%d %s ", n->p->id, n->p->status);
	int x = n->offset.x;
	int w = n->p->ws.ws_col;
	if( x > 0 || x + n->extent.x < w ) {
		wprintw(n->wtit, "%d-%d/%d ", x + 1, x + n->extent.x, w);
	}
	whline(n->wtit, ACS_HLINE, n->extent.x);
	struct point o = n->origin;
	draw_pane(n->wtit, o.y + n->extent.y, o.x);
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
		int rev = S.mode == control && n == S.f;
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
	if( n ) {
		int dir = n->typ;
		while( n->c[dir] != NULL ) {
			n = n->c[dir];
		}
		for(int count = 1; n; n = n->parent ) {
			*(dir ? &n->split.x : &n->split.y) = 1.0 / count++;
			if( n->parent && n->parent->c[dir] != n ) {
				break;
			}
			if( n->typ != dir ) {
				break;
			}
		}
	}
}

static void
wait_child(struct pty *p)
{
	int status, k = 0;
	const char *fmt = "exited %d";
	if( check(waitpid(p->pid, &status, WNOHANG) != -1, "waitpid") ) {
		if( WIFEXITED(status) ) {
			k = WEXITSTATUS(status);
		} else if( WIFSIGNALED(status) ) {
			fmt = "caught signal %d";
			k = WTERMSIG(status);
		}
		FD_CLR(p->fd, &S.fds);
		check(close(p->fd) == 0, "close fd %d", p->fd);
		snprintf(p->status, sizeof p->status, fmt, k);
		p->pid = p->fd = -1; /* (1) */
		S.reshape = 1;
	}
}
/* (1) We do not free(p) because we wish to retain error messages.
 * The windows will persist until the user explicitly destroys them.
 */

static void
getinput(void) /* check stdin and all pty's for input. */
{
	fd_set sfds = S.fds;
	if( select(S.maxfd + 1, &sfds, NULL, NULL, NULL) < 0 ) {
		check(errno == EINTR, "select");
		return;
	}
	if( FD_ISSET(STDIN_FILENO, &sfds) ) {
		int r;
		wint_t w;
		while( S.f && (r = wget_wch(S.f->p->s->win, &w)) != ERR ) {
			struct handler *b = NULL;
			if( r == OK && w > 0 && w < 128 ) {
				b = S.modes[S.mode].keys + w;
			} else if( r == KEY_CODE_YES ) {
				assert( w >= KEY_MIN && w <= KEY_MAX );
				b = &code_keys[w - KEY_MIN];
			}
			if( b ) {
				assert( b->act );
				b->act(b->arg);
				if( b->act != digit ) {
					S.count = -1;
				}
			}
		}
	}
	for( struct pty *t = S.p; t; t = t->next ) {
		if( t->fd > 0 && FD_ISSET(t->fd, &sfds) ) {
			FD_CLR(t->fd, &sfds);
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
		t = type ? q : find_window(S.c, target.y, target.x);
		n = t ? t : n;
	}
	focus(n);
}

void
mov(const char *arg)
{
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
	assert( k >= 0 && k <= KEY_MAX - KEY_MIN );
	b[k].act = act;
	b[k].arg = arg;
}

/* These lookup tables are used by passthru, which
 * receive a pointer into these tables.  The tables
 * are constructed so that passthru gets a read count in the first position,
 * and writes that many subsequent char to the underlying fd.
 * That is, key_lut[2x] will be set to 1 and key_lut[2x+1] == x
 * This way, passthru reads the 1 and writes 1 char of data.
 *
 * wc_lut is built from the output of wctomb, so that for k such that
 * k % (1 + MB_LEN_MAX) == 0, wc_lut[k] is the value returned by
 * wctomb( ..., k) and wc_lut[k+1 ...] is the resultant multi-byte value.
 */
static const char key_lut[128 * 2] = {
	1, 0x00, 1, 0x01, 1, 0x02, 1, 0x03, 1, 0x04, 1, 0x05, 1, 0x06, 1, 0x07,
	1, 0x08, 1, 0x09, 1, 0x0a, 1, 0x0b, 1, 0x0c, 1, 0x0d, 1, 0x0e, 1, 0x0f,
	1, 0x10, 1, 0x11, 1, 0x12, 1, 0x13, 1, 0x14, 1, 0x15, 1, 0x16, 1, 0x17,
	1, 0x18, 1, 0x19, 1, 0x1a, 1, 0x1b, 1, 0x1c, 1, 0x1d, 1, 0x1e, 1, 0x1f,
	1, 0x20, 1, 0x21, 1, 0x22, 1, 0x23, 1, 0x24, 1, 0x25, 1, 0x26, 1, 0x27,
	1, 0x28, 1, 0x29, 1, 0x2a, 1, 0x2b, 1, 0x2c, 1, 0x2d, 1, 0x2e, 1, 0x2f,
	1, 0x30, 1, 0x31, 1, 0x32, 1, 0x33, 1, 0x34, 1, 0x35, 1, 0x36, 1, 0x37,
	1, 0x38, 1, 0x39, 1, 0x3a, 1, 0x3b, 1, 0x3c, 1, 0x3d, 1, 0x3e, 1, 0x3f,
	1, 0x40, 1, 0x41, 1, 0x42, 1, 0x43, 1, 0x44, 1, 0x45, 1, 0x46, 1, 0x47,
	1, 0x48, 1, 0x49, 1, 0x4a, 1, 0x4b, 1, 0x4c, 1, 0x4d, 1, 0x4e, 1, 0x4f,
	1, 0x50, 1, 0x51, 1, 0x52, 1, 0x53, 1, 0x54, 1, 0x55, 1, 0x56, 1, 0x57,
	1, 0x58, 1, 0x59, 1, 0x5a, 1, 0x5b, 1, 0x5c, 1, 0x5d, 1, 0x5e, 1, 0x5f,
	1, 0x60, 1, 0x61, 1, 0x62, 1, 0x63, 1, 0x64, 1, 0x65, 1, 0x66, 1, 0x67,
	1, 0x68, 1, 0x69, 1, 0x6a, 1, 0x6b, 1, 0x6c, 1, 0x6d, 1, 0x6e, 1, 0x6f,
	1, 0x70, 1, 0x71, 1, 0x72, 1, 0x73, 1, 0x74, 1, 0x75, 1, 0x76, 1, 0x77,
	1, 0x78, 1, 0x79, 1, 0x7a, 1, 0x7b, 1, 0x7c, 1, 0x7d, 1, 0x7e, 1, 0x7f,
};
static char wc_lut[(KEY_MAX - KEY_MIN + 1) * ( 1 + MB_LEN_MAX )];

static void
initialize_mode(struct mode *m, action a)
{
	for( wchar_t k = 0; k < 128; k++ ) {
		add_key(m->keys, k, a, key_lut + 2 * k);
	}
}

void
build_bindings(void)
{
	for( wchar_t k = KEY_MIN; k < KEY_MAX; k++ ) {
		assert( MB_LEN_MAX < 128 );
		int i = (k - KEY_MIN) * (1 + MB_LEN_MAX);
		int v = wctomb(wc_lut + i + 1, k);
		assert( v < 128 && v > -2 );
		wc_lut[ i ] = v == -1 ? 0 : v;
		add_key(code_keys, k, passthru, wc_lut + i);
	}

	struct mode *m = &S.modes[enter];
	initialize_mode(m, passthru);
	add_key(m->keys, S.ctlkey, transition, "control");
	add_key(m->keys, L'\r', send, "\r");

	m = &S.modes[control];
	initialize_mode(m, bad_key);
	add_key(m->keys, S.ctlkey, transition, "*enter");
	add_key(m->keys, L'\r', transition, "enter");
	add_key(m->keys, L'\n', transition, "enter");
	/* TODO: rebind b,f,<,> to hjkl in different binding */
	add_key(m->keys, L'a', attach, NULL);
	add_key(m->keys, L'b', scrolln, "-");
	add_key(m->keys, L'f', scrolln, "+");
	/* If default bindings for scrollh are changed, edit README.rst */
	add_key(m->keys, L'>', scrollh, ">");
	add_key(m->keys, L'<', scrollh, "<");
	add_key(m->keys, L'=', equalize, NULL);
	add_key(m->keys, L'c', create, NULL);
	add_key(m->keys, L'C', create, "C");
	add_key(m->keys, L'j', mov, "j");
	add_key(m->keys, L'k', mov, "k");
	add_key(m->keys, L'l', mov, "l");
	add_key(m->keys, L'h', mov, "h");
	add_key(m->keys, L'J', resize, "J");
	add_key(m->keys, L'H', resize, "H");
	add_key(m->keys, L'q', quit, NULL);
#ifndef NDEBUG
	add_key(m->keys, L'Q', show_status, "");
#endif
	add_key(m->keys, L's', swap, NULL);
	add_key(m->keys, L't', new_tabstop, NULL);
	add_key(m->keys, L'W', set_width, NULL);
	add_key(m->keys, L'Z', set_history, NULL);
	add_key(m->keys, L'x', prune, NULL);
	add_key(m->keys, L'0', digit, "0");
	add_key(m->keys, L'1', digit, "1");
	add_key(m->keys, L'2', digit, "2");
	add_key(m->keys, L'3', digit, "3");
	add_key(m->keys, L'4', digit, "4");
	add_key(m->keys, L'5', digit, "5");
	add_key(m->keys, L'6', digit, "6");
	add_key(m->keys, L'7', digit, "7");
	add_key(m->keys, L'8', digit, "8");
	add_key(m->keys, L'9', digit, "9");

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

void
passthru(const char *arg)
{
	struct canvas *n = S.f;
	if( n->p && n->p->fd > 0 && arg[0] > 0 ) {
		scrollbottom(n);
		rewrite(n->p->fd, arg + 1, arg[0]);
	}
}

static volatile sig_atomic_t interrupted;
static void
handle_term(int s)
{
	interrupted = s;
}

static void
main_loop(void)
{
	while( S.c != NULL && ! interrupted ) {
		if( S.reshape ) {
			reshape_root(NULL);
		}
		draw(S.c);
		if( *S.errmsg ) {
			mvwprintw(S.werr, 0, 0, "%s", S.errmsg);
			wclrtoeol(S.werr);
			draw_pane(S.werr, LINES - 1, 0);
		}
		fixcursor();
		doupdate();
		getinput();
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
			S.ctlkey = CTL(optarg[0]);
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
			check(0, "Unknown option: %c", optopt);
		}
	}
}

static void
init(void)
{
	char buf[16];
	struct sigaction sa;
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
	/* Setting locale like this is absolutely the wrong thing to do.
	 * TODO: figure out the right thing to do.
	 */
	setlocale(LC_ALL, "en_US.UTF-8");

	build_bindings();
	if( (new = newterm(term, stdin, stdout)) == NULL ) {
		initscr();
	} else {
		set_term(new);
		setenv("TERM", term, 1);
	}
	S.history = MAX(LINES, S.history);
	raw();
	noecho();
	nonl();
	timeout(0);
	intrflush(NULL, FALSE);
	start_color();
	use_default_colors();
	resize_pad(&S.werr, 1, COLS);
	resize_pad(&S.wbkg, LINES, COLS);
	wbkgd(S.wbkg, ACS_BULLET);
	wattron(S.werr, A_REVERSE);
	create(NULL);
	S.f = S.c;
	if( S.c == NULL || S.werr == NULL ) {
		endwin();
		err(EXIT_FAILURE, "Unable to create root window");
	}
}

int
smtx_main(int argc, char *argv[])
{
	parse_args(argc, argv);
	init();
	main_loop();
	endwin();
	if( interrupted ) {
		fputs("terminated", stderr);
	}
	return EXIT_SUCCESS;
}
