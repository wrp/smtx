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
#include <assert.h>
#include <stdbool.h>
#include <string.h>
#include "vtparser.h"

typedef void (callback)(struct vtp *p, wchar_t w);
struct action {
	callback *cb;
	struct state *next;
};
struct state {
	void (*entry)(struct vtp *v);
	struct action act[0x80];
};
static void
reset(struct vtp *v)
{
	v->inter = v->narg = v->nosc = 0;
	memset(v->args, 0, sizeof v->args);
	memset(v->oscbuf, 0, sizeof v->oscbuf);
}

static callback ignore;
static void
ignore(struct vtp *v, wchar_t w)
{
	(void)v;
	(void)w;
}

static struct state esc, esc_intermediate, csi_entry,
	csi_ignore, csi_param, csi_intermediate, osc_string;

static void
collect(struct vtp *v, wchar_t w)
{
	if( v->s == &osc_string ) {
		if( v->nosc < MAXOSC ) {
			v->oscbuf[v->nosc++] = wctob(w);
			assert( v->nosc < (int)sizeof v->oscbuf );
			assert( v->oscbuf[v->nosc] == '\0' );
		}
	} else if( !v->inter ) {
		v->inter = (int)w;
	}
}

static void
param(struct vtp *v, wchar_t w)
{
	v->narg = v->narg ? v->narg : 1;
	int *a = v->args + v->narg - 1;
	if( w == L';' ) {
		v->narg += 1;
	} else if( v->narg < MAXPARAM && *a < 9999 ) {
		*a = *a * 10 + w - '0';
	}
}

static void
docontrol(struct vtp *v, wchar_t w)
{
	assert( w < MAXCALLBACK );
	tput(v, w, v->inter, 0, NULL, cons[w]);
}

static callback doescape;
static void
doescape(struct vtp *v, wchar_t w)
{
	assert( w < MAXCALLBACK );
	tput(v, w, v->inter, v->inter > 0, &v->inter, v->escs[w]);
}

static callback docsi;
static void
docsi(struct vtp *v, wchar_t w)
{
	assert( w < MAXCALLBACK );
	tput(v, w, v->inter, v->narg, v->args, v->csis[w]);
}

static callback doprint;
static void
doprint(struct vtp *v, wchar_t w)
{
	tput(v, w, v->inter, 0, NULL, v->print);
}

static callback doosc;
static void
doosc(struct vtp *v, wchar_t w)
{
	tput(v, w, v->inter, v->nosc, v->oscbuf, v->osc);
}


/*
 * State definitions built by consulting the excellent state chart created by
 * Paul Flo Williams: http://vt100.net/emu/dec_ansi_parser
 * Please note that Williams does not (AFAIK) endorse this work.
 */
static struct state ground = {
	.entry = NULL,
	.act = {
		[0]             = {ignore, NULL},
		[0x01 ... 0x17] = {docontrol, NULL},
		[0x18]          = {docontrol, &ground},
		[0x19]          = {docontrol, NULL},
		[0x1a]          = {docontrol, &ground},
		[0x1b]          = {ignore, &esc},
		[0x1c ... 0x1f] = {docontrol, NULL},
		[0x20 ... 0x7f] = {doprint, NULL},
	}
};

static struct state esc = {
	.entry = reset,
	.act = {
		[0]             = {ignore, NULL},
		[0x01 ... 0x17] = {docontrol, NULL},
		[0x18]          = {docontrol, &ground},
		[0x19]          = {docontrol, NULL},
		[0x1a]          = {docontrol, &ground},
		[0x1b]          = {ignore, &esc},
		[0x1c ... 0x1f] = {docontrol, NULL},
		[0x20]          = {collect, &esc_intermediate},
		[0x21]          = {ignore, &osc_string}, /* ! */
		[0x22 ... 0x2f] = {collect, &esc_intermediate},
		[0x30 ... 0x4f] = {doescape, &ground},
		[0x50]          = {ignore, &osc_string}, /* P */
		[0x51 ... 0x57] = {doescape, &ground},
		[0x58]          = {ignore, NULL},
		/* Why is 0x58 ('X') skipped ? (1) */
		[0x59 ... 0x5a] = {doescape, &ground},
		[0x5b]          = {ignore, &csi_entry},  /* [ */
		[0x5c]          = {doescape, &ground},   /* \ */
		[0x5d]          = {ignore, &osc_string}, /* ] */
		[0x5e]          = {ignore, &osc_string}, /* ^ */
		[0x5f]          = {ignore, &osc_string}, /* _ */
		[0x60 ... 0x6a] = {doescape, &ground},
		[0x6b]          = {ignore, &osc_string}, /* k */
		[0x6c ... 0x7e] = {doescape, &ground},
		[0x7f]          = {ignore, NULL},
	}
};
/*
 * (1) I suspect this is a bug from mtm
 */

static struct state esc_intermediate = {
	.entry = NULL,
	.act = {
		[0]             = {ignore, NULL},
		[0x01 ... 0x17] = {docontrol, NULL},
		[0x18]          = {docontrol, &ground},
		[0x19]          = {docontrol, NULL},
		[0x1a]          = {docontrol, &ground},
		[0x1b]          = {ignore, &esc},
		[0x1c ... 0x1f] = {docontrol, NULL},
		[0x20 ... 0x2f] = {collect, NULL},
		[0x30 ... 0x7e] = {doescape, &ground},
		[0x7f]          = {ignore, NULL},
	}
};

static struct state csi_entry = {
	.entry = reset,
	.act = {
		[0]             = {ignore, NULL},
		[0x01 ... 0x17] = {docontrol, NULL},
		[0x18]          = {docontrol, &ground},
		[0x19]          = {docontrol, NULL},
		[0x1a]          = {docontrol, &ground},
		[0x1b]          = {ignore, &esc},
		[0x1c ... 0x1f] = {docontrol, NULL},
		[0x20 ... 0x2f] = {collect, &csi_intermediate},
		[0x30 ... 0x39] = {param, &csi_param}, /* 0 - 9 */
		[0x3a]          = {ignore, &csi_ignore},
		[0x3b]          = {param, &csi_param}, /* ; */
		[0x3c ... 0x3f] = {collect, &csi_param},
		[0x40 ... 0x7e] = {docsi, &ground},
		[0x7f]          = {ignore, NULL},
	}
};

static struct state csi_ignore = {
	.entry = NULL,
	.act = {
		[0]             = {ignore, NULL},
		[0x01 ... 0x17] = {docontrol, NULL},
		[0x18]          = {docontrol, &ground},
		[0x19]          = {docontrol, NULL},
		[0x1a]          = {docontrol, &ground},
		[0x1b]          = {ignore, &esc},
		[0x1c ... 0x1f] = {docontrol, NULL},
		[0x20 ... 0x3f] = {ignore, NULL},
		[0x40 ... 0x7e] = {ignore, &ground},
		[0x7f]          = {ignore, NULL},
	}
};

static struct state csi_param = {
	.entry = NULL,
	.act = {
		[0]             = {ignore, NULL},
		[0x01 ... 0x17] = {docontrol, NULL},
		[0x18]          = {docontrol, &ground},
		[0x19]          = {docontrol, NULL},
		[0x1a]          = {docontrol, &ground},
		[0x1b]          = {ignore, &esc},
		[0x1c ... 0x1f] = {docontrol, NULL},
		[0x20 ... 0x2f] = {collect, &csi_intermediate},
		[0x30 ... 0x39] = {param, NULL}, /* 0 - 9 */
		[0x3a]          = {ignore, &csi_ignore},
		[0x3b]          = {param, NULL}, /* ; */
		[0x3c ... 0x3f] = {ignore, &csi_ignore},
		[0x40 ... 0x7e] = {docsi, &ground},
		[0x7f]          = {ignore, NULL},
	}
};

static struct state csi_intermediate = {
	.entry = NULL,
	.act = {
		[0]             = {ignore, NULL},
		[0x01 ... 0x17] = {docontrol, NULL},
		[0x18]          = {docontrol, &ground},
		[0x19]          = {docontrol, NULL},
		[0x1a]          = {docontrol, &ground},
		[0x1b]          = {ignore, &esc},
		[0x1c ... 0x1f] = {docontrol, NULL},
		[0x20 ... 0x2f] = {collect, NULL},       /* !"#$%&'()*+,-./ */
		[0x30 ... 0x3f] = {ignore, &csi_ignore}, /* 0-9 :;<=>? */
		[0x40 ... 0x7e] = {docsi, &ground},
		[0x7f]          = {ignore, NULL},
	}
};

static struct state osc_string = {
	.entry = reset,
	.act = {
		[0]             = {ignore, NULL},
		[0x01 ... 0x06] = {docontrol, NULL},
		[0x07]          = {doosc, &ground},
		[0x08 ... 0x09] = {docontrol, NULL},
		[0x0a]          = {doosc, &ground},  /* \n */
		[0x0b ... 0x0c] = {docontrol, NULL},
		[0x0d]          = {doosc, &ground},  /* \r */
		[0x0e ... 0x17] = {docontrol, NULL},
		[0x18]          = {docontrol, &ground},
		[0x19]          = {docontrol, NULL},
		[0x1a]          = {docontrol, &ground},
		[0x1b]          = {ignore, &esc},
		[0x1c ... 0x1f] = {docontrol, NULL},
		[0x20 ... 0x7f] = {collect, NULL},
	}
};

void
vtwrite(struct vtp *vp, const char *s, size_t n)
{
	wchar_t w = 0;
	while( n ) {
		size_t r = mbrtowc(&w, s, n, &vp->ms);
		switch( r ) {
		case -1: /* invalid character, skip it */
		case -2: /* incomplete character, skip it */
			w = VTPARSER_BAD_CHAR;
			memset(&vp->ms, 0, sizeof vp->ms);
			r = 1;
			break;
		case 0: /* literal zero, write it and advance */
			r = 1;
		}
		n -= r;
		s += r;
		if( w >= 0 && w < MAXCALLBACK ) {
			struct action *a = (vp->s ? vp->s : &ground)->act + w;
			assert( a->cb != NULL );
			a->cb(vp, w);
			if( a->next ) {
				assert( a->cb );
				vp->s = a->next;
				if( vp->s->entry ) {
					vp->s->entry(vp);
				}
			}
		} else {
			doprint(vp, w);
		}
	}
}
