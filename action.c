#include "smtx.h"

void
attach(const char *arg)
{
	struct canvas *n = S.f;
	int target = arg ? strtol(arg, NULL, 10) : S.count;
	for( struct pty *t = S.p; t; t = t->next ) {
		if( t->id == target ) {
			n->p = t;
			reshape_flag = 1;
			return;
		}
	}
	show_err("No pty exists with id %d", target);
}

void
equalize(const char *arg)
{
	(void)arg;
	balance(S.f);
	reshape_flag = 1;
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
	reshape_flag = 1;
}

void
send(const char *arg)
{
	struct canvas *n = S.f;
	if( n->p && n->p->fd > 0 && arg ) {
		if( n->p->lnm ) {
			const char *s;
			while( strchr(s = arg, '\r') ) {
				rewrite(n->p->fd, arg, s - arg);
				rewrite(n->p->fd, "\n", 1);
				arg = s + 1;
			}
		}
		rewrite(n->p->fd, arg, strlen(arg));
		scrollbottom(n);
	}
}

void
transition(const char *arg)
{
	S.binding = S.maps[ S.binding == S.maps[0] ];
	wmove(S.werr, 0, 0);
	scrollbottom(S.f);
	send(arg);
}

