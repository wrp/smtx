/* Copyright (c) 2017-2019 Rob King
 * Copyright (c) 2020 William R. Pursell
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
#include <assert.h>
#include <stdbool.h>
#include <string.h>
#include "vtparser.h"

typedef void (*callback)(VTPARSER *p, wchar_t w);
struct action {
    callback cb;
    struct state *next;
};

struct state{
    void (*entry)(VTPARSER *v);
    struct action act[0x80];
};

static struct state ground, escape, escape_intermediate, csi_entry,
	csi_ignore, csi_param, csi_intermediate, osc_string;

static void
reset(VTPARSER *v)
{
	v->inter = v->narg = v->nosc = 0;
	memset(v->args, 0, sizeof v->args);
	memset(v->oscbuf, 0, sizeof v->oscbuf);
}

static void
ignore(VTPARSER *v, wchar_t w)
{
	(void)v;
	(void)w;
}

static void
collect(VTPARSER *v, wchar_t w)
{
	if( !v->inter ) {
		v->inter = (int)w;
	}
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
	v->narg = v->narg ? v->narg : 1;
	int *a = v->args + v->narg - 1;
	if( w == L';' ) {
		v->args[v->narg++] = 0;
	} else if( v->narg < MAXPARAM && *a < 9999 ) {
		*a = *a * 10 + w - '0';
	}
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

void
vtonevent(VTPARSER *vp, VtEvent t, wchar_t w, int cb)
{
	if( ! initialized ) {
		init();
	}
	assert( w < MAXCALLBACK );
	switch( t ) {
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
	struct action *a = vp->s->act + w;

	if( a->cb ) {
		a->cb(vp, w);
		if( a->next ) {
			vp->s = a->next;
			if( a->next->entry ) {
				a->next->entry(vp);
			}
		}
	}
}

void
vtwrite(VTPARSER *vp, const char *s, size_t n)
{
	wchar_t w = 0;
	while( n ) {
		size_t r = mbrtowc(&w, s, n, &vp->ms);
		switch( r ) {
		case -2: /* incomplete character, try again */
			return;
		case -1: /* invalid character, skip it */
			w = VTPARSER_BAD_CHAR;
			memset(&vp->ms, 0, sizeof vp->ms);
			r = 1;
			break;
		case 0: /* literal zero, write it and advance */
			r = 1;
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
init_action(struct state *s, wchar_t idx, callback cb, struct state *next)
{
	s->act[idx].cb = cb;
	s->act[idx].next = next;
}

static void
init_range(struct state *s, wchar_t b, wchar_t e, callback cb, struct state *n)
{
	for( ; b <= e; b++ ) {
		init_action(s, b, cb, n);
	}
}

static void
initstate(struct state *s, void (*entry)(VTPARSER *))
{
	s->entry = entry;
	init_action(s, 0, ignore, NULL);
	init_action(s, 0x7f, ignore, NULL);
	init_action(s, 0x18, docontrol, &ground);
	init_action(s, 0x1a, docontrol, &ground);
	init_action(s, 0x1b, ignore, &escape);
	init_range(s, 0x01, 0x17, docontrol, NULL);
	init_action(s, 0x19, docontrol, NULL);
	init_range(s, 0x1c, 0x1f, docontrol, NULL);
}

static void
init(void)
{
	initialized = 1;
	initstate(&ground, NULL);
	init_range(&ground, 0x20, 0x7f, doprint, NULL);
	initstate(&escape, reset);
	init_action(&escape, 0x21, ignore, &osc_string);
	init_action(&escape, 0x6b, ignore, &osc_string);
	init_action(&escape, 0x5d, ignore, &osc_string);
	init_action(&escape, 0x5e, ignore, &osc_string);
	init_action(&escape, 0x50, ignore, &osc_string);
	init_action(&escape, 0x5f, ignore, &osc_string);
	init_range(&escape, 0x20, 0x2f, collect, &escape_intermediate);
	init_range(&escape, 0x30, 0x4f, doescape, &ground);
	init_range(&escape, 0x51, 0x57, doescape, &ground);
	init_range(&escape, 0x60, 0x7e, doescape, &ground);
	init_range(&escape, 0x59, 0x5a, doescape, &ground);
	init_action(&escape, 0x5b, ignore, &csi_entry);
	init_action(&escape, 0x5c, doescape, &ground);

	initstate(&escape_intermediate, NULL);
	init_range(&escape_intermediate, 0x20, 0x2f, collect, NULL);
	init_range(&escape_intermediate, 0x30, 0x7e, doescape, &ground);

	initstate(&csi_entry, reset);
	init_range(&csi_entry, 0x20, 0x2f, collect, &csi_intermediate);
	init_range(&csi_entry, 0x30, 0x39, param, &csi_param);
	init_action(&csi_entry, 0x3a, ignore, &csi_ignore);
	init_action(&csi_entry, 0x3b, param, &csi_param);
	init_range(&csi_entry, 0x3c, 0x3f, collect, &csi_param);
	init_range(&csi_entry, 0x40, 0x7e, docsi, &ground);

	initstate(&csi_ignore, NULL);
	init_range(&csi_ignore, 0x20, 0x3f, ignore, NULL);
	init_range(&csi_ignore, 0x40, 0x7e, ignore, &ground);

	initstate(&csi_param, NULL);
	init_range(&csi_param, 0x20, 0x2f, collect, &csi_intermediate);
	init_range(&csi_param, 0x30, 0x39, param, NULL);
	init_action(&csi_param, 0x3a, ignore, &csi_ignore);
	init_action(&csi_param, 0x3b, param, NULL);
	init_range(&csi_param, 0x3c, 0x3f, ignore, &csi_ignore);
	init_range(&csi_param, 0x40, 0x7e, docsi, &ground);

	initstate(&csi_intermediate, NULL);
	init_range(&csi_intermediate, 0x20, 0x2f, collect, NULL);
	init_range(&csi_intermediate, 0x30, 0x3f, ignore, &csi_ignore);
	init_range(&csi_intermediate, 0x40, 0x7e, docsi, &ground);

	initstate(&osc_string, reset);
	init_action(&osc_string, 0x07, doosc, &ground);
	init_range(&osc_string, 0x20, 0x7f, collectosc, NULL);
}
