/*
 * Copyright 2017 - 2019 Rob King <jking@deadpixi.com>
 * Copyright 2020 - 2023 William Pursell <william.r.pursell@gmail.com>
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

void
set_status(struct pty *p, const char *arg)
{
	snprintf(p->status, sizeof p->status, "%s", arg);
}

static void
restore_cursor(struct screen *s)
{
	if (s->sc.gc) {
		s->c = s->sc;
		#if HAVE_ALLOC_PAIR
		s->c.p = alloc_pair(s->c.color[0], s->c.color[1]);
		#endif
		wattr_set(s->w, s->c.attr, s->c.p, NULL);
		wbkgrndset(s->w, &s->c.bkg);
	}
}

static void
save_cursor(struct screen *s)
{
	wattr_get(s->w, &s->c.attr, &s->c.p, NULL);
	pair_content(s->c.p, s->sc.color, s->sc.color + 1);
	s->sc = s->c;
}

static void
reset_sgr(struct screen *s)
{
	pair_content(s->c.p = COLOR_PAIR(0), s->c.color, s->c.color + 1);
	setcchar(&s->c.bkg, L" ", A_NORMAL, s->c.p, NULL);
	wattr_set(s->w, A_NORMAL, s->c.p, NULL);
	wbkgrndset(s->w, &s->c.bkg);
}

static void
newline(struct screen *s, int cr)
{
	if (cr) {
		s->c.xenl = s->c.x = 0;
	}
	if (s->c.y == s->scroll.bot) {
		scroll(s->w);
	} else {
		wmove(s->w, ++s->c.y, s->c.x);
	}
}

static void
print_char(wchar_t w, struct pty *p)
{
	if (p->s->insert) {
		wins_wch(p->s->w, &p->s->c.bkg);
	}
	if (p->s->c.xenl && p->s->decawm) {
		newline(p->s, 1);
	}
	if (w < 0x7f && p->s->c.gc[w]) {
		w = p->s->c.gc[w];
	}
	if (p->s->c.x >= p->ws.ws_col - wcwidth(w)) {
		p->s->c.xenl = 1;
		wins_nwstr(p->s->w, &w, 1);
	} else {
		p->s->c.xenl = 0;
		waddnwstr(p->s->w, &w, 1);
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
#if HAVE_A_ITALIC
	[3] = A_ITALIC,
#else
	[3] = 0,
#endif
	[4] = A_UNDERLINE,
	[5] = A_BLINK,
	[7] = A_REVERSE,
	[8] = A_INVIS,
};

void
tput(struct pty *p, wchar_t w, wchar_t iw, int argc, int *argv, int handler)
{
	int i, t1;

	/* First arg, defaulting to 0 or 1 */
	int p0[] = { argc ? *argv : 0, argc ? *argv : 1 };
	struct screen *s = p->s; /* the current SCRN buffer */
	WINDOW *win = s->w;      /* the current window */

	const int tos = p->tos;
	const int y = s->c.y - tos; /* cursor position w.r.t. top of screen */
	const int bot = s->scroll.bot - tos + 1;
	const int top = MAX(0, s->scroll.top - tos);
	const int dtop = tos + (p->decom ? top : 0);

	switch ((enum cmd)handler) {
	case ack:
		rewrite(p->fd, "\006", 1);
		break;
	case bell:
		beep();
		break;
	case cr:
		s->c.x = 0;
		break;
	case csr:
		t1 = argc > 1 ? argv[1] : p->ws.ws_row;
		set_scroll(s, tos + p0[1] - 1, tos + t1 - 1);
		s->c.y = dtop;
		s->c.x = 0;
		break;
	case cub:
		s->c.x -= p0[1];
		break;
	case cud:
		s->c.y += p0[1];
		break;
	case cuf:
		s->c.x += p0[1];
		break;
	case cup:
		s->c.y = dtop + p0[1] - 1;
		s->c.x = argc > 1 ? argv[1] - 1 : 0;
		break;
	case cuu:
		s->c.y -= p0[1];
		break;
	case dch:
		for (i = 0; i < p0[1]; i++) {
			wdelch(win);
		}
		break;
	case ech:
		for (i = 0; i < p0[1]; i++) {
			mvwadd_wch(win, s->c.y, s->c.x + i, &s->c.bkg);
		}
		break;
	case ed: /* Fallthru */
	case el:
		switch (p0[0]) {
		case 2:
			wmove(win, handler == el ? s->c.y : tos, 0);
			/* Fall Thru */
		case 0:
			(handler == el ? wclrtoeol : wclrtobot)(win);
			break;
		case 3:
			if (handler == ed) {
				werase(win);
			}
			break;
		case 1:
			if (handler == ed) {
				for (i = tos; i < s->c.y; i++) {
					wmove(win, i, 0);
					wclrtoeol(win);
				}
				wmove(win, s->c.y, s->c.x);
			}
			for (i = 0; i <= s->c.x; i++) {
				mvwadd_wch(win, s->c.y, i, &s->c.bkg);
			}
		}
		break;
	case hpa:
		s->c.x = -1;
		/* Fallthru */
	case hpr:
		s->c.x += p0[1];
		break;
	case hts:
		p->tabs[s->c.x] = true;
		break;
	case ich:
		for (i = 0; i < p0[1]; i++) {
			wins_wch(win, &p->s->c.bkg);
		}
		break;
	case idl:
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
	case numkp:
		p->pnm = (w == L'=');
		break;
	case osc:
		assert( 0 );
		break;
	case rc:
		if (iw == L'#' ) for (int r = 0; r < p->ws.ws_row; r++) {
			cchar_t e;
			setcchar(&e, L"E", A_NORMAL, COLOR_PAIR(0), NULL);
			for (int c = 0; c < p->ws.ws_col; c++) {
				mvwadd_wch(p->s->w, tos + r, c, &e);
			}
		}
		restore_cursor(s);
		break;
	case ri:
		if (y == top) {
			wsetscrreg(win, MAX(s->scroll.top, tos), s->scroll.bot);
			wscrl(win, -1);
			wsetscrreg(win, s->scroll.top, s->scroll.bot);
		} else {
			s->c.y = MAX(tos, s->c.y - 1);
		}
		break;
	case sc:
		save_cursor(s);
		break;
	case su:
		wscrl(win, (w == L'T' || w == L'^') ? -p0[1] : p0[1]);
		break;
	case tab:
		for (i = 0; i < p0[1]; i += p->tabs[s->c.x] ? 1 : 0) {
			s->c.x += (w == L'Z' ? -1 : +1);
		}
		break;
	case tbc:
		switch (p0[0]) {
		case 0:
			p->tabs[s->c.x] = false;
			break;
		case 3:
			memset(p->tabs, 0, p->ws.ws_col * sizeof *p->tabs);
		}
		break;
	case vis:
		s->vis = iw != L'6';
		break;
	case vpa:
		s->c.y = tos - 1;
		/* Fallthru */
	case vpr:
		s->c.y = MAX(tos + top, p0[1] + s->c.y);
		break;
	case ris:
		ioctl(p->fd, TIOCGWINSZ, &p->ws);
		p->g[0] = p->g[2] = CSET_US;
		p->g[1] = p->g[3] = CSET_GRAPH;
		p->decom = s->insert = p->lnm = false;
		reset_sgr(s);
		s->decawm = p->pnm = true;
		for (i = 0, s = p->s = p->scr; i < 2; i++, s++) {
			s->c.gs = s->c.gc = CSET_US;
			s->vis = 1;
			set_scroll(s, 0, s->rows - 1);
		}
		set_tabs(p, p->tabstop);
		vtreset(&p->vp);
		break;
	case mode:
		for (i = 0; i < argc; i++) {
			bool set = (w == L'h');
			switch (argv[i]) {
			case  1:
				p->pnm = set;
				break;
			case  3:
				set_width(set ? "132" : "80");
				break;
			case  4:
				s->insert = set;
				break;
			case  6:
				p->decom = set;
				s->c.x = s->c.xenl = 0;
				s->c.y = dtop;
				break;
			case  7:
				s->decawm = set;
				break;
			case 20:
				p->lnm = set;
				break;
			case 25:
				s->vis = set ? 1 : 0;
				break;
			case 34:
				s->vis = set ? 1 : 2;
				break;
			case 1048:
				(set ? save_cursor : restore_cursor)(s);
				break;
			case 1049:
				(set ? save_cursor : restore_cursor)(s);
				/* fall thru */
			case 47:
			case 1047:
				/* Switch to alternate screen */
				if (set && p->s == p->scr) {
					struct screen *alt = p->scr + 1;
					alt->c.x = alt->c.xenl = 0;
					alt->c.y = dtop;
					wclear(alt->w);
				}
				p->s = p->scr + !!set;
			}
		}
		break;
	case sgr:
	{
		bool doc = false;
		if (!argc) {
			reset_sgr(s);
		} else for (i = 0; i < argc; i++) {
			int k = 1, a;
			switch (a = argv[i]) {
			case  0:
				reset_sgr(s);
				break;
			case  1:
			case  2:
			case  3:
			case  4:
			case  5:
			case  7:
			case  8:
				wattron(win, attrs[a]);
				break;
			case 21:
			case 22:
			case 23:
			case 24:
			case 25:
			case 27:
				wattroff(win, attrs[a - 20]);
				break;
			case 30:
			case 31:
			case 32:
			case 33:
			case 34:
			case 35:
			case 36:
			case 37:
				k = 0; /* Fallthru */
			case 40:
			case 41:
			case 42:
			case 43:
			case 44:
			case 45:
			case 46:
			case 47:
				s->c.color[k] = colors[a - ( k ? 40 : 30 )];
				doc = COLORS >= 8;
				break;
			case 38:
			case 48:
				if (argc > i + 2 && argv[i + 1] == 5){
					s->c.color[a == 48] = argv[i + 2];
				}
				i += 2;
				doc = COLORS >= 256;
				break;
			case 39:
			case 49:
				s->c.color[a == 49] = -1;
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
				k = 0; /* Fallthru */
			case 100:
			case 101:
			case 102:
			case 103:
			case 104:
			case 105:
			case 106:
			case 107:
				s->c.color[k] = colors[a - ( k ? 100 : 90 )];
				doc = COLORS >= 16;
			}
		}
		if (doc) {
			#if HAVE_ALLOC_PAIR
			s->c.p = alloc_pair(s->c.color[0], s->c.color[1]);
			#endif
			wcolor_set(win, s->c.p, NULL);
			setcchar(&s->c.bkg, L" ", A_NORMAL, s->c.p, NULL);
			wbkgrndset(win, &s->c.bkg);
		}
	}
		break;
	case pnl:
	case nel:
	case ind:
		newline(s, handler == pnl ? p->lnm : handler == nel);
		break;
	case cpl:
		s->c.y = MAX(tos + top, s->c.y - p0[1]);
		s->c.x = 0;
		break;
	case cnl:
		s->c.y = MIN(tos + bot - 1, s->c.y + p0[1]);
		s->c.x = 0;
		break;
	case print:
		s->repc = w;
		/* Fallthru */
	case rep:
		if (wcwidth(w = s->repc) > 0) {
			for (i = 0; i < p0[1]; i++) {
				print_char(w, p);
			}
		}
		break;
	case scs:
		for (const char *s = "()*+", *c = strchr(s, iw); c; c = NULL )
		switch (w) {
		case L'A':
			p->g[c-s] = CSET_UK;
			break;
		case L'B':
			p->g[c-s] = CSET_US;
			break;
		case L'0':
			p->g[c-s] = CSET_GRAPH;
			break;
		case L'1':
			p->g[c-s] = CSET_US;
			break;
		case L'2':
			p->g[c-s] = CSET_GRAPH;
		}
		break;
	case so:
		for (char *s = "\x0f\x0e}|NO", *c = strchr(s, w); c; c = NULL) {
			switch (c - s) {
			case 0: /* locking shift */
			case 1:
			case 2:
			case 3:
				p->s->c.gs = p->s->c.gc = p->g[c - s];
				break;
			case 4:
			case 5: /* non-locking shift */
				p->s->c.gs = p->s->c.gc;
				p->s->c.gc = p->g[c - s - 2];
			}
		}
	}
	switch (handler) {
	case cr:
	case csr:
	case cub:
	case cup:
	case ris:
		p->s->c.xenl = 0;  /*Fallthru */
	default:
		p->s->repc = 0;
		break;
	case sgr:
	case print:
		;
	}
	p->s->c.x = MAX(0, MIN(p->s->c.x, p->ws.ws_col - 1));
	p->s->c.y = MAX(0, MIN(p->s->c.y, tos + bot - 1));
	p->s->maxy = MAX(p->s->c.y, p->s->maxy);
	p->tos = MAX(0, p->s->maxy - p->ws.ws_row + 1);
	wmove(p->s->w, p->s->c.y, p->s->c.x);
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
