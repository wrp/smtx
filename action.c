/*
 * Copyright 2017 - 2019 Rob King <jking@deadpixi.com>
 * Copyright 2020 - 2023 William Pursell <william.r.pursell@gmail.com>
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

/* Index of a pty is based on its fd */
#define IDX(t) ((t)->fd - 2)

/* Attach pty p to the canvas n. */
static void
attach_pty(struct canvas *n, struct pty *p)
{
	n->p->count -= 1;
	(n->p = p)->count += 1;
	S.reshape = 1; /* Need to adjust row count of pty */
}

void
attach(void)
{
	for( struct pty *t = S.p; t; t = t->next ){
		if( IDX(t) == S.count ){
			attach_pty(S.f, t);
			return;
		}
	}
	check(0, errno = 0, "No pty exists with id %d", S.count);
}

static const char *default_balance_arg[] = { "-|", "-", "|", "-|" };
void
balance(const char *arg)
{
	int c = MIN(S.count + 1, 3);
	for( arg = (*arg == '=') ? default_balance_arg[c]: arg; *arg; arg++ ){
		int d = *arg == '|', count = 0;
		for( struct canvas *n = S.f; n; n = n->c[d] ){
			count += 1;
		}
		for( struct canvas *n = S.f; n; n = n->c[d] ){
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
	while( n->c[dir] ){
		n = n->c[dir];
	}
	for( int i = S.count < 1 ? 1 : S.count; i; i -= 1 ){
		struct canvas *v = n->c[dir] = newcanvas(0, n);
		if( v && v->p ){
			v->typ = n->typ = n->c[!dir] ? n->typ : dir;
			assert(v->parent == n);
			n = v;
		} else if( v && !v->p ){
			freecanvas(v);
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

static struct canvas * find_canvas(struct canvas *c, int id);

void
focus(void)
{
	struct canvas *t = find_canvas(S.c, S.count);
	S.f = t ? t : S.f;
}

void
mov(const char *arg)
{
	int count = S.count < 1 ? 1 : S.count;
	struct canvas *n = S.f;
	for( struct canvas *t = S.f; t && count--; n = t ? t : n ){
		switch( *arg ){
		case 'k':
			t = t->parent;
			break;
		case 'j':
			t = t->c[t->typ];
			break;
		case 'h':
			t = t->c[0];
			break;
		case 'l':
			t = t->c[1];
			break;
		}
	}
	S.f = n ? n : S.c;
}

static struct canvas *
find_canvas(struct canvas *c, int id)
{
	struct canvas *r = NULL;
	if( c && id > 0 ){
		if( IDX(c->p) == id ){
			r = c;
		} else if( (r = find_canvas(c->c[0], id)) == NULL ){
			r = find_canvas(c->c[1], id);
		}
	}
	return id < 1 ? S.f : r;
}

static void
pty_size(struct pty *p)
{
	check(p->fd == -1 || ! ioctl(p->fd, TIOCGWINSZ, &p->ws), errno = 0,
		"ioctr error getting size of pty %d", IDX(p));
}

void
new_tabstop(void)
{
	pty_size(S.f->p); /* Update S.f->p->ws */
	set_tabs(S.f->p, S.f->p->tabstop = S.count > -1 ? S.count : 8);
}

void
new_shell(void)
{
	attach_pty(S.f, new_pty(S.history, MAX(S.width, S.f->extent.x), true));
}

void
next(void)
{
	S.f->p = S.f->p->next ? S.f->p->next : S.p;
}

void
prune(void)
{
	struct canvas *t = find_canvas(S.c, S.count);
	if( S.count == 0 ){
		S.c = NULL;  /* Trigger an exit from main loop */
	} else if( t && t->parent ){
		struct canvas *p = t->parent;
		int c = t == p->c[1];
		*(c ? &p->split.x : &p->split.y) = 1.0;
		p->c[c] = NULL;
		change_count(t, -1, 1);
	}
	S.reshape = 1;
}

static void grow_screens(struct pty *p, int siz);

void
reshape_root(void)
{
	if( LINES > S.history ){
		S.history = LINES;
	}
	for( struct pty *p = S.p; p; p = p->next ){
		grow_screens(p, LINES);
	}
	resize_pad(&S.werr, 1, COLS);
	resize_pad(&S.wbkg, LINES, COLS);
	reshape(S.c, 0, 0, LINES, COLS);
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
	if( n && n->p && n->p->s && n->p->s->w ){
		int c = S.count;
		int count = c < 0 ? n->extent.x : !c ? n->p->ws.ws_col : c;
		int x = n->offset.x + (*arg == '<' ? -count : count);
		n->offset.x = MAX(0, MIN(x, n->p->ws.ws_col - n->extent.x));
		n->manualscroll = !!c;
	}
}

void
scrolln(const char *arg)
{
	struct canvas *n = S.f;
	if( n && n->p && n->p->s && n->p->s->w ){
		int count = S.count == -1 ? n->extent.y - 1 : S.count;
		int top = n->p->s->maxy - n->extent.y + 1;
		n->offset.y += *arg == '-' ? -count : count;
		n->offset.y = MIN(MAX(0, n->offset.y), top);
	}
}

void
send(const char *arg)
{
	rewrite(S.f->p->fd, arg + 1, arg[0]);
	scrollbottom(S.f);
}

void
send_cr(void)
{
	rewrite(S.f->p->fd, "\r\n", S.f->p->lnm ? 2 : 1);
	scrollbottom(S.f);
}

static void
grow_screens(struct pty *p, int siz)
{
	struct screen *s, *w[] = { &p->scr[0], &p->scr[1], NULL };
	for( struct screen **sp = w; *sp && (s = *sp)->rows < siz; sp++ ){
		WINDOW *new = NULL;
		if( resize_pad(&new, siz, p->ws.ws_col) ){
			copywin(s->w, new, 0, 0, siz - s->rows, 0,
				siz - 1, p->ws.ws_col - 1, 1);
			delwin(s->w);
			s->w = new;
			wmove(s->w, s->c.y += siz - s->rows, s->c.x);
			/* TODO?: mov maxy to struct pty */
			s->maxy += siz - s->rows;
			p->tos = MAX(0, s->maxy - p->ws.ws_row + 1);
			s->scroll.top += siz - s->rows;
			s->scroll.bot += siz - s->rows;
			s->rows = siz;
		}
	}
}

void
help(void)
{
	for( int i = 0; i < LINES * COLS; i++ ){
		putchar(' ');
	}
	putchar('\r');
	putchar('\n');
	printf("Command key is ^%c\r\n", S.rawkey);
	puts("Avaliable commands (in command mode):\r");
	puts("[N]C create N new windows (left/right)\r");
	puts("[N]c create N new windows (up/down)\r");
	puts("[N]g move focus to window N\r");
	puts("[N]v use preset window layout N\r");
	puts("[N]W set width of underlying tty to N\r");
	puts("[N]x Close window N.  If N == 0, exit smtx\r");
	puts("[N]= rebalance all windows below current\r");
	puts("[N]< scroll left N characters\r");
	puts("[N]> scroll right N characters\r");
	puts("[N]- set height of current window to N% of canvas\r");
	puts("[N]| set width of current window to N% of canvas\r");
	fflush(stdout);
}

void
set_history(void)
{
	struct canvas *n = S.f;
	struct pty *p = n->p;
	S.history = MAX(LINES, S.count);
	grow_screens(p, S.history);
	S.reshape = 1;
}

void
set_layout(void)
{
	const char *defaults[] = {
		[0] = "1,1",
		[1] = "1,1",
		[2] = ".5,1 1,1",
		[3] = "1,.5 .5,1 1,1",
		[4] = ".5,.5 .5,1 1,.5 1,1",
		[5] = "1,.5 .25,1 .5,1 .75,1 1,1",
		[6] = ".5,1 .75,.5 .75,1 1,.33 1,.66 1,1",
		[7] = ".4,1 .8,1 1,.2 1,.4 1,.6 1,.8 1,1",
		[8] = ".5,.25 .5,.5 .5,.75 .5,1 1,.25 1,.5 1,.75 1,1",
		[9] = (
			".34,.33 .34,.67 .34,1 .68,.33 .68,.67 .68,1 "
			"1,.33 1,.67 1,1"
		),
	};
	size_t count = S.count < 0 ? 1 : S.count;
	if( count < sizeof defaults / sizeof *defaults ){
		build_layout(defaults[count]);
	} else {
		check(0, errno = 0, "Invalid layout: %d", count);
	}
}

/* Set width of the underlying tty */
void
set_width(const char *arg)
{
	struct canvas *n = S.f;
	struct pty *p = n->p;
	int w = *arg ? strtol(arg, NULL, 10) : S.count;
	if( w == -1 ){
		w = n->extent.x;
	}
	if( p->fd > 0 && (pty_size(p), w != p->ws.ws_col) ){
		p->ws.ws_col = w;
		resize_pad(&p->scr[0].w, p->scr[0].rows, w);
		resize_pad(&p->scr[1].w, p->scr[1].rows, w);
		if( p->s->c.x > w - 1 ){
			wmove(p->s->w, p->s->c.y, p->s->c.x = w - 1);
		}
		set_tabs(p, p->tabstop);
		reshape_window(p);
	}
}

void
swap(void)
{
	struct canvas *n = S.f;
	struct canvas *t;
	if( S.count == -1 ){
		t = n->c[n->typ];
		t = t ? t : n->c[!n->typ];
		t = t ? t : n->parent;
	} else {
		t = find_canvas(S.c, S.count);
	}
	if( t ){
		struct pty *tmp = n->p;
		n->p = t->p;
		t->p = tmp;
		S.reshape = 1;
	} else {
		check(0, errno = 0, "Cannot find target canvas");
	}
}

void
transition(const char *arg)
{
	switch( *arg++ ){
	case '*':
		rewrite(S.f->p->fd, (char *)&S.ctlkey, 1);
		break;
	case '\n':
		send_cr();
	}
	S.errmsg[0] = 0; /* Clear any existing error message */
	switch( *arg ){
	case 'e':
		S.binding = k1; /* enter */
		break;
	case 'i':
		S.binding = k2;  /* insert */
		break;
	case 'c':
		S.binding = ctl; /* control */
		break;
	}
	scrollbottom(S.f);
	wrefresh(curscr);
}

static void
transpose_r(struct canvas *c)
{
	if( c ){
		c->typ = !c->typ;
		struct canvas *t = c->c[0];
		double s = c->split.y;
		c->split.y = c->split.x;
		c->split.x = s;
		transpose_r(c->c[0] = c->c[1]);
		transpose_r(c->c[1] = t);
	}
}

void
transpose(void)
{
	if( S.f && !S.f->c[0] && !S.f->c[1] ){
		transpose_r(S.f->parent);
	} else {
		transpose_r(S.f);
	}
	S.reshape = 1;
}

void
vbeep(void)
{
	(void)beep();
}
