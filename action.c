#include "smtx.h"

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

