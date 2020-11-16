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
	ack=1,        /* Acknowledge Enquiry */
	bell,         /* Terminal bell. */
	cnl,          /* Cursor Next Line */
	cpl,          /* Cursor Previous Line */
	cr,           /* Carriage Return */
	csr,          /* Change Scrolling Region */
	cub,          /* Cursor Backward */
	cud,          /* Cursor Down */
	cuf,          /* Cursor Forward */
	cup,          /* Cursor Position */
	cuu,          /* Cursor Up */
	dch,          /* Delete Character */
	ech,          /* Erase Character */
	ed,           /* Erase in Display */
	el,           /* Erase in Line */
	hpa,          /* Cursor Horizontal Absolute */
	hpr,          /* Cursor Horizontal Relative */
	hts,          /* Horizontal Tab Set */
	ich,          /* Insert Character */
	idl,          /* Insert/Delete Line */
	ind,          /* Index */
	mode,         /* Set or Reset Mode */
	nel,          /* Next Line */
	numkp,        /* Application/Numeric Keypad Mode */
	osc,          /* Operating System Command */
	pnl,          /* Newline */
	print,        /* Print a character to the terminal */
	rc,           /* Restore Cursor */
	rep,          /* Repeat Character */
	ri,           /* Reverse Index (scroll back) */
	ris,          /* Reset to Initial State */
	sc,           /* Save Cursor */
	scs,          /* Select Character Set */
	sgr,          /* SGR - Select Graphic Rendition */
	so,           /* Switch Out/In Character Set */
	su,           /* Scroll Up/Down */
	tab,          /* Tab forwards or backwards */
	tbc,          /* Tabulation Clear */
	vis,          /* Cursor visibility */
	vpa,          /* Cursor Vertical Absolute */
	vpr,          /* Cursor Vertical Relative */
#if 0
unimplemented

decid,        /* Send Terminal Identification */
decreqtparm,  /* Request Device Parameters */
dsr,          /* Device Status Report */
#endif
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
#define MAXOSC      511

struct pty;
struct vtp {
	struct state *s;
	mbstate_t ms;
	struct pty *p;
	struct {
		int inter;
		int argc;
		union {
			int args[MAXPARAM];
			char oscbuf[MAXOSC + 1];
		} argv;
	} z;
};
extern int cons[0x80];
extern int csis[0x80];
extern int escs[0x80];
extern int oscs[0x80];
extern int gnds[0x80];

void tput(struct pty *, wchar_t, wchar_t, int, void *, int);
void vtreset(struct vtp *v);
void vtwrite(struct vtp *vp, const char *s, size_t n);
#endif
