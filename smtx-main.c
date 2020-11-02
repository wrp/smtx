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

struct state S = {
	.ctlkey = CTL('g'),
	.term = "smtx",
	.width = 80,
	.binding = k1,
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
	if( p->ntabs < w && (n = realloc(p->tabs, w * sizeof *n)) != NULL ) {
		for( p->tabs = n; p->ntabs < w; p->ntabs++ ) {
			p->tabs[p->ntabs] = tabstop && p->ntabs % tabstop == 0;
		}
	}
}

int
resize_pad(WINDOW **p, int h, int w)
{
	int rv = 0;
	if( *p ) {
		rv = check(wresize(*p, h, w ) == OK, "Out of memory");
	} else if( check((*p = newpad(h, w)) != NULL, "Out of memory") ) {
		wtimeout(*p, 0);
		scrollok(*p, 1);
		rv = (keypad(*p, 1) == OK);
	}
	return rv; /* 0 is failure */
}

static struct pty *
get_freepty(void)
{
	struct pty *t = S.p;
	while( t && t->count && t->fd != -1 ) {
		t = t->next;
	}
	return t;
}

static struct pty *
new_pty(int cols)
{
	struct pty *p = get_freepty();
	if( ! check((p = p ? p : calloc(1, sizeof *p)) != NULL, "calloc") ) {
		return NULL;
	}
	if( p->s == NULL ) {
		if( resize_pad(&p->pri.win, S.history, cols)
			&& resize_pad(&p->alt.win, S.history, cols)
		) {
			p->next = S.p;
			S.p = p;
		} else {
			delwin(p->pri.win);
			delwin(p->alt.win);
			free(p);
			return NULL;
		}
	}
	if( p->fd < 1 ) {
		const char *sh = getshell();
		p->ws.ws_col = cols;
		p->pid = forkpty(&p->fd, p->secondary, NULL, &p->ws);
		if( check(p->pid != -1, "forkpty") && p->pid == 0 ) {
			setsid();
			signal(SIGCHLD, SIG_DFL);
			execl(sh, sh, NULL);
			err(EXIT_FAILURE, "exec SHELL='%s'", sh);
		}
		p->vp.p = p;
		extend_tabs(p, p->tabstop = 8);
		FD_SET(p->fd, &S.fds);
		S.maxfd = p->fd > S.maxfd ? p->fd : S.maxfd;
		fcntl(p->fd, F_SETFL, O_NONBLOCK);
		const char *bname = strrchr(sh, '/');
		bname = bname ? bname + 1 : sh;
		strncpy(p->status, bname, sizeof p->status - 1);
	}
	if( p->s == NULL ) {
		p->s = &p->pri;
		setupevents(&p->vp);
	}
	return p;
}

struct canvas *
newcanvas(struct pty *p, struct canvas *parent)
{
	struct canvas *n = NULL;
	if( (p = p ? p : new_pty(MAX(COLS, S.width))) != NULL ) {
		if( (n = S.unused) != NULL ) {
			S.unused = n->c[0];
		} else {
			check((n = calloc(1 , sizeof *n)) != NULL, "calloc");
		}
		if( n ) {
			n->c[0] = n->c[1] = NULL;
			n->manualscroll = n->offset.y = n->offset.x = 0;
			n->p = p;
			n->parent = parent;
			p->count += 1;
			n->split.y = 1.0;
			n->split.x = 1.0;
		}
	}
	return n;
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
	int show = S.binding == k1 && f->p->s->vis;
	if( f->p && f->p->s && f->extent.y ) {
		struct screen *s = f->p->s;
		int y, x;
		getmaxyx(s->win, y, x);
		assert( y > f->extent.y );
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
	check(ioctl(p->fd, TIOCSWINSZ, &p->ws) == 0, "ioctl on %d", p->fd);
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
		int have_div = h1 > 0 && w1 > 0 && n->c[1];

		assert(n->split.y >= 0.0);
		assert(n->split.y <= 1.0);
		assert(n->split.x >= 0.0);
		assert(n->split.x <= 1.0);
		resize_pad(&n->wdiv, n->typ ? h : h1, 1);
		resize_pad(&n->wtit, 1, w1);
		reshape(n->c[0], y + h1, x, h - h1, n->typ ? w1 : w);
		reshape(n->c[1], y, x + w1 + have_div,
			n->typ ? h : h1, w - w1 - have_div);
		n->extent.y = h1 > 0 ?  h1 - 1 : 0;
		n->extent.x = w1;
		if( p ) {
			if( p->fd >= 0 && n->extent.y > p->ws.ws_row ) {
				set_height(n);
			}
			scrollbottom(n);
		}
	}
	S.reshape = 0;
}

void
freecanvas(struct canvas * n, int count)
{
	assert( count < 2 && count > -2 );
	if( n ) {
		n->p->count += count ? count : -1;
		freecanvas(n->c[0], count);
		freecanvas(n->c[1], count);
		if( ! count ) {
			n->c[0] = S.unused;
			n->c[1] = NULL;
			S.unused = n;
		}
	}
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
	mvwprintw(n->wtit, 0, 0, "%d %s ", n->p->fd - 2, n->p->status);
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
	( rev ? &wattron : &wattroff )(n->wdiv, A_REVERSE);
	mvwvline(n->wdiv, 0, 0, ACS_VLINE, INT_MAX);
	draw_pane(n->wdiv, n->origin.y, n->origin.x + n->extent.x);
}

void
draw(struct canvas *n) /* Draw a canvas. */
{
	if( n != NULL && n->extent.y > 0 ) {
		int rev = S.binding == ctl && n == S.f;
		draw(n->c[0]);
		if( n->c[1] ) {
			draw_div(n, rev && !n->extent.x);
			draw(n->c[1]);
		}
		draw_window(n);
		draw_title(n, rev);
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
				b = S.binding + w;
			} else if( r == KEY_CODE_YES ) {
				assert( w >= KEY_MIN && w <= KEY_MAX );
				b = &code_keys[w - KEY_MIN];
			}
			if( b ) {
				b->arg ? b->act.a(b->arg) : b->act.v();
				if( b->act.a != digit ) {
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

void
sendarrow(const char *k)
{
	struct canvas *n = S.f;
	char buf[3] = { '\033', n->p->pnm ? 'O' : '[', *k };
	rewrite(n->p->fd, buf, 3);
}

/*
 * wc_lut is built from the output of wctomb, so that for k such that
 * k % (1 + MB_LEN_MAX) == 0, wc_lut[k] is the value returned by
 * wctomb( ..., k) and wc_lut[k+1 ...] is the resultant multi-byte value.
 */
static char wc_lut[(KEY_MAX - KEY_MIN + 1) * ( 1 + MB_LEN_MAX )];

void
build_bindings(void)
{

	k1[S.ctlkey] = (struct handler){ { .a = transition }, "control" };
	ctl[S.ctlkey] = (struct handler){ { .a = transition }, "*enter" };

	for( wchar_t k = KEY_MIN; k < KEY_MAX; k++ ) {
		int idx = k - KEY_MIN;
		if( code_keys[idx].arg || code_keys[idx].act.v ) {
			continue;
		}
		assert( MB_LEN_MAX < 128 );
		int i = idx * (1 + MB_LEN_MAX);
		int v = wctomb(wc_lut + i + 1, k);
		assert( v < 128 && v > -2 );
		wc_lut[ i ] = v == -1 ? 0 : v;
		code_keys[idx] = (struct handler){ { .a = passthru }, wc_lut + i };
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
			reshape(S.c, 0, 0, LINES, COLS);
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
init(void)
{
	struct sigaction sa;
	SCREEN *new;
	memset(&sa, 0, sizeof sa);
	sa.sa_flags = 0;
	sigemptyset(&sa.sa_mask);
	sa.sa_handler = handle_term;
	sigaction(SIGTERM, &sa, NULL);
	FD_ZERO(&S.fds);
	FD_SET(STDIN_FILENO, &S.fds);
	/* Setting locale like this is absolutely the wrong thing to do.
	 * TODO: figure out the right thing to do.
	 */
	setlocale(LC_ALL, "en_US.UTF-8");

	build_bindings();
	if( (new = newterm(S.term, stdin, stdout)) == NULL ) {
		initscr();
	} else {
		set_term(new);
		setenv("TERM", S.term, 1);
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
	S.f = S.c = newcanvas(0, NULL);
	if( S.c == NULL || S.werr == NULL ) {
		endwin();
		err(EXIT_FAILURE, "Unable to create root window");
	}
	reshape_root();
}

int
smtx_main()
{
	init();
	main_loop();
	endwin();
	if( interrupted ) {
		fputs("terminated", stderr);
	}
	return EXIT_SUCCESS;
}

/* Parse a string to build a layout. The layout is expected
 * to consist of coordinates of the lower right corner of each
 * window as a percentage of the full screen.  eg a layout of 12x40:

+--------------------------------------+
|                  |      |      |     |
|                  |      |      |     |
|                  |------b      |     |
|                  |      |      |     |
|                  |      |      |     |
-------------------a------c------d-----e
|         |                            |
|         |                            |
|         |                            |
|         |                            |
+---------f----------------------------g
would be described by: ".5:.5 .25:.66 .5:.66 .5:.83 .5:1 1:.25 1:1"
Note that order matters.
 */
static struct canvas *
add_canvas(const char **lp, double oy, double ox, double ey, double ex,
	struct pty **pp, struct canvas *parent)
{
	double y, x;
	int e;
	const char *layout = *lp;
	struct pty *p = *pp;
	struct canvas *n = newcanvas(p, parent);
	if( n == NULL || ! check(
			2 == sscanf(layout, "%lf:%lf%n", &y, &x, &e)
			&& y >= oy && y <= ey && x >= ox && x <= ex,
			"Invalid layout format: %s", layout) ) {
		goto fail;
	}
	layout += e;
	*lp = layout;
	p = *pp = p ? p->next : NULL;
	n->split.y = (y - oy) / (ey - oy);
	n->split.x = (x - ox) / (ex - ox);
	if( y == ey && x == ex ) {
		return n;
	} else if( y == ey ) {
		assert( x < ex );
		n->typ = 1;
	} else if( x == ex ) {
		assert( y < ey );
		n->typ = 0;
	} else {
		double ny, nx;
		assert( y < ey && x < ex );
		if( 2 != sscanf(layout, "%lf:%lf", &ny, &nx)
			|| (ny > y && nx > x )
		) {
			check(0, "Invalid format: %s", layout);
			goto fail;
		}
		n->typ = y < ny;
		if( (n->c[!n->typ] = add_canvas(&layout,
				n->typ ? y : oy, n->typ ? ox : x,
				n->typ ? ey : y, n->typ ? x : ex,
				&p, n)) == NULL ) {
			goto fail;
		}
		*lp = layout;
	}
	if( (n->c[n->typ] = add_canvas(&layout,
			n->typ ? oy : y, n->typ ? x : ox,
			ey, ex, &p, n)) == NULL ) {
		goto fail;
	}
	*lp = layout;

	return n;
fail:
	freecanvas(n, 0);
	return NULL;
}

void
build_layout(const char *layout)
{
	struct pty *p = S.f->p;
	freecanvas(S.c, -1); /* (1) */
	struct canvas *n = add_canvas(&layout, 0.0, 0.0, 1.0, 1.0, &p, NULL);
	freecanvas(S.c, +1);
	if( n ) {
		freecanvas(S.c, 0);
		S.f = S.c = n;
	}
	S.reshape = 1;
}
/* (1) Decrement the view counts so that visible ptys will be availalbe for
 * the new layout.
 */
