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
			n->p->ws.ws_row = 0; /* rows is set during resize */
		}
	}
	S.reshape = 1;
}

void
create(const char *arg)
{
	int dir = *arg == '|';
	struct canvas *n = S.f;
	while( n->c[dir] ) {
		n = n->c[dir];
	}
	for( int count = S.count < 1 ? 1 : S.count; count; count -= 1 ) {
		struct canvas *v = n->c[dir] = newcanvas(0, n, 1);
		if( v != NULL ) {
			v->typ = n->typ = n->c[!dir] ? n->typ : dir;
			assert(v->parent == n);
			n = v;
		}
	}
	balance(arg);
	reshape(S.c, 0, 0, LINES, COLS);
}

void
digit(const char *arg)
{
	S.count = 10 * (S.count == -1 ? 0 : S.count) + *arg - '0';
}

void
mov(const char *arg)
{
	int count = S.count < 1 ? 1 : S.count;
	struct canvas *n = S.f;
	for( struct canvas *t = S.f; t && count--; n = t ? t : n ) {
		switch( *arg ) {
		case 'k': t = t->parent; break;
		case 'j': t = t->c[t->typ]; break;
		case 'h': t = t->c[0]; break;
		case 'l': t = t->c[1];
		}
	}
	S.f = n ? n : S.c;
}

static struct canvas *
find_canvas(struct canvas *c, int id)
{
	struct canvas *r = NULL;
	if( c && id > 0 ) {
		if( c->p->fd == id + 2 ) {
			r = c;
		} else if( (r = find_canvas(c->c[0], id)) == NULL ) {
			r = find_canvas(c->c[1], id);
		}
	}
	return id < 1 ? S.f : r;
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
	struct canvas *t = find_canvas(S.c, S.count);
	if( S.count == 0 ) {
		S.c = NULL;  /* Trigger an exit from main loop */
	} else if( t && t->parent ) {
		struct canvas *p = t->parent;
		int c = t == p->c[1];
		*(c ? &p->split.x : &p->split.y) = 1.0;
		p->c[c] = NULL;
		change_count(t, -1, 1);
	}
	return S.reshape = 1;
}

static void grow_screens(struct pty *p, int siz);

int
reshape_root(void)
{
	if( LINES > S.history ) {
		S.history = LINES;
	}
	for( struct pty *p = S.p; p; p = p->next ) {
		grow_screens(p, LINES);
	}
	resize_pad(&S.werr, 1, COLS);
	resize_pad(&S.wbkg, LINES, COLS);
	reshape(S.c, 0, 0, LINES, COLS);
	return 1;
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
		int c = S.count;
		int count = c < 0 ? n->extent.x : !c ? n->p->ws.ws_col : c;
		n->offset.x += *arg == '<' ? -count : count;
		if( n->offset.x < 0 ) {
			n->offset.x = 0;
		} else if( n->offset.x > n->p->ws.ws_col - n->extent.x ) {
			n->offset.x = n->p->ws.ws_col - n->extent.x;
		}
		n->manualscroll = !!c;
	}
}

void
scrolln(const char *arg)
{
	struct canvas *n = S.f;
	if( n && n->p && n->p->s && n->p->s->win ) {
		int count = S.count == -1 ? n->extent.y - 1 : S.count;
		int top = n->p->s->maxy - n->extent.y + 1;
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
grow_screens(struct pty *p, int siz)
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
				s->tos += siz - my;
				s->maxy += siz - my;
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
	grow_screens(p, S.history);
	return S.reshape = 1;
}

int
set_layout(void)
{
	size_t count = S.count < 0 ? 1 : S.count;
	const char *def[] = {
		[0] = "1:1",
		[1] = "1:1",
		[2] = ".5:1 1:1",
		[3] = "1:.5 .5:1 1:1",
		[4] = ".5:.5 .5:1 1:.5 1:1",
		[5] = "1:.5 .25:1 .5:1 .75:1 1:1",
		[6] = ".5:1 .75:.5 .75:1 1:.33 1:.66 1:1",
		[7] = ".4:1 .8:1 1:.2 1:.4 1:.6 1:.8 1:1",
		[8] = ".5:.25 .5:.5 .5:.75 .5:1 1:.25 1:.5 1:.75 1:1",
		[9] = ".34:.33 .34:.67 .34:1 .68:.33 .68:.67 .68:1 "
			"1:.33 1:.67 1:1",
	};
	return count < 10 ? build_layout(def[count]) : 0;
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
