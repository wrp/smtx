/*
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

/* This file contains descripive functions that should only ever be
 * used by the test harness.  Note that this is not really true: it might
 * be convenient to have the ability to display state in the terminal,
 * so we should clean this code up and expect it to remain in production.
 */
#include "smtx.h"

static size_t
describe_layout(char *d, ptrdiff_t siz, const struct canvas *c, unsigned flags,
	int tab)
{
	/* Describe a layout. */
	const char * const e = d + siz;
	int recurse = flags & 0x1;
	int show_id = flags & 0x4;
	int show_pos = flags & 0x10;
	int show_2nd = flags & 0x40;
	int human = flags & 0x80;
	int show_pty = flags & 0x100;

	char *isfocus = recurse && c == S.f ? "*" : "";
	if (human) {
		d += snprintf(d, e - d, "%c", c->typ ? '|' : '-');
	}
	d += snprintf(d, e - d, "%s%dx%d", isfocus, c->extent.y, c->extent.x);
	if (show_pos){
		d += snprintf(d, e - d, "@%d,%d", c->origin.y, c->origin.x);
	}
	if (show_id && c->p) {
		d += snprintf(d, e - d, "(id=%d)", c->p->fd - 2);
	}
	if (c->p->s && ! c->p->s->vis) {
		d += snprintf(d, e - d, "!"); /* Cursor hidden */
	}
	if (! c->p->pnm) {
		d += snprintf(d, e - d, "#"); /* Numeric keypad  */
	}
	if (show_2nd && c->p && c->p->fd != -1) {
		d += snprintf(d, e - d, "(2nd=%s)", c->p->secondary );
	}
	if (show_pty) {
		d += snprintf(d, e - d, "(pri %d,%d)(2nd %d,%d)",
			c->p->scr[0].rows,
			c->p->scr[0].maxy,
			c->p->scr[1].rows,
			c->p->scr[1].maxy
		);
	}
	for (int i = 0; recurse && i < 2; i ++) {
		if (e - d > 3 + tab && c->c[i]) {
			*d++ = human ? '\r' : ';';
			*d++ = human ? '\n' : ' ';
			for (int i = 0; human && i < tab; i++) {
				*d++ = '\t';
			}
			d += describe_layout(d, e - d, c->c[i], flags, tab + 1);
		}
	}
	return siz - ( e - d );
}

static void
check_attr(unsigned f, unsigned *flags, char **d, char *e, char *msg, int set,
	int star)
{
	char *dest = *d;
	size_t len = strlen(msg);
	if (((set && !(*flags & f)) || (!set && (*flags & f)))
			&& len > 0 && dest + len + 3 < e) {
		*dest++ = '<';
		if (!set) {
			*dest++ = '/';
		}
		memcpy(dest, msg, len);
		dest += len;
		if (star) {
			*dest++ = '*';
		}
		*dest++ = '>';
	}
	if (set) {
		*flags |= f;
	} else {
		*flags &= ~f;
	}
	*d = dest;
}

static char *
color_name(short k)
{
	char *n;
	switch (k) {
	case COLOR_BLACK:    n =   "black"; break;
	case COLOR_RED:      n =     "red"; break;
	case COLOR_GREEN:    n =   "green"; break;
	case COLOR_YELLOW:   n =  "yellow"; break;
	case COLOR_BLUE:     n =    "blue"; break;
	case COLOR_MAGENTA:  n = "magenta"; break;
	case COLOR_CYAN:     n =    "cyan"; break;
	case COLOR_WHITE:    n =   "white"; break;
	default:             n = "";
	}
	return n;
}

static size_t
describe_row(char *desc, size_t siz, int row)
{
	int y, x;
	size_t i = 0;
	unsigned attrs = 0;
	unsigned fgflag = 0;
	unsigned bgflag = 0;
	int last_color_pair = 0;
	int offset = 0;
	const struct canvas *c = S.root;
	unsigned width = c->p->ws.ws_col;
	char *end = desc + siz;
	WINDOW *w = c->p->s->w;
	struct {
		unsigned attr;
		unsigned flag;
		char *name;
	} *atrp, atrs[] = {
		{ A_BOLD,       0x1, "bold" },
		{ A_DIM,        0x2, "dim" },
#if HAVE_A_ITALIC
		{ A_ITALIC,     0x4, "italic" },
#endif
		{ A_UNDERLINE,  0x8, "ul" },
		{ A_BLINK,     0x10, "blink" },
		{ A_REVERSE,   0x20, "rev" },
		{ A_INVIS,     0x40, "inv" },
		{ 0, 0, NULL }
	};

	if (row < c->extent.y) {
		row += c->offset.y;
		offset = c->offset.x;
	} else if (row == c->extent.y) {
		w = c->wtit;
		row = 0;
	}
	getyx(w, y, x);
	for (i = 0; i < (size_t)c->extent.x && i < width && desc < end; i++) {
		int p;
		chtype k = mvwinch(w, row, i + offset);
		for (atrp = atrs; atrp->flag; atrp += 1) {
			check_attr(atrp->flag, &attrs, &desc, end, atrp->name,
				( (k & A_ATTRIBUTES) & atrp->attr ) ? 1 : 0, 0
			);
		}
		p = PAIR_NUMBER(k);
		if (p != last_color_pair) {
			int put = p ? p : last_color_pair;
			short fg, bg;
			if (pair_content((short)put, &fg, &bg) == OK
					&& bg < (short)sizeof(1u) * CHAR_BIT
					&& fg < (short)sizeof(1u) * CHAR_BIT) {
				check_attr(1u << fg, &fgflag, &desc, end,
					color_name(fg), p, 0
				);
				check_attr(1u << bg, &bgflag, &desc, end,
					color_name(bg), p, 1
				);
			}
			last_color_pair = p;
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
	if (len < siz - 1) {
		len += snprintf(desc + len, siz - len, "y=%d, x=%d, ", LINES,
			COLS);
	}
	if (len < siz - 1) {
		len += snprintf(desc + len, siz - len, "w=%d", S.width);
	}
	if (len > siz - 3) {
		len = siz - 3;
	}
	desc[len++] = '\r';
	desc[len++] = '\n';
	desc[len++] = '\0';
	return len;
}

static void
show_layout(const char *arg)
{
	char buf[1024] = "layout: ";
	int flag = strtol(*arg == ';' ? arg + 1 : arg, NULL, 16);
	struct canvas *c = flag & 0x20 ? S.f : S.root;
	if (flag & 0x80) {
		strcat(buf, "\r\n");
	}
	int w = strlen(buf);
	size_t s = describe_layout(buf + w, sizeof buf - w - 3, c, flag, 1);
	for (size_t i = w; i < w + s; i++) {
		assert( buf[i] != ':' );
	}
	buf[w + s++] = ':';
	buf[w + s++] = '\r';
	buf[w + s++] = '\n';
	rewrite(STDOUT_FILENO, buf, s + w);
}

static void
show_row(const char *arg)
{
	int row = strtol(arg, NULL, 10);
	char buf[1024];
	char val[1024];
	size_t s = describe_row(val, sizeof val, row);
	int k = snprintf(buf, sizeof buf, "row %d:(%zd)%s", row, s, val);
	rewrite(1, buf, k < (int)sizeof buf ? k : (int)sizeof buf);
}

static void
show_procs(void)
{
	char buf[1024] = "procs:\tid\tpid\tcount\ttitle\r\n";
	rewrite(1, buf, strlen(buf));
	for (struct pty *p = S.p; p; p = p->next) {
		int k = snprintf(buf, sizeof buf, "\t%d\t%d\t%d\t%s\r\n",
			p->fd - 2, p->pid, p->count, p->status);
		rewrite(1, buf, k);
	}
}

static void
show_state(void)
{
	char buf[1024];
	int k = sprintf(buf, "state: ");
	size_t s = describe_state(buf + k, sizeof buf - k);
	rewrite(1, buf, s + k);
}

void
show_status(const char *arg)
{
	if (S.count == -1 && *arg == 'x'){
		for (int i = 0; i < LINES * COLS; i++) {
			putchar(' ');
		}
		putchar('\r');
		putchar('\n');
		fflush(stdout);
	}
	if (*arg == 'x') {
		arg += 1;
	}
	if (*arg == 'r') {
		show_row(arg + 1);
	} else {
		show_procs();
		show_state();
		show_layout(*arg ? arg : "0x1d5");
	}
}
