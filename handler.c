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
handle_osc(struct pty *p, const char *arg)
{
	const char *parm;
	int cmd = strtol(arg, (char **)&parm, 10);

	parm = *parm == ';' ? parm + 1 : "";
	/* TODO: pick better codes.  Right now, I'm using 60+ simply because
	 * those value appear to be unused by xterm.
	 *
	 * Also: fix the state machine.  Right now, we are just passing
	 * a string like "2;text" instead of parsing the semi-colon in the
	 * state machine.  Yechh.
	 */
	switch( cmd ) {
	case 2:
		snprintf(p->status, sizeof p->status, "%s", parm);
		break;
	case 60:
		build_layout(parm);
		break;
#ifndef NDEBUG
	case 62:
		show_status(parm);
		break;
#endif
	}
}

static void
clear_alt(struct pty *p, int top, int tos)
{
	p->alt.xenl = false;
	p->alt.c.y = tos + (p->decom ? top : 0);
	wmove(p->alt.win, p->alt.c.y, p->alt.c.x = 0);
	wclrtobot(p->alt.win);
}

static void
restore_cursor(struct screen *s)
{
	cchar_t b;
	s->c = s->sc;
	wmove(s->win, s->c.y, s->c.x);
	wattr_set(s->win, s->sattr, s->c.p, NULL);
	s->xenl = s->oxenl;

	/* restore colors */
	wcolor_set(s->win, s->c.p, NULL);
	setcchar(&b, L" ", A_NORMAL, s->c.p, NULL);
	wbkgrndset(s->win, &b);
}

static void
save_cursor(struct screen *s)
{
	wattr_get(s->win, &s->sattr, &s->c.p, NULL);
	s->sc = s->c;
	s->oxenl = s->xenl;
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
decaln(struct pty *p, int tos)
{
	for( int r = 0; r < p->ws.ws_row; r++ ) {
		const chtype e[] = { COLOR_PAIR(0) | 'E', 0 };
		for( int c = 0; c <= p->ws.ws_col; c++ ) {
			mvwaddchnstr(p->s->win, tos + r, c, e, 1);
		}
	}
	wmove(p->s->win, p->s->c.y, p->s->c.x);
}


static void
newline(struct pty *p, int cr, int y, int bot)
{
	if( cr ) {
		p->s->xenl = false;
		p->s->c.x = 0;
	}
	if( y == bot - 1 ) {
		wmove(p->s->win, p->s->c.y, p->s->c.x);
		scroll(p->s->win);
	} else {
		wmove(p->s->win, ++p->s->c.y, p->s->c.x);
	}
}

static void
print_char(wchar_t w, struct pty *p, int y, int bot)
{
	if( p->s->insert ) {
		insert_space(1, p->s->win);
	}
	if( p->s->xenl ) {
		p->s->xenl = false;
		if( p->decawm ) {
			newline(p, 1, y, bot);
		}
	}
	if( w < 0x7f && p->s->c.gc[w] ) {
		w = p->s->c.gc[w];
	}
	p->repc = w;
	if( p->s->c.x == p->ws.ws_col - wcwidth(w) ) {
		p->s->xenl = true;
		wins_nwstr(p->s->win, &w, 1);
	} else {
		waddnwstr(p->s->win, &w, 1);
		p->s->c.x += wcwidth(w);
	}
	p->s->c.gc = p->s->c.gs;
}

void
tput(struct vtp *v, wchar_t w, wchar_t iw, int argc, void *arg, int handler)
{
	int p0[2];       /* First arg, defaulting to 0 or 1 */
	int p1;          /* argv[1], defaulting to 1 */
	int i, t1 = 0, t2 = 0;
	int y;           /* cursor position */
	int tos;         /* top of screen */
	int top = 0, bot = 0;/* the scrolling region */
	char buf[32];
	cchar_t b;

	int *argv = arg;
	enum cmd c = handler;
	struct pty *p = v->p;    /* the current pty */
	struct screen *s = p->s; /* the current SCRN buffer */
	WINDOW *win = s->win;    /* the current window */

	p0[0] = argv && argc > 0 ? argv[0] : 0;
	p0[1] = argv && argc > 0 ? argv[0] : 1;
	p1 = argv && argc > 1 ? argv[1] : 1;
	getyx(win, s->c.y, s->c.x);
	s->maxy = MAX(s->c.y, s->maxy);
	tos = MAX(0, s->maxy - p->ws.ws_row + 1);
	y = s->c.y - tos;
	wgetscrreg(win, &top, &bot);
	bot -= tos - 1;
	top = top <= tos ? 0 : top - tos;
	switch(c) {
	case ack: /* Acknowledge Enquiry */
		rewrite(p->fd, "\006", 1);
		break;
	case bell: /* Terminal bell. */
		beep();
		break;
	case cr: /* Carriage Return */
		s->xenl = false;
		wmove(win, s->c.y, s->c.x = 0);
		break;
	case csr: /* CSR - Change Scrolling Region */
		t1 = argv && argc > 1 ? argv[1] : p->ws.ws_row;
		if( wsetscrreg(win, tos + p0[1] - 1, tos + t1 - 1) == OK ) {
			s->xenl = false;
			s->c.y = tos + (p->decom ? top : 0);
			wmove(s->win, s->c.y, s->c.x = 0);
		}
		break;
	case cub: /* Cursor Backward */
		s->xenl = false;
		s->c.x = MAX(s->c.x - p0[1], 0);
		wmove(win, s->c.y, s->c.x);
		break;
	case cud: /* Cursor Down */
		s->c.y = MIN(s->c.y + p0[1], tos + bot - 1);
		wmove(win, s->c.y, s->c.x);
		break;
	case cuf: /* Cursor Forward */
		s->c.x = MIN(s->c.x + p0[1], p->ws.ws_col - 1);
		wmove(win, s->c.y, s->c.x);
		break;
	case cup: /* Cursor Position */
		s->xenl = false;
		s->c.y = tos + (p->decom ? top : 0) + p0[1] - 1;
		s->c.x = p1 - 1;
		wmove(win, s->c.y, s->c.x);
		break;
	case cuu: /* Cursor Up */
		s->c.y = MAX(s->c.y - p0[1], tos + top);
		wmove(win, s->c.y, s->c.x);
		break;
	case dch: /* Delete Character */
		for( i = 0; i < p0[1]; i++ ) {
			wdelch(win);
		}
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
	case dsr: /* DSR - Device Status Report */
		if( p0[0] == 6 ) {
			i = snprintf(buf, sizeof buf, "\033[%d;%dR",
				(p->decom ? y - top : y) + 1, s->c.x + 1);
		} else {
			i = snprintf(buf, sizeof buf, "\033[0n");
		}
		assert( i < (int)sizeof buf ); /* Assumes INT_MAX < 1e14 */
		rewrite(p->fd, buf, i);
		break;
	case ech: /* Erase Character */
		setcchar(&b, L" ", A_NORMAL, s->c.p, NULL);
		for( i = 0; i < p0[1]; i++ )
			mvwadd_wchnstr(win, s->c.y, s->c.x + i, &b, 1);
		wmove(win, s->c.y, s->c.x);
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
			for( i = tos; i < s->c.y; i++ ) {
				wmove(win, i, 0);
				wclrtoeol(win);
			}
			wmove(win, s->c.y, s->c.x);
			tput(v, w, iw, 1, argv, el);
			break;
		}
		wmove(win, s->c.y, s->c.x);
		break;
	case el: /* Erase in Line */
		setcchar(&b, L" ", A_NORMAL, s->c.p, NULL);
		switch( argc > 0 ? argv[0] : 0 ) {
		case 2:
			wmove(win, s->c.y, 0); /* Fall Thru */
		case 0:
			wclrtoeol(win);
			break;
		case 1:
			for( i = 0; i <= s->c.x; i++ ) {
				mvwadd_wchnstr(win, s->c.y, i, &b, 1);
			}
		}
		wmove(win, s->c.y, s->c.x);
		break;
	case hpa: /* Cursor Horizontal Absolute */
		s->c.x = MIN(p0[1] - 1, p->ws.ws_col - 1);
		wmove(win, s->c.y, s->c.x);
		break;
	case hpr: /* Cursor Horizontal Relative */
		s->c.x = MIN(s->c.x + p0[1], p->ws.ws_col - 1);
		wmove(win, s->c.y, s->c.x);
		break;
	case hts: /* Horizontal Tab Set */
		if( s->c.x < p->ntabs && s->c.x > 0 ) {
			p->tabs[s->c.x] = true;
		}
		break;
	case ich: /* Insert Character */
		insert_space(p0[1], win);
		break;
	case idl: /* Insert/Delete Line */
		/* We don't use insdelln here because it inserts above and
		   not below, and has a few other edge cases. */
		i = MIN(p0[1], p->ws.ws_row - 1 - y);

		assert( y == s->c.y - tos);
		assert( tos == 0 || p->ws.ws_row - 1 - y == s->maxy - s->c.y );

		wgetscrreg(win, &t1, &t2);
		wsetscrreg(win, s->c.y, t2);
		wscrl(win, w == L'L' ? -i : i);
		wsetscrreg(win, t1, t2);
		wmove(win, s->c.y, s->c.x = 0);
		break;
	case numkp: /* Application/Numeric Keypad Mode */
		p->pnm = (w == L'=');
		break;
	case rc: /* Restore Cursor */
		if( iw == L'#' ) {
			decaln(p, tos);
		} else if( s->sc.gc ) {
			restore_cursor(s);
		}
		break;
	case ri: /* Reverse Index (scroll back) */
		wgetscrreg(win, &t1, &t2);
		wsetscrreg(win, t1 >= tos ? t1 : tos, t2);
		if( y == top ) {
			 wscrl(win, -1);
		} else {
			s->c.y = MAX(tos, s->c.y - 1);
			wmove(win, s->c.y, s->c.x);
		}
		wsetscrreg(win, t1, t2);
		break;
	case sc: /* Save Cursor */
		save_cursor(s);
		break;
	case su: /* Scroll Up/Down */
		wscrl(win, (w == L'T' || w == L'^') ? -p0[1] : p0[1]);
		break;
	case tab: /* Tab forwards or backwards */
		while( p0[1] && s->c.x > 0 && s->c.x < p->ws.ws_col - 1 ) {
			s->c.x += w == L'Z' ? -1 : +1;
			if( p->tabs[s->c.x] ) {
				p0[1] -= 1;
			}
		}
		wmove(win, s->c.y, s->c.x);
		break;
	case tbc: /* Tabulation Clear */
		if( p->tabs != NULL ) {
			if( p0[0] == 0 ) {
				if( s->c.x < p->ntabs ) {
					p->tabs[s->c.x] = false;
				}
			} else if( p0[0] == 3 ) {
				memset(p->tabs, 0, p->ntabs * sizeof *p->tabs);
			}
		}
		break;
	case osc: /* Operating System Command */
		handle_osc(p, arg);
		break;
	case vis: /* Cursor visibility */
		s->vis = iw == L'6'? 0 : 1;
		break;
	case vpa: /* Cursor Vertical Absolute */
		s->c.y = MIN(tos + bot - 1, MAX(tos + top, tos + p0[1] - 1));
		wmove(win, s->c.y, s->c.x);
		break;
	case vpr: /* Cursor Vertical Relative */
		s->c.y = MIN(tos + bot - 1, MAX(tos + top, s->c.y + p0[1]));
		wmove(win, s->c.y, s->c.x);
		break;
	case decreqtparm: /* DECREQTPARM - Request Device Parameters */
		if( p0[0] ) {
			rewrite(p->fd, "\033[3;1;2;120;1;0x", 16);
		} else {
			rewrite(p->fd, "\033[2;1;2;120;128;1;0x", 20);
		}
		break;
	case ris: /* Reset to Initial State */
		ioctl(p->fd, TIOCGWINSZ, &p->ws);
		p->g0 = CSET_US;
		p->g1 = CSET_GRAPH;
		p->g2 = CSET_US;
		p->g3 = CSET_GRAPH;
		p->pri.c.gs = p->pri.c.gc = p->g0;
		p->alt.c.gs = p->alt.c.gc = p->g0;
		p->decom = s->insert = s->oxenl = s->xenl = p->lnm = false;
		reset_sgr(s);
		p->decawm = p->pnm = true;
		p->pri.vis = p->alt.vis = 1;
		p->s = &p->pri;

		wsetscrreg(p->pri.win, 0, p->pri.rows - 1);
		wsetscrreg(p->alt.win, 0, p->alt.rows - 1);
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
			case  3: set_width(set ? "132" : "80"); break;
			case  4: s->insert = set;           break;
			case  6:
				p->decom = set;
				s->xenl = false;
				s->c.y = tos + (p->decom ? top : 0);
				wmove(win, s->c.y, s->c.x = 0);
				break;
			case  7: p->decawm = set;           break;
			case 20: p->lnm = set;              break;
			case 25: s->vis = set ? 1 : 0;      break;
			case 34: s->vis = set ? 1 : 2;      break;
			case 1048:
				(set ? save_cursor : restore_cursor)(s);
				break;
			case 1049:
				(set ? save_cursor : restore_cursor)(s);
				/* fall-thru */
			case 47: case 1047:
				if( set && p->s != &p->alt ) {
					clear_alt(p, top, tos);
				}
				p->s = set ? &p->alt : &p->pri;
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
			reset_sgr(s);
		}
		short fg, bg;
		pair_content(s->c.p, &fg, &bg);
		for( i = 0; i < argc; i++ ) {
			bool at = argc > i + 2 && argv[i + 1] == 5;
			int val = argc > i + 2 ? argv[i + 2] : 0;
			switch( argv[i] ) {
			case  0:
				reset_sgr(s);
				break;
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
				if( at ) {
					fg = val;
				}
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
				if( at ) {
					bg = val;
				}
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
		if( doc ) {
			s->c.p = alloc_pair(fg, bg);
			wcolor_set(win, s->c.p, NULL);
			setcchar(&b, L" ", A_NORMAL, s->c.p, NULL);
			wbkgrndset(win, &b);
		}
	}
		break;
	case pnl: /* Newline */
		newline(p, p->lnm, y, bot);
		break;
	case nel: /* Next Line */
		newline(p, 1, y, bot);
		break;
	case ind: /* Index */
		newline(p, 0, y, bot);
		break;
	case cpl: /* CPL - Cursor Previous Line */
		s->c.y = MAX(tos + top, s->c.y - p0[1]);
		wmove(win, s->c.y, s->c.x = 0);
		break;
	case cnl: /* CNL - Cursor Next Line */
		s->c.y = MIN(tos + bot - 1, s->c.y + p0[1]);
		wmove(win, s->c.y, s->c.x = 0);
		break;
	case print: /* Print a character to the terminal */
		if( wcwidth(w) > 0 ) {
			print_char(w, p, y, bot);
		}
		break;
	case rep: /* REP - Repeat Character */
		for( i=0; i < p0[1] && p->repc; i++ ) {
			print_char(p->repc, p, y, bot);
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
			p->s->c.gs = p->s->c.gc = p->g1; /* locking shift */
		} else if( w == 0xf ) {
			p->s->c.gs = p->s->c.gc = p->g0; /* locking shift */
		} else if( w == L'}' ) {
			p->s->c.gs = p->s->c.gc = p->g2; /* locking shift */
		} else if( w == L'|' ) {
			p->s->c.gs = p->s->c.gc = p->g3; /* locking shift */
		} else if( w == L'N' ) {
			p->s->c.gs = p->s->c.gc; /* non-locking shift */
			p->s->c.gc = p->g2;
		} else if( w == L'O' ) {
			p->s->c.gs = p->s->c.gc; /* non-locking shift */
			p->s->c.gc = p->g3;
		}
		break;
	}
	if( handler != sgr && handler != print ) {
		p->repc = 0;
	}
	s->maxy = MAX(s->c.y, s->maxy);
}

int cons[MAXCALLBACK] = {
	[0x05] = ack,
	[0x07] = bell,
	[0x08] = cub,
	[0x09] = tab,
	[0x0a] = pnl,
	[0x0b] = pnl,
	[0x0c] = pnl,
	[0x0d] = cr,
	[0x0e] = so,
	[0x0f] = so,
};
int csis[MAXCALLBACK] = {
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
	[L'n'] = dsr,
	[L'r'] = csr,
	[L's'] = sc,
	[L'u'] = rc,
	[L'x'] = decreqtparm,
};
int escs[MAXCALLBACK] = {
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
