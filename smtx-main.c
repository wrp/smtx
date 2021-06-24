/*
 * Copyright 2017 - 2019 Rob King <jking@deadpixi.com>
 * Copyright 2020 - 2021 William Pursell <william.r.pursell@gmail.com>
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
	.ctlkey = CTRL('g'),
	.rawkey = 'g',
	.term = "smtx",
	.width = 80,
	.binding = k1,
	.history = 1024,
	.count = -1,
};

static const char *
getshell(void)
{
	const char *s = getenv("SHELL");
	struct passwd *pwd = s ? NULL : getpwuid(geteuid());
	return s ? s : pwd ? pwd->pw_shell : "/bin/sh";
}

void
set_tabs(struct pty *p, int tabstop)
{
	typeof(*p->tabs) *n;
	if( (n = realloc(p->tabs, p->ws.ws_col * sizeof *n)) != NULL ){
		memset(p->tabs = n, 0, p->ws.ws_col * sizeof *n);
		for( int i = 0; i < p->ws.ws_col; i += tabstop ){
			p->tabs[i] = true;
		}
	}
}

int
resize_pad(WINDOW **p, int h, int w)
{
	int rv = 0;
	if( *p ){
		rv = wresize(*p, h, w ) == OK;
	} else if( (*p = newpad(h, w)) != NULL ){
		wtimeout(*p, 0);
		scrollok(*p, 1);
		rv = (keypad(*p, 1) == OK);
	}
	return check(rv, ENOMEM, "wresize"); /* 0 is failure */
}

static struct pty *
get_freepty(bool allow_hidden)
{
	struct pty *t = S.p;
	while( t && (!allow_hidden || t->count) && t->fd != -1 ){
		t = t->next;
	}
	return t ? t : calloc(1, sizeof *t);
}

struct pty *
new_pty(int rows, int cols, bool new)
{
	struct pty *p = get_freepty(!new);
	if( check(p != NULL, errno = 0, "calloc") ){
		if( p->s == NULL ){
			if( resize_pad(&p->scr[0].w, rows, cols)
				&& resize_pad(&p->scr[1].w, rows, cols)
			){
				p->scr[0].rows = p->scr[1].rows = rows;
				*(S.tail ? &S.tail->next : &S.p) = p;
				S.tail = p;
				set_scroll(p->scr, 0, rows - 1);
				set_scroll(&p->scr[1], 0, rows - 1);
			} else {
				delwin(p->scr[0].w);
				delwin(p->scr[1].w);
				free(p);
				return NULL;
			}
		}
		if( p->fd < 1 ){
			const char *sh = getshell();
			p->ws.ws_row = LINES - 1;
			p->ws.ws_col = cols;
			p->tos = rows - p->ws.ws_row;
			p->pid = forkpty(&p->fd, p->secondary, NULL, &p->ws);
			if( check(p->pid != -1, 0, "forkpty") && p->pid == 0 ){
				setsid();
				signal(SIGCHLD, SIG_DFL);
				setenv("TERM", S.term, 1);
				execl(sh, sh, NULL);
				err(EXIT_FAILURE, "exec SHELL='%s'", sh);
			}
			set_tabs(p, p->tabstop = 8);
			FD_SET(p->fd, &S.fds);
			S.maxfd = p->fd > S.maxfd ? p->fd : S.maxfd;
			fcntl(p->fd, F_SETFL, O_NONBLOCK);
			const char *bname = strrchr(sh, '/');
			bname = bname ? bname + 1 : sh;
			strncpy(p->status, bname, sizeof p->status - 1);
		}
		if( p->s == NULL ){
			p->s = &p->scr[0];
			tput(p, 0, 0, 0, NULL, ris);
			p->vp.p = p;
		}
	}
	return p;
}

struct canvas *
newcanvas(struct pty *p, struct canvas *parent)
{
	struct canvas *n = NULL;
	if( (n = S.unused) != NULL ){
		S.unused = n->c[0];
	} else {
		check((n = calloc(1 , sizeof *n)) != NULL, 0, "calloc");
	}
	if( n ){
		n->c[0] = n->c[1] = NULL;
		n->manualscroll = n->offset.y = n->offset.x = 0;
		n->p = p ? p : new_pty(S.history, MAX(COLS, S.width), false);
		n->parent = parent;
		if( n->p ){
			n->p->count += 1;
		}
		n->split = (typeof(n->split)){1.0, 1.0};
	}
	return n;
}

static void
draw_window(struct canvas *n)
{
	struct point o = n->origin;
	struct point e = { o.y + n->extent.y - 1, o.x + n->extent.x - 1 };
	if( n->p && e.y > 0 && e.x > 0 ){
		if( ! n->manualscroll ){
			n->offset.x = MAX(0, n->p->s->c.x - n->extent.x + 1);
		}
		struct point off = n->offset;
		if( n->p->ws.ws_col < n->extent.x ){
			assert( n->offset.x == 0 );
			pnoutrefresh(S.wbkg, 0, 0, o.y, o.x + n->p->ws.ws_col,
				e.y, e.x);
		}
		pnoutrefresh(n->p->s->w, off.y, off.x, o.y, o.x, e.y, e.x);
	}
}

static void
fixcursor(void) /* Move the terminal cursor to the active window. */
{
	int y = S.f->p->s->c.y, x = S.f->p->s->c.x;
	int show = S.binding != ctl && S.f->extent.y
		&& x >= S.f->offset.x && x < S.f->offset.x + S.f->extent.x
		&& y >= S.f->offset.y && y < S.f->offset.y + S.f->extent.y;
	draw_window(S.f);
	curs_set(show ? S.f->p->s->vis : 0);
}

void
reshape_window(struct pty *p)
{
	check(ioctl(p->fd, TIOCSWINSZ, &p->ws) == 0, 0, "ioctl on %d", p->fd);
	check(kill(p->pid, SIGWINCH) == 0, 0, "send WINCH to %d", (int)p->pid);
	set_scroll(p->scr, 0, p->scr->rows - 1);
	set_scroll(p->scr + 1, p->tos, p->scr->rows - 1);
}

void
set_scroll(struct screen *s, int top, int bottom)
{
	wsetscrreg(s->w, s->scroll.top = top, s->scroll.bot = bottom);
}


void
scrollbottom(struct canvas *n)
{
	if( n && n->p && n->p->s && n->extent.y ){
		n->offset.y = MAX(n->p->s->maxy - n->extent.y + 1, 0);
	}
}

void
reshape(struct canvas *n, int y, int x, int h, int w)
{
	if( n ){
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

		/* If the pty is visible in multiple canvasses,
		set ws.ws_row to the one with biggest extent.y */
		if( n->p->fd >= 0 && n->extent.y > n->p->ws.ws_row ){
			n->p->ws.ws_row = n->extent.y;
			n->p->tos = n->p->scr->rows - n->extent.y;
			reshape_window(n->p);
			wrefresh(n->p->s->w);
		}
		scrollbottom(n);
	}
	S.reshape = 0;
}

void
change_count(struct canvas * n, int count, int pop)
{
	assert( count < 2 && count > -2 );
	if( n && n->p ){
		n->p->count += count;
		change_count(n->c[0], count, pop);
		change_count(n->c[1], count, pop);
		if( pop && n->p->count == 0 ){
			freecanvas(n);
		}
	}
}

void
freecanvas(struct canvas *n)
{
	if( n == S.f ){
		S.f = n->parent;
	}
	if( n->parent ){
		n->parent->c[n == n->parent->c[1]] = NULL;
	}
	n->c[0] = S.unused;
	n->c[1] = NULL;
	n->parent = NULL;
	S.unused = n;
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
	mvwprintw(n->wtit, 0, 0, "%d %s ",
		n->p->fd > 2 ? n->p->fd - 2 : n->p->pid,
		n->p->status);
	int x = n->offset.x;
	int w = n->p->ws.ws_col;
	if( x > 0 || x + n->extent.x < w ){
		wprintw(n->wtit, "%d-%d/%d ", x + 1, x + n->extent.x, w);
	}
	whline(n->wtit, ACS_HLINE, n->extent.x);
	struct point o = n->origin;
	draw_pane(n->wtit, o.y + n->extent.y, o.x);
}

static void
draw_div(struct canvas *n, int rev)
{
	wattrset(n->wdiv, rev ? A_REVERSE : A_NORMAL);
	mvwvline(n->wdiv, 0, 0, ACS_VLINE, INT_MAX);
	draw_pane(n->wdiv, n->origin.y, n->origin.x + n->extent.x);
}

void
draw(struct canvas *n) /* Draw a canvas. */
{
	if( n != NULL && n->extent.y > 0 ){
		int rev = S.binding == ctl && n == S.f;
		draw(n->c[0]);
		if( n->c[1] ){
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
	if( check(waitpid(p->pid, &status, WNOHANG) != -1, 0, "waitpid") ){
		if( WIFEXITED(status) ){
			k = WEXITSTATUS(status);
		} else if( WIFSIGNALED(status) ){
			fmt = "caught signal %d";
			k = WTERMSIG(status);
		}
		FD_CLR(p->fd, &S.fds);
		check(close(p->fd) == 0, 0, "close fd %d", p->fd);
		snprintf(p->status, sizeof p->status, fmt, k);
		p->fd = -1; /* (1) */
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
	if( select(S.maxfd + 1, &sfds, NULL, NULL, NULL) < 0 ){
		check(errno == EINTR, 0, "select");
		return;
	}
	if( FD_ISSET(STDIN_FILENO, &sfds) ){
		int r;
		wint_t w;
		while( S.f && (r = wget_wch(S.f->p->s->w, &w)) != ERR ){
			struct handler *b = NULL;
			if( r == OK && w > 0 && w < 128 ){
				b = S.binding + w;
			} else if( r == KEY_CODE_YES ){
				assert( w >= KEY_MIN && w <= KEY_MAX );
				b = &code_keys[w - KEY_MIN];
			}
			if( b ){
				b->arg ? b->act.a(b->arg) : b->act.v();
				if( b->act.a != digit ){
					S.count = -1;
				}
			}
		}
	}
	for( struct pty *t = S.p; t; t = t->next ){
		if( t->fd > 0 && FD_ISSET(t->fd, &sfds) ){
			FD_CLR(t->fd, &sfds);
			char iobuf[BUFSIZ];
			int oldmax = t->s->maxy;
			ssize_t r = read(t->fd, iobuf, sizeof iobuf);
			if( r > 0 ){
				vtwrite(&t->vp, iobuf, r);
				t->s->delta = t->s->maxy - oldmax;
			} else if( errno != EINTR && errno != EWOULDBLOCK ){
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

static void
build_bindings(void)
{

	k1[S.ctlkey] = (struct handler){ { .a = transition }, " control" };
	ctl[S.ctlkey] = (struct handler){ { .a = transition }, "*enter" };

	for( wchar_t k = KEY_MIN; k < KEY_MAX; k++ ){
		int idx = k - KEY_MIN;
		if( code_keys[idx].arg || code_keys[idx].act.v ){
			continue;
		}
		assert( MB_LEN_MAX < 128 );
		int i = idx * (1 + MB_LEN_MAX);
		int v = wctomb(wc_lut + i + 1, k);
		assert( v < 128 && v > -2 );
		wc_lut[ i ] = v == -1 ? 0 : v;
		code_keys[idx].act.a = send;
		code_keys[idx].arg = wc_lut + i;
	}
}

static void
update_offset_r(struct canvas *n)
{
	if( n ){
		if( n->p && n->p->s && n->p->s->delta ){
			int d = n->p->s->delta;
			int t = n->p->s->maxy - n->extent.y + 1;
			if( t > 0 ){
				n->offset.y += MIN(d, t);
			}
		}
		update_offset_r(n->c[0]);
		update_offset_r(n->c[1]);
	}
}

static void
main_loop(void)
{
	while( S.c != NULL ){
		if( S.reshape ){
			reshape(S.c, 0, 0, LINES, COLS);
			wrefresh(curscr);
		}
		draw(S.c);
		if( *S.errmsg ){
			mvwprintw(S.werr, 0, 0, "%s", S.errmsg);
			wclrtoeol(S.werr);
			draw_pane(S.werr, LINES - 1, 0);
		}
		fixcursor();
		doupdate();
		getinput();
		update_offset_r(S.c);
		for( struct pty *p = S.p; p; p = p->next ){
			p->s->delta = 0;
		}
	}
}

void
endwin_wrap(void)
{
	(void)endwin();
}

static void
init(void)
{
	signal(SIGTERM, exit);
	FD_ZERO(&S.fds);
	FD_SET(STDIN_FILENO, &S.fds);
	build_bindings();
	atexit(endwin_wrap);
	initscr(); /* exits on failure */
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
	wborder(S.wbkg, ACS_VLINE, ACS_BULLET, ACS_BULLET, ACS_BULLET,
		ACS_VLINE, ACS_BULLET, ACS_BULLET, ACS_BULLET);
	wattron(S.werr, A_REVERSE);
	S.f = S.c = newcanvas(NULL, NULL);
	if( S.c == NULL || S.werr == NULL || S.wbkg == NULL || !S.c->p ){
		endwin();
		errx(EXIT_FAILURE, "Unable to create root window");
	}
	reshape_root();
}

int
smtx_main()
{
	init();
	main_loop();
	return EXIT_SUCCESS;
}

/*
Parse a string to build a layout. The layout is expected
to consist of coordinates of the lower right corner of each
window as a percentage of the full screen.  eg:

.5:.5 .25:.66 .5:.66 .5:.83 .5:1 1:.25 1:1

would describe a layout that looks like:
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
Note that order matters.
 */
static struct canvas *
add_canvas(const char **lp, double oy, double ox, double ey, double ex,
	struct pty **pp, struct canvas *parent)
{
	double y, x;
	int e;
	struct pty *p;
	struct canvas *n;
	if( ( n = newcanvas(p = *pp, parent)) == NULL
		|| n->p == NULL
		|| ! check(2 == sscanf(*lp, "%lf%*1[,:]%lf%n", &y, &x, &e),
			errno = 0, "Invalid format at %s", **lp ? *lp : "end")
		|| ! check(y > oy && y <= ey && x > ox && x <= ex,
			errno = 0, "Out of bounds: %s", *lp) ){
		goto fail;
	}
	*lp += e;
	*pp = p ? (( p->next ? p->next : S.p ) == S.f->p ) ? NULL : p->next : p;
	n->split.y = (y - oy) / (ey - oy);
	n->split.x = (x - ox) / (ex - ox);
	if( y == ey && x == ex ){
		return n;
	} else if( y == ey ){
		n->typ = 1;
	} else if( x == ex ){
		n->typ = 0;
	} else {
		double ny, nx;
		if( ! check(2 == sscanf(*lp, "%lf%*1[,:]%lf", &ny, &nx),
				errno = 0, "Invalid format @ '%s'", *lp)
			|| ! check(y <= ny || x <= nx, 0,
				"Out of bounds: %s", *lp)
			|| (n->typ = y < ny,
				(n->c[!n->typ] = add_canvas(lp,
				n->typ ? y : oy, n->typ ? ox : x,
				n->typ ? ey : y, n->typ ? x : ex,
				pp, n)) == NULL ) ){
			goto fail;
		}
	}
	if( (n->c[n->typ] = add_canvas(lp,
			n->typ ? oy : y, n->typ ? x : ox,
			ey, ex, pp, n)) == NULL ){
		goto fail;
	}

	return n;
fail:
	change_count(n, -1, 1);
	return NULL;
}

int
build_layout(const char *layout)
{
	struct pty *p = S.f->p;
	change_count(S.c, -1, 0); /* (1) */
	struct canvas *n = add_canvas(&layout, 0.0, 0.0, 1.0, 1.0, &p, NULL);
	change_count(S.c, +1, 0);
	if( n ){
		change_count(S.c, -1, 1);
		S.f = S.c = n;
		S.reshape = 1;
	}
	return S.reshape;
}
/* (1) Decrement the view counts so that visible ptys will be availalbe for
 * the new layout.
 */
