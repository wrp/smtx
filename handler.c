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

static void
handle_osc(struct pty *p, int cmd, const char *arg)
{
	switch( cmd ) {
	case 2: snprintf(p->status, sizeof p->status, "%s", arg);
	Kase 60: build_layout(arg);
#ifndef NDEBUG
	Kase 62:
		show_status(arg);
#endif
	}
}

static void
clear_scr(struct screen *s, int top)
{
	s->c.x = s->c.xenl = 0;
	s->c.y = top;
	wclear(s->win);
}

static void
restore_cursor(struct screen *s)
{
	if( s->sc.gc ) {
		cchar_t b;
		s->c = s->sc;
		wattr_set(s->win, s->sattr, s->c.p, NULL);

		/* restore colors */
		wcolor_set(s->win, s->c.p, NULL);
		setcchar(&b, L" ", A_NORMAL, s->c.p, NULL);
		wbkgrndset(s->win, &b);
	}
}

static void
save_cursor(struct screen *s)
{
	wattr_get(s->win, &s->sattr, &s->c.p, NULL);
	s->sc = s->c;
}

static void
reset_sgr(struct screen *s)
{
	wattrset(s->win, A_NORMAL);
	wcolor_set(s->win, s->c.p = 0, NULL);
	wbkgdset(s->win, COLOR_PAIR(0) | ' ');
}

static void
insert_space(int count, WINDOW *win)
{
	while( count-- ) {
		wins_wstr(win, L" ");
	}
}

static void
newline(struct pty *p, int cr)
{
	if( cr ) {
		p->s->c.xenl = p->s->c.x = 0;
	}
	if( p->s->c.y == p->s->scroll.bot ) {
		scroll(p->s->win);
	} else {
		wmove(p->s->win, ++p->s->c.y, p->s->c.x);
	}
}

static void
print_char(wchar_t w, struct pty *p)
{
	if( p->s->insert ) {
		insert_space(1, p->s->win);
	}
	if( p->s->c.xenl && p->decawm ) {
		newline(p, 1);
	}
	p->s->c.xenl = 0;
	if( w < 0x7f && p->s->c.gc[w] ) {
		w = p->s->c.gc[w];
	}
	p->repc = w;
	if( p->s->c.x == p->ws.ws_col - wcwidth(w) ) {
		p->s->c.xenl = 1;
		wins_nwstr(p->s->win, &w, 1);
	} else {
		waddnwstr(p->s->win, &w, 1);
		p->s->c.x += wcwidth(w);
	}
	p->s->c.gc = p->s->c.gs;
}

static short colors[] = {
	COLOR_BLACK,
	COLOR_RED,
	COLOR_GREEN,
	COLOR_YELLOW,
	COLOR_BLUE,
	COLOR_MAGENTA,
	COLOR_CYAN,
	COLOR_WHITE,
};

static int attrs[] = {
	[1] = A_BOLD,
	[2] = A_DIM,
	[4] = A_UNDERLINE,
	[5] = A_BLINK,
	[7] = A_REVERSE,
	[8] = A_INVIS,
};

void
tput(struct pty *p, wchar_t w, wchar_t iw, int argc, void *arg, int handler)
{
	int i, t1;
	cchar_t b;

	/* First arg, defaulting to 0 or 1 */
	int p0[] = { argc ? *(int*)arg : 0, argc ? *(int*)arg : 1 };
	int *argv = arg;
	struct screen *s = p->s; /* the current SCRN buffer */
	WINDOW *win = s->win;    /* the current window */

	const int tos = s->tos;
	const int y = s->c.y - tos; /* cursor position w.r.t. top of screen */
	const int bot = s->scroll.bot - tos + 1;
	const int top = MAX(0, s->scroll.top - tos);
	const int dtop = tos + (p->decom ? top : 0);

	switch( (enum cmd)handler ) {
	case ack: rewrite(p->fd, "\006", 1);
	Kase bell: beep();
	Kase cr: s->c.xenl = s->c.x = 0;
	Kase csr:
		t1 = argc > 1 ? argv[1] : p->ws.ws_row;
		if( wsetscrreg(win, tos + p0[1] - 1, tos + t1 - 1) == OK ) {
			s->scroll.top = tos + p0[1] - 1;
			s->scroll.bot = tos + t1 - 1;
			s->c.y = dtop;
			s->c.xenl = s->c.x = 0;
		}
	Kase cub:
		s->c.xenl = 0;
		s->c.x -= p0[1];
	Kase cud: s->c.y += p0[1];
	Kase cuf: s->c.x += p0[1];
	Kase cup:
		s->c.xenl = 0;
		s->c.y = dtop + p0[1] - 1;
		s->c.x = argc > 1 ? argv[1] - 1 : 0;
	Kase cuu: s->c.y -= p0[1];
	Kase dch:
		for( i = 0; i < p0[1]; i++ ) {
			wdelch(win);
		}
	Kase decid:
		if( w == L'c' ) {
			if( iw == L'>' ) {
				rewrite(p->fd, "\033[>1;10;0c", 10);
			} else {
				rewrite(p->fd, "\033[?1;2c", 7);
			}
		} else if( w == L'Z' ) {
			rewrite(p->fd, "\033[?6c", 5);
		}
	Kase ech:
		setcchar(&b, L" ", A_NORMAL, s->c.p, NULL);
		for( i = 0; i < p0[1]; i++ ) {
			mvwadd_wchnstr(win, s->c.y, s->c.x + i, &b, 1);
		}
	Kase ed: /* Fallthru */
	case el:
		switch( p0[0] ) {
		case 2: wmove(win, handler == el ? s->c.y : tos, 0);
			/* Fall Thru */
		case 0: (handler == el ? wclrtoeol : wclrtobot)(win);
		Kase 3:
			if( handler == ed ) {
				werase(win);
			}
		Kase 1:
			if( handler == ed ) {
				for( i = tos; i < s->c.y; i++ ) {
					wmove(win, i, 0);
					wclrtoeol(win);
				}
				wmove(win, s->c.y, s->c.x);
			}
			setcchar(&b, L" ", A_NORMAL, s->c.p, NULL);
			for( i = 0; i <= s->c.x; i++ ) {
				mvwadd_wchnstr(win, s->c.y, i, &b, 1);
			}
		}
	Kase hpa: s->c.x = -1; /* Fallthru */
	case hpr: s->c.x += p0[1];
	Kase hts: p->tabs[s->c.x] = true;
	Kase ich: insert_space(p0[1], win);
	Kase idl:
		/* We don't use insdelln here because it inserts above and
		   not below, and has a few other edge cases. */
		i = MIN(p0[1], p->ws.ws_row - 1 - y);

		assert( y == s->c.y - tos);
		assert( tos == 0 || p->ws.ws_row - 1 - y == s->maxy - s->c.y );

		wsetscrreg(win, s->c.y, s->scroll.bot);
		wscrl(win, w == L'L' ? -i : i);
		wsetscrreg(win, s->scroll.top, s->scroll.bot);
		s->c.x = 0;
	Kase numkp:
		p->pnm = (w == L'=');
	Kase rc:
		if( iw == L'#' ) for( int r = 0; r < p->ws.ws_row; r++ ) {
			const chtype e[] = { COLOR_PAIR(0) | 'E', 0 };
			for( int c = 0; c < p->ws.ws_col; c++ ) {
				mvwaddchnstr(p->s->win, tos + r, c, e, 1);
			}
		}
		restore_cursor(s);
	Kase ri:
		if( y == top ) {
			wsetscrreg(win, MAX(s->scroll.top, tos), s->scroll.bot);
			wscrl(win, -1);
		} else {
			s->c.y = MAX(tos, s->c.y - 1);
		}
		wsetscrreg(win, s->scroll.top, s->scroll.bot);
	Kase sc:
		save_cursor(s);
	Kase su:
		wscrl(win, (w == L'T' || w == L'^') ? -p0[1] : p0[1]);
	Kase tab:
		while( p0[1] ) {
			s->c.x += w == L'Z' ? -1 : +1;
			if( p->tabs[s->c.x] ) {
				p0[1] -= 1;
			}
		}
	Kase tbc:
		if( p0[0] == 0 ) {
			p->tabs[s->c.x] = false;
		} else if( p0[0] == 3 ) {
			memset(p->tabs, 0, p->ws.ws_col * sizeof *p->tabs);
		}
	Kase osc:
		{
		/* TODO: parse properly in the vt state machine */
		const char *parm;
		int cmd = strtol(arg, (char **)&parm, 10);
		handle_osc(p, cmd, parm ? parm + 1 : "");
		}
	Kase vis:
		s->vis = iw != L'6';
	Kase vpa:
		s->c.y = tos - 1; /* Fallthru */
	case vpr:
		s->c.y = MAX(tos + top, p0[1] + s->c.y);
	Kase decreqtparm:
		if( p0[0] ) {
			rewrite(p->fd, "\033[3;1;2;120;1;0x", 16);
		} else {
			rewrite(p->fd, "\033[2;1;2;120;128;1;0x", 20);
		}
	Kase ris:
		ioctl(p->fd, TIOCGWINSZ, &p->ws);
		p->scr[0].c.gs = p->scr[0].c.gc = p->g[0] = CSET_US;
		p->scr[1].c.gs = p->scr[1].c.gc = p->g[2] = CSET_US;
		p->g[1] = p->g[3] = CSET_GRAPH;
		p->decom = s->insert = p->lnm = false;
		s->c.xenl = 0;
		reset_sgr(s);
		p->decawm = p->pnm = true;
		p->scr[0].vis = p->scr[1].vis = 1;
		s = &p->scr[1];
		wsetscrreg(s->win, s->scroll.top=0, s->scroll.bot=s->rows - 1);
		s = p->s = &p->scr[0];
		wsetscrreg(s->win, s->scroll.top=0, s->scroll.bot=s->rows - 1);
		set_tabs(p, p->tabstop);
		vtreset(&p->vp);
	Kase mode:
		for( i = 0; i < argc; i++ ) {
			bool set = (w == L'h');
			switch( argv[i] ) {
			case  1: p->pnm = set;
			Kase  3: set_width(set ? "132" : "80");
			Kase  4: s->insert = set;
			Kase  6:
				p->decom = set;
				s->c.x = s->c.xenl = 0;
				s->c.y = dtop;
			Kase  7: p->decawm = set;
			Kase 20: p->lnm = set;
			Kase 25: s->vis = set ? 1 : 0;
			Kase 34: s->vis = set ? 1 : 2;
			Kase 1048:
			case 1049:
				(set ? save_cursor : restore_cursor)(s);
				if( argv[i] == 1049 ) {
			case 47:
			case 1047:
					if( set && p->s == p->scr ) {
						clear_scr(p->scr + 1, dtop);
					}
					p->s = p->scr + (set ? 1 : 0);
				}
			}
		}
	Kase sgr:
	{
		bool doc = false;
		bool do8 = COLORS >= 8;
		bool do16 = COLORS >= 16;
		bool do256 = COLORS >= 256;
		if( !argc ) {
			reset_sgr(s);
		}
		short fg, bg;
		pair_content(s->c.p, &fg, &bg);
		for( i = 0; i < argc; i++ ) {
			bool at = argc > i + 2 && argv[i + 1] == 5;
			int val = argc > i + 2 ? argv[i + 2] : 0, k = 0, a;
			switch( a = argv[i] ) {
			case  0: reset_sgr(s);
			Kase  1:
			case  2:
			case  4:
			case  5:
			case  7:
			case  8:
				wattron(win, attrs[argv[i]]);
			Kase 22:
				wattroff(win, A_DIM);
				wattroff(win, A_BOLD);
			Kase 24:
			case 25:
			case 27:
				wattroff(win, attrs[argv[i] - 20]);
			Kase 30:
			case 31:
			case 32:
			case 33:
			case 34:
			case 35:
			case 36:
			case 37:
				k = 1; /* Fallthru */
			case 40:
			case 41:
			case 42:
			case 43:
			case 44:
			case 45:
			case 46:
			case 47:
				*(k ? &fg : &bg) = colors[a - (k ? 30 : 40)];
				doc = do8;
			Kase 38:
			case 48:
				if( at ) {
					*(a == 48 ? &bg : &fg) = val;
				}
				i += 2;
				doc = do256;
			Kase 39:
			case 49:
				*(a == 49 ? &bg : &fg) = -1;
				doc = true;
			Kase 90:
			case 91:
			case 92:
			case 93:
			case 94:
			case 95:
			case 96:
			case 97:
				k = 1; /* Fallthru */
			case 100:
			case 101:
			case 102:
			case 103:
			case 104:
			case 105:
			case 106:
			case 107:
				*(k ? &fg : &bg) = colors[a - (k ? 90 : 100)];
				doc = do16;
			#if HAVE_A_ITALIC
			Kase  3:  wattron(win,  A_ITALIC);
			Kase 23:  wattroff(win, A_ITALIC);
			#endif
			}
		}
		if( doc ) {
			s->c.p = alloc_pair(fg, bg);
			wcolor_set(win, s->c.p, NULL);
			setcchar(&b, L" ", A_NORMAL, s->c.p, NULL);
			wbkgrndset(win, &b);
		}
	}
	Kase pnl:
	case nel:
	case ind:
		newline(p, handler == pnl ? p->lnm : handler == nel);
	Kase cpl:
		s->c.y = MAX(tos + top, s->c.y - p0[1]);
		s->c.x = 0;
	Kase cnl:
		s->c.y = MIN(tos + bot - 1, s->c.y + p0[1]);
		s->c.x = 0;
	Kase print:
		if( wcwidth(w) > 0 ) {
			print_char(w, p);
		}
	Kase rep:
		for( i=0; i < p0[1] && p->repc; i++ ) {
			print_char(p->repc, p);
		}
	Kase scs:
	{
		wchar_t **t;
		switch( iw ) {
		default: return;
		Kase L'(': t = &p->g[0];
		Kase L')': t = &p->g[1];
		Kase L'*': t = &p->g[2];
		Kase L'+': t = &p->g[3];
		}
		switch( w ) {
		case L'A': *t = CSET_UK;
		Kase L'B': *t = CSET_US;
		Kase L'0': *t = CSET_GRAPH;
		Kase L'1': *t = CSET_US;
		Kase L'2': *t = CSET_GRAPH;
		}
	}
	Kase so:
		for( char *s = "\x0f\x0e}|NO", *c = strchr(s, w); c; c = NULL )
		switch( c - s ) {
		case 0: /* locking shift */
		case 1:
		case 2:
		case 3:
			p->s->c.gs = p->s->c.gc = p->g[c - s];
		Kase 4:
		case 5: /* non-locking shift */
			p->s->c.gs = p->s->c.gc;
			p->s->c.gc = p->g[c - s - 2];
		}
	}
	if( handler != sgr && handler != print ) {
		p->repc = 0;
	}
	s->c.x = MAX(0, MIN(s->c.x, p->ws.ws_col - 1));
	s->c.y = MAX(0, MIN(s->c.y, tos + bot - 1));
	s->maxy = MAX(s->c.y, s->maxy);
	s->tos = MAX(0, s->maxy - p->ws.ws_row + 1);
	wmove(win, s->c.y, s->c.x);
}

#define CONTROL \
	[0x05] = ack, \
	[0x07] = bell, \
	[0x08] = cub, \
	[0x09] = tab, \
	[0x0a] = pnl, \
	[0x0b] = pnl, \
	[0x0c] = pnl, \
	[0x0d] = cr, \
	[0x0e] = so, \
	[0x0f] = so

int cons[0x80] = {
	CONTROL,
};

int csis[0x80] = {
	CONTROL,
	[L'A'] = cuu,
	[L'B'] = cud,
	[L'C'] = cuf,
	[L'D'] = cub,
	[L'E'] = cnl,
	[L'F'] = cpl,
	[L'G'] = hpa,
	[L'H'] = cup,
	[L'I'] = tab,
	[L'J'] = ed,
	[L'K'] = el,
	[L'L'] = idl,
	[L'M'] = idl,
	[L'P'] = dch,
	[L'S'] = su,
	[L'T'] = su,
	[L'X'] = ech,
	[L'Z'] = tab,
	[L'`'] = hpa,
	[L'^'] = su,
	[L'@'] = ich,
	[L'a'] = hpr,
	[L'b'] = rep,
	[L'c'] = decid,
	[L'd'] = vpa,
	[L'e'] = vpr,
	[L'f'] = cup,
	[L'g'] = tbc,
	[L'h'] = mode,
	[L'l'] = mode,
	[L'm'] = sgr,
	[L'r'] = csr,
	[L's'] = sc,
	[L'u'] = rc,
	[L'x'] = decreqtparm,
};

int escs[0x80] = {
	CONTROL,
	[L'0'] = scs,
	[L'1'] = scs,
	[L'2'] = scs,
	[L'7'] = sc,
	[L'8'] = rc,
	[L'A'] = scs,
	[L'B'] = scs,
	[L'D'] = ind,
	[L'E'] = nel,
	[L'H'] = hts,
	[L'M'] = ri,
	[L'N'] = so,
	[L'O'] = so,
	[L'}'] = so,
	[L'|'] = so,
	[L'Z'] = decid,
	[L'c'] = ris,
	[L'p'] = vis,
	[L'='] = numkp,
	[L'>'] = numkp,
};

#pragma GCC diagnostic ignored "-Woverride-init"
int oscs[0x80] = {
	[0 ... 0x7f] = osc,
	CONTROL,
	[0x07] = osc,
	[0x0a] = osc,
	[0x0d] = osc,
};

int gnds[0x80] = {
	[0 ... 0x7f] = print,
	CONTROL,
};
