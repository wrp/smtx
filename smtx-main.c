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

static struct handler keys[128];
static struct handler cmd_keys[128];
static struct handler code_keys[KEY_MAX - KEY_MIN + 1];
static struct state S = {
	.commandkey = CTL('g'),
	.width = 80,
	.binding = &keys,
	.display_level = UINT_MAX
};

/* Variables exposed to test suite */
struct canvas *focused;
int cmd_count = -1;
int scrollback_history = 1024;

static void
set_errmsg(const char *fmt, ...)
{
	int e = errno;
	wmove(S.werr, 0, 0);
	va_list ap;
	va_start(ap, fmt);
	vw_printw(S.werr, fmt, ap);
	va_end(ap);
	if( e ) {
		wprintw(S.werr, ": %s", strerror(e));
	}
	errno = e;
}

int
rewrite(int fd, const char *b, size_t n)
{
	const char *e = b + n;
	while( b < e ) {
		ssize_t s = write(fd, b, e - b);
		if( s < 0 && errno != EINTR ) {
			set_errmsg("write to fd %d", fd);
			return -1;
		}
		b += s < 0 ? 0 : s;
	}
	return 0;
}

static const char *
getshell(void)
{
	const char *s = getenv("SHELL");
	struct passwd *pwd = s ? NULL : getpwuid(geteuid());
	return s ? s : pwd ? pwd->pw_shell : "/bin/sh";
}

static void
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

static int
delwinnul(WINDOW **w)
{
	int rv = *w ? delwin(*w) : OK;
	*w = NULL;
	return rv;
}

static int
find_max_fd(struct canvas *n)
{
	return n ? MAX(n->p ? n->p->pt : -1,
		MAX(find_max_fd(n->c[0]), find_max_fd(n->c[1]))) : -1;
}

static void
free_proc(struct pty **pv)
{
	struct pty *p = *pv;
	if( p != NULL && p->count == 0 ) {
		free(p->tabs);
		delwinnul(&p->pri.win);
		delwinnul(&p->alt.win);
		struct pty *t, *prev = NULL;
		for( t = S.p; t; t = t->next ) {
			if( t == p ) {
				*(prev ? &prev->next : &S.p) = p->next;
			}
			prev = t;
		}
		free(p);
		*pv = NULL;
	}
}

static void
resize_pad(WINDOW **p, int h, int w)
{
	if( *p ) {
		if( wresize(*p, h, w ) != OK ) {
			set_errmsg("Error resizing window");
			delwinnul(p);
		}
	} else if( (*p = newpad(h, w)) != NULL ) {
		nodelay(*p, TRUE);
	}
}

static int
new_screens(struct pty *p)
{
	int rows = MAX(LINES, scrollback_history);
	int cols = MAX(COLS, S.width);
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
		p->ws.ws_row = rows;
		p->ws.ws_col = MAX(cols, S.width);
		p->pid = forkpty(&p->pt, NULL, NULL, &p->ws);
		if( p->pid == 0 ) {
			const char *sh = getshell();
			setsid();
			signal(SIGCHLD, SIG_DFL);
			execl(sh, sh, NULL);
			err(EXIT_FAILURE, "exec SHELL='%s'", sh);
		} else if( p->pid < 0 || new_screens(p) == -1 ) {
			set_errmsg("new_pty");
			free_proc(&p);
		} else if( p->pid > 0 ) {
			FD_SET(p->pt, &S.fds);
			S.maxfd = p->pt > S.maxfd ? p->pt : S.maxfd;
			fcntl(p->pt, F_SETFL, O_NONBLOCK);
			extend_tabs(p, p->tabstop = 8);
		}
		p->next = S.p;
		S.p = p;
	}
	return p;
}

static struct canvas *
newcanvas(void)
{
	struct canvas *n = calloc(1, sizeof *n);
	if( !n ) {
		set_errmsg("calloc");
	} else if( ( n->p = new_pty(LINES, COLS)) == NULL ) {
		set_errmsg("new_pty");
		free(n);
		n = NULL;
	} else {
		n->split_point[0] = 1.0;
		n->split_point[1] = 1.0;
		n->auto_prune = 1;
		n->p->count = 1;
		n->input = n->p->pri.win;
	}
	return n;
}

static void
focus(struct canvas *n)
{
	focused = n ? n : S.v;
}

static void
freecanvas(struct canvas *n)
{
	if( n ) {
		delwinnul(&n->wtit);
		delwinnul(&n->wdiv);
		delwinnul(&n->win);
		if( n->p ) {
			n->p->count -= 1;
			free_proc(&n->p);
		}
		free(n);
	}
}

static int
winpos(WINDOW *w, int dir)
{
	int y = 0, x = 0;
	if( w ) {
		getyx(w, y, x);
	}
	return dir ? x : y;
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
draw_window(struct canvas *n)
{
	struct point o = n->origin;
	struct point e = { o.y + n->extent.y - 1, o.x + n->extent.x - 1 };
	if( e.y > 0 && e.x > 0 ) {
		WINDOW *w = n->win;
		struct point off = { 0, 0 };
		int x = winpos(n->input, 1);
		if( ! n->manualscroll ) {
			if( x < n->extent.x - 1 ) {
				n->offset.x = 0;
			} else if( n->offset.x + n->extent.x < x + 1 ) {
				n->offset.x = x - n->extent.x + 1;
			}
		}
		if( n->p ) {
			w = n->p->s->win;
			off = n->offset;
		}
		pnoutrefresh(w, off.y, off.x, o.y, o.x, e.y, e.x);
	}
}

void
fixcursor(void) /* Move the terminal cursor to the active window. */
{
	struct canvas *f = focused;
	int x = 0, y = 0;
	if( f->p ) {
		assert( f->p->s );
		int show = S.binding != &cmd_keys && f->p->s->vis;
		getyx(f->input, y, x);
		if( x < f->offset.x ) {
			show = false;
		}
		curs_set(f->offset.y != f->p->s->tos ? 0 : show);
		y = MIN(MAX(y, f->p->s->tos), f->p->s->tos + f->extent.y);
		assert( y >= f->p->s->tos && y <= f->p->s->tos + f->extent.y );
	} else {
		f->input = f->win ? f->win : f->wtit ? f->wtit : f->wdiv;
	}
	assert(f->input || S.c == NULL);
	wmove(f->input, y, x);
	draw_window(f);
	wmove(f->input, y, x);
}

static const char *
getterm(void)
{
	const char *t = getenv("TERM");
	return t ? t : COLORS > 255 ? DEFAULT_COLOR_TERMINAL : DEFAULT_TERMINAL;
}

static void
reshape_window(struct canvas *n, const char *arg)
{
	struct pty *p = n->p;
	struct winsize ws = p->ws;
	int h = MAX(n->extent.y, scrollback_history);
	int w = MAX(n->extent.x, cmd_count < 1 ? S.width : cmd_count);

	memset(&p->ws, 0, sizeof p->ws);
	p->ws.ws_row = strchr(arg, 'h') ? n->extent.y : ws.ws_row;
	p->ws.ws_col = strchr(arg, 'w') ? w : ws.ws_col;

	resize_pad(&p->pri.win, h, p->ws.ws_col);
	resize_pad(&p->alt.win, h, p->ws.ws_col);
	p->pri.tos = n->offset.y = h - n->extent.y;
	assert( p->alt.tos == 0 );
	wsetscrreg(p->pri.win, 0, h - 1);
	wsetscrreg(p->alt.win, 0, n->extent.y - 1);
	wrefresh(p->s->win);
	extend_tabs(p, p->tabstop);
	if( ioctl(p->pt, TIOCSWINSZ, &p->ws) ) {
		set_errmsg("ioctl");
	}
	if( kill(p->pid, SIGWINCH) ) {
		set_errmsg("kill");
	}
}

static void
scrollbottom(struct canvas *n)
{
	if( n && n->p && n->p->s ) {
		n->offset.y = n->p->s->tos;
	}
}

static void
set_title(struct canvas *n)
{
	assert( n->p != NULL );
	mvwprintw(n->wtit, 0, 0, "%d %d-%d/%d %s",
		n->p->pid,
		n->offset.x + 1,
		n->offset.x + n->extent.x,
		n->p->ws.ws_col,
		getshell()
	);
	whline(n->wtit, ACS_HLINE, n->extent.x);
	/* See draw_title().  We must leave the cursor at the start of
	the HLINE so that we can reliably change reverse video */
}

static void
reshape(struct canvas *n, int y, int x, int h, int w, unsigned level)
{
	if( n ) {
		n->origin.y = y;
		n->origin.x = x;
		int h1 = level < S.display_level ? h * n->split_point[0] : h;
		int w1 = level < S.display_level ? w * n->split_point[1] : w;
		int have_title = h1 && w1;
		int have_div = h && w && n->c[1] && level < S.display_level;

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

		if( level < S.display_level ) {
			reshape(n->c[0], y + h1, x, h - h1, n->typ ? w1 : w,
				level + 1);
			reshape(n->c[1], y, x + w1 + have_div,
				n->typ ? h : h1, w - w1 - have_div, level + 1);
		}
		bool changed = n->extent.y != h1 - 1;
		n->extent.y = h1 - 1; /* Subtract one for title line */
		n->extent.x = w1;
		/* TODO: avoid resizing window unnecessarily */
		if( n->p && n->p->pt >= 0 ) {
			set_title(n);
			if( changed ) {
				reshape_window(n, "h");
			}
			scrollbottom(n);
		} else if( w1 && h1 > 1 ) {
			resize_pad(&n->win, n->extent.y, n->extent.x);
		}
	}
}

static void
reshape_root(struct canvas *n, const char *arg)
{
	(void)arg;
	(void)n;
	reshape(S.c, 0, 0, LINES, COLS, 1);
}

static void
toggle_prune(struct canvas *n, const char *arg)
{
	(void)arg;
	n->auto_prune = !n->auto_prune;
}

static void
prune(struct canvas *x)
{
	struct canvas *p = x->parent;
	struct canvas *dummy;
	struct canvas *del = x;
	int d = x->typ;
	struct canvas *n = x->c[d];
	struct canvas *o = x->c[!d];
	if( ! x->auto_prune || ( o && o->c[d]) ) {
		del = NULL;
	} else if( o ) {
		assert( o->c[d] == NULL );
		assert( o->parent == x );
		assert( o->typ != d );
		o->typ = d;
		o->parent = p;
		*(p ? &p->c[d] : &S.c) = o;
		o->c[d] = n;
		*(n ? &n->parent : &dummy) = o;
		o->origin = x->origin;
		o->split_point[d] = x->split_point[d];
	} else if( n ) {
		n->parent = p;
		n->origin = x->origin;
		*(p ? &p->c[d] : &S.c) = n;
	} else if( p ) {
		p->split_point[d] = 1.0;
		p->c[d] = NULL;
	} else {
		S.c = NULL;
	}
	if( del ) {
		freecanvas(del);
		if( x == focused ) {
			focus(o ? o : n ? n : p);
		}
		if( S.v == x ) {
			S.v = o ? o : n ? n : p;
		}
		reshape_root(NULL, NULL);
	}
}

static void
draw_pane(WINDOW *w, int y, int x)
{
	pnoutrefresh(w, 0, 0, y, x, y + winsiz(w, 0) - 1, x + winsiz(w, 1) - 1);
}

static void
draw_title(struct canvas *n, int r)
{
	if( n->wtit ) {
		/* This relies on set_title() leaving the
		cursor at the start of the ACS_HLINE */
		struct point o = n->origin;
		int x = winpos(n->wtit, 1);
		mvwchgat(n->wtit, 0, 0, x, r ? A_REVERSE : A_NORMAL, 0, NULL);
		wattrset(n->wtit, r ? A_REVERSE : A_NORMAL);
		mvwhline(n->wtit, 0, x, ACS_HLINE, n->extent.x);
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
	if( n != NULL ) {
		int rev = S.binding == &cmd_keys && n == focused;
		draw(n->c[0]);
		draw(n->c[1]);
		draw_div(n, rev && !n->extent.x);
		draw_title(n, rev);
		draw_window(n);
	}
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
	return n ? n : S.c;
}

void
create(struct canvas *n, const char *arg)
{
	int dir = arg && *arg == 'C' ? 1 : 0;
	while( n && n->c[dir] != NULL ) {
		n = n->c[dir]; /* Split last window in a chain. */
	}
	struct canvas *v = *( n ? &n->c[dir] : &S.c) = newcanvas();
	if( v != NULL ) {
		v->typ = dir;
		v->parent = n;
		balance(v);
	}
	reshape_root(NULL, NULL);
}

static void
wait_child(struct canvas *n)
{
	int status, k = 0;
	const char *fmt;
	if( waitpid(n->p->pid, &status, WNOHANG) == n->p->pid ) {
		if( WIFEXITED(status) ) {
			fmt = "%d exited %d";
			k = WEXITSTATUS(status);
		} else if( WIFSIGNALED(status) ) {
			fmt = "%d caught signal %d";
			k = WTERMSIG(status);
		} else {
			fmt = "%d stopped";
			assert( WIFSTOPPED(status) );
		}
		mvwprintw(n->wtit, 0, 0, fmt, n->p->pid, k);
		whline(n->wtit, ACS_HLINE, n->extent.x);
		assert(n->p->pt > 0 );
		close(n->p->pt);
		FD_CLR(n->p->pt, &S.fds);
		if( S.maxfd == n->p->pt ) {
			n->p->pt = -1;
			S.maxfd = find_max_fd(S.c);
		}
	}
}

static bool
getinput(struct canvas *n, fd_set *f) /* check all ptty's for input. */
{
	bool status = true;
	if( n && n->c[0] && !getinput(n->c[0], f) ) {
		status = false;
	} else if( n && n->c[1] && !getinput(n->c[1], f) ) {
		status = false;
	} else if( n && n->p && n->p->pt > 0 && FD_ISSET(n->p->pt, f) ) {
		char iobuf[BUFSIZ];
		ssize_t r = read(n->p->pt, iobuf, sizeof iobuf);
		if( r > 0 ) {
			vtwrite(&n->p->vp, iobuf, r);
		} else if( errno != EINTR && errno != EWOULDBLOCK ) {
			wait_child(n);
			prune(n);
			status = false;
		}
	}
	return status;
}

static void
digit(struct canvas *n, const char *arg)
{
	(void)n;
	cmd_count = 10 * (cmd_count == -1 ? 0 : cmd_count) + *arg - '0';
}

static void
set_view_count(struct canvas *n, const char *arg)
{
	(void)arg;
	S.v = n;
	switch( cmd_count ) {
	case 0:
		S.v = S.c;
		S.display_level = UINT_MAX;
		break;
	case -1:
		S.display_level = S.display_level == 1 ? UINT_MAX : 1;
		break;
	default:
		S.display_level = cmd_count;
	}
	reshape_root(NULL, NULL);
}

void
scrollh(struct canvas *n, const char *arg)
{
	if( n && n->p && n->p->s && n->p->s->win ) {
		int x = winsiz(n->p->s->win, 1);
		int count = cmd_count == -1 ? n->extent.x - 1 : cmd_count;
		n->offset.x += *arg == '<' ? -count : count;
		if( n->offset.x < 0 ) {
			n->offset.x = 0;
		} else if( n->offset.x > x - n->extent.x ) {
			n->offset.x = x - n->extent.x;
		}
		n->manualscroll = n->offset.x != 0;
	}
}

void
scrolln(struct canvas *n, const char *arg)
{
	if( n && n->p && n->p->s && n->p->s->win ) {
		int count = cmd_count == -1 ? n->extent.y - 1 : cmd_count;
		n->offset.y += *arg == '-' ? -count : count;
		n->offset.y = MIN(MAX(0, n->offset.y), n->p->s->tos);
	}
}

static void
sendarrow(struct canvas *n, const char *k)
{
	char buf[3] = { '\033', n->p->pnm ? 'O' : '[', *k };
	rewrite(n->p->pt, buf, 3);
}

int
contains(struct canvas *n, int y, int x)
{
	return
		y >= n->origin.y && y <= n->origin.y + n->extent.y &&
		x >= n->origin.x && x <= n->origin.x + n->extent.x;
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

void
resize(struct canvas *n, const char *arg)
{
	int typ = strchr("JK", *arg) ? 0 : 1;
	enum { down, up } dir = strchr("JL", *arg) ? down : up;
	if( dir == down ) while( n && n->c[dir] == NULL ) {
		n = n->parent;
	}
	if( !n || !n->c[typ] ) {
		return;
	}
	switch(*arg) {
	case 'K':
	case 'J':
	{
		int full = (n->extent.y + 1) / n->split_point[0];
		int count = cmd_count < 1 ? 1 : cmd_count;
		double new = (n->extent.y + 1) + count * ( *arg == 'K' ? -1 : 1 );
		n->split_point[0] = MAX( 0, MIN(new / full, 1.0) );
		} break;
	}
	reshape_root(NULL, NULL);
}

void
mov(struct canvas *n, const char *arg)
{
	assert( n == focused && n != NULL );
	char cmd = *arg;
	int count = cmd_count < 1 ? 1 : cmd_count;
	int startx = n->origin.x;
	int starty = n->origin.y + n->extent.y;
	struct canvas *t = n;
	for( ; t && count--; n = t ? t : n ) {
		switch( cmd ) {
		case 'k': /* move up */
			t = find_window(S.v, t->origin.y - 1, startx);
			break;
		case 'j': /* move down */
			t = find_window(S.v,
				t->origin.y + t->extent.y + 1, startx);
			break;
		case 'l': /* move right */
			t = find_window(S.v, starty,
				t->origin.x + t->extent.x + 1);
			break;
		case 'h': /* move left */
			t = find_window(S.v, starty, t->origin.x - 1);
			break;
		}
	}
	focus(n);
}

void
send_nul(struct canvas *n, const char *arg)
{
	(void) arg;
	scrollbottom(n);
	rewrite(n->p->pt, "\x00", 1);
}

static void
send(struct canvas *n, const char *arg)
{
	if( n->p && arg ) {
		if( n->p->lnm && *arg == '\r' ) {
			assert( arg[1] == '\0' );
			arg = "\r\n";
		}
		scrollbottom(n);
		rewrite(n->p->pt, arg, strlen(arg));
	}
}

void
equalize(struct canvas *n, const char *arg)
{
	(void) arg;
	assert( n != NULL );
	n = balance(n);
	reshape_root(NULL, NULL);
}

void
transition(struct canvas *n, const char *arg)
{
	if( S.binding == &keys ) {
		S.binding = &cmd_keys;
	} else {
		S.binding = &keys;
		wmove(S.werr, 0, 0);
		scrollbottom(n);
	}
	send(n, arg);
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

void
new_tabstop(struct canvas *n, const char *arg)
{
	int c = arg ? strtol(arg, NULL, 10) : cmd_count > -1 ? cmd_count : 8;
	n->p->ntabs = 0;
	extend_tabs(n->p, n->p->tabstop = c);
}

void
build_bindings()
{
	assert( KEY_MAX - KEY_MIN < 2048 ); /* Avoid overly large luts */

	add_key(keys, S.commandkey, transition, NULL);
	add_key(keys, L'\r', send, "\r");
	add_key(keys, L'\n', send, "\n");
	add_key(keys, 0, send_nul, NULL);

	add_key(cmd_keys, S.commandkey, transition, &S.commandkey);
	add_key(cmd_keys, L'\r', transition, NULL);
	/* TODO: rebind b,f,<,> to hjkl in different binding */
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
	add_key(cmd_keys, L't', new_tabstop, NULL);
	add_key(cmd_keys, L'W', reshape_window, "hw");
	add_key(cmd_keys, L'v', set_view_count, NULL);
	add_key(cmd_keys, L'x', toggle_prune, NULL);
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
	struct canvas *n = focused;

	if( r == OK && k > 0 && k < (int)sizeof *S.binding ) {
		b = &(*S.binding)[k];
	} else if( r == KEY_CODE_YES ) {
		if( k >= KEY_MIN && k <= KEY_MAX ) {
			b = &code_keys[k - KEY_MIN];
		}
	}

	if( b && b->act ) {
		b->act(n, b->arg);
	} else if( S.mode == passthru ) {
		char c[MB_LEN_MAX + 1];
		if( ( r = wctomb(c, k)) > 0 && n->p ) {
			scrollbottom(n);
			rewrite(n->p->pt, c, r);
		}
		n->manualscroll = 0;
	}
	if( !b || !(b->act == digit) ) {
		cmd_count = -1;
	}
}

void
main_loop(void)
{
	while( S.c != NULL ) {
		int r;
		wint_t w = 0;
		fd_set sfds = S.fds;

		draw(S.v);
		if( winpos(S.werr, 1) ) {
			int y = LINES - 1, x = MIN(winsiz(S.werr, 1), COLS);
			pnoutrefresh(S.werr, 0, 0, y, 0, y, x);
		}
		fixcursor();
		doupdate();
		if( select(S.maxfd + 1, &sfds, NULL, NULL, NULL) < 0 ) {
			FD_ZERO(&sfds);
		}
		while( (r = wget_wch(focused->input, &w)) != ERR ) {
			handlechar(r, w);
			fixcursor();
		}
		getinput(S.c, &sfds);
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
			scrollback_history = strtol(optarg, NULL, 10);
			break;
		case 't':
			setenv("TERM", optarg, 1);
			break;
		case 'v':
			printf("%s-%s\n", PACKAGE_NAME, PACKAGE_VERSION);
			exit(EXIT_SUCCESS);
		case 'w':
			S.width = strtol(optarg, NULL, 10);
			break;
		default:
			fprintf(stderr, "Unknown option: %c\n", optopt);
			exit(EXIT_FAILURE);
		}
	}
}


struct canvas *
init(void)
{
	char buf[16];
	FD_ZERO(&S.fds);
	FD_SET(STDIN_FILENO, &S.fds);
	S.maxfd = STDIN_FILENO;
	snprintf(buf, sizeof buf - 1, "%d", getpid());
	setenv("SMTX", buf, 1);
	setenv("TERM", getterm(), 1);
	setenv("SMTX_VERSION", VERSION, 1);
	setlocale(LC_ALL, "");
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
	resize_pad(&S.werr, 1, COLS);
	if( S.werr == NULL ) {
		errx(EXIT_FAILURE, "Unable to create error window");
	}
	wattron(S.werr, A_REVERSE);
	create(NULL, NULL);
	if( ( focused = S.v = S.c ) == NULL ) {
		errx(EXIT_FAILURE, "Unable to create root window");
	}
	return S.c;
}

int
smtx_main(int argc, char *const argv[])
{
	parse_args(argc, argv);
	init();
	main_loop();
	endwin();
	return EXIT_SUCCESS;
}
