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
		if( v->z.argc < MAXOSC ) {
			v->z.argv.oscbuf[v->z.argc++] = wctob(w);
			assert( v->z.argc < (int)sizeof v->z.argv.oscbuf );
			assert( v->z.argv.oscbuf[v->z.argc] == '\0' );
		}
	} else if( !v->z.inter ) {
		v->z.inter = (int)w;
	}
}

static void
param(struct vtp *v, wchar_t w)
{
	v->z.argc = v->z.argc ? v->z.argc : 1;
	int *a = v->z.argv.args + v->z.argc - 1;
	if( w == L';' ) {
		v->z.argc += 1;
	} else if( v->z.argc < MAXPARAM && *a < 9999 ) {
		*a = *a * 10 + w - '0';
	}
}

static void
send(struct vtp *v, wchar_t w)
{
	typeof(v->s->typ) typ = v->s ? v->s->typ : gnd;
	tput(v->p, w, v->z.inter, v->z.argc, &v->z.argv,
		typ == osc_s ? oscs[w] :
		( w < 0x20 && w >= 0 ) ? cons[w] :
		typ == esc_s ? escs[w] :
		typ == csi   ? csis[w] :
		gnds[w]
	);
}

/*
 * State definitions built by consulting the excellent state chart created by
 * Paul Flo Williams: http://vt100.net/emu/dec_ansi_parser
 * Please note that Williams does not (AFAIK) endorse this work.
 */
 #define LOWBITS                                         \
		[0]             = {ignore, NULL},        \
		[0x01 ... 0x17] = {send, NULL},     \
		[0x18]          = {send, &ground},  \
		[0x19]          = {send, NULL},     \
		[0x1a]          = {send, &ground},  \
		[0x1b]          = {ignore, &esc_entry},        \
		[0x1c ... 0x1f] = {send, NULL}

static struct state ground = {
	.reset = 1,
	.typ = gnd,
	.act = {
		LOWBITS,
		[0x20 ... 0x7f] = {send, NULL},
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
		[0x30 ... 0x4f] = {send, &ground},
		[0x50]          = {ignore, &osc_string},   /* P */
		[0x51 ... 0x57] = {send, &ground},
		[0x58]          = {ignore, NULL},
		[0x59 ... 0x5a] = {send, &ground},
		[0x5b]          = {ignore, &csi_entry},  /* [ */
		[0x5c]          = {send, &ground},      /* \ */
		[0x5d]          = {ignore, &osc_string}, /* ] */
		[0x5e]          = {ignore, &osc_string}, /* ^ */
		[0x5f]          = {ignore, &osc_string}, /* _ */
		[0x60 ... 0x6a] = {send, &ground},      /* `a-j */
		[0x6b]          = {ignore, &osc_string}, /* k */
		[0x6c ... 0x7e] = {send, &ground},      /* l-z{|}~ */
		[0x7f]          = {ignore, NULL},
	}
};

static struct state esc_collect = {
	.reset = 0,
	.typ = esc_s,
	.act = {
		LOWBITS,
		[0x20 ... 0x2f] = {collect, NULL},  /* sp!"#$%&'()*+,-./ */
		[0x30 ... 0x7e] = {send, &ground}, /* 0-9a-zA-z ... */
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
		[0x40 ... 0x7e] = {send, &ground}, /* @A-Za-z[\]^_`{|}~ */
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
		[0x40 ... 0x7e] = {send, &ground},
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
		[0x40 ... 0x7e] = {send, &ground},
		[0x7f]          = {ignore, NULL},
	}
};

#pragma GCC diagnostic ignored "-Woverride-init"
static struct state osc_string = {
	.reset = 1,
	.typ = osc_s,
	.act = {
		LOWBITS,
		[0x07]          = {send, &ground},
		[0x0a]          = {send, &ground},  /* \n */
		[0x0d]          = {send, &ground},  /* \r */
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
		if( w >= 0 && w < 0x80 ) {
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
			tput(vp->p, w, 0, 0, NULL, print);
		}
	}
}
