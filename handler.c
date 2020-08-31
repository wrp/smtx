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

/*
 * The names for handlers come from their ANSI/ECMA/DEC mnemonics.
 */

enum cmd {
	noop = 0, ack, bell, cbt, cls, cnl, cpl, cr, csr, cub, cud, cuf, cup,
	cuu, dch, decaln, decid, decreqtparm, dsr, ech, ed, el, hpa, hpr, ht,
	hts, ich, idl, ind, mode, nel, numkp, pnl, print, rc, rep,
	ri, ris, sc, scs, sgr, sgr0, so, su, tab, tbc, vis, vpa, vpr
};

#define CALL(x) tput(v, 0, 0, 0, NULL, x)
void tput(struct vtp *v, wchar_t w, wchar_t iw,
	int argc, int *argv, enum cmd c)
{
	int noclear_repc = 0;
	int p0[2];               /* First arg, defaulting to 0 or 1 */
	int p1;                  /* argv[1], defaulting to 1 */
	int i, t1 = 0, t2 = 0;   /* Some temp ints */
	struct pty *p = v->p;    /* the current pty */
	struct screen *s = p->s; /* the current SCRN buffer */
	WINDOW *win = s->win;    /* the current window */
	int y, x;                /* cursor position */
	int my, mx;              /* max possible values for x and y */
	int py, px;              /* physical cursor position in scrollback */
	int top = 0, bot = 0;    /* the scrolling region */
	int tos = S.history - p->ws.ws_row;
	char buf[32];
	cchar_t b;

	p0[0] = argv && argc > 0 ? argv[0] : 0;
	p0[1] = argv && argc > 0 ? argv[0] : 1;
	p1 = argv && argc > 1 ? argv[1] : 1;
	getyx(win, py, px);
	y = py - tos;
	x = px;
	getmaxyx(win, my, mx);
	my -= tos;
	wgetscrreg(win, &top, &bot);
	bot += 1 - tos;
	top = top <= tos ? 0 : top - tos;
	switch(c) {
	case noop:
		return;
	case bell: /* Terminal bell. */
		beep();
		break;
	case numkp: /* Application/Numeric Keypad Mode */
		p->pnm = (w == L'=');
		break;
	case vis: /* Cursor visibility */
		s->vis = iw == L'6'? 0 : 1;
		break;
	case cup: /* Cursor Position */
		s->xenl = false;
		wmove(win, tos + (p->decom ? top : 0) + p0[1] - 1, p1 - 1);
		break;
	case dch: /* Delete Character */
		for( i = 0; i < p0[1]; i++ ) {
			wdelch(win);
		}
		break;
	case ich: /* Insert Character */
		for( i = 0; i < p0[1]; i++ ) {
			wins_nwstr(win, L" ", 1);
		}
		break;
	case cuu: /* Cursor Up */
		wmove(win, MAX(py - p0[1], tos + top), x);
		break;
	case cud: /* Cursor Down */
		wmove(win, MIN(py + p0[1], tos + bot - 1), x);
		break;
	case cuf: /* Cursor Forward */
		wmove(win, py, MIN(x + p0[1], mx - 1));
		break;
	case ack: /* Acknowledge Enquiry */
		rewrite(p->fd, "\006", 1);
		break;
	case hts: /* Horizontal Tab Set */
		if( x < p->ntabs && x > 0 ) {
			p->tabs[x] = true;
		}
		break;
	case ri: /* Reverse Index (scroll back) */
		wgetscrreg(win, &t1, &t2);
		wsetscrreg(win, t1 >= tos ? t1 : tos, t2);
		y == top ? wscrl(win, -1) : wmove(win, MAX(tos, py - 1), x);
		wsetscrreg(win, t1, t2);
		break;
	case decid: /* Send Terminal Identification */
		if( w == L'c' ) {
			if( iw == L'>' ) {
				rewrite(p->fd, "\033[>1;10;0c", 10);
			} else {
				rewrite(p->fd, "\033[?1;2c", 7);
			}
		} else if( w == L'Z' ) {
			rewrite(p->fd, "\033[?6c", 5);
		}
		break;
	case hpa: /* Cursor Horizontal Absolute */
		wmove(win, py, MIN(p0[1] - 1, mx - 1));
		break;
	case hpr: /* Cursor Horizontal Relative */
		wmove(win, py, MIN(px + p0[1], mx - 1));
		break;
	case vpa: /* Cursor Vertical Absolute */
		wmove(win, MIN(tos + bot - 1, MAX(tos + top, tos + p0[1] - 1)), x);
		break;
	case vpr: /* Cursor Vertical Relative */
		wmove(win, MIN(tos + bot - 1, MAX(tos + top, py + p0[1])), x);
		break;
	case cbt: /* Cursor Backwards Tab */
		for( i = x - 1; i >= 0 && ! p->tabs[i]; i-- ) {
			assert( i < p->ntabs );
		}
		wmove(win, py, i);
		break;
	case ht: /* Horizontal Tab */
		for( i = x + 1; i < mx - 1 && !p->tabs[i]; i++ ) {
			assert(i < p->ntabs);
		}
		wmove(win, py, i);
		break;
	case tab: /* Tab forwards or backwards */
		for( i = 0; i < p0[1]; i++ ) {
			CALL(w == L'Z' ? cbt : ht);
		}
		break;
	case decaln: { /* Screen Alignment Test */
		chtype e[] = { COLOR_PAIR(0) | 'E', 0 };
		for( int r = 0; r < my; r++ ) {
			for( int c = 0; c <= mx; c++ ) {
				mvwaddchnstr(win, tos + r, c, e, 1);
			}
		}
		wmove(win, py, px);
	} break;
	case su: /* Scroll Up/Down */
		wscrl(win, (w == L'T' || w == L'^') ? -p0[1] : p0[1]);
		break;
	case sc: /* Save Cursor */
		s->sx = px;                              /* X position */
		s->sy = py;                              /* Y position */
		wattr_get(win, &s->sattr, &s->sp, NULL); /* attrs/color pair */
		s->sfg = s->fg;                          /* foreground color */
		s->sbg = s->bg;                          /* background color */
		s->oxenl = s->xenl;                      /* xenl state */
		s->saved = true;                         /* data is valid */
		p->sgc = p->gc; p->sgs = p->gs;          /* character sets */
		break;
	case rc: /* Restore Cursor */
		if( iw == L'#' ) {
			CALL(decaln);
			return;
		}
		if( !s->saved ) {
			return;
		}
		wmove(win, s->sy, s->sx);              /* old position */
		wattr_set(win, s->sattr, s->sp, NULL); /* attrs/color pair */
		s->fg = s->sfg;                        /* foreground color */
		s->bg = s->sbg;                        /* background color */
		s->xenl = s->oxenl;                    /* xenl state */
		p->gc = p->sgc; p->gs = p->sgs;        /* save character sets */

		/* restore colors */
		#if HAVE_ALLOC_PAIR
		int cp = alloc_pair(s->fg, s->bg);
		wcolor_set(win, cp, NULL);
		setcchar(&b, L" ", A_NORMAL, cp, NULL);
		wbkgrndset(win, &b);
		#endif
		break;
	case tbc: /* Tabulation Clear */
		if( p->tabs != NULL ) {
			switch( argc >= 0 && argv ? argv[0] : 0 ) {
			case 0:
				p->tabs[x < p->ntabs ? x : 0] = false;
				break;
			case 3:
				memset(p->tabs, 0, p->ntabs * sizeof *p->tabs);
				break;
			}
		}
		break;
	case cub: /* Cursor Backward */
		s->xenl = false;
		wmove(win, py, MAX(x - p0[1], 0));
		break;
	case el: /* Erase in Line */
#if HAVE_ALLOC_PAIR
		setcchar(&b, L" ", A_NORMAL, alloc_pair(s->fg, s->bg), NULL);
#endif
		switch( argc > 0 ? argv[0] : 0 ) {
		case 2:
			wmove(win, py, 0); /* Fall Thru */
		case 0:
			wclrtoeol(win);
			break;
		case 1:
			for( i = 0; i <= x; i++ ) {
				mvwadd_wchnstr(win, py, i, &b, 1);
			}
			break;
		}
		wmove(win, py, x);
		break;
	case ed: /* Erase in Display */
		switch( p0[0] ) {
		case 2:
			wmove(win, tos, 0); /* Fall Thru */
		case 0:
			wclrtobot(win);
			break;
		case 3:
			werase(win);
			break;
		case 1:
			for( i = tos; i < py; i++ ) {
				wmove(win, i, 0);
				wclrtoeol(win);
			}
			wmove(win, py, x);
			tput(v, w, iw, 1, argv, el);
			break;
		}
		wmove(win, py, px);
		break;
	case ech: /* Erase Character */
		#if HAVE_ALLOC_PAIR
		setcchar(&b, L" ", A_NORMAL, alloc_pair(s->fg, s->bg), NULL);
		#endif
		for( i = 0; i < p0[1]; i++ )
			mvwadd_wchnstr(win, py, x + i, &b, 1);
		wmove(win, py, px);
		break;
	case dsr: /* DSR - Device Status Report */
		if( p0[0] == 6 ) {
			i = snprintf(buf, sizeof buf, "\033[%d;%dR",
				(p->decom ? y - top : y) + 1, x + 1);
		} else {
			i = snprintf(buf, sizeof buf, "\033[0n");
		}
		assert( i < (int)sizeof buf ); /* Assumes INT_MAX < 1e14 */
		rewrite(p->fd, buf, i);
		break;
	case idl: /* Insert/Delete Line */
		/* We don't use insdelln here because it inserts above and
		   not below, and has a few other edge cases. */
		i = MIN(p0[1], my - 1 - y);
		wgetscrreg(win, &t1, &t2);
		wsetscrreg(win, py, t2);
		wscrl(win, w == L'L' ? -i : i);
		wsetscrreg(win, t1, t2);
		wmove(win, py, 0);
		break;
	case csr: /* CSR - Change Scrolling Region */
		t1 = argv && argc > 1 ? argv[1] : my;
		if( wsetscrreg(win, tos + p0[1] - 1, tos + t1 - 1) == OK ) {
			CALL(cup);
		}
		break;
case decreqtparm: /* DECREQTPARM - Request Device Parameters */
	if( p0[0] ) {
		rewrite(p->fd, "\033[3;1;2;120;1;0x", 16);
	} else {
		rewrite(p->fd, "\033[2;1;2;120;128;1;0x", 20);
	}
	break;
	case sgr0: /* Reset SGR to default */
		wattrset(win, A_NORMAL);
		wcolor_set(win, 0, NULL);
		s->fg = s->bg = -1;
		wbkgdset(win, COLOR_PAIR(0) | ' ');
		break;
	case cls: /* Clear screen */
		CALL(cup);
		wclrtobot(win);
		CALL(cup);
		break;
	case ris: /* Reset to Initial State */
		ioctl(p->fd, TIOCGWINSZ, &p->ws);
		p->gs = p->gc = p->g0 = CSET_US;
		p->g1 = CSET_GRAPH;
		p->g2 = CSET_US;
		p->g3 = CSET_GRAPH;
		p->decom = s->insert = s->oxenl = s->xenl = p->lnm = false;
		CALL(cls);
		CALL(sgr0);
		p->am = p->pnm = true;
		p->pri.vis = p->alt.vis = 1;
		p->s = &p->pri;
		assert( S.history >= p->ws.ws_row );
		wsetscrreg(p->pri.win, 0, S.history - 1);
		wsetscrreg(p->alt.win, 0, p->ws.ws_row - 1);
		memset(p->tabs, 0, p->ntabs * sizeof *p->tabs);
		for( i = 0; i < p->ntabs; i += p->tabstop ) {
			p->tabs[i] = true;
		}
	break;
	case mode: /* Set or Reset Mode */
	{
		bool set = (w == L'h');
		for( i = 0; i < argc; i++ ) {
			switch( argv[i] ) {
			case  1: p->pnm = set;              break;
			case  3: CALL(cls);                 break;
			case  4: s->insert = set;           break;
			case  6: p->decom = set; CALL(cup); break;
			case  7: p->am = set;               break;
			case 20: p->lnm = set;              break;
			case 25: s->vis = set ? 1 : 0;      break;
			case 34: s->vis = set ? 1 : 2;      break;
			case 1048: CALL((set ? sc : rc));   break;
			case 1049: CALL((set ? sc : rc)); /* fall-thru */
			case 47: case 1047:
				if( set && p->s != &p->alt ) {
					p->s = &p->alt;
					CALL(cls);
				} else if( !set && p->s != &p->pri ) {
					p->s = &p->pri;
				}
			}
		}
	}
		break;
	case sgr: /* SGR - Select Graphic Rendition */
	{
		bool doc = false;
		bool do8 = COLORS >= 8;
		bool do16 = COLORS >= 16;
		bool do256 = COLORS >= 256;
		if( !argc ) {
			CALL(sgr0);
		}
		short bg = s->bg, fg = s->fg;
		for( i = 0; i < argc; i++ ) {
			bool at = argc > i + 2 && argv[i + 1] == 5;
			int val = argc > i + 2 ? argv[i + 2] : 0;
			switch( argv[i] ) {
			case  0: CALL(sgr0);                        break;
			case  1: wattron(win,  A_BOLD);             break;
			case  2: wattron(win,  A_DIM);              break;
			case  4: wattron(win,  A_UNDERLINE);        break;
			case  5: wattron(win,  A_BLINK);            break;
			case  7: wattron(win,  A_REVERSE);          break;
			case  8: wattron(win,  A_INVIS);            break;
			case 22:
				wattroff(win, A_DIM);
				wattroff(win, A_BOLD);
				break;
			case 24:  wattroff(win, A_UNDERLINE);        break;
			case 25:  wattroff(win, A_BLINK);            break;
			case 27:  wattroff(win, A_REVERSE);          break;
			case 30:  fg = COLOR_BLACK;     doc = do8;   break;
			case 31:  fg = COLOR_RED;       doc = do8;   break;
			case 32:  fg = COLOR_GREEN;     doc = do8;   break;
			case 33:  fg = COLOR_YELLOW;    doc = do8;   break;
			case 34:  fg = COLOR_BLUE;      doc = do8;   break;
			case 35:  fg = COLOR_MAGENTA;   doc = do8;   break;
			case 36:  fg = COLOR_CYAN;      doc = do8;   break;
			case 37:  fg = COLOR_WHITE;     doc = do8;   break;
			case 38:
				fg = at ? val : s->fg;
				i += 2;
				doc = do256;
				break;
			case 39:  fg = -1;               doc = true;  break;
			case 40:  bg = COLOR_BLACK;      doc = do8;   break;
			case 41:  bg = COLOR_RED;        doc = do8;   break;
			case 42:  bg = COLOR_GREEN;      doc = do8;   break;
			case 43:  bg = COLOR_YELLOW;     doc = do8;   break;
			case 44:  bg = COLOR_BLUE;       doc = do8;   break;
			case 45:  bg = COLOR_MAGENTA;    doc = do8;   break;
			case 46:  bg = COLOR_CYAN;       doc = do8;   break;
			case 47:  bg = COLOR_WHITE;      doc = do8;   break;
			case 48:
				bg = at ? val : s->bg;
				i += 2;
				doc = do256;
				break;
			case 49:  bg = -1;             doc = true;  break;
			case 90:  fg = COLOR_BLACK;    doc = do16;  break;
			case 91:  fg = COLOR_RED;      doc = do16;  break;
			case 92:  fg = COLOR_GREEN;    doc = do16;  break;
			case 93:  fg = COLOR_YELLOW;   doc = do16;  break;
			case 94:  fg = COLOR_BLUE;     doc = do16;  break;
			case 95:  fg = COLOR_MAGENTA;  doc = do16;  break;
			case 96:  fg = COLOR_CYAN;     doc = do16;  break;
			case 97:  fg = COLOR_WHITE;    doc = do16;  break;
			case 100: bg = COLOR_BLACK;    doc = do16;  break;
			case 101: bg = COLOR_RED;      doc = do16;  break;
			case 102: bg = COLOR_GREEN;    doc = do16;  break;
			case 103: bg = COLOR_YELLOW;   doc = do16;  break;
			case 104: bg = COLOR_BLUE;     doc = do16;  break;
			case 105: bg = COLOR_MAGENTA;  doc = do16;  break;
			case 106: bg = COLOR_CYAN;     doc = do16;  break;
			case 107: bg = COLOR_WHITE;    doc = do16;  break;
			#if HAVE_A_ITALIC
			case  3:  wattron(win,  A_ITALIC); break;
			case 23:  wattroff(win, A_ITALIC); break;
			#endif
			}
		}
#if HAVE_ALLOC_PAIR
		if( doc ) {
			int p = alloc_pair(s->fg = fg, s->bg = bg);
			wcolor_set(win, p, NULL);
			setcchar(&b, L" ", A_NORMAL, p, NULL);
			wbkgrndset(win, &b);
		}
#endif
		noclear_repc = 1;
	}
		break;
	case cr: /* Carriage Return */
		s->xenl = false;
		wmove(win, py, 0);
		break;
	case ind: /* Index */
		y == (bot - 1) ? scroll(win) : wmove(win, py + 1, x);
		break;
	case nel: /* Next Line */
		CALL(cr);
		CALL(ind);
		break;
	case pnl: /* Newline */
		CALL((p->lnm? nel : ind));
		break;
	case cpl: /* CPL - Cursor Previous Line */
		wmove(win, MAX(tos + top, py - p0[1]), 0);
		break;
	case cnl: /* CNL - Cursor Next Line */
		wmove(win, MIN(tos + bot - 1, py + p0[1]), 0);
		break;
	case print: /* Print a character to the terminal */
		if( wcwidth(w) < 0 ) {
			return;
		}
		if( s->insert ) {
			CALL(ich);
		}
		if( s->xenl ) {
			s->xenl = false;
			if( p->am ) {
				CALL(nel);
			}
			getyx(win, y, x);
			y -= tos;
		}
		if( w < MAXMAP && p->gc[w] ) {
			w = p->gc[w];
		}
		p->repc = w;
		if( x == mx - wcwidth(w) ) {
			s->xenl = true;
			wins_nwstr(win, &w, 1);
		} else {
			waddnwstr(win, &w, 1);
		}
		p->gc = p->gs;
		noclear_repc = 1;
		break;
	case rep: /* REP - Repeat Character */
		for( i=0; i < p0[1] && p->repc; i++ ) {
			tput(v, p->repc, 0, 0, NULL, print);
		}
		break;

	case scs: /* Select Character Set */
	{
		wchar_t **t;
		switch( iw ) {
		case L'(': t = &p->g0;  break;
		case L')': t = &p->g1;  break;
		case L'*': t = &p->g2;  break;
		case L'+': t = &p->g3;  break;
		default: return;
		}
		switch( w ) {
		case L'A': *t = CSET_UK;    break;
		case L'B': *t = CSET_US;    break;
		case L'0': *t = CSET_GRAPH; break;
		case L'1': *t = CSET_US;    break;
		case L'2': *t = CSET_GRAPH; break;
		}
	}
		break;
	case so: /* Switch Out/In Character Set */
		if( w == 0x0e ) {
			p->gs = p->gc = p->g1; /* locking shift */
		} else if( w == 0xf ) {
			p->gs = p->gc = p->g0; /* locking shift */
		} else if( w == L'p' ) {
			p->gs = p->gc = p->g2; /* locking shift */
		} else if( w == L'o' ) {
			p->gs = p->gc = p->g3; /* locking shift */
		} else if( w == L'N' ) {
			p->gs = p->gc; /* non-locking shift */
			p->gc = p->g2;
		} else if( w == L'O' ) {
			p->gs = p->gc; /* non-locking shift */
			p->gc = p->g3;
		}
		break;
	}

	if( !noclear_repc ) {
		p->repc = 0;
	}
}

void
setupevents(struct vtp *v)
{
	vtonevent(v, VTPARSER_CONTROL, 0x05, ack);
	vtonevent(v, VTPARSER_CONTROL, 0x07, bell);
	vtonevent(v, VTPARSER_CONTROL, 0x08, cub);
	vtonevent(v, VTPARSER_CONTROL, 0x09, tab);
	vtonevent(v, VTPARSER_CONTROL, 0x0a, pnl);
	vtonevent(v, VTPARSER_CONTROL, 0x0b, pnl);
	vtonevent(v, VTPARSER_CONTROL, 0x0c, pnl);
	vtonevent(v, VTPARSER_CONTROL, 0x0d, cr);
	vtonevent(v, VTPARSER_CONTROL, 0x0e, so);
	vtonevent(v, VTPARSER_CONTROL, 0x0f, so);
	vtonevent(v, VTPARSER_CSI,     L'A', cuu);
	vtonevent(v, VTPARSER_CSI,     L'B', cud);
	vtonevent(v, VTPARSER_CSI,     L'C', cuf);
	vtonevent(v, VTPARSER_CSI,     L'D', cub);
	vtonevent(v, VTPARSER_CSI,     L'E', cnl);
	vtonevent(v, VTPARSER_CSI,     L'F', cpl);
	vtonevent(v, VTPARSER_CSI,     L'G', hpa);
	vtonevent(v, VTPARSER_CSI,     L'H', cup);
	vtonevent(v, VTPARSER_CSI,     L'I', tab);
	vtonevent(v, VTPARSER_CSI,     L'J', ed);
	vtonevent(v, VTPARSER_CSI,     L'K', el);
	vtonevent(v, VTPARSER_CSI,     L'L', idl);
	vtonevent(v, VTPARSER_CSI,     L'M', idl);
	vtonevent(v, VTPARSER_CSI,     L'P', dch);
	vtonevent(v, VTPARSER_CSI,     L'S', su);
	vtonevent(v, VTPARSER_CSI,     L'T', su);
	vtonevent(v, VTPARSER_CSI,     L'X', ech);
	vtonevent(v, VTPARSER_CSI,     L'Z', tab);
	vtonevent(v, VTPARSER_CSI,     L'`', hpa);
	vtonevent(v, VTPARSER_CSI,     L'^', su);
	vtonevent(v, VTPARSER_CSI,     L'@', ich);
	vtonevent(v, VTPARSER_CSI,     L'a', hpr);
	vtonevent(v, VTPARSER_CSI,     L'b', rep);
	vtonevent(v, VTPARSER_CSI,     L'c', decid);
	vtonevent(v, VTPARSER_CSI,     L'd', vpa);
	vtonevent(v, VTPARSER_CSI,     L'e', vpr);
	vtonevent(v, VTPARSER_CSI,     L'f', cup);
	vtonevent(v, VTPARSER_CSI,     L'g', tbc);
	vtonevent(v, VTPARSER_CSI,     L'h', mode);
	vtonevent(v, VTPARSER_CSI,     L'l', mode);
	vtonevent(v, VTPARSER_CSI,     L'm', sgr);
	vtonevent(v, VTPARSER_CSI,     L'n', dsr);
	vtonevent(v, VTPARSER_CSI,     L'r', csr);
	vtonevent(v, VTPARSER_CSI,     L's', sc);
	vtonevent(v, VTPARSER_CSI,     L'u', rc);
	vtonevent(v, VTPARSER_CSI,     L'x', decreqtparm);
	vtonevent(v, VTPARSER_ESCAPE,  L'0', scs);
	vtonevent(v, VTPARSER_ESCAPE,  L'1', scs);
	vtonevent(v, VTPARSER_ESCAPE,  L'2', scs);
	vtonevent(v, VTPARSER_ESCAPE,  L'7', sc);
	vtonevent(v, VTPARSER_ESCAPE,  L'8', rc);
	vtonevent(v, VTPARSER_ESCAPE,  L'A', scs);
	vtonevent(v, VTPARSER_ESCAPE,  L'B', scs);
	vtonevent(v, VTPARSER_ESCAPE,  L'D', ind);
	vtonevent(v, VTPARSER_ESCAPE,  L'E', nel);
	vtonevent(v, VTPARSER_ESCAPE,  L'H', hts);
	vtonevent(v, VTPARSER_ESCAPE,  L'M', ri);
	vtonevent(v, VTPARSER_ESCAPE,  L'Z', decid);
	vtonevent(v, VTPARSER_ESCAPE,  L'c', ris);
	vtonevent(v, VTPARSER_ESCAPE,  L'p', vis);
	vtonevent(v, VTPARSER_ESCAPE,  L'=', numkp);
	vtonevent(v, VTPARSER_ESCAPE,  L'>', numkp);
	vtonevent(v, VTPARSER_PRINT,   0,    print);
	tput(v, L'c', 0, 0, NULL, ris);
}
