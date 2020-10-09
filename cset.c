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

wchar_t CSET_US[MAXMAP]; /* "USASCII" */

#if defined(__STDC_ISO_10646__) || defined(WCHAR_IS_UNICODE)
wchar_t CSET_UK[MAXMAP] = { /* "United Kingdom" */
    [L'#'] = 0x00a3
};

wchar_t CSET_GRAPH[MAXMAP] = { /* Graphics Set One */
    [L'-'] = 0x2191,  [L'a'] = 0x2592,  [L'k'] = 0x2510,  [L'u'] = 0x2524,
    [L'}'] = 0x00a3,  [L'b'] = 0x2409,  [L'l'] = 0x250c,  [L'v'] = 0x2534,
    [L'~'] = 0x00b7,  [L'c'] = 0x240c,  [L'm'] = 0x2514,  [L'w'] = 0x252c,
    [L'{'] = 0x03c0,  [L'd'] = 0x240d,  [L'n'] = 0x253c,  [L'x'] = 0x2502,
    [L','] = 0x2190,  [L'e'] = 0x240a,  [L'o'] = 0x23ba,  [L'y'] = 0x2264,
    [L'+'] = 0x2192,  [L'f'] = 0x00b0,  [L'p'] = 0x23bb,  [L'z'] = 0x2265,
    [L'.'] = 0x2193,  [L'g'] = 0x00b1,  [L'q'] = 0x2500,  [L'_'] = L' ',
    [L'|'] = 0x2260,  [L'h'] = 0x2592,  [L'r'] = 0x23bc,  [L'0'] = 0x25ae,
    [L'>'] = 0x2265,  [L'i'] = 0x2603,  [L's'] = 0x23bd,
    [L'`'] = 0x25c6,  [L'j'] = 0x2518,  [L't'] = 0x251c,
};

#else /* wchar_t doesn't map to Unicode */

wchar_t CSET_UK[] = { /* "United Kingdom" */
    [L'#'] = L'&'
};

wchar_t CSET_GRAPH[] = { /* Graphics Set One */
    [L'-'] = '^',   [L'a'] = L':',   [L'k'] = L'+',  [L'u'] = L'+',
    [L'}'] = L'&',  [L'b'] = L' ',   [L'l'] = L'+',  [L'v'] = L'+',
    [L'~'] = L'o',  [L'c'] = L' ',   [L'm'] = L'+',  [L'w'] = L'+',
    [L'{'] = L'p',  [L'd'] = L' ',   [L'n'] = '+',   [L'x'] = L'|',
    [L','] = L'<',  [L'e'] = L' ',   [L'o'] = L'-',  [L'y'] = L'<',
    [L'+'] = L'>',  [L'f'] = L'\'',  [L'p'] = L'-',  [L'z'] = L'>',
    [L'.'] = L'v',  [L'g'] = L'#',   [L'q'] = L'-',  [L'_'] = L' ',
    [L'|'] = L'!',  [L'h'] = L'#',   [L'r'] = L'-',  [L'0'] = L'#',
    [L'>'] = L'>',  [L'i'] = L'i',   [L's'] = L'_',
    [L'`'] = L'+',  [L'j'] = L'+',   [L't'] = L'+',
};

#endif
