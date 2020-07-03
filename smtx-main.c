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
     make tput rep work.  eg, tput rep w 5 should write 5 'w' to term,
       but the parameters do not seem to be getting sent properly.  We get
       argc == 1 and argv[0] == 5 - 1, but the w is chomped.  Note that this
       is the only terminfo entry that uses %c, and I suspect there is a bug
       in vtparser
     In full-screen mode, make hjkl scroll the screen.
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

static struct state S = {
	.commandkey = CTL('g'),
	.width = 80,
	.display_level = UINT_MAX
};
static struct handler keys[128];
static struct handler cmd_keys[128];
static struct handler code_keys[KEY_MAX - KEY_MIN + 1];
static struct handler (*binding)[128] = &keys;
static int maxfd = STDIN_FILENO;
static fd_set fds;
static WINDOW *werr;
static char errmsg[80];
static struct canvas *root;
static struct canvas *view_root;

/* Variables exposed to test suite */
struct canvas *focused;
int cmd_count = -1;
int scrollback_history = 1024;

static void
set_errmsg(const char *fmt, ...)
{
	int e = errno;
	va_list ap;
	va_start(ap, fmt);
	int k = vsnprintf(errmsg, sizeof errmsg, fmt, ap);
	va_end(ap);
	if( e && k < (int)sizeof errmsg ) {
		snprintf(errmsg + k, sizeof errmsg - k, ": %s", strerror(e));
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
extend_tabs(struct proc *p, int tabstop)
{
	int w = p->ws.ws_col;
	if( p->ntabs < w ) {
		typeof(*p->tabs) *n = realloc(p->tabs, w * sizeof *n);
		for( p->tabs = n; n && p->ntabs < w; p->ntabs++ ) {
			p->tabs[p->ntabs] = p->ntabs % tabstop == 0;
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
new_screens(struct proc *p)
{
	int rows = MAX(LINES, scrollback_history);
	int cols = MAX(COLS, S.width);
	if( !p ) {
		return -1;
	}
	resize_pad(&p->pri.win, rows, cols);
	resize_pad(&p->alt.win, rows, cols);
	if( ! p->pri.win || !p->alt.win ) {
		delwinnul(&p->pri.win);
		delwinnul(&p->alt.win);
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

static struct proc *
new_pty(int rows, int cols)
{
	struct proc *p = calloc(1, sizeof *p);
	if( p != NULL ) {
		p->ws.ws_row = rows;
		p->ws.ws_col = MAX(cols, S.width);
		p->pid = forkpty(&p->pt, NULL, NULL, &p->ws);
		if( p->pid < 0 ) {
			set_errmsg("forkpty");
			free(p);
			p = NULL;
		} else if( p->pid == 0 ) {
			const char *sh = getshell();
			setsid();
			signal(SIGCHLD, SIG_DFL);
			execl(sh, sh, NULL);
			err(EXIT_FAILURE, "exec SHELL='%s'", sh);
		} else if( p->pid > 0 ) {
			FD_SET(p->pt, &fds);
			maxfd = p->pt > maxfd ? p->pt : maxfd;
			fcntl(p->pt, F_SETFL, O_NONBLOCK);
			extend_tabs(p, p->tabstop = 8);
		}
	}
	return p;
}

static struct canvas *
newcanvas(void)
{
	struct canvas *n = calloc(1, sizeof *n);
	if( !n ) {
		set_errmsg("calloc");
	} else {
		n->split_point[0] = 1.0;
		n->split_point[1] = 1.0;
		strncpy(n->title, getshell(), sizeof n->title);
		n->title[sizeof n->title - 1] = '\0';
		n->p = new_pty(LINES, COLS);
		if( new_screens(n->p) == -1 ) {
			free(n->p);
			free(n);
			n = NULL;
		}
		n->input = n->p->pri.win;
	}
	return n;
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

static void
focus(struct canvas *n)
{
	focused = n ? n : view_root;
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
		int y, x;
		WINDOW *w = n->win;
		struct point off = { 0, 0 };
		getyx(n->input, y, x);
		(void)y;
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
		int show = binding != &cmd_keys && f->p->s->vis;
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
	assert(f->input || root == NULL);
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
	struct proc *p = n->p;
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
			if( changed ) {
				reshape_window(n, "h");
			}
			scrollbottom(n);
		} else if( w1 && h1 > 1 ) {
			resize_pad(&n->win, n->extent.y, n->extent.x);
			wbkgd(n->win, ACS_CKBOARD);
		}
	}
}

static void
reshape_root(struct canvas *n, const char *arg)
{
	(void)arg;
	(void)n;
	reshape(root, 0, 0, LINES, COLS, 1);
}

static void
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
		focus(o ? o : n ? n : p);
	}
	if( view_root == x && del != NULL ) {
		view_root = o ? o : n ? n : p;
	}
	reshape_root(NULL, NULL);
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
		int pid = n->p ? n->p->pid : -1;
		struct point o = n->origin;
		( r ? &wattron : &wattroff )(n->wtit, A_REVERSE);
		mvwprintw(n->wtit, 0, 0, "%d %d-%d/%d %s",
			pid,
			n->offset.x + 1,
			n->offset.x + n->extent.x,
			n->p->ws.ws_col,
			n->title);
		whline(n->wtit, ACS_HLINE, n->extent.x);
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
		int rev = binding == &cmd_keys && n == focused;
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
	return n ? n : root;
}

void
create(struct canvas *n, const char *arg)
{
	int dir = arg && *arg == 'C' ? 1 : 0;
	/* Always split last window in a chain */
	while( n && n->c[dir] != NULL ) {
		n = n->c[dir];
	}
	struct canvas *v = *( n ? &n->c[dir] : &root) = newcanvas();
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
			fmt = "exited %d";
			k = WEXITSTATUS(status);
		} else if( WIFSIGNALED(status) ) {
			fmt = "signal %d";
			k = WTERMSIG(status);
		} else {
			fmt = "stopped";
			assert( WIFSTOPPED(status) );
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
digit(struct canvas *n, const char *arg)
{
	(void)n;
	cmd_count = 10 * (cmd_count == -1 ? 0 : cmd_count) + *arg - '0';
}

static void
set_view_count(struct canvas *n, const char *arg)
{
	(void)arg;
	view_root = n;
	switch( cmd_count ) {
	case 0:
		view_root = root;
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
    char buf[100] = {0};
    snprintf(buf, sizeof(buf) - 1, "\033%s%s", n->p->pnm? "O" : "[", k);
    rewrite(n->p->pt, buf, strlen(buf));
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
	if( binding == &keys ) {
		binding = &cmd_keys;
	} else {
		binding = &keys;
		errmsg[0] = 0;
		scrollbottom(n);
	}
	send(n, arg);
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

void
new_tabstop(struct canvas *n, const char *arg)
{
	(void) arg;
	n->p->ntabs = 0;
	extend_tabs(n->p, n->p->tabstop = cmd_count > -1 ? cmd_count : 8);
}

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
	add_key(cmd_keys, L't', new_tabstop, NULL);
	add_key(cmd_keys, L'W', reshape_window, "hw", NULL);
	add_key(cmd_keys, L'v', set_view_count, NULL);
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

static void
handlechar(int r, int k) /* Handle a single input character. */
{
	struct handler *b = NULL;
	struct canvas *n = focused;

	assert( r != ERR );
	if( r == OK && k > 0 && k < (int)sizeof *binding ) {
		b = &(*binding)[k];
	} else if( r == KEY_CODE_YES ) {
		if( k >= KEY_MIN && k <= KEY_MAX ) {
			b = &code_keys[k - KEY_MIN];
		}
	}

	if( b && b->act ) {
		b->act(n, b->arg);
	} else if( S.mode == passthru ) {
		char c[MB_LEN_MAX + 1] = {0};
		if( wctomb(c, k) > 0 && n->p ) {
			scrollbottom(n);
			(void)rewrite(n->p->pt, c, strlen(c));
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


struct canvas *
init(void)
{
	char buf[16];
	FD_SET(maxfd, &fds);
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
	resize_pad(&werr, 1, sizeof errmsg);
	if( werr == NULL ) {
		errx(EXIT_FAILURE, "Unable to create error window");
	}
	wattron(werr, A_REVERSE);
	create(NULL, NULL);
	if( ( focused = view_root = root ) == NULL ) {
		errx(EXIT_FAILURE, "Unable to create root window: %s", errmsg);
	}
	return root;
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
