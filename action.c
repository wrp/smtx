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

void
attach(const char *arg)
{
	struct canvas *n = S.f;
	int target = arg ? strtol(arg, NULL, 10) : S.count;
	for( struct pty *t = S.p; t; t = t->next ) {
		if( t->id == target ) {
			n->p = t;
			S.reshape = 1;
			return;
		}
	}
	check(0, "No pty exists with id %d", target);
}

void
bad_key(const char *arg)
{
	(void)arg;
	beep();
}

void
create(const char *arg)
{
	struct canvas *n = S.f;
	int dir = arg && *arg == 'C' ? 1 : 0;
	while( n && n->c[dir] != NULL ) {
		n = n->c[dir]; /* Split last window in a chain. */
	}
	for( int count = S.count < 1 ? 1 : S.count; count; count -= 1 ) {
		struct canvas *v = *( n ? &n->c[dir] : &S.c ) = newcanvas();
		if( v != NULL ) {
			v->typ = dir;
			v->parent = n;
			n = v;
		}
	}
	balance(n);
	reshape_root(NULL);
	if( n ) {
		struct screen*s = n->p->s;
		wmove(s->win, s->c.y = n->offset.y, s->c.x = 0);
	}
}

void
digit(const char *arg)
{
	S.count = 10 * (S.count == -1 ? 0 : S.count) + *arg - '0';
}

void
equalize(const char *arg)
{
	(void)arg;
	balance(S.f);
	S.reshape = 1;
}

static struct canvas *
find_canvas(struct canvas *c, int id)
{
	struct canvas *r = NULL;
	if( c ) {
		if( c->p->id == id ) {
			r = c;
		} else if( (r = find_canvas( c->c[0], id)) == NULL ) {
			r = find_canvas( c->c[1], id);
		}
	}
	return r;
}

static void
pty_size(struct pty *p)
{
	check(p->fd == -1 || ! ioctl(p->fd, TIOCGWINSZ, &p->ws),
		"ioctl error getting size of pty %d", p->id);
}

void
new_tabstop(const char *arg)
{
	struct canvas *n = S.f;
	int c = arg ? strtol(arg, NULL, 10) : S.count > -1 ? S.count : 8;
	n->p->ntabs = 0;
	pty_size(n->p); /* Update n->p->ws */
	extend_tabs(n->p, n->p->tabstop = c);
}

void
quit(const char *arg)
{
	struct canvas *n = S.f;
	pid_t p = n->p->pid;
	int s = S.count == -1 ? SIGINT : S.count;
	(void)arg;
	switch( s ) {
	case SIGKILL: case SIGTERM: case SIGUSR1: case SIGHUP:
	case SIGUSR2: case SIGINT:
		if( p != -1 ) {
			check(kill(-p, s) != -1, "kill %d, %d", p, s);
		} else {
			prune("X");
		}
		break;
	default:
		check(0, "invalid signal: %d", s);
	}
}

static void set_pty_history(struct pty *p, int siz);

void
reshape_root(const char *arg)
{
	(void)arg;
	if( LINES > S.history ) {
		S.history = LINES;
	}
	for( struct pty *p = S.p; p; p = p->next ) {
		p->ws.ws_row = 0;
		set_pty_history(p, LINES);
	}
	resize_pad(&S.werr, 1, COLS);
	resize_pad(&S.wbkg, LINES, COLS);
	reshape(S.c, 0, 0, LINES, COLS);
	S.reshape = 0;
}

void
resize(const char *arg)
{
	struct canvas *n = S.f;
	int typ = strchr("JK", *arg) ? 0 : 1;
	int dir = strchr("JL", *arg) ? 1 : -1;
	int count = S.count < 1 ? 1 : S.count;
	int s = *(typ ? &n->extent.x : &n->extent.y) + 1;

	while( n && n->c[typ] == NULL ) {
		n = n->parent;
	}
	double split = !n ? 0 : typ ? n->split.x : n->split.y;
	if( !n || !n->c[typ] || split == 0 || s < 1) {
		return;
	}
	double full = s / split;
	double new = s + count * dir;
	if( new > 0 ) {
		*(typ ? &n->split.x : &n->split.y) = MIN(new / full, 1.0);
	} else {
		*(typ ? &n->split.x : &n->split.y) = 0.0;
		focus(S.c);
	}
	S.reshape = 1;
}

void
scrollh(const char *arg)
{
	struct canvas *n = S.f;
	if( n && n->p && n->p->s && n->p->s->win ) {
		int c = S.count, count, x;
		getmaxyx(n->p->s->win, count, x);
		count = c == -1 ? n->p->tabstop : c == 0 ? n->extent.x : c;
		n->offset.x += *arg == '<' ? -count : count;
		if( n->offset.x < 0 ) {
			n->offset.x = 0;
		} else if( n->offset.x > x - n->extent.x ) {
			n->offset.x = x - n->extent.x;
		}
		n->manualscroll = 1;
	}
}

void
scrolln(const char *arg)
{
	struct canvas *n = S.f;
	if( n && n->p && n->p->s && n->p->s->win ) {
		int y, x;
		int count = S.count == -1 ? n->extent.y - 1 : S.count;
		getmaxyx(n->p->s->win, y, x);
		(void)x;
		int top = y - n->extent.y;
		n->offset.y += *arg == '-' ? -count : count;
		n->offset.y = MIN(MAX(0, n->offset.y), top);
	}
}

void
send(const char *arg)
{
	struct canvas *n = S.f;
	if( n->p && n->p->fd > 0 && arg ) {
		if( n->p->lnm ) {
			const char *s;
			while( (s = strchr(arg, '\r')) != NULL ) {
				rewrite(n->p->fd, arg, s - arg);
				rewrite(n->p->fd, "\r\n", 2);
				arg = s + 1;
			}
		}
		rewrite(n->p->fd, arg, strlen(arg));
		scrollbottom(n);
	}
}

static int
replacewin(WINDOW **src, int rows, int cols, int delta)
{
	int rv = -1;
	WINDOW *new = NULL;
	if( resize_pad(&new, rows, cols) == 0 ) {
		copywin(*src, new, 0, 0, delta, 0, rows - 1, cols - 1, 1);
		delwin(*src);
		*src = new;
		rv = 0;
	}
	return rv;
}

static void
set_pty_history(struct pty *p, int siz)
{
	struct screen *w[] = { &p->pri, &p->alt, NULL };
	for( struct screen **sp = w; *sp; sp++ ) {
		struct screen *s = *sp;
		int my, x;
		getmaxyx(s->win, my, x);

		if( my < siz ) {
			WINDOW *orig = s->win;
			int y;

			getyx(s->win, y, x);
			replacewin(&s->win, siz, p->ws.ws_col, siz - my);

			if( orig != s->win ) {
				wmove(s->win, s->c.y = y + siz - my, x);
			}
		}
	}
}

void
set_history(const char *arg)
{
	struct canvas *n = S.f;
	struct pty *p = n->p;
	S.history = arg ? strtol(arg, NULL, 10) : S.count;
	set_pty_history(p, S.history);
	S.reshape = 1;
}

void
set_width(const char *arg)
{
	struct canvas *n = S.f;
	struct pty *p = n->p;
	int w = arg ? strtol(arg, NULL, 10) : S.count;
	if( w == -1 ) {
		w = n->extent.x;
	}
	if( p->fd > 0 && (pty_size(p), w != p->ws.ws_col) ) {
		int py, ay, x;
		getmaxyx(p->pri.win, py, x);
		getmaxyx(p->alt.win, ay, x);
		(void)x;
		assert( py >= n->extent.y );
		assert( ay >= n->extent.y );
		p->ws.ws_col = w;
		resize_pad(&p->pri.win, py, w);
		resize_pad(&p->alt.win, ay, w);
		extend_tabs(p, p->tabstop);
		reshape_window(p);
	}
}

void
swap(const char *arg)
{
	struct canvas *n = S.f;
	struct canvas *t;
	(void) arg;
	if( S.count == -1 ) {
		t = n->c[n->typ];
		t = t ? t : n->c[!n->typ];
		t = t ? t : n->parent;
	} else {
		t = find_canvas(S.c, S.count);
	}
	if( t ) {
		struct pty *tmp = n->p;
		n->p = t->p;
		t->p = tmp;
		S.reshape = 1;
	} else {
		check(0, "Cannot find target canvas");
	}
}

void
transition(const char *arg)
{
	if( *arg == '*' ) {
		rewrite(S.f->p->fd, &S.ctlkey, 1);
		arg += 1;
	}
	S.errmsg[0] = 0; /* Clear any existing error message */
	if( ! strcmp(arg, "enter") ) {
		S.mode = enter;
	} else if( ! strcmp(arg, "control") ) {
		S.mode = control;
	}
	scrollbottom(S.f);
}
