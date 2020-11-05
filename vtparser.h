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

struct vtp {
	struct state *s;
	int narg;
	int nosc;
	int args[MAXPARAM];
	int inter;
	char oscbuf[MAXOSC + 1];
	mbstate_t ms;
	void *p;
	int print;
	int osc;
	int cons[MAXCALLBACK];
	int escs[MAXCALLBACK];
	int csis[MAXCALLBACK];
};

typedef void (VTCALLBACK)(
	struct vtp *,
	wchar_t,
	wchar_t,
	int,
	void *,
	int
);
extern VTCALLBACK tput;

void vtwrite(struct vtp *vp, const char *s, size_t n);
#endif
