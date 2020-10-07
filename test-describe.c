/*
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

/* This file contains descripive functions that should only ever be
 * used by the test harness.
 */
#ifndef NDEBUG
#include "smtx.h"

static size_t
describe_layout(char *d, ptrdiff_t siz, const struct canvas *c, unsigned flags)
{
	/* Describe a layout. */
	const char * const e = d + siz;
	int recurse = flags & 0x1;
	int show_id = flags & 0x4;
	int show_pos = flags & 0x10;

	char *isfocus = recurse && c == S.f ? "*" : "";
	d += snprintf(d, e - d, "%s%dx%d", isfocus, c->extent.y, c->extent.x);
	if( show_pos) {
		d += snprintf(d, e - d, "@%d,%d", c->origin.y, c->origin.x);
	}
	if( show_id && c->p ) {
		d += snprintf(d, e - d, "(id=%d)", c->p->id);
	}
	if( c->p->s && ! c->p->s->vis ) {
		d += snprintf(d, e - d, "!"); /* Cursor hidden */
	}
	if( ! c->p->pnm ) {
		d += snprintf(d, e - d, "#"); /* Numeric keypad  */
	}
	for( int i = 0; recurse && i < 2; i ++ ) {
		if( e - d > 3 && c->c[i] ) {
			*d++ = ';';
			*d++ = ' ';
			d += describe_layout(d, e - d, c->c[i], flags);
		}
	}
	return siz - ( e - d );
}

static void
check_flag(unsigned f, unsigned *flags, char **d, char *e, char *msg, int set)
{
	char *dest = *d;
	size_t len = strlen(msg);
	if( ((set && !(*flags & f)) || (!set && (*flags & f)))
			&& dest + len + 3 < e ) {
		*dest++ = '<';
		if( !set ) {
			*dest++ = '/';
		}
		memcpy(dest, msg, len);
		dest += len;
		*dest++ = '>';
	}
	if( set ) {
		*flags |= f;
	} else {
		*flags &= ~f;
	}
	*d = dest;
}

static size_t
describe_row(char *desc, size_t siz, int row)
{
	int y, x;
	size_t i = 0;
	unsigned flags = 0;
	int offset = 0;
	const struct canvas *c = S.c;
	char *end = desc + siz;
	WINDOW *w = c->p->s->win;

	if( row < c->extent.y ) {
		row += c->offset.y;
		offset = c->offset.x;
	} else if( row == c->extent.y ) {
		w = c->wtit;
		row = 0;
	}
	getyx(w, y, x);
	for( i = 0; i < (size_t)c->extent.x && desc < end; i++ ) {
		chtype k = mvwinch(w, row, i + offset);
		switch( k & A_ATTRIBUTES ) {
		case A_BOLD:
			check_flag(0x1, &flags, &desc, end, "bold", 1); break;
		case A_DIM:
			check_flag(0x2, &flags, &desc, end, "dim", 1); break;
		case A_UNDERLINE:
			check_flag(0x4, &flags, &desc, end, "ul", 1); break;
		case A_BLINK:
			check_flag(0x8, &flags, &desc, end, "blink", 1); break;
		case A_REVERSE:
			check_flag(0x10, &flags, &desc, end, "rev", 1); break;
		case A_INVIS:
			check_flag(0x20, &flags, &desc, end, "inv", 1); break;
		default:
			check_flag(0x1, &flags, &desc, end, "bold", 0);
			check_flag(0x2, &flags, &desc, end, "dim", 0);
			check_flag(0x4, &flags, &desc, end, "ul", 0);
			check_flag(0x8, &flags, &desc, end, "blink", 0);
			check_flag(0x10, &flags, &desc, end, "rev", 0);
			check_flag(0x20, &flags, &desc, end, "inv", 0);
		}
		*desc++ = k & A_CHARTEXT;
	}
	wmove(w, y, x);
	return siz - ( end - desc );
}

static size_t
describe_state(char *desc, size_t siz)
{
	size_t len = 0;

	len += snprintf(desc, siz, "history=%d, ", S.history);
	if( len < siz - 1 ) {
		len += snprintf(desc + len, siz - len, "y=%d, x=%d, ", LINES,
			COLS);
	}
	if( len < siz - 1 ) {
		len += snprintf(desc + len, siz - len, "w=%d", S.width);
	}
	assert( len < siz - 1 );
	desc[len++] = '\n';
	return len;
}

void
show_layout(const char *arg)
{
	char buf[1024] = "layout: ";
	int w = strlen(buf);
	int flag = strtol(*arg == ';' ? arg + 1 : arg, NULL, 10);
	struct canvas *c = flag & 0x20 ? S.f : S.c;
	size_t s = describe_layout(buf + w, sizeof buf - w - 1, c, flag);
	#ifndef NDEBUG
	for( size_t i = w; i < w + s; i++ ) {
		assert( buf[i] != ':' );
	}
	#endif
	buf[w + s] = ':';
	rewrite(STDOUT_FILENO, buf, s + w + 1);
}

void
show_row(const char *arg)
{
	int row = strtol(arg, NULL, 10);
	char buf[1024];
	char val[1024];
	size_t s = describe_row(val, sizeof val, row);
	int k = snprintf(buf, sizeof buf, "row %d:(%zd)%s", row, s, val);
	rewrite(1, buf, k < (int)sizeof buf ? k : (int)sizeof buf);
}

void
show_state(const char *arg)
{
	(void)arg;
	char buf[1024];
	int k = sprintf(buf, "state: ");
	size_t s = describe_state(buf + k, sizeof buf - k);
	rewrite(1, buf, s + k);
}
#endif
