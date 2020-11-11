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

struct action {
	void (*cb)(struct vtp *p, wchar_t w);;
	struct state *next;
};
struct state {
	int reset;
	enum { gnd, csi, esc_s, osc_s } typ;
	struct action act[0x80];
};

static void
ignore(struct vtp *v, wchar_t w)
{
	(void)v;
	(void)w;
}

static struct state ground, esc_entry, esc_collect, csi_entry,
	csi_ignore, csi_param, csi_collect, osc_string;

static void
collect(struct vtp *v, wchar_t w)
{
	if( v->s == &osc_string ) {
		if( v->z.nosc < MAXOSC ) {
			v->z.oscbuf[v->z.nosc++] = wctob(w);
			assert( v->z.nosc < (int)sizeof v->z.oscbuf );
			assert( v->z.oscbuf[v->z.nosc] == '\0' );
		}
	} else if( !v->z.inter ) {
		v->z.inter = (int)w;
	}
}

static void
param(struct vtp *v, wchar_t w)
{
	v->z.narg = v->z.narg ? v->z.narg : 1;
	int *a = v->z.args + v->z.narg - 1;
	if( w == L';' ) {
		v->z.narg += 1;
	} else if( v->z.narg < MAXPARAM && *a < 9999 ) {
		*a = *a * 10 + w - '0';
	}
}

static void
doall(struct vtp *v, wchar_t w)
{
	tput(v->p, w, v->z.inter, 0, NULL,
		( w < 0x20 && w >= 0 ) ? cons[w] :
		( v->s && v->s->typ == esc_s ) ? escs[w] :
		print
	);
}

static void
docsi(struct vtp *v, wchar_t w)
{
	tput(v->p, w, v->z.inter, v->z.narg, v->z.args, csis[w]);
}

static void
doosc(struct vtp *v, wchar_t w)
{
	tput(v->p, w, v->z.inter, v->z.nosc, v->z.oscbuf, osc);
}


/*
 * State definitions built by consulting the excellent state chart created by
 * Paul Flo Williams: http://vt100.net/emu/dec_ansi_parser
 * Please note that Williams does not (AFAIK) endorse this work.
 */
 #define LOWBITS                                         \
		[0]             = {ignore, NULL},        \
		[0x01 ... 0x17] = {doall, NULL},     \
		[0x18]          = {doall, &ground},  \
		[0x19]          = {doall, NULL},     \
		[0x1a]          = {doall, &ground},  \
		[0x1b]          = {ignore, &esc_entry},        \
		[0x1c ... 0x1f] = {doall, NULL}

static struct state ground = {
	.reset = 0,
	.typ = gnd,
	.act = {
		LOWBITS,
		[0x20 ... 0x7f] = {doall, NULL},
	}
};

static struct state esc_entry = {
	.reset = 1,
	.typ = esc_s,
	.act = {
		LOWBITS,
		[0x20]          = {collect, &esc_collect}, /* sp */
		[0x21]          = {ignore, &osc_string},   /* ! */
		[0x22 ... 0x2f] = {collect, &esc_collect}, /* "#$%&'()*+,-./ */
		[0x30 ... 0x4f] = {doall, &ground},
		[0x50]          = {ignore, &osc_string},   /* P */
		[0x51 ... 0x57] = {doall, &ground},
		[0x58]          = {ignore, NULL},
		[0x59 ... 0x5a] = {doall, &ground},
		[0x5b]          = {ignore, &csi_entry},  /* [ */
		[0x5c]          = {doall, &ground},      /* \ */
		[0x5d]          = {ignore, &osc_string}, /* ] */
		[0x5e]          = {ignore, &osc_string}, /* ^ */
		[0x5f]          = {ignore, &osc_string}, /* _ */
		[0x60 ... 0x6a] = {doall, &ground},      /* `a-j */
		[0x6b]          = {ignore, &osc_string}, /* k */
		[0x6c ... 0x7e] = {doall, &ground},      /* l-z{|}~ */
		[0x7f]          = {ignore, NULL},
	}
};

static struct state esc_collect = {
	.reset = 0,
	.typ = esc_s,
	.act = {
		LOWBITS,
		[0x20 ... 0x2f] = {collect, NULL},  /* sp!"#$%&'()*+,-./ */
		[0x30 ... 0x7e] = {doall, &ground}, /* 0-9a-zA-z ... */
		[0x7f]          = {ignore, NULL},
	}
};

static struct state csi_entry = {
	.reset = 1,
	.typ = csi,
	.act = {
		LOWBITS,
		[0x20 ... 0x2f] = {collect, &csi_collect}, /* !"#$%&'()*+,-./ */
		[0x30 ... 0x39] = {param, &csi_param},     /* 0 - 9 */
		[0x3a]          = {ignore, &csi_ignore},   /* : */
		[0x3b]          = {param, &csi_param},     /* ; */
		[0x3c ... 0x3f] = {collect, &csi_param},   /* <=>? */
		[0x40 ... 0x7e] = {docsi, &ground}, /* @A-Za-z[\]^_`{|}~ */
		[0x7f]          = {ignore, NULL},
	}
};

static struct state csi_ignore = {
	.reset = 0,
	.typ = csi,
	.act = {
		LOWBITS,
		[0x20 ... 0x3f] = {ignore, NULL},    /* !"#$%&'()*+,-./0-9... */
		[0x40 ... 0x7e] = {ignore, &ground}, /* @A-Za-z[\]^_`{|}~ */
		[0x7f]          = {ignore, NULL},
	}
};

static struct state csi_param = {
	.reset = 0,
	.typ = csi,
	.act = {
		LOWBITS,
		[0x20 ... 0x2f] = {collect, &csi_collect},
		[0x30 ... 0x39] = {param, NULL}, /* 0 - 9 */
		[0x3a]          = {ignore, &csi_ignore},
		[0x3b]          = {param, NULL}, /* ; */
		[0x3c ... 0x3f] = {ignore, &csi_ignore},
		[0x40 ... 0x7e] = {docsi, &ground},
		[0x7f]          = {ignore, NULL},
	}
};

static struct state csi_collect = {
	.reset = 0,
	.typ = csi,
	.act = {
		LOWBITS,
		[0x20 ... 0x2f] = {collect, NULL},       /* !"#$%&'()*+,-./ */
		[0x30 ... 0x3f] = {ignore, &csi_ignore}, /* 0-9 :;<=>? */
		[0x40 ... 0x7e] = {docsi, &ground},
		[0x7f]          = {ignore, NULL},
	}
};

#pragma GCC diagnostic ignored "-Woverride-init"
static struct state osc_string = {
	.reset = 1,
	.typ = osc_s,
	.act = {
		LOWBITS,
		[0x07]          = {doosc, &ground},
		[0x0a]          = {doosc, &ground},  /* \n */
		[0x0d]          = {doosc, &ground},  /* \r */
		[0x20 ... 0x7f] = {collect, NULL},   /* sp!"#$%&'()*+,-./ */
	}
};

void
vtwrite(struct vtp *vp, const char *s, size_t n)
{
	size_t r;
	for( const char *e = s + n; s < e; s += r ) {
		wchar_t w;
		switch( r = mbrtowc(&w, s, e - s, &vp->ms) ) {
		case -1: /* invalid character, skip it */
		case -2: /* incomplete character, skip it */
			w = VTPARSER_BAD_CHAR;
			memset(&vp->ms, 0, sizeof vp->ms); /* Fallthru */
		case 0: /* literal zero, write it and advance */
			r = 1;
		}
		if( w >= 0 && w < MAXCALLBACK ) {
			struct action *a = (vp->s ? vp->s : &ground)->act + w;
			assert( a->cb != NULL );
			a->cb(vp, w);
			if( a->next ) {
				vp->s = a->next;
				if( vp->s->reset ) {
					memset(&vp->z, 0, sizeof vp->z);
				}
			}
		} else {
			doall(vp, w);
		}
	}
}
