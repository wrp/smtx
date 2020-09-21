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
append_command(const char *arg)
{
	assert( *arg == 1 );
	unsigned len = strlen(S.command);
	if( len < sizeof S.command - 1 ) {
		S.command[len++] = arg[1];
		S.command[len] = '\0';
	}
}

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
	err_check(1, "No pty exists with id %d", target);
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
	/* TODO: we should be able to set S.reshape_root here, but
	 * doing so causes odd (eg, unexplained) behavior in the tests.
	 * Need to track down why.
	 */
	reshape_root(NULL);
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
	(void)arg;
	pid_t p = n->p->pid;
	int s = S.count;
	switch( s ) {
	case SIGUSR1 + 128: case SIGUSR2 + 128: case SIGTERM + 128:
		p = getpid();
		s -= 128; /* Fall thru */
	case SIGKILL: case SIGTERM: case SIGUSR1: case SIGHUP:
	case SIGUSR2: case SIGINT:
		if( p != -1 ) {
			err_check(kill(p, s) == -1, "kill %d, %d", p, s);
		} else {
			err_check(1, "invalid process. No signal sent");
		}
		break;
	default:
		err_check(1, "invalid signal: %d", s);
	}
}

void
reshape_root(const char *arg)
{
	(void)arg;
	int y, x;
	getmaxyx(stdscr, y, x);
	if( y > S.history ) {
		S.history = y;
	}
	for( struct pty *p = S.p; p; p = p->next ) {
		p->ws.ws_row = 0;
	}
	resize_pad(&S.werr, 1, x);
	reshape(S.c, 0, 0, y, x);
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
	if( !n || !n->c[typ] || n->split_point[typ] == 0 || s < 1) {
		return;
	}
	double full = s / n->split_point[typ];
	double new = s + count * dir;
	if( new > 0 ) {
		n->split_point[typ] = MIN(new / full, 1.0);
	} else {
		n->split_point[typ] = 0.0;
		focus(S.v);
	}
	S.reshape = 1;
}

void
scrollh(const char *arg)
{
	struct canvas *n = S.f;
	if( n && n->p && n->p->s && n->p->s->win ) {
		int y, x;
		getmaxyx(n->p->s->win, y, x);
		(void)y;
		int count = S.count == -1 ? n->extent.x - 1 : S.count;
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
		int count = S.count == -1 ? n->extent.y - 1 : S.count;
		int top = S.history - n->extent.y;
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
				if( s - arg ) {
					rewrite(n->p->fd, arg, s - arg);
				}
				rewrite(n->p->fd, "\r\n", 2);
				arg = s + 1;
			}
		}
		rewrite(n->p->fd, arg, strlen(arg));
		scrollbottom(n);
	}
}

void
set_width(const char *arg)
{
	struct canvas *n = S.f;
	struct pty *p = n->p;
	assert( S.history >= n->extent.y );
	int h = S.history;
	int w = arg ? strtol(arg, NULL, 10) : S.count;
	if( w == -1 ) {
		w = n->extent.x;
	}
	if( p->fd > 0 && (pty_size(p), w != p->ws.ws_col) ) {
		p->ws.ws_col = w;
		resize_pad(&p->pri.win, h, w);
		resize_pad(&p->alt.win, h, w);
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
		err_check(1, "Cannot find target canvas");
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
	S.command[0] = 0;
	if( ! strcmp(arg, "enter") ) {
		S.mode = S.modes;
	} else if( ! strcmp(arg, "control") ) {
		S.mode = S.modes + 1;
	} else if( ! strcmp(arg, "command") ) {
		S.mode = S.modes + 2;
		strcat(S.command, ": ");
	}
	scrollbottom(S.f);
}
