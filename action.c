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
	show_err("No pty exists with id %d", target);
}

void
create(const char *arg)
{
	struct canvas *n = S.f;
	int dir = arg && *arg == 'C' ? 1 : 0;
	while( n && n->c[dir] != NULL ) {
		n = n->c[dir]; /* Split last window in a chain. */
	}
	struct canvas *v = *( n ? &n->c[dir] : &S.c ) = newcanvas();
	if( v != NULL ) {
		v->typ = dir;
		v->parent = n;
		balance(v);
	}
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
			show_err("invalid process. No signal sent");
		}
		break;
	default:
		show_err("invalid signal: %d", s);
	}
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

