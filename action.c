#include "smtx.h"

void
transition(const char *arg)
{
	S.binding = S.maps[ S.binding == S.maps[0] ];
	wmove(S.werr, 0, 0);
	scrollbottom(S.f);
	send(arg);
}

