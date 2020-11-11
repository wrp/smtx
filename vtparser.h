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
#ifndef VTC_H
#define VTC_H

#include <stddef.h>
#include <wchar.h>

/* The names for handlers come from their ANSI/ECMA/DEC mnemonics.  */
enum cmd {
	ack=1, bell, cnl, cpl, cr, csr, cub, cud, cuf, cup,
	cuu, dch, decid, decreqtparm, dsr, ech, ed, el, hpa, hpr,
	hts, ich, idl, ind, mode, nel, numkp, osc, pnl, print, rc, rep,
	ri, ris, sc, scs, sgr, so, su, tab, tbc, vis, vpa, vpr
};

/*
 * VTPARSER_BAD_CHAR is the character that will be displayed when
 * an application sends an invalid multibyte sequence to the terminal.
 */
#ifndef VTPARSER_BAD_CHAR
	#ifdef __STDC_ISO_10646__
		#define VTPARSER_BAD_CHAR ((wchar_t)0xfffd)
	#else
		#define VTPARSER_BAD_CHAR L'?'
	#endif
#endif

#define MAXPARAM    16
#define MAXCALLBACK 128
#define MAXOSC      511

struct pty;
struct vtp {
	struct state *s;
	mbstate_t ms;
	struct pty *p;
	struct {
		int inter;
		int argc;
		int args[MAXPARAM];
		char oscbuf[MAXOSC + 1];
	} z;
};
extern int cons[MAXCALLBACK];
extern int csis[MAXCALLBACK];
extern int escs[MAXCALLBACK];

void tput(struct pty *, wchar_t, wchar_t, int, void *, int);

void vtwrite(struct vtp *vp, const char *s, size_t n);
#endif
