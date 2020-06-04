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
     Use current cursor position to set bottom of screen when splitting
     Copy-mode, with stack of registers and ability to edit.
       Or, maybe just have a binding (s) that dumps the current content
       of the scrollback region to a file like ~/.smtx-pid-timestamp
       Perhaps use (e) to edit the file (eg, spawn $EDITOR), then (p)
       to paste it.
     Ability to set titles.
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
static void reshape(struct canvas *n, int y, int x, int h, int w);

const char *term = NULL;

void
safewrite(int fd, const char *b, size_t n)
{
	ssize_t s;
	const char *e = b + n;
	while( b < e ) {
		s = write(fd, b, e - b);
		if( s == -1 && errno != EINTR ) {
			fprintf(stderr, "write to fd %d: %s", fd,
				strerror(errno));
			return;
		} else if( s > 0 ) {
			b += s;
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
	int w = p->ws.ws_col;
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
	if( n != NULL ) {
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
	return *p ? wresize(*p, h, w ) == OK : (*p = newpad(h, w)) != NULL;
}

static void
free_proc(struct proc *p)
{
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
}

void
focus(struct canvas *n, int reset)
{
	if( n && n->p && n->p->s && n->p->s->win ) {
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
		delwinnul(&n->wpty);
		free_proc(n->p);
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
	if( n->wpty ) {
		struct screen *s = n->p->s;
		pnoutrefresh(s->win, s->off, 0, n->origin.y, n->origin.x,
			s->off + winsiz(n->wpty, 0) - 1,
			n->origin.x + winsiz(n->wpty, 1) - 1
		);
	}
}

static void
fixcursor(void) /* Move the terminal cursor to the active window. */
{
	struct proc *p = focused->p;
	assert( p && p->s );
	int show = binding != &cmd_keys && p->s->vis;
	curs_set(p->s->off != p->s->tos ? 0 : show);

	int x, y;
	getyx(p->s->win, y, x);
	y = MIN(MAX(y, p->s->tos), winsiz(p->s->win, 0) + 1);

	assert( p->ws.ws_row == winsiz(p->s->win, 0) - p->s->tos );

	wmove(p->s->win, y, x);
	wmove(focused->wpty, y, x);
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
	if( !n ) {
		return 0;
	}
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

static struct proc *
new_pty()
{
	struct proc *p = calloc(1, sizeof *p);
	if( p != NULL ) {
		p->ws = (struct winsize) {.ws_row = 24, .ws_col = 80};
		p->pid = forkpty(&p->pt, NULL, NULL, &p->ws);
		if( p->pid < 0 ) {
			perror("forkpty");
			free(p);
			p = NULL;
		} else if( p->pid == 0 ) {
			const char *sh = getshell();
			setsid();
			signal(SIGCHLD, SIG_DFL);
			execl(sh, sh, NULL);
			perror("execl");
			_exit(EXIT_FAILURE);
		} else if( p->pid > 0 ) {
			FD_SET(p->pt, &fds);
			maxfd = p->pt > maxfd ? p->pt : maxfd;
			fcntl(p->pt, F_SETFL, O_NONBLOCK);
			extend_tabs(p, p->tabstop = 8);
		}
	}
	return p;
}

static int
prune(struct canvas *x, const char **args)
{
	(void) args;
	struct canvas *p = x->parent;
	struct canvas *dummy;
	struct canvas *del = x;
	int d = x->typ;
	struct canvas *n = x->c[d];
	struct canvas *o = x->c[!d];
	if( o && o->c[d] ) {
		x->split_point[!d] = 0.0;
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

static void
reshape_window(struct canvas *N, int h, int w)
{
	struct proc *n = N->p;

	if( h > 1 && w > 0 ) {
		resize_pad(&N->wpty, h - 1, w);
	} else {
		delwinnul(&N->wpty);
	}
	if( n->pt >= 0 ) {
		h = h > 1 ? h - 1 : 24;
		w = w > 0 ? w : 80;
		n->ws = (struct winsize) {.ws_row = h, .ws_col = w};
		resize_pad(&n->pri.win, MAX(h, scrollback_history), w);
		resize_pad(&n->alt.win, h, w);
		n->pri.tos = n->pri.off = MAX(0, scrollback_history - h);
		n->alt.tos = n->alt.off = 0;
		wsetscrreg(n->pri.win, 0, MAX(scrollback_history, h) - 1);
		wsetscrreg(n->alt.win, 0, h - 1);
		wrefresh(n->s->win);
		extend_tabs(n, n->tabstop);
		if( ioctl(n->pt, TIOCSWINSZ, &n->ws) ) {
			perror("ioctl");
		}
		if( kill(n->pid, SIGWINCH) ) {
			perror("kill");
		}
	}
}

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
		reshape_window(n, h1, w1);
	}
}

static void
draw_pane(WINDOW *w, int y, int x)
{
	pnoutrefresh(w, 0, 0, y, x, y + winsiz(w, 0) - 1, x + winsiz(w, 1) - 1);
}

static void
draw_title(struct canvas *n)
{
	if( n->wtit ) {
		char t[128];
		int x = winsiz(n->wpty, 1);
		size_t s = MAX(x - 1, (int)sizeof t);
		if( binding == &cmd_keys && n == focused ) {
			wattron(n->wtit, A_REVERSE);
		} else {
			wattroff(n->wtit, A_REVERSE);
		}
		snprintf(t, s, "%d: %s ", (int)n->p->pid, n->title);
		/*
		snprintf(t, s, "%d: %d,%d %d,%d %d,%d", (int)n->p->pid,
			n->origin.y, n->origin.x,
			n->x.y, n->x.x, n->siz.y, n->siz.x);
		*/
		mvwprintw(n->wtit, 0, 0, "%s", t);
		int len = strlen(t);
		if( x - len > 0 ) {
			mvwhline(n->wtit, 0, len, ACS_HLINE, x - len);
		}
		assert( n->p->ws.ws_row == winsiz(n->wpty, 0) );
		draw_pane(n->wtit, n->origin.y + winsiz(n->wpty, 0),
			n->origin.x);
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
			getmaxyx(n->p->s->win, y, x);
			mvwvline(n->wdiv, 0, 0, ACS_VLINE, y);
			draw_pane(n->wdiv, n->origin.y, n->origin.x + x);
		}
		draw_title(n);
		draw_window(n);
	}
}

int
create(struct canvas *n, const char *args[])
{
	assert( n != NULL );
	int dir = *args && **args == 'C' ? 1 : 0;
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
		new_screens(v->p = new_pty());
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
		free_proc(n->p);
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
	} else if( n && n->p->pt > 0 && FD_ISSET(n->p->pt, f) ) {
		char iobuf[BUFSIZ];
		ssize_t r = read(n->p->pt, iobuf, sizeof(iobuf));
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
	if( n && n->p->s ) {
		n->p->s->off = n->p->s->tos;
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
	if( n && n->p->s && n->p->s->win ) {
		int y, x;
		getmaxyx(n->p->s->win, y, x);
		(void) x;
		int count = cmd_count == -1 ? (y - n->p->s->tos) - 1 : cmd_count;
		if( args[0][0] == '-' ) {
			n->p->s->off = MAX(0, n->p->s->off - count);
		} else {
			n->p->s->off = MIN(n->p->s->tos, n->p->s->off + count);
		}
	}
	return 0;
}

static int
sendarrow(struct canvas *n, const char **args)
{
	const char *k = args[0];
    char buf[100] = {0};
    snprintf(buf, sizeof(buf) - 1, "\033%s%s", n->p->pnm? "O" : "[", k);
    safewrite(n->p->pt, buf, strlen(buf));
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

int
contains(struct canvas *n, int y, int x)
{
	int y1, x1;
	getmaxyx(n->p->s->win, y1, x1);
	return
		y >= n->origin.y && y <= n->origin.y + y1 - n->p->s->tos + 1 &&
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
	int count = cmd_count < 1 ? 1 : cmd_count;
	int startx = n->origin.x;
	int starty = n->origin.y + winsiz(n->p->s->win, 0) - n->p->s->tos;
	struct canvas *t = n;
	if( cmd == 'p' ) {
		n = lastfocused;
	} else for( ; t && count--; n = t ? t : n ) {
		int y, x;
		getmaxyx(t->p->s->win, y, x);
		switch( cmd ) {
		case 'k': /* move up */
			t = find_window(view_root, t->origin.y - 1,
				startx);
			break;
		case 'j': /* move down */
			t = find_window(view_root,
				t->origin.y + y - t->p->s->tos + 2,
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
	if( n->p->lnm && args[0][0] == '\r' ) {
		assert( args[0][1] == '\0' );
		assert( args[1] == NULL );
		args[0] = "\r\n";
	}
	size_t len = args[1] ? strtoul(args[1], NULL, 10 ) : strlen(args[0]);
	safewrite(n->p->pt, args[0], len);
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
	n = balance(n);
	reshape(view_root, 0, 0, LINES, COLS);
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
		if( k == '\r' ) {
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
		assert( k >= KEY_MIN && k <= KEY_MAX );
		b = &code_keys[k - KEY_MIN];
	}

	if( b && b->act ) {
		b->act(n, b->args);
	} else {
		char c[MB_LEN_MAX + 1] = {0};
		if( wctomb(c, k) > 0 ) {
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
		draw_window(focused);
		fixcursor();
		doupdate();
		if( select(maxfd + 1, &sfds, NULL, NULL, NULL) < 0 ) {
			FD_ZERO(&sfds);
		}
		while( (r = wget_wch(focused->p->s->win, &w)) != ERR ) {
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
smtx_main(int argc, char *const*argv)
{
	char buf[32];
	FD_SET(maxfd, &fds);
	snprintf(buf, sizeof buf - 1, "%lu", (unsigned long)getpid());
	setenv("SMTX", buf, 1);
	setenv("TERM", getterm(), 1);
	setenv("SMTX_VERSION", VERSION, 1);
	unsetenv("COLUMNS");
	unsetenv("LINES");
	setlocale(LC_ALL, "");
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

	view_root = root = newcanvas();
	if( !root || !new_screens(root->p = new_pty()) ) {
		err(EXIT_FAILURE, "Unable to create root window");
	}
	reshape(view_root, 0, 0, LINES, COLS);
	focus(view_root, 0);
	main_loop();
	endwin();
	return EXIT_SUCCESS;
}
