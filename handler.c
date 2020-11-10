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
	p->alt.c.xenl = 0;
	p->alt.c.y = tos + (p->decom ? top : 0);
	wmove(p->alt.win, p->alt.c.y, p->alt.c.x = 0);
	wclrtobot(p->alt.win);
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
decaln(struct pty *p, int tos)
{
	for( int r = 0; r < p->ws.ws_row; r++ ) {
		const chtype e[] = { COLOR_PAIR(0) | 'E', 0 };
		for( int c = 0; c <= p->ws.ws_col; c++ ) {
			mvwaddchnstr(p->s->win, tos + r, c, e, 1);
		}
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
tput(struct vtp *v, wchar_t w, wchar_t iw, int argc, void *arg, int handler)
{
	int i, t1;
	cchar_t b;

	/* First arg, defaulting to 0 or 1 */
	int p0[] = { argc ? *(int*)arg : 0, argc ? *(int*)arg : 1 };
	int *argv = arg;
	struct pty *p = v->p;    /* the current pty */
	struct screen *s = p->s; /* the current SCRN buffer */
	WINDOW *win = s->win;    /* the current window */

	int tos = s->tos;
	int y = s->c.y - tos;   /* cursor position relative to top of screen */
	int bot = s->scroll.bot - tos + 1;
	int top = MAX(0, s->scroll.top - tos);

	switch( (enum cmd)handler ) {
	case ack: /* Acknowledge Enquiry */
		rewrite(p->fd, "\006", 1);
		break;
	case bell: /* Terminal bell. */
		beep();
		break;
	case cr: /* Carriage Return */
		s->c.xenl = s->c.x = 0;
		break;
	case csr: /* CSR - Change Scrolling Region */
		t1 = argc > 1 ? argv[1] : p->ws.ws_row;
		if( wsetscrreg(win, tos + p0[1] - 1, tos + t1 - 1) == OK ) {
			s->scroll.top = tos + p0[1] - 1;
			s->scroll.bot = tos + t1 - 1;
			s->c.y = tos + (p->decom ? top : 0);
			s->c.xenl = s->c.x = 0;
		}
		break;
	case cub: /* Cursor Backward */
		s->c.xenl = 0;
		s->c.x -= p0[1];
		break;
	case cud: /* Cursor Down */
		s->c.y += p0[1];
		break;
	case cuf: /* Cursor Forward */
		s->c.x += p0[1];
		break;
	case cup: /* Cursor Position */
		s->c.xenl = 0;
		s->c.y = tos + (p->decom ? top : 0) + p0[1] - 1;
		s->c.x = argc > 1 ? argv[1] - 1 : 0;
		break;
	case cuu: /* Cursor Up */
		s->c.y -= p0[1];
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
			char buf[32];
			i = snprintf(buf, sizeof buf, "\033[%d;%dR",
				(p->decom ? y - top : y) + 1, s->c.x + 1);
			assert( i < (int)sizeof buf ); /* INT_MAX < 1e14 */
			rewrite(p->fd, buf, i);
		} else {
			rewrite(p->fd, "\033[0n", 4);
		}
		break;
	case ech: /* Erase Character */
		setcchar(&b, L" ", A_NORMAL, s->c.p, NULL);
		for( i = 0; i < p0[1]; i++ ) {
			mvwadd_wchnstr(win, s->c.y, s->c.x + i, &b, 1);
		}
		break;
	case ed: /* Erase in Display */
	case el: /* Erase in Line */
		switch( p0[0] ) {
		case 2:
			wmove(win, handler == el ? s->c.y : tos, 0);
			/* Fall Thru */
		case 0:
			(handler == el ? wclrtoeol : wclrtobot)(win);
			break;
		case 3:
			if( handler == ed ) {
				werase(win);
			}
			break;
		case 1:
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
		break;
	case hpa: /* Cursor Horizontal Absolute */
	case hpr: /* Cursor Horizontal Relative */
		s->c.x = p0[1] + (handler == hpa ? -1 : s->c.x);;
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

		wsetscrreg(win, s->c.y, s->scroll.bot);
		wscrl(win, w == L'L' ? -i : i);
		wsetscrreg(win, s->scroll.top, s->scroll.bot);
		s->c.x = 0;
		break;
	case numkp: /* Application/Numeric Keypad Mode */
		p->pnm = (w == L'=');
		break;
	case rc: /* Restore Cursor */
		if( iw == L'#' ) {
			decaln(p, tos);
		}
		restore_cursor(s);
		break;
	case ri: /* Reverse Index (scroll back) */
		t1 = s->scroll.top;
		wsetscrreg(win, t1 >= tos ? t1 : tos, s->scroll.bot);
		if( y == top ) {
			wscrl(win, -1);
		} else {
			s->c.y = MAX(tos, s->c.y - 1);
			wmove(win, s->c.y, s->c.x);
		}
		wsetscrreg(win, s->scroll.top, s->scroll.bot);
		break;
	case sc: /* Save Cursor */
		save_cursor(s);
		break;
	case su: /* Scroll Up/Down */
		wscrl(win, (w == L'T' || w == L'^') ? -p0[1] : p0[1]);
		break;
	case tab: /* Tab forwards or backwards */
		while( p0[1] ) {
			s->c.x += w == L'Z' ? -1 : +1;
			if( s->c.x < p->ntabs && s->c.x >= 0
					&& p->tabs[s->c.x] ) {
				p0[1] -= 1;
			}
		}
		break;
	case tbc: /* Tabulation Clear */
		assert( s->c.x < p->ntabs && s->c.x >= 0 );
		if( p0[0] == 0 ) {
			p->tabs[s->c.x] = false;
		} else if( p0[0] == 3 ) {
			memset(p->tabs, 0, p->ntabs * sizeof *p->tabs);
		}
		break;
	case osc: /* Operating System Command */
		handle_osc(p, arg);
		break;
	case vis: /* Cursor visibility */
		s->vis = iw != L'6';
		break;
	case vpa: /* Cursor Vertical Absolute */
		s->c.y = tos - 1; /* Fallthru */
	case vpr: /* Cursor Vertical Relative */
		s->c.y = MAX(tos + top, p0[1] + s->c.y);
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
		p->pri.c.gs = p->pri.c.gc = p->g0 = CSET_US;
		p->alt.c.gs = p->alt.c.gc = p->g2 = CSET_US;
		p->g1 = p->g3 = CSET_GRAPH;
		p->decom = s->insert = p->lnm = false;
		s->c.xenl = 0;
		reset_sgr(s);
		p->decawm = p->pnm = true;
		p->pri.vis = p->alt.vis = 1;
		p->s = &p->pri;

		wsetscrreg(p->pri.win, p->pri.scroll.top = 0,
			p->pri.scroll.bot = p->pri.rows - 1);
		wsetscrreg(p->alt.win, p->alt.scroll.top = 0,
			p->alt.scroll.bot = p->alt.rows - 1);
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
				s->c.xenl = 0;
				s->c.y = tos + (p->decom ? top : 0);
				wmove(win, s->c.y, s->c.x = 0);
				break;
			case  7: p->decawm = set;           break;
			case 20: p->lnm = set;              break;
			case 25: s->vis = set ? 1 : 0;      break;
			case 34: s->vis = set ? 1 : 2;      break;
			case 1048: case 1049:
				(set ? save_cursor : restore_cursor)(s);
				if( argv[i] == 1048 ) {
					break;
				} /* fall-thru */
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
			int val = argc > i + 2 ? argv[i + 2] : 0, k = 0, a;
			switch( a = argv[i] ) {
			case  0:
				reset_sgr(s);
				break;
			case  1:
			case  2:
			case  4:
			case  5:
			case  7:
			case  8:
				wattron(win, attrs[argv[i]]);
				break;
			case 22:
				wattroff(win, A_DIM);
				wattroff(win, A_BOLD);
				break;
			case 24:
			case 25:
			case 27:
				wattroff(win, attrs[argv[i] - 20]);
				break;
			case 30:
			case 31:
			case 32:
			case 33:
			case 34:
			case 35:
			case 36:
			case 37:
				k = true; /* Fallthru */
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
				break;
			case 38:
			case 48:
				if( at ) {
					*(argv[i] == 48 ? &bg : &fg) = val;
				}
				i += 2;
				doc = do256;
				break;
			case 39:
			case 49:
				*(argv[i] == 49 ? &bg : &fg) = -1;
				doc = true;
				break;
			case 90:
			case 91:
			case 92:
			case 93:
			case 94:
			case 95:
			case 96:
			case 97:
				fg = colors[argv[i] - 90];
				doc = do16;
				break;
			case 100:
			case 101:
			case 102:
			case 103:
			case 104:
			case 105:
			case 106:
			case 107:
				bg = colors[argv[i] - 100];
				doc = do16;
				break;
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
	case nel: /* Next Line */
	case ind: /* Index */
		newline(p, handler == pnl ? p->lnm : handler == nel);
		break;
	case cpl: /* CPL - Cursor Previous Line */
		s->c.y = MAX(tos + top, s->c.y - p0[1]);
		s->c.x = 0;
		break;
	case cnl: /* CNL - Cursor Next Line */
		s->c.y = MIN(tos + bot - 1, s->c.y + p0[1]);
		s->c.x = 0;
		break;
	case print: /* Print a character to the terminal */
		if( wcwidth(w) > 0 ) {
			print_char(w, p);
		}
		break;
	case rep: /* REP - Repeat Character */
		for( i=0; i < p0[1] && p->repc; i++ ) {
			print_char(p->repc, p);
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
	s->c.x = MAX(0, MIN(s->c.x, p->ws.ws_col - 1));
	s->c.y = MAX(0, MIN(s->c.y, tos + bot - 1));
	s->maxy = MAX(s->c.y, s->maxy);
	s->tos = MAX(0, s->maxy - p->ws.ws_row + 1);
	wmove(win, s->c.y, s->c.x);
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
