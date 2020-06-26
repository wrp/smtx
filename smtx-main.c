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
     fix alt screen.   It's wonky (as in, totally does not work).  Need to fix
       sizing (probably cannot completely disacoosiate the proc from the
       canvas if we want pagers to work) and implement some tests.
     make tput rep work.  eg, tput rep w 5 should write 5 'w' to term,
       but the parameters do not seem to be getting sent properly.  We get
       argc == 1 and argv[0] == 5 - 1, but the w is chomped.  Note that this
       is the only terminfo entry that uses %c, and I suspect there is a bug
       in vtparser
     Full-screen mode.
     In full-screen mode, make hjkl scroll the screen.
     Test Suite!!
       Write a screen dump function.  Capability to write text description
       of the layout.  This could be also be used for non-testing purposes
       if the description can be used to construct a new layout.
     Make default minimum width (default 80) instead of tying it to the
       current window width.  Once it is easy to scroll horizontally, this
       will make more sense.
     Use current cursor position to set bottom of screen when splitting (?)
     Make it easy to swap bindings.  eg, so that hjkl could be used for
       scrolling in non-full screen mode.  Maybe have labelled bindings,
       so perhaps 'a or 'b would select binding a or b.  Would be simpler
       to use numbers, so 1B or 2B could select bindings 1 or 2.  It seems
       hjkl would be better for scrolling than <> and fb
     Copy-mode, with stack of registers and ability to edit.
       Or, maybe just have a binding (s) that dumps the current content
       of the scrollback region to a file like ~/.smtx-pid-timestamp
       Perhaps use (e) to edit the file (eg, spawn $EDITOR), then (p)
       to paste it.
     Ability to set titles.
     Be able to set tty screen width. Eg, perhaps 'w' to set width of the
       focused window, and `W` to change the tty.  Then +/- to adjust
       width of window, and maybe </> to adjust width of tty.  Or </>
       should scroll the window.  But +/- should also change number
       of rows in a type 0 canvas.  Need to think about this, but defintely
       want ability to change tty width.  Probably use one binding in which
       hjkl navigate windows, one in which hjkl grows windows, and one in
       which hjkl changes scroll in pty.  Also use <tab> to scroll right
       one tabstop in the pty.
     Needs to be easier to navigate in full screen mode.   Perhaps
       title bar would show title of windows above, below, to the
       left, and right.  Then hjkl navigation would stay in full
       screen mode while going between them.
     Register signal handlers for TERM and HUP.  Need to ensure
       that endwin is called.
     List all windows with titles, with ability to navigate.
     Be able to attach a process to multiple windows.
     Pass master fd of a pty through a socket.
     Configure bindings.
     Multi-key bindings (?)
     Handle memory allocation errors.  Implement decent error reporting.
 */

#include "smtx.h"

struct state S;
static struct handler keys[128];
static struct handler cmd_keys[128];
static struct handler code_keys[KEY_MAX - KEY_MIN + 1];
static struct handler (*binding)[128] = &keys;
struct canvas *focused, *lastfocused = NULL;
struct canvas *root, *view_root;
static int maxfd = STDIN_FILENO;
fd_set fds;
int cmd_count = -1;
int scrollback_history = 1024; /* Change at runtime with -s */

static struct canvas * balance(struct canvas *n);
static void reshape(struct canvas *n, int y, int x, int h, int w);

static WINDOW *werr;
char errmsg[80];

static void
show_error(const char *fmt, ...)
{
	int e = errno;
	int k;
	va_list ap;
	va_start(ap, fmt);
	k = vsnprintf(errmsg, sizeof errmsg, fmt, ap);
	va_end(ap);
	if( e && k < (int)sizeof errmsg ) {
		snprintf(errmsg + k, sizeof errmsg - k, ": %s", strerror(e));
	}
}

void
safewrite(int fd, const char *b, size_t n)
{
	ssize_t s = 0;
	const char *e = b + n;
	while( s != -1 && (b += s) < e ) {
		if( (s = write(fd, b, e - b)) == -1 && errno != EINTR ) {
			show_error("write to fd %d", fd); /* uncovered */
			s = 0; /* uncovered */
		}
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
	int w = MAX(p->ws.ws_col, 80);
	if( p->ntabs < w ) {
		typeof(*p->tabs) *n = realloc(p->tabs, w * sizeof *n);
		if( n ) for( p->tabs = n; p->ntabs < w; p->ntabs++ ) {
			p->tabs[p->ntabs] = p->ntabs % tabstop == 0;
		}
	}
}

static struct canvas *
newcanvas()
{
	struct canvas *n = calloc(1, sizeof *n);
	if( !n ) {
		show_error("newcanvas");
	} else {
		n->split_point[0] = 1.0;
		n->split_point[1] = 1.0;
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
	if( *p ) {
		if( wresize(*p, h, w ) != OK ) {
			show_error("Error resizing window");
			*p = NULL;
		}
	} else if( (*p = newpad(h, w)) != NULL ) {
		nodelay(*p, TRUE);
	}
	return *p != NULL;
}

static void
free_proc(struct proc **pv)
{
	struct proc *p = *pv;
	if( p != NULL ) {
		free(p->tabs);
		if( p->pt >= 0 ) {
			close(p->pt);
			FD_CLR(p->pt, &fds);
		}
		delwinnul(&p->pri.win);
		delwinnul(&p->alt.win);
		free(p);
	}
	*pv = NULL;
}

void
focus(struct canvas *n, int reset)
{
	if( n ) {
		if( n != focused && reset ) {
			lastfocused = focused;
		}
		focused = n;
	} else {
		focused = view_root;
	}
}

static void
freecanvas(struct canvas *n)
{
	if( n ) {
		delwinnul(&n->wtit);
		delwinnul(&n->wdiv);
		delwinnul(&n->win);
		free_proc(&n->p);
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
draw_window(struct canvas *n)
{
	struct point o = n->origin;
	struct point e = { o.y + n->extent.y - 1, o.x + n->extent.x - 1 };
	if( e.y > 0 && e.x > 0 ) {
		WINDOW *w = n->win;
		struct point off = { 0, 0 };
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
		f->input = f->p->s->win;
		int show = binding != &cmd_keys && f->p->s->vis;
		curs_set(f->offset.y != f->p->s->tos ? 0 : show);

		getyx(f->input, y, x);
		y = MIN(MAX(y, f->p->s->tos), f->p->s->tos + f->extent.y);
		assert( y >= f->p->s->tos && y <= f->p->s->tos + f->extent.y );
	} else {
		f->input = f->win ? f->win : f->wtit ? f->wtit : f->wdiv;
	}
	assert(f->input || root == NULL);
	draw_window(f);
	wmove(f->input, y, x);
}

static const char *
getterm(void)
{
	const char *t = getenv("TERM");
	return t ? t : COLORS > 255 ? DEFAULT_COLOR_TERMINAL : DEFAULT_TERMINAL;
}

static int
new_screens(struct proc *p)
{
	int rows = MAX(LINES, scrollback_history);
	int cols = MAX(COLS, S.width);
	if( !p ) {
		return 0;
	}
	resize_pad(&p->pri.win, rows, cols);
	resize_pad(&p->alt.win, rows, cols);
	if( ! p->pri.win || !p->alt.win ) {
		return 0;
	}
	scrollok(p->pri.win, TRUE);
	scrollok(p->alt.win, TRUE);
	keypad(p->pri.win, TRUE);
	keypad(p->alt.win, TRUE);
	p->s = &p->pri;
	p->vp.p = p;
	setupevents(&p->vp);
	return 1;
}

static struct proc *
new_pty(int rows, int cols, struct canvas *c)
{
	rows = MAX(rows, scrollback_history);
	cols = MAX(cols, S.width);
	int count = 1;
	struct proc *p = calloc(1, sizeof *p + count + sizeof *p->c);
	if( p != NULL ) {
		p->ws = (struct winsize) {.ws_row = rows, .ws_col = cols};
		p->pid = forkpty(&p->pt, NULL, NULL, &p->ws);
		if( p->pid < 0 ) {
			show_error("forkpty");
			free(p);
			p = NULL;
		} else if( p->pid == 0 ) {
			const char *sh = getshell();
			setsid();
			signal(SIGCHLD, SIG_DFL);
			execl(sh, sh, NULL);
			show_error("execl");
			_exit(EXIT_FAILURE);
		} else if( p->pid > 0 ) {
			FD_SET(p->pt, &fds);
			maxfd = p->pt > maxfd ? p->pt : maxfd;
			fcntl(p->pt, F_SETFL, O_NONBLOCK);
			extend_tabs(p, p->tabstop = 8);
		}
		p->canvas_count = count;
		p->c[0] = c;
	}
	return p;
}

static int
prune(struct canvas *x, const char *arg)
{
	(void) arg;
	struct canvas *p = x->parent;
	struct canvas *dummy;
	struct canvas *del = x;
	int d = x->typ;
	struct canvas *n = x->c[d];
	struct canvas *o = x->c[!d];
	if( o && o->c[d] ) {
		x->split_point[!d] = 0.0;
		free_proc(&x->p);
		del = NULL;
	} else if( o ) {
		assert( o->c[d] == NULL );
		assert( o->parent == x );
		assert( o->typ != d );
		o->typ = d;
		o->parent = p;
		*(p ? &p->c[d] : &root) = o;
		o->c[d] = n;
		*(n ? &n->parent : &dummy) = o;
		o->origin = x->origin;
		o->split_point[d] = x->split_point[d];
	} else if( n ) {
		n->parent = p;
		n->origin = x->origin;
		*(p ? &p->c[d] : &root) = n;
	} else if( p ) {
		p->split_point[d] = 1.0;
		p->c[d] = NULL;
	} else {
		root = NULL;
	}
	freecanvas(del);
	if( x == focused ) {
		focus(o ? o : n ? n : p, 0);
	}
	if( view_root == x && del != NULL ) {
		view_root = o ? o : n ? n : p;
	}
	reshape(view_root, 0, 0, LINES, COLS);
	return 0;
}

#if 0

TODO: make this an action
static void
reshape_window(struct canvas *n)
{
	struct proc *p = n->p;
	int h = MAX(n->extent.y, scrollback_history);
	int w = MAX(n->extent.x, S.width);
	p->ws = (struct winsize) {.ws_row = h, .ws_col = w};
	resize_pad(&p->pri.win, h, w);
	resize_pad(&p->alt.win, h, w);
	p->pri.tos = n->offset.y = h - n->extent.y;
	assert( p->alt.tos == 0 );
	wsetscrreg(p->pri.win, 0, h - 1);
	wsetscrreg(p->alt.win, 0, h - 1);
	wrefresh(p->s->win);
	extend_tabs(p, p->tabstop);
	if( ioctl(p->pt, TIOCSWINSZ, &p->ws) ) {
		show_error("ioctl");
	}
	if( kill(p->pid, SIGWINCH) ) {
		show_error("kill");
	}
}
#endif

static void
reshape(struct canvas *n, int y, int x, int h, int w)
{
	if( n ) {
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
		n->extent.y = h1 - 1; /* Subtract one for title line */
		n->extent.x = w1;
		if( n->p && n->p->pt >= 0 ) {
			int h = MAX(n->extent.y, scrollback_history);
			n->p->pri.tos = n->offset.y = h - n->extent.y;
		} else if( w1 && h1 > 1 ) {
			resize_pad(&n->win, n->extent.y, n->extent.x);
			wbkgd(n->win, ACS_CKBOARD);
		}
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
		char t[128];
		struct point *o = &n->origin;
		size_t s = MAX(n->extent.x, (int)sizeof t);
		( r ? &wattron : &wattroff )(n->wtit, A_REVERSE);
		snprintf(t, s, "%d: %s ", n->p ? (int)n->p->pid : -1, n->title);
		mvwprintw(n->wtit, 0, 0, "%s", t);
		int len = strlen(t);
		if( n->extent.x - len > 0 ) {
			mvwhline(n->wtit, 0, len, ACS_HLINE, n->extent.x - len);
		}
		draw_pane(n->wtit, o->y + n->extent.y, o->x);
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
		int rev = binding == &cmd_keys && n == focused;
		draw(n->c[0]);
		draw(n->c[1]);
		draw_div(n, rev && !n->extent.x);
		draw_title(n, rev);
		draw_window(n);
	}
}

int
create(struct canvas *n, const char *arg)
{
	assert( n != NULL );
	int dir = arg && *arg == 'C' ? 1 : 0;
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
		new_screens(v->p = new_pty(LINES, COLS, v));
	}
	reshape(view_root, 0, 0, LINES, COLS);
	return 0;
}

static void
wait_child(struct canvas *n)
{
	int status, k;
	const char *fmt;
	if( waitpid(n->p->pid, &status, WNOHANG) == n->p->pid ) {
		if( WIFEXITED(status) ) {
			fmt = "exited %d";
			k = WEXITSTATUS(status);
		} else if( WIFSIGNALED(status) ) {
			fmt = "signal %d";
			k = WTERMSIG(status);
		}
		snprintf(n->title, sizeof n->title, fmt, k);
		free_proc(&n->p);
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
			if( n->no_prune ) {
				wait_child(n);
			} else {
				prune(n, NULL);
			}
			status = false;
		}
	}
	return status;
}

static void
scrollbottom(struct canvas *n)
{
	assert(n);
	if( n->p && n->p->s ) {
		n->offset.y = n->p->s->tos;
	}
}

int
digit(struct canvas *n, const char *arg)
{
	(void)n;
	cmd_count = 10 * (cmd_count == -1 ? 0 : cmd_count) + *arg - '0';
	return 0;
}

int
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
	}
	return 0;
}

int
scrolln(struct canvas *n, const char *arg)
{
	if( n && n->p && n->p->s && n->p->s->win ) {
		int count = cmd_count == -1 ? n->extent.y - 1 : cmd_count;
		n->offset.y += *arg == '-' ? -count : count;
		n->offset.y = MIN(MAX(0, n->offset.y), n->p->s->tos);
	}
	return 0;
}

static int
sendarrow(struct canvas *n, const char *k)
{
    char buf[100] = {0};
    snprintf(buf, sizeof(buf) - 1, "\033%s%s", n->p->pnm? "O" : "[", k);
    safewrite(n->p->pt, buf, strlen(buf));
    return 0;
}

int
reshape_root(struct canvas *n, const char *arg)
{
	(void)arg;
	reshape(view_root, 0, 0, LINES, COLS);
	scrollbottom(n);
	return 0;
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

int
resize(struct canvas *n, const char *arg)
{
	int typ = strchr("JK", *arg) ? 0 : 1;
	enum { down, up } dir = strchr("JL", *arg) ? down : up;
	if( dir == down ) while( n && n->c[dir] == NULL ) {
		n = n->parent;
	}
	if( !n || !n->c[typ] ) {
		return 1;
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
	reshape(view_root, 0, 0, LINES, COLS);
	return 0;
}

int
mov(struct canvas *n, const char *arg)
{
	assert( n == focused && n != NULL );
	char cmd = *arg;
	int count = cmd_count < 1 ? 1 : cmd_count;
	int startx = n->origin.x;
	int starty = n->origin.y + n->extent.y;
	struct canvas *t = n;
	if( cmd == 'p' ) {
		n = lastfocused;
	} else for( ; t && count--; n = t ? t : n ) {
		switch( cmd ) {
		case 'k': /* move up */
			t = find_window(view_root, t->origin.y - 1, startx);
			break;
		case 'j': /* move down */
			t = find_window(view_root,
				t->origin.y + t->extent.y + 1, startx);
			break;
		case 'l': /* move right */
			t = find_window(view_root, starty,
				t->origin.x + t->extent.x + 1);
			break;
		case 'h': /* move left */
			t = find_window(view_root, starty, t->origin.x - 1);
			break;
		}
	}
	focus(n, 1);
	return 0;
}

int
send_nul(struct canvas *n, const char *arg)
{
	(void) arg;
	safewrite(n->p->pt, "\x00", 1);
	scrollbottom(n);
	return 0;
}

int
send(struct canvas *n, const char *arg)
{
	if( n->p ) {
		if( n->p->lnm && *arg == '\r' ) {
			assert( arg[1] == '\0' );
			arg = "\r\n";
		}
		safewrite(n->p->pt, arg, strlen(arg));
		scrollbottom(n);
	}
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
equalize(struct canvas *n, const char *arg)
{
	(void) arg;
	assert( n != NULL );
	n = balance(n);
	reshape(view_root, 0, 0, LINES, COLS);
	return 0;
}

int
transition(struct canvas *n, const char *arg)
{
	binding = binding == &keys ? &cmd_keys : &keys;
	if( arg ) {
		send(n, arg);
	}
	if( binding == &keys ) {
		scrollbottom(n);
	} else {
		errmsg[0] = 0;
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
	b[k].act = act;
	va_list ap;
	va_start(ap, act);
	b[k].arg = va_arg(ap, const char *);
	if( b[k].arg != NULL ) {
		const char *n = va_arg(ap, const char *);
		assert( n == NULL );
	}
	va_end(ap);
}

int
new_tabstop(struct canvas *n, const char *arg)
{
	(void) arg;
	n->p->ntabs = 0;
	extend_tabs(n->p, n->p->tabstop = cmd_count > -1 ? cmd_count : 8);
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

	add_key(keys, S.commandkey, transition, NULL);
	add_key(keys, L'\r', send, "\r",  NULL);
	add_key(keys, L'\n', send, "\n", NULL);
	add_key(keys, 0, send_nul, NULL);

	add_key(cmd_keys, S.commandkey, transition, &S.commandkey, NULL);
	add_key(cmd_keys, L'\r', transition, NULL);
	/* TODO: rebind b,f,<,> to hjkl in different binding */
	add_key(cmd_keys, L'b', scrolln, "-", NULL);
	add_key(cmd_keys, L'f', scrolln, "+", NULL);
	add_key(cmd_keys, L'>', scrollh, ">", NULL);
	add_key(cmd_keys, L'<', scrollh, "<", NULL);

	add_key(cmd_keys, L'=', equalize, NULL);
	add_key(cmd_keys, L'c', create, NULL);
	add_key(cmd_keys, L'C', create, "C", NULL);
	add_key(cmd_keys, L'j', mov, "j", NULL);
	add_key(cmd_keys, L'k', mov, "k", NULL);
	add_key(cmd_keys, L'l', mov, "l", NULL);
	add_key(cmd_keys, L'h', mov, "h", NULL);
	add_key(cmd_keys, L'J', resize, "J", NULL);
	add_key(cmd_keys, L'K', resize, "K", NULL);
	add_key(cmd_keys, L'L', resize, "L", NULL);
	add_key(cmd_keys, L'H', resize, "H", NULL);
	add_key(cmd_keys, L'p', mov, "p", NULL);
	add_key(cmd_keys, L't', new_tabstop, NULL);
	add_key(cmd_keys, L'x', prune, NULL);
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
	int rv = 0;
	while( isspace(*k) ) {
		k += 1;
	}
	const char *space = strchr(k, ' ');
	char *c, *path = getenv("PATH");
	char name[PATH_MAX];
	size_t len = space ? (size_t)(space - k) : strlen(k);
	if( len == 0 ) {
		;
	} else if( !strncmp(k, "cd", 2) || !strncmp(k, "exec", 4) ) {
		rv = 1;
	} else if( (c = strchr(k, '/' )) && c < k + len ) {
		memcpy(name, k, len);
		name[len] = '\0';
		rv = access(k, X_OK) == 0;
	} else for( c = strchr(path, ':'); c && !rv; c = strchr(path, ':') ) {
		size_t n = c - path;
		if( n > sizeof name - 2 - len ) {
			/* horribly ill-formed PATH */
			return 0;
		}
		memcpy(name, path, n);
		name[n] = '/';
		memcpy(name + n + 1, k, len);
		name[n + len + 1] = '\0';
		path = c + 1;
		rv = !access(name, X_OK);
	}
	return rv;
}

static void
handlechar(int r, int k) /* Handle a single input character. */
{
	struct handler *b = NULL;
	struct canvas *n = focused;

	assert( r != ERR );
	if( r == OK && k > 0 && k < (int)sizeof *binding ) {
		unsigned len = strlen(n->putative_cmd);
		if( n->p == NULL ) {
			;
		} else if( k == '\r' ) {
			if( n->p->pt > -1 && is_command(n->putative_cmd) ) {
				strcpy(n->title, n->putative_cmd);
			}
			len = 0;
		} else if( len < sizeof n->putative_cmd - 1 && isprint(k) ) {
			n->putative_cmd[len++] = k;
		}
		n->putative_cmd[len] = '\0';
		b = &(*binding)[k];
	} else if( r == KEY_CODE_YES ) {
		if( k >= KEY_MIN && k <= KEY_MAX ) {
			b = &code_keys[k - KEY_MIN];
		}
	}

	if( b && b->act ) {
		b->act(n, b->arg);
	} else {
		char c[MB_LEN_MAX + 1] = {0};
		if( wctomb(c, k) > 0 && n->p ) {
			scrollbottom(n);
			safewrite(n->p->pt, c, strlen(c));
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
		if( errmsg[0] ) {
			int y = LINES - 1, x = MIN(winsiz(werr, 1), COLS);
			mvwprintw(werr, 0, 0, "%s", errmsg);
			pnoutrefresh(werr, 0, 0, y, 0, y, x);
		}
		fixcursor();
		doupdate();
		if( select(maxfd + 1, &sfds, NULL, NULL, NULL) < 0 ) {
			FD_ZERO(&sfds);
		}
		while( (r = wget_wch(focused->input, &w)) != ERR ) {
			handlechar(r, w);
			fixcursor();
		}
		getinput(root, &sfds);
	}
}

static void
parse_args(int argc, char *const*argv)
{
	int c;
	char *name = strrchr(argv[0], '/');
	while( (c = getopt(argc, argv, ":c:hs:T:t:w:")) != -1 ) {
		switch( c ) {
		case 'h':
			printf("usage: %s"
				" [-c ctrl-key]"
				" [-s history-size]"
				" [-T NAME]"
				" [-t NAME]"
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
		case 'T':
		case 't':
			setenv("TERM", optarg, 1);
			break;
		case 'w':
			S.width = strtol(optarg, NULL, 10);
			break;
		default:
			fprintf(stderr, "Unknown option: %c\n", optopt);
			exit(EXIT_FAILURE);
		}
	}
}

static void
intenv(const char *name, int v)
{
	if( v ) {
		char buf[16];
		snprintf(buf, sizeof buf - 1, "%d", v);
		setenv(name, buf, 1);
	} else {
		unsetenv(name);
	}
}

struct canvas *
init(void)
{
	struct canvas *b = NULL;
	FD_SET(maxfd, &fds);
	intenv("SMTX", (int)getpid());
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
	if( !resize_pad(&werr, 1, sizeof errmsg) ) {
		warnx("Unable to create error window");
	} else {
		wattron(werr, A_REVERSE);
		b = newcanvas();
		if( !b || !new_screens(b->p = new_pty(LINES, COLS, b)) ) {
			warnx("Unable to create root window");
		}
		reshape(b, 0, 0, LINES, COLS);
		focus(b, 0);
	}
	return view_root = root = b;
}

int
smtx_main(int argc, char *const argv[])
{
	S.commandkey = CTL('g'); /* Change at runtime with -c */
	S.width = 80;
	parse_args(argc, argv);
	init();
	main_loop();
	endwin();
	return EXIT_SUCCESS;
}
