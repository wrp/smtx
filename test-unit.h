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
#include <assert.h>
#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <signal.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/wait.h>
#include <unistd.h>
#if HAVE_PTY_H
# include <pty.h>
# include <utmp.h>
#elif HAVE_LIBUTIL_H
# include <libutil.h>
#elif HAVE_UTIL_H
# include <util.h>
#endif

#define CTL(x) ((x) & 0x1f)
#define PROMPT "ps1>"

extern int ctlkey;

void __attribute__((format(printf,3,4)))
send_txt(int fd, const char *wait, const char *fmt, ...);

void __attribute__((format(printf,3,4)))
send_cmd(int fd, const char *wait, const char *fmt, ...);

void __attribute__((format(printf,3,4)))
send_raw(int fd, const char *wait, const char *fmt, ...);

int __attribute__((format(printf,3,4)))
validate_row(int fd, int row, const char *fmt, ... );

int get_layout(int fd, int flag, char *layout, size_t siz);
int get_state(int fd, char *state, size_t siz);
int get_row(int fd, int row, char *buf, size_t siz);

int __attribute__((format(printf,3,4)))
check_layout(int fd, int flag, const char *fmt, ...);

typedef int(test)(int fd);
test test_ack;
test test_alt;
test test_attach;
test test_bighist;
test test_changehist;
test test_cols;
test test_command;
test test_csr;
test test_cup;
test test_cursor;
test test_dashc;
test test_dasht;
test test_dch;
test test_decaln;
test test_decid;
test test_dsr;
test test_ech;
test test_ed;
test test_el;
test test_equalize;
test test_hpr;
test test_ich;
test test_insert;
test test_layout;
test test_lnm;
test test_navigate;
test test_nel;
test test_pager;
test test_pnm;
test test_quit;
test test_resend;
test test_reset;
test test_resize;
test test_resizepty;
test test_ri;
test test_row;
test test_scrollback;
test test_scrollh;
test test_sgr;
test test_su;
test test_swap;
test test_tabstop;
test test_title;
test test_tput;
test test_vis;
test test_width;
