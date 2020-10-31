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

int
attach(void)
{
	struct canvas *n = S.f;
	assert( n->p->count > 0 );
	for( struct pty *t = S.p; t; t = t->next ) {
		if( t->fd - 2 == S.count ) {
			(n->p    )->count -= 1;
			(n->p = t)->count += 1;
			return S.reshape = 1;
		}
	}
	return check(0, "No pty exists with id %d", S.count);
}

static const char *default_balance_arg[] = { "-|", "-", "|", "-|" };
void
balance(const char *arg)
{
	int c = MIN(S.count + 1, 3);
	for( arg = (*arg == '=') ? default_balance_arg[c]: arg; *arg; arg++ ) {
		int d = *arg == '|', count = 0;
		for( struct canvas *n = S.f; n; n = n->c[d] ) {
			count += 1;
		}
		for( struct canvas *n = S.f; n; n = n->c[d] ) {
			*(d ? &n->split.x : &n->split.y) = 1.0 / count--;
		}
	}
	S.reshape = 1;
}

void
create(const char *arg)
{
	int dir = *arg == '|';
	struct canvas *n = S.f, *c = n->c[dir], dummy;
	for( int count = S.count < 1 ? 1 : S.count; count; count -= 1 ) {
		struct canvas *v = n->c[dir] = newcanvas(0, n);
		if( n->c[!dir] == NULL ) {
			n->typ = dir;
		}
		if( v != NULL ) {
			v->c[dir] = c;
			(c ? c : &dummy)->parent = v;
			n = v;
		}
	}
	n->c[dir] = c;
	balance(arg);
	reshape_root(); /* (1) */
	wmove(n->p->s->win, n->p->s->c.y = n->offset.y, n->p->s->c.x = 0);
}
/* (1): TODO: for some reason, it is not sufficient to call reshape()
 * here.  We need to understand why that is.
 */

void
digit(const char *arg)
{
	S.count = 10 * (S.count == -1 ? 0 : S.count) + *arg - '0';
}

static struct canvas *
find_canvas(struct canvas *c, int id)
{
	struct canvas *r = NULL;
	if( c ) {
		if( c->p->fd == id + 2 ) {
			r = c;
		} else if( (r = find_canvas(c->c[0], id)) == NULL ) {
			r = find_canvas(c->c[1], id);
		}
	}
	return r;
}

static void
pty_size(struct pty *p)
{
	check(p->fd == -1 || ! ioctl(p->fd, TIOCGWINSZ, &p->ws),
		"ioctl error getting size of pty %d", p->fd - 2);
}

int
new_tabstop(void)
{
	S.f->p->ntabs = 0;
	pty_size(S.f->p); /* Update S.f->p->ws */
	extend_tabs(S.f->p, S.f->p->tabstop = S.count > -1 ? S.count : 8);
	return 1;
}

void
passthru(const char *arg)
{
	if( S.f->p && S.f->p->fd > 0 && arg[0] > 0 ) {
		scrollbottom(S.f);
		rewrite(S.f->p->fd, arg + 1, arg[0]);
	}
}

int
prune(void)
{
	if( S.count == 9 ) {
		S.c = NULL;
	} else if( S.f && S.f->parent ) {
		struct canvas *p = S.f->parent;
		int c = S.f == p->c[1];
		*(c ? &p->split.x : &p->split.y) = 1.0;
		p->c[c] = NULL;
		freecanvas(S.f);
		S.f = p;
	}
	return S.reshape = 1;
}

static void set_pty_history(struct pty *p, int siz);

int
reshape_root(void)
{
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
	return S.reshape = 0;
}

void
resize(const char *arg)
{
	struct canvas *n = S.f, *child = S.f->c[!strchr("-", *arg)];
	double *s = strchr("-", *arg) ? &n->split.y : &n->split.x;
	int count = S.count < 0 ? 50 : S.count > 100 ? 100 : S.count;
	*s = child ? count / 100.0 : 1.0;
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

static void
set_pty_history(struct pty *p, int siz)
{
	struct screen *w[] = { &p->pri, &p->alt, NULL };
	for( struct screen **sp = w; *sp; sp++ ) {
		struct screen *s = *sp;
		int my, x;
		getmaxyx(s->win, my, x);

		if( my < siz ) {
			int y;
			getyx(s->win, y, x);
			WINDOW *new = NULL;
			if( resize_pad(&new, siz, p->ws.ws_col) ) {
				copywin(s->win, new, 0, 0, siz - my, 0, siz - 1,
					p->ws.ws_col - 1, 1);
				delwin(s->win);
				s->win = new;
				wmove(s->win, s->c.y = y + siz - my, x);
			}
		}
	}
}

int
set_history(void)
{
	struct canvas *n = S.f;
	struct pty *p = n->p;
	S.history = MAX(LINES, S.count);
	set_pty_history(p, S.history);
	return S.reshape = 1;
}

int
set_layout(void)
{
	const char *layouts[] = {
		"1:1",                                /* 0 */
		"1:1",                                /* 1 */
		".5:1 1:1",                           /* 2 */
		"1:.5 .5:1 1:1",                      /* 3 */
		".5:.5 .5:1 1:.5 1:1",                /* 4 */
		"1:.5 .25:1 .5:1 .75:1 1:1",          /* 5 */
		".5:1 .75:.5 .75:1 1:.33 1:.66 1:1",  /* 6 */
		".5:1 1:.33 1:.666 1:1",              /* 7 */
	};
	build_layout(layouts[S.count >= 0 && S.count < 8 ? S.count : 0]);
	return 1;
}

void
set_width(const char *arg)
{
	struct canvas *n = S.f;
	struct pty *p = n->p;
	int w = *arg ? strtol(arg, NULL, 10) : S.count;
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

int
swap(void)
{
	struct canvas *n = S.f;
	struct canvas *t;
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
	return 1;
}

void
transition(const char *arg)
{
	if( *arg == '*' ) {
		rewrite(S.f->p->fd, (char *)&S.ctlkey, 1);
		arg += 1;
	}
	S.errmsg[0] = 0; /* Clear any existing error message */
	if( ! strcmp(arg, "enter") ) {
		S.binding = k1;
		wrefresh(curscr);
	} else if( ! strcmp(arg, "control") ) {
		S.binding = ctl;
	}
	scrollbottom(S.f);
}

static void
transpose_r(struct canvas *c)
{
	if( c ) {
		c->typ = !c->typ;
		struct canvas *t = c->c[0];
		double s = c->split.y;
		c->split.y = c->split.x;
		c->split.x = s;
		transpose_r(c->c[0] = c->c[1]);
		transpose_r(c->c[1] = t);
	}
}

int
transpose(void)
{
	if( S.f && !S.f->c[0] && !S.f->c[1] ) {
		transpose_r(S.f->parent);
	} else {
		transpose_r(S.f);
	}
	return S.reshape = 1;
}
