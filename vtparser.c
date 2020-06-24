/* Copyright (c) 2017-2019 Rob King
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *   * Neither the name of the copyright holder nor the
 *     names of contributors may be used to endorse or promote products
 *     derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDER AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHORS,
 * COPYRIGHT HOLDERS, OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF
 * USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 * ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#include <stdbool.h>
#include <string.h>
#include "vtparser.h"

/**** DATA TYPES */
#define MAXACTIONS  128

typedef struct ACTION ACTION;
struct ACTION{
    void (*cb)(VTPARSER *p, wchar_t w);
    STATE *next;
};

struct STATE{
    void (*entry)(VTPARSER *v);
    ACTION act[MAXACTIONS];
};

/**** GLOBALS */
static STATE ground, escape, escape_intermediate, csi_entry,
             csi_ignore, csi_param, csi_intermediate, osc_string;

/**** ACTION FUNCTIONS */
static void
reset(VTPARSER *v)
{
    v->inter = v->narg = v->nosc = 0;
    memset(v->args, 0, sizeof(v->args));
    memset(v->oscbuf, 0, sizeof(v->oscbuf));
}

static void
ignore(VTPARSER *v, wchar_t w)
{
    (void)v; (void)w; /* avoid warnings */
}

static void
collect(VTPARSER *v, wchar_t w)
{
    v->inter = v->inter? v->inter : (int)w;
}

static void
collectosc(VTPARSER *v, wchar_t w)
{
    if (v->nosc < MAXOSC)
        v->oscbuf[v->nosc++] = w;
}

static void
param(VTPARSER *v, wchar_t w)
{
    v->narg = v->narg? v->narg : 1;

    if (w == L';')
        v->args[v->narg++] = 0;
    else if (v->narg < MAXPARAM && v->args[v->narg - 1] < 9999)
        v->args[v->narg - 1] = v->args[v->narg - 1] * 10 + (w - '0');
}

/*
 * TODO: There is a memory issue somewhere.  If we replace handle_terminal_cmd
 * with the wrapper in doprint, the tests fail.  Need to track down the
 * error.
 */
extern void handle_terminal_cmd2(VTPARSER *v, wchar_t w, wchar_t iw,
	int argc, int *argv, int);
extern void handle_terminal_cmd(VTPARSER *v, void *p, wchar_t w, wchar_t iw,
	int argc, int *argv, int);
static void
docontrol(VTPARSER *v, wchar_t w)
{
	if( w < MAXCALLBACK && v->cons[w] ) {
		handle_terminal_cmd2(v, w, v->inter, 0, NULL, v->cons[w]);
	}
}

static void
doescape(VTPARSER *v, wchar_t w)
{
	if( w < MAXCALLBACK && v->escs[w] ) {
		handle_terminal_cmd2(v, w, v->inter, v->inter > 0,
			&v->inter, v->escs[w]);
	}
}

static void
docsi(VTPARSER *v, wchar_t w)
{
	if( w < MAXCALLBACK && v->csis[w] ) {
		handle_terminal_cmd2(v, w, v->inter, v->narg, v->args,
			v->csis[w]);
	}
}

static void
doprint(VTPARSER *v, wchar_t w)
{
	if( v->print ) {
		handle_terminal_cmd(v, NULL, w, v->inter, 0, NULL, v->print);
	}
}

static void
doosc(VTPARSER *v, wchar_t w)
{
	if( v->osc ) {
		handle_terminal_cmd2(v, w, v->inter, v->nosc, NULL, v->osc);
	}
}

static int initialized;
static void init(void);

/**** PUBLIC FUNCTIONS */
void
vtonevent(VTPARSER *vp, VtEvent t, wchar_t w, int cb)
{
	if( ! initialized ) {
		init();
	}
    if (w < MAXCALLBACK) switch (t){
        case VTPARSER_CONTROL: vp->cons[w] = cb; break;
        case VTPARSER_ESCAPE:  vp->escs[w] = cb; break;
        case VTPARSER_CSI:     vp->csis[w] = cb; break;
        case VTPARSER_PRINT:   vp->print   = cb; break;
        case VTPARSER_OSC:     vp->osc     = cb; break;
    }
}

static void
handlechar(VTPARSER *vp, wchar_t w)
{
	vp->s = vp->s ? vp->s : &ground;

	if( w < 0 || w > 127 )
		return;
	ACTION *a = vp->s->act + w;

	if( a->cb ) {
		a->cb(vp, w);
		if( a->next ) {
			vp->s = a->next;
			if( a->next->entry ) {
				a->next->entry(vp);
			}
		}
	}

#if 0
	for( ACTION *a = vp->s->actions; a->cb; a++ ) {

		if( w >= a->lo && w <= a->hi ) {
			a->cb(vp, w);
			if( a->next ) {
				vp->s = a->next;
				if( a->next->entry ) {
					a->next->entry(vp);
				}
			}
			return;
		}

	}
#endif
}

void
vtwrite(VTPARSER *vp, const char *s, size_t n)
{
    wchar_t w = 0;
    while (n){
        size_t r = mbrtowc(&w, s, n, &vp->ms);
        switch (r){
            case -2: /* incomplete character, try again */
                return;

            case -1: /* invalid character, skip it */
                w = VTPARSER_BAD_CHAR;
                memset(&vp->ms, 0, sizeof vp->ms);
                r = 1;
                break;

            case 0: /* literal zero, write it but advance */
                r = 1;
                break;
        }

        n -= r;
        s += r;
        handlechar(vp, w);
    }
}

/*
 * State definitions built by consulting the excellent state chart created by
 * Paul Flo Williams: http://vt100.net/emu/dec_ansi_parser
 * Please note that Williams does not (AFAIK) endorse this work.
 */

static void
initstate(struct STATE *s, void (*entry)(VTPARSER *))
{
	s->entry = entry;
	s->act[0] = (ACTION){ ignore, NULL };
	s->act[0x7f] = (ACTION){ ignore, NULL };
	s->act[0x18] = (ACTION){ docontrol, &ground };
	s->act[0x1a] = (ACTION){ docontrol, &ground };
	s->act[0x1b] = (ACTION){ ignore, &escape };
	for( wchar_t i = 0x1; i < 0x20; i++ ) {
		if( i != 0x18 && i != 0x1a && i != 0x1b ) {
			s->act[i] = (ACTION){ docontrol, NULL };
		}
	}
}


static void
init(void)
{
	initialized = 1;
	initstate(&ground, NULL);
	for( wchar_t i = 0x20; i < MAXACTIONS; i++ ) {
		ground.act[i] = (ACTION){ doprint, NULL };
	}
	initstate(&escape, reset);
	escape.act[0x21] = (ACTION){ ignore, &osc_string };
	escape.act[0x6b] = (ACTION){ ignore, &osc_string };
	escape.act[0x5d] = (ACTION){ ignore, &osc_string };
	escape.act[0x5e] = (ACTION){ ignore, &osc_string };
	escape.act[0x50] = (ACTION){ ignore, &osc_string };
	escape.act[0x5f] = (ACTION){ ignore, &osc_string };
	for( wchar_t i = 0x20; i <= 0x2f; i++ ) {
		escape.act[i] = (ACTION){ collect, &escape_intermediate };
	}
	for( wchar_t i = 0x30; i <= 0x57; i++ ) {
		if( i != 0x50 ) {
			escape.act[i] = (ACTION){ doescape, &ground };
		}
	}
	for( wchar_t i = 0x60; i <= 0x7e; i++ ) {
		escape.act[i] = (ACTION){ doescape, &ground };
	}
	escape.act[0x59] = (ACTION){ doescape, &ground };
	escape.act[0x5a] = (ACTION){ doescape, &ground };
	escape.act[0x5b] = (ACTION){ ignore, &csi_entry };
	escape.act[0x5c] = (ACTION){ doescape, &ground };

	initstate(&escape_intermediate, NULL);
	for( wchar_t i = 0x20; i <= 0x2f; i++ ) {
		escape_intermediate.act[i] = (ACTION){ collect, NULL };
	}
	for( wchar_t i = 0x30; i <= 0x7e; i++ ) {
		escape_intermediate.act[i] = (ACTION){ doescape, &ground };
	}

	initstate(&csi_entry, reset);
	for( wchar_t i = 0x20; i <= 0x2f; i++ ) {
		csi_entry.act[i] = (ACTION){ collect, &csi_intermediate };
	}
	csi_entry.act[0x3a] = (ACTION){ ignore,  &csi_ignore};
	for( wchar_t i = 0x30; i <= 0x39; i++ ) {
		csi_entry.act[i] = (ACTION){ param, &csi_param };
	}
	csi_entry.act[0x3b] = (ACTION){ param, &csi_param };
	for( wchar_t i = 0x3c; i <= 0x3f; i++ ) {
		csi_entry.act[i] = (ACTION){ collect, &csi_param };
	}
	for( wchar_t i = 0x40; i <= 0x7e; i++ ) {
		csi_entry.act[i] = (ACTION){ docsi, &ground };
	}

	initstate(&csi_ignore, NULL);
	for( wchar_t i = 0x20; i <= 0x3f; i++ ) {
		csi_ignore.act[i] = (ACTION){ ignore, NULL };
	}
	for( wchar_t i = 0x40; i <= 0x7e; i++ ) {
		csi_ignore.act[i] = (ACTION){ ignore, &ground };
	}

	initstate(&csi_param, NULL);
	for( wchar_t i = 0x30; i <= 0x39; i++ ) {
		csi_param.act[i] = (ACTION){ param, NULL };
	}
	csi_param.act[0x3a] = (ACTION){ ignore, &csi_ignore };
	csi_param.act[0x3b] = (ACTION){ param, NULL };
	for( wchar_t i = 0x3c; i <= 0x3f; i++ ) {
		csi_param.act[i] = (ACTION){ ignore, &csi_ignore };
	}
	for( wchar_t i = 0x20; i <= 0x2f; i++ ) {
		csi_param.act[i] = (ACTION){ collect, &csi_intermediate };
	}
	for( wchar_t i = 0x40; i <= 0x7e; i++ ) {
		csi_param.act[i] = (ACTION){ docsi, &ground };
	}

	initstate(&csi_intermediate, NULL);
	for( wchar_t i = 0x20; i <= 0x2f; i++ ) {
		csi_intermediate.act[i] = (ACTION){ collect, NULL };
	}
	for( wchar_t i = 0x30; i <= 0x3f; i++ ) {
		csi_intermediate.act[i] = (ACTION){ ignore, &csi_ignore };
	}
	for( wchar_t i = 0x40; i <= 0x7e; i++ ) {
		csi_intermediate.act[i] = (ACTION){ docsi, &ground };
	}

	initstate(&osc_string, reset);
	osc_string.act[0x07] = (ACTION){ doosc, &ground };
	for( wchar_t i = 0x20; i <= 0x7f; i++ ) {
		osc_string.act[i] = (ACTION){ collectosc, NULL };
	}
}
