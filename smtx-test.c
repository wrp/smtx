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
#include "config.h"
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
#include "smtx-test.h"

/* TODO: always use the p2c/c2p pipes for synchornization. */

#define PROMPT "ps1>"

static int read_timeout = 1;  /* Set to 0 when interactively debugging */
static int main_timeout = 2;
int c2p[2];
int p2c[2];
static int check_test_status(int rv, int status, int pty, const char *name);
static void grep(int fd, const char *needle);

union param {
	struct { unsigned flag; } hup;
	struct { int row; } usr1;
};

static void
write_pty(int fd, unsigned flags, const char *wait, const char *fmt, va_list ap)
{
	char cmd[1024];
	size_t n;
	char *b = cmd;
	if( flags & 0x1 ) {
		*b++ = CTL('g');
	}
	n = vsnprintf(b, sizeof cmd - (b - cmd), fmt, ap);
	if( n > sizeof cmd - 4 ) {
			err(EXIT_FAILURE, "Invalid string in write_pty");
	}
	assert( b[n] == '\0' );
	if( flags & 0x2 ) {
		b[n++] = '\r';
		b[n] = '\0';
	}
	const char *e = b + n;
	b = cmd;
	while( b < e ) {
		ssize_t s = write(fd, b, e - b);
		if( s < 0 && errno != EINTR ) {
			err(EXIT_FAILURE, "write to pty");
		}
		b += s < 0 ? 0 : s;
	}
	if( wait != NULL ) {
		grep(fd, wait);
	}
}

static void __attribute__((format(printf,3,4)))
send_cmd(int fd, const char *wait, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	write_pty(fd, 0x3, wait, fmt, ap);
	va_end(ap);
}

static void __attribute__((format(printf,3,4)))
send_txt(int fd, const char *wait, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	write_pty(fd, 0x2, wait, fmt, ap);
	va_end(ap);
}

static void __attribute__((format(printf,3,4)))
send_str(int fd, const char *wait, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	write_pty(fd, 0, wait, fmt, ap);
	va_end(ap);
}
ssize_t timed_read(int, void *, size_t, const char *);

static int
get_layout(pid_t pid, int flag, char *layout, size_t siz)
{
	int rv = 0;
	union param p = { .hup.flag = flag };
	write(p2c[1], &p, sizeof p);
	kill(pid, SIGHUP);
	ssize_t s = timed_read(c2p[0], layout, siz - 1, "layout");
	if( s == -1 ) {
		fprintf(stderr, "reading from child: %s\n", strerror(errno));
		rv = -1;
	} else {
		layout[s] = '\0';
	}
	return rv;
}


static int __attribute__((format(printf,3,4)))
check_layout(pid_t pid, int flag, const char *fmt, ...)
{
	char buf[1024];
	va_list ap;
	char expect[1024];
	int rv = -1;

	va_start(ap, fmt);
	(void)vsnprintf(expect, sizeof expect, fmt, ap);
	va_end(ap);

	if( get_layout(pid, flag, buf, sizeof buf) == 0 ) {
		if( strcmp( buf, expect ) ) {
			fprintf(stderr, "unexpected layout:\n");
			fprintf(stderr, "received: %s\n", buf);
			fprintf(stderr, "expected: %s\n", expect);
		} else {
			rv = 0;
		}
	}
	return rv;
}

static void noop(int s) { (void)s; }
ssize_t
timed_read(int fd, void *buf, size_t count, const char *n)
{
	fd_set set;
	struct timeval t = { .tv_sec = read_timeout, .tv_usec = 0 };
	struct timeval *timeout = read_timeout ? &t : NULL;
	struct sigaction sa;
	memset(&sa, 0, sizeof sa);
	sa.sa_flags = 0;
	sigemptyset(&sa.sa_mask);
	sa.sa_handler = noop;
	sigaction(SIGINT, &sa, NULL);

	FD_ZERO(&set);
	FD_SET(fd, &set);

	switch( select(fd + 1, &set, NULL, NULL, timeout) ) {
	case -1:
		err(EXIT_FAILURE, "select %d waiting for %s", fd, n);
	case 0:
		errx(EXIT_FAILURE, "timedout waiting for %s", n);
	}
	sa.sa_handler = SIG_DFL;
	sigaction(SIGINT, &sa, NULL);
	return read(fd, buf, count);
}

/*
 * Read from fd until needle is seen or end of file.  This is not
 * intended to check anything, but is merely a synchronization device
 * to delay the test until data is seen to verify that the underlying
 * shell has processed input.
 */
static void
grep(int fd, const char *needle)
{
	assert( needle != NULL );

	char buf[BUFSIZ];
	const char *end = buf;
	const char *b = buf;
	const char *n = needle;
	while( *n != '\0' ) {
		if( b == end ) {
			ptrdiff_t d = n - needle;
			if( d > 0 ) {
				memcpy(buf, b - d, d);
			}
			size_t rc = timed_read(fd, buf + d, sizeof buf - d, n);
			switch( rc ) {
			case -1: err(EXIT_FAILURE, "read from pty");
			case 0: return;
			}
			end = buf + d + rc;
			b = buf + d;
		}
		if( *b++ != *n++ ) {
			n = needle;
		}
	}
}

/* TODO: allow alternatives.  That is, sometimes there is a race, and
 * a row has one of two values (depending on if the shell has printed
 * a prompt).  It might be nice to check that.
 */
static int
validate_row(pid_t pid, int row, const char *fmt, ... )
{
	int status = 0;
	union param p = { .usr1.row = row };
	char expect[1024];
	char buf[1024];
	va_list ap;
	va_start(ap, fmt);
	(void)vsnprintf(expect, sizeof expect, fmt, ap);
	va_end(ap);
	write(p2c[1], &p, sizeof p);
	kill(pid, SIGUSR1);
	ssize_t s = timed_read(c2p[0], buf, sizeof buf - 1, expect);
	buf[s] = 0;
	if( strcmp( buf, expect ) ) {
		fprintf(stderr, "unexpected content in row %d\n", row);
		fprintf(stderr, "received: '%s'\n", buf);
		fprintf(stderr, "expected: '%s'\n", expect);
		status = 1;
	}
	return status;
}

static int
test_ack(int fd, pid_t pid)
{
	/* Expect an \x06 in response to \x05
	 * I don't completely understand how the \x06 is getting converted
	 * to "^F"
	 */
	(void)pid;
	send_txt(fd, "^F", "printf '\\005'");
	send_cmd(fd, NULL, "143q");
	return 0; /* Test will timeout if it fails */
}

static int
test_attach(int fd, pid_t p)
{
	int id;
	char desc[1024];

	send_cmd(fd, NULL, "cc1000a");  /* Invalid attach */
	send_txt(fd, "sync", "echo 's'y'n'c");
	int rv = validate_row(p, 2, "%-80s", "sync");

	send_cmd(fd, "ps1", "j");
	send_txt(fd, "other", "echo 'o't'h'er");

	get_layout(p, 5, desc, sizeof desc);
	if( sscanf(desc, "7x80(id=%*d); *7x80(id=%d);", &id) != 1 ) {
		fprintf(stderr, "received unexpected: '%s'\n", desc);
		rv = 1;
	} else {
		send_cmd(fd, "uniq", "k%da\recho 'u'ni'q'", id);
	}
	rv |= check_layout(p, 0x1, "*7x80; 7x80; 7x80");

	/* 2nd row of the first window should now be different */
	rv |= validate_row(p, 2, "%-80s", "other");
	return rv;
}

static int
test_cols(int fd, pid_t p)
{
	/* Ensure that tput correctly identifies the width */
	int rv;
	send_txt(fd, "uniq1", "%s", "tput cols; printf 'uniq%s\\n' 1");
	rv = validate_row(p, 2, "%-92s", "97");
	send_txt(fd, NULL, "exit");
	return rv;
}

static int
test_csr(int fd, pid_t p)
{
	int rv = 0;
	/* Change scroll region */
	send_txt(fd, "uniq1", "%s;%s", "tput csr 6 12; yes | nl -s: | sed 25q",
		"printf 'uni%s\\n' q1");
	for(int i = 2; i <= 6; i++ ) {
		rv |= validate_row(p, i, "     %d:%-73s", i, "y");
	}
	for(int i = 7; i <= 11; i++ ) {
		rv |= validate_row(p, i, "    %2d:%-73s", i + 14, "y");
	}
	send_str(fd, NULL, "exit\r");
	return rv;
}

static int
test_cup(int fd, pid_t p)
{
	int status = 0;
	/* cup: move to n, m;  cub: back n;  buf: forward n */
	send_txt(fd, "uniq1", "%s", "tput cup 5 50; printf 'uniq%s\\n' 1");
	char *cmd = "printf '0123456'; tput cub 4; printf '789\\nuniq%s\\n' 2";
	send_txt(fd, "uniq2", "%s", cmd);
	cmd = "printf abc; tput cuf 73; printf '12345678%s\\n' wrapped";
	send_txt(fd, "5678wrapped", "%s", cmd);

	status |= validate_row(p, 6, "%50s%-30s", "", "uniq1");
	status |= validate_row(p, 8, "%-80s", "0127896");
	status |= validate_row(p, 11, "abc%73s1234", "");
	status |= validate_row(p, 12, "%-80s", "5678wrapped");
	send_txt(fd, NULL, "kill $SMTX");
	return status;
}

static int
test_cursor(int fd, pid_t p)
{
	int rv = 0;
	send_txt(fd, "un01", "printf '0123456'; tput cub 4; printf 'un%%s' 01");

	send_txt(fd, NULL, "tput sc; echo abcdefg; tput rc; echo bar");
	send_txt(fd, "uniq01", "printf 'uniq%%s' 01");
	rv |= validate_row(p, 4, "%-80s", "bardefg");

	send_txt(fd, "foobaz", "tput cup 15 50; printf 'foo%%s\\n' baz");
	rv |= validate_row(p, 16, "%-50sfoobaz%24s", "", "");

	send_str(fd, "foo37", "tput clear; printf 'foo%%s\n' 37\r");
	rv |= validate_row(p, 1, "%-80s", "foo37");

	send_str(fd, "bar38", "printf foo; tput ht; printf 'bar%%s\\n' 38\r");
	rv |= validate_row(p, 3, "%-80s", "foo     bar38");

	send_str(fd, "foo39", "printf 'a\\tb\\tc\\t'; tput cbt; tput cbt; "
		"printf 'foo%%s\\n' 39\r");
	rv |= validate_row(p, 5, "%-80s", "a       foo39   c");

#if 0
	check_cmd(T, "tput cud 6", "*23x80@0,0(%d,6)", y += 1 + 6);

	check_cmd(T, "printf 012; tput cub 2; tput ich 2; echo",
		"*23x80@0,0(%d,6)", y += 2);
	expect_row(y - 1001 - 1, T, "0  12%75s", " ");

	check_cmd(T, "tput cud 6", "*23x80@0,0(%d,6)", y += 1 + 6);
	check_cmd(T, ":", "*23x80@0,0(%d,6)", ++y);
	check_cmd(T, ":", "*23x80@0,0(%d,6)", ++y);
	assert( y == 1023 );
	check_cmd(T, ":", "*23x80@0,0(%d,6)", y);
#endif
	send_txt(fd, NULL, "kill $SMTX");

	return rv;
}

static int
test_dashc(int fd, pid_t p)
{
	int rv;
	send_str(fd, "uniq", "%cc\recho u'n'i'q'\r", CTL('l'));
	rv = check_layout(p, 0x1, "*11x80; 11x80");
	send_txt(fd, NULL, "kill $SMTX");
	return rv;
}

static int
test_dasht(int fd, pid_t p)
{
	/* This test exercises -t with a terminal type that should not
	 * exist in order to test the code path that uses initscr() */
	int rv;
	send_cmd(fd, "uniq", "c\recho u'n'i'q'");
	rv = check_layout(p, 0x1, "*11x80; 11x80");
	send_txt(fd, NULL, "kill $SMTX");
	return rv;
}

static int
test_dch(int fd, pid_t p)
{
	int rv = 0;
	/* Print string, move back 3, delete 2, forward 3 */
	send_txt(fd, "uniq", "%s%s%s%su'n'i'q'\\n'",
		"printf '1234567", /* Print a string */
		"\\033[3D",        /* Cursor back 3 */
		"\\033[2P",        /* Delete 2 */
		"\\033[1C"         /* Forward 1 */
	);
	rv |= validate_row(p, 2, "%-80s", "12347uniq");
	send_txt(fd, NULL, "kill $SMTX");
	return rv;
}

static int
test_decaln(int fd, pid_t p)
{
	char e[81] = "EEE";
	int rv = 0;
	memset(e, 'E', 80);
	send_txt(fd, "uniq", "printf '\\033[1048#u'; echo 'u'n'i'q;");
	rv |= validate_row(p, 1, e);
	for( int i = 4; i < 24; i++ ) {
		rv |= validate_row(p, i, e);
	}
	memcpy(e, "uniq", 4);
	rv |= validate_row(p, 2, e);
	send_str(fd, NULL, "kill $SMTX\r");
	return rv;
}

static int
test_decid(int fd, pid_t p)
{
	(void) p;
	send_txt(fd, "^[[>1;10;0c", "%s", "printf '\\033[>c'");
	send_txt(fd, "^[[?1;2c", "%s", "\rprintf '\\033[c'");
	send_txt(fd, "^[[?6c", "%s", "\rprintf '\\033Z'");
	send_txt(fd, NULL, "\rkill $SMTX");
	return 0;
}

static int
test_dsr(int fd, pid_t p)
{
	(void) p;
	send_txt(fd, "^[[2;1R", "%s", "printf '\\033[6n'");
	send_txt(fd, "^[[0n", "%s", "\rprintf '\\033[n'");
	send_txt(fd, NULL, "\rkill $SMTX");
	return 0;
}

static int
test_ech(int fd, pid_t p)
{
	int rv = 0;
	/* ech: erase N characters */
	send_txt(fd, "uniq1", "%s%s",
		"printf 012345; tput cub 3; tput ech 1;",
		"printf '\\nuniq%s\\n' 1"
	);
	rv |= validate_row(p, 2, "%-80s", "012 45");
	send_str(fd, NULL, "exit\r");
	return rv;
}

static int
test_ed(int fd, pid_t p)
{
	int rv = 0;
	send_txt(fd, "uniq", "yes | sed 15q; tput cuu 8; echo u'n'i'q'");
	rv |= validate_row(p, 8, "%-80s", "y");
	rv |= validate_row(p, 12, "%-80s", "y");
	send_txt(fd, "uniq2", "printf '\\033[J'u'n'i'q'2"); /* Clear to end */
	rv |= validate_row(p, 8, "%-80s", "y");
	rv |= validate_row(p, 12, "%-80s", "");
	send_txt(fd, "uniq3", "printf '\\033[2J'u'n'i'q'3"); /* Clear all */
	rv |= validate_row(p, 8, "%-80s", "");
	rv |= validate_row(p, 13, "%-80s", "");
	send_txt(fd, "uniq6", "clear; printf 'un''iq6'");
	send_txt(fd, "uniq4", "yes | sed 15q; tput cuu 8; echo u'n'i'q4'");
	for(int i = 2; i < 9; i++ ) {
		rv |= validate_row(p, i, "%-80s", "y");
	}
	send_txt(fd, "uniq5", "printf '\\033[1J'u'n'i'q'5"); /* Clear to top */
	for(int i = 2; i < 9; i++ ) {
		rv |= validate_row(p, i, "%-80s", "");
	}
	for(int i = 12; i < 15; i++ ) {
		rv |= validate_row(p, i, "%-80s", "y");
	}
	send_txt(fd, "uniq7", "printf '\\033[3J\\033[1;1H'u'n'i'q'7");
	for(int i = 2; i < 15; i++ ) {
		rv |= validate_row(p, i, "%-80s", "");
	}
	send_txt(fd, NULL, "exit");
	return rv;
}

static int
test_el(int fd, pid_t p)
{
	int rv = 0;
	send_txt(fd, "uniq01", "%s", "printf 01234; tput cub 3; tput el; "
		"printf 'uniq%s\\n' 01");
	rv |= validate_row(p, 2, "%-80s", "01uniq01");

	send_txt(fd, "uniq02", "%s", "printf 01234; tput cub 3; tput el1; "
		"printf '\\nuniq%s' 02");
	rv |= validate_row(p, 4, "%-80s", "   34");

	/* Delete full line with csi 2K */
	send_txt(fd, "uniq03", "%s", "printf '01234\\033[2Ku'ni'q03\\n'");
	rv |= validate_row(p, 6, "%-80s", "     uniq03");

	send_txt(fd, NULL, "exit");
	return rv;
}

static int
test_equalize(int fd, pid_t p)
{
	int status = 0;
	send_cmd(fd, "uniq1", "%s", "cc5J\rprintf uniq%s 1");
	status |= check_layout(p, 0x1, "*12x80; 4x80; 5x80");
	send_cmd(fd, "uniq2", "%s", "=\rprintf uniq%s 2");
	status |= check_layout(p, 0x1, "*7x80; 7x80; 7x80");
	send_str(fd, NULL, "kill -TERM $SMTX\r");
	return status;
}

static int
test_hpr(int fd, pid_t p)
{
	int rv = 0;
	const char *cmd = "printf 'abcd\\033[5aef'gh'ij\\n'";
	send_txt(fd, "efghij", "%s", cmd);
	rv |= validate_row(p, 2, "%-80s", "abcd     efghij");
	send_txt(fd, NULL, "exit");
	return rv;
}

static int
test_ich(int fd, pid_t p)
{
	int rv = 0;
	/* ich: insert N characters, cub: move back N */
	const char *cmd = "printf abcdefg; tput cub 3; tput ich 5";
	send_txt(fd, "uniq1", "%s; %s", cmd, "printf '\\nuni%s\\n' q1");
	rv |= validate_row(p, 2, "%-80s", "abcd     efg");

	cmd = "yes | nl | sed 6q; tput cuu 3; tput il 3; tput cud 6";
	send_txt(fd, "uniq2", "%s; %s", cmd, "printf uni'%s\\n' q2");
	for( int i=1; i < 4; i++ ) {
		rv |= validate_row(p, 4 + i, "%6d  y%71s", i, "");
	}
	for( int i=4; i < 7; i++ ) {
		rv |= validate_row(p, 4 + i, "%80s", "");
	}
	for( int i=7; i < 10; i++ ) {
		rv |= validate_row(p, 4 + i, "%6d  y%71s", i - 3, "");
	}
	/* dl: delete n lines */
	cmd = "yes | nl | sed 6q; tput cuu 5; tput dl 4; tput cud 1";
	send_txt(fd, "uniq3", "%s; %s", cmd, "printf uni'%s\\n' q3");
	rv |= validate_row(p, 16, "     %d  y%71s", 1, "");
	rv |= validate_row(p, 17, "     %d  y%71s", 6, "");

	send_txt(fd, NULL, "exit");
	return rv;
}

static int
test_insert(int fd, pid_t p)
{
	int rc = 0;
	/* smir -- begin insert mode;  rmir -- end insert mode */
	send_txt(fd, "sync01", "%s", "printf 0123456; tput cub 3; tput smir; "
		"echo foo; tput rmir; printf 'sync%s\\n' 01");
	rc |= validate_row(p, 3, "%-80s", "0123foo456");
	rc |= validate_row(p, 4, "%-80s", "sync01");
	send_str(fd, NULL, "exit\r");
	return rc;
}

static int
test_layout(int fd, pid_t p)
{
	int rv = check_layout(p, 0x13, "%s", "*23x80@0,0");

	send_cmd(fd, "uniq01", "\rprintf 'uniq%%s' 01\r");
	rv |= check_layout(p, 0x11, "*23x80@0,0");

	send_cmd(fd, "gnat", "c\rprintf 'gn%%s' at\r");
	rv |= check_layout(p, 0x11, "*11x80@0,0; 11x80@12,0");

	send_cmd(fd, "foobar", "j\rprintf 'foo%%s' bar\r");
	rv |= check_layout(p, 0x11, "11x80@0,0; *11x80@12,0");

	send_cmd(fd, "uniq02", "C\rprintf 'uniq%%s' 02\r");
	rv |= check_layout(p, 0x11, "11x80@0,0; *11x40@12,0; 11x39@12,41");

	send_cmd(fd, "foobaz", "l\rprintf 'foo%%s' baz\r");
	rv |= check_layout(p, 0x11, "11x80@0,0; 11x40@12,0; *11x39@12,41");

	send_str(fd, NULL, "kill $SMTX\r");
	return rv;
}

static int
test_lnm(int fd, pid_t pid)
{
	union param p = { .hup.flag = 1 };
	send_txt(fd, "uniq01", "printf '\\e[20huniq%%s' 01");
	send_txt(fd, NULL, "kill -HUP $SMTX");
	write(p2c[1], &p, sizeof p);
	read(c2p[0], &pid, 1); /* Read and discard */
	send_txt(fd, NULL, "printf 'foo\\rbar\\r\\n'");
	send_txt(fd, NULL, "printf '\\e[20l'");
	send_txt(fd, "uniq02", "printf 'uniq%%s' 02");
	send_txt(fd, NULL, "kill -TERM $SMTX");
	return 0;
}

static int
test_navigate(int fd, pid_t p)
{
	int status = 0;
	send_cmd(fd, NULL, "cjkhl2Cjkhlc");
	send_txt(fd, "foobar", "%s", "printf 'foo%s\\n' bar");
	status |= check_layout(p, 0x11, "%s; %s; %s; %s; %s",
		"11x26@0,0",
		"11x80@12,0",
		"*5x26@0,27",
		"5x26@6,27",
		"11x26@0,54"
	);
	send_cmd(fd, NULL, "8chhk");
	send_txt(fd, "foobaz", "%s", "printf 'foo%s\\n' baz");
	status |= check_layout(p, 0x11, "%s; %s; %s",
		"*11x26@0,0; 11x80@12,0; 0x26@0,27",
		"0x26@1,27; 0x26@2,27; 0x26@3,27; 0x26@4,27; 0x26@5,27",
		"0x26@6,27; 0x26@7,27; 1x26@8,27; 1x26@10,27; 11x26@0,54"
	);
	send_cmd(fd, NULL, "%dq", SIGTERM + 128);  /* Terminate SMTX */
	return status;
}

static int
test_nel(int fd, pid_t p)
{
	int rv = 0;
	/* nel is a newline */
	const char *cmd = "tput cud 3; printf foo; tput nel; "
		"printf 'uniq%s\\n' 01";
	send_txt(fd, "uniq01", "%s", cmd);
	rv |= validate_row(p, 5, "%-80s", "foo");
	rv |= validate_row(p, 6, "%-80s", "uniq01");
	cmd = "printf foobarz012; tput cub 7; echo blah; "
		"printf 'uniq%s\\n' 02";
	send_txt(fd, "uniq02", "%s", cmd);
	rv |= validate_row(p, 8, "%-80s", "fooblah012");
	send_str(fd, NULL, "exit\r");
	return rv;
}

static int
test_pager(int fd, pid_t p)
{
	int rv = 0;

	send_txt(fd, "--More--", "%s; %s",
		"yes abcd | nl -s: | sed -e '23,44y/abcd/wxyz/' -e 500q | more",
		"PS1=u'ni'q\\>"
	);
	rv |= validate_row(p, 02, "%-80s", "     2:abcd");
	rv |= validate_row(p, 10, "%-80s", "    10:abcd");
	rv |= validate_row(p, 22, "%-80s", "    22:abcd");
	rv |= validate_row(p, 23, "%-80s", "--More--");
	rv |= check_layout(p, 0x1, "*23x80");
	send_str(fd, "uniq>", "%s", " q");
	rv |= validate_row(p, 1,  "%-80s", "    23:wxyz");
	rv |= validate_row(p, 10, "%-80s", "    32:wxyz");
	rv |= validate_row(p, 22, "%-80s", "    44:wxyz");
	rv |= validate_row(p, 23, "%-80s", "uniq>");
	send_txt(fd, NULL, "exit");
	return rv;
}

static int
test_pnm(int fd, pid_t p)
{
	int rv = 0;
	send_txt(fd, "uniq", "printf '\\033>'u'n'i'q\\n'"); /* numkp */
	rv |= check_layout(p, 0, "23x80#");
	send_txt(fd, "uniq2", "\rprintf '\\033='u'n'i'q2\\n'"); /* numkp */
	rv |= check_layout(p, 0, "23x80");
	send_txt(fd, "uniq3", "\rprintf '\\033[1l'u'n'i'q3\\n'"); /* csi 1l */
	rv |= check_layout(p, 0, "23x80#");
	send_txt(fd, "uniq4", "\rprintf '\\033[1h'u'n'i'q4\\n'"); /* csi 1h */
	rv |= check_layout(p, 0, "23x80");
	send_txt(fd, NULL, "exit");
	return rv;
}

static int
test_quit(int fd, pid_t p)
{
	(void) p;
	send_cmd(fd, NULL, "%dq", SIGBUS); /* Invalid signal */
	send_cmd(fd, "exited", "c\rexit"); /* (2) */
	send_cmd(fd, NULL, "%dq", SIGTERM);  /* Invalid window */
	send_cmd(fd, NULL, "j");
	send_txt(fd, PROMPT, "trap \"printf 'uniq%%s' 01\" HUP");
	send_cmd(fd, "uniq01", "%dq\r", SIGHUP);  /* (1) */
	send_cmd(fd, NULL, "%dq", SIGTERM + 128);  /* Terminate SMTX */
	return 0;
}
/*
(1) The extra return seems necessary, as the shell on debian is not
    firing the trap until the newline is processed
(2) The string "exited" is expected to appear in the title line
*/

static int
test_reset(int fd, pid_t p)
{
	int k[] = { 1, 3, 4, 6, 7, 20, 25, 34, 1048, 1049, 47, 1047 };
	(void)p;

	for( unsigned long i = 0; i < sizeof k / sizeof *k; i++ ) {
		int v = k[i];
		const char *fmt =  "printf '\\e[%d%c\r";
		send_str(fd, NULL, fmt, v, 'l');
		send_str(fd, NULL, fmt, v, 'h');
	}
	send_str(fd, NULL, "kill -TERM $SMTX\r");
	return 0;
}

static int
test_resize(int fd, pid_t p)
{
	int status = 0;
	send_cmd(fd, "uniq1", "%s\r%s", "JccC", "printf 'uniq%s\\n' 1");
	status |= check_layout(p, 0x1, "*7x40; 7x80; 7x80; 7x39");
	send_cmd(fd, "uniq2", "%s\r%s", "5J", "printf 'uniq%s\\n' 2");
	status |= check_layout(p, 0x1, "*12x40; 4x80; 5x80; 12x39");
	send_cmd(fd, "uniq3", "%s\r%s", "jj10K", "printf 'uniq%s\\n' 3");
	status |= check_layout(p, 0x1, "*12x40; 0x80; 10x80; 12x39");
	send_cmd(fd, "uniq4", "%s\r%s", "kkl20H", "printf 'uniq%s\\n' 4");
	status |= check_layout(p, 0x1, "12x20; 0x80; 10x80; *12x59");
	send_str(fd, NULL, "kill -TERM $SMTX\r");
	return status;
}

/* test_resizepty() is called with -s 10 to trigger minimal history.
 * The main program sets the history to the screen size,
 * and this test resizes the screen to be larger to test
 * increase of history.
 */
static int
test_resizepty(int fd, pid_t p)
{
	int rc = 0;
	struct winsize ws = { .ws_row = 67, .ws_col = 80 };
	char buf[1024];
	(void)p;
	ssize_t s;
	int history = 24;

	while( history != 67 ) {
		if( ioctl(fd, TIOCSWINSZ, &ws) ) {
			err(EXIT_FAILURE, "ioctl");
		}
		send_cmd(fd, NULL, "%dq", SIGUSR2 + 128);
		s = read(c2p[0], buf, sizeof buf - 1);
		buf[s] = '\0';
		if( sscanf(buf, "history=%d", &history) != 1
			|| ( history != 24 && history != 67 )
		) {
			rc = 1;
			fprintf(stderr, "unexpected response: %s\n", buf);
			break;
		}
	}

	send_cmd(fd, NULL, "143q"); /* Send SIGTERM */
	return rc;
}

static int
test_ri(int fd, pid_t p)
{
	send_txt(fd, "sync", "printf '%s%s%s%s'",
		"012345678\\n", /* Print a string */
		"\\033M",        /* ri to move up one line */
		"abc\\n",       /* overwrite */
		"s'y'nc"
	);
	int rc = validate_row(p, 2, "%-80s", "abc345678");
	return rc;
}

static int
test_row(int fd, pid_t p)
{
	int status = 0;
	send_txt(fd, "uniq1", "%s; %s", "yes | nl -ba | sed 400q",
		"printf 'uniq%s\\n' 1");

	status |= validate_row(p, 20, "%6d%-74s", 399, "  y");
	status |= validate_row(p, 21, "%6d%-74s", 400, "  y");
	status |= validate_row(p, 22, "%-80s", "uniq1");
	send_txt(fd, NULL, "kill $SMTX");
	return status;
}

static int
test_scrollback(int fd, pid_t p)
{
	int status = 0;
	const char *string = "This is a relatively long string!";
	char trunc[128];

	send_cmd(fd, PROMPT ":", "CC\r:");
	status |= check_layout(p, 0x1, "*23x26; 23x26; 23x26");

	send_txt(fd, NULL, "a='%s'\rPS1=$(printf 'un%%s>' iq)", string);
	send_cmd(fd, "uniq>", "100<");
	send_txt(fd, "uniq>", "yes \"$a\" | nl |\rsed 50q");
	snprintf(trunc, 19, "%s", string);
	status |= validate_row(p, 1, "%6d  %-18s", 29, trunc);
	status |= validate_row(p, 22, "%6d  %-18s", 50, trunc);

	/* Scrollback 3, then move to another term and write a unique string */
	send_cmd(fd, "foobar", "3bl\rprintf 'foo%%s' bar");
	status |= validate_row(p, 22, "%6d  %-18s", 47, trunc);

	/* Scrollright 8, then move to another term and write a unique string */
	snprintf(trunc, 27, "%s", string);
	send_cmd(fd, "foobaz", "h8>l\rprintf 'foo%%s' baz");
	status |= validate_row(p, 14, "%-26s", trunc);

	/* Exit all pty instead of killing.  This was triggering a segfault
	 * on macos.  The test still times out whether we kill the SMTX
	 * or exit. */
	status |= check_layout(p, 0x1, "23x26; *23x26; 23x26");
	send_txt(fd, NULL, "kill $SMTX");

	return status;
}

static int
test_swap(int fd, pid_t pid)
{
	int rv = 0;
	int id[3];
	char desc[1024];
	union param p = { .hup.flag = 5 };
	send_cmd(fd, NULL, "cCjC");
	send_txt(fd, "uniq02", "printf 'uniq%%s\\n' 02");
	/* Write string2 into lower left canvas */
	send_txt(fd, NULL, "printf 'str%%s\\n' ing2; kill -HUP $SMTX");
	write(p2c[1], &p, sizeof p);
	read(c2p[0], desc, sizeof desc);
	if( sscanf(desc, "11x40(id=%*d); *11x40(id=%d); "
			"11x39(id=%*d); 11x39(id=%d)", id, id + 1) != 2 ) {
		fprintf(stderr, "received unexpected: '%s'\n", desc);
		rv = 1;
	}
	rv |= validate_row(pid, 4, "%-40s", "");
	send_cmd(fd, NULL, "k"); /* Move to upper left */
	send_txt(fd, "uniq01", "printf 'uniq%%s\\n' 01");
	send_txt(fd, NULL, "printf 'string%%s\\n' 1");

	send_cmd(fd, NULL, "1024s");   /* Invalid swap */
	send_cmd(fd, NULL, "%ds", id[0]); /* Swap upper left and lower left */
	send_txt(fd, "uniq03", "printf 'uniq%%s\\n' 03");
	rv |= validate_row(pid, 4, "%-40s", "string2");
	rv |= validate_row(pid, 6, "%-40s", "uniq03");

	/* Swap back */
	send_cmd(fd, NULL, "s");
	send_txt(fd, "uniq04", "printf 'uniq%%s\\n' 04");
	rv |= validate_row(pid, 4, "%-40s", "string1");
	rv |= validate_row(pid, 6, "%-40s", "uniq04");

	send_cmd(fd, NULL, "jl");
	send_txt(fd, "uniq05", "printf '\\nuniq%%s\\n' 05");
	send_cmd(fd, NULL, "kh");
	send_txt(fd, "uniq06", "printf 'uniq%%s\\n' 06");
	rv |= validate_row(pid, 4, "%-40s", "string1");
	send_cmd(fd, NULL, "%ds", id[1]); /* Swap upper left and lower rt */
	send_txt(fd, NULL, "printf 'uniq%%s\\n' 07; kill -HUP $SMTX");
	write(p2c[1], &p, sizeof p);
	read(c2p[0], desc, sizeof desc);

	if( sscanf(desc, "*11x40(id=%d); 11x40(id=%*d); "
			"11x39(id=%*d); 11x39(id=%*d)", id + 2) != 1 ) {
		fprintf(stderr, "received unexpected: '%s'\n", desc);
		rv = 1;
	}
	if( id[2] != id[1] ) {
		fprintf(stderr, "unexpected id in first window: %s\n", desc);
		rv = 1;
	}
	send_txt(fd, NULL, "kill -TERM %d", pid);
	return rv;
}

static int
test_tabstop(int fd, pid_t p)
{
	int rv = 0;
	int d = 0;
	const char *cmd = "printf 'this\\tis\\ta\\ttest%%d\\n' %d";
	send_txt(fd, "test0", cmd, d);
	rv |= validate_row(p, 2, "%-80s", "this    is      a       test0");

	send_cmd(fd, NULL, "3t");
	send_txt(fd, "test1", cmd, ++d);
	rv |= validate_row(p, 4, "%-80s", "this  is a  test1");

	send_cmd(fd, NULL, "t");
	rv |= validate_row(p, 6, "%-80s", "");
	send_txt(fd, "test2", cmd, ++d);
	rv |= validate_row(p, 6, "%-80s", "this    is      a       test2");

	send_txt(fd, "uniq:", "%s; %s", "tabs -5", "PS1=un'iq:'");
	send_txt(fd, "test3", cmd, ++d);
	rv |= validate_row(p, 9, "%-80s", "this is   a    test3");

	send_txt(fd, NULL, "kill -TERM %d", p);
	return rv;
}

static int
test_title(int fd, pid_t p)
{
	int rv = 0;
	char buf[69];

	/* The tail of the title should be ACS_HLINE.
	 * When the locale is wrong, ACS_HLINE == 'q', but we set the
	 * locale in smtx-main, so it seems we should set buf to have
	 * ACS_HLINE & A_CHARTEXT, but that is a space.
	 * Not entirely sure why the title comes back with 'q', but the
	 * purpose of this test is to check that the string "foobar" is
	 * set in the beginning of the title, so I'm going to punt on the
	 * 'q' for now and just hack this.
	 */
	memset(buf, 'q', 68);
	buf[68] = '\0';
	rv |= validate_row(p, 24, "1 sh 1-80/80%s", buf);
	send_txt(fd, "uniq", "printf '\\033]2foobar\\007'; echo u'n'iq");
	buf[64] = '\0';
	rv |= validate_row(p, 24, "1 foobar 1-80/80%s", buf);

	send_txt(fd, NULL, "exit");
	return rv;
}

static int
test_vis(int fd, pid_t p)
{
	int rv = 0;
	/* tput civis to hide cursor */
	send_txt(fd, "uniq1", "%s; %s", "tput civis", "printf 'uniq%s\\n' 1");
	rv |= check_layout(p, 0, "23x80!");

	/* tput cvvis to show cursor */
	send_txt(fd, "uniq2", "%s; %s", "tput cvvis", "printf 'uniq%s\\n' 2");
	rv |= check_layout(p, 0, "23x80");

	/* CSI 25l to hide cursor */
	send_txt(fd, "uniq3", "%s", "printf '\\033[?25l u'n'iq3'");
	rv |= check_layout(p, 0, "23x80!");

	/* CSI 25h to show cursor */
	send_txt(fd, "uniq4", "%s", "printf '\\033[?25h u'n'iq4'");
	rv |= check_layout(p, 0, "23x80");

	/* CSI 25l to hide cursor */
	send_txt(fd, "uniq5", "%s", "printf '\\033[?25l u'n'iq5'");
	rv |= check_layout(p, 0, "23x80!");

	/* esc p to show cursor */
	send_txt(fd, "uniq6", "%s", "printf '\\033p u'n'iq6'");
	rv |= check_layout(p, 0, "23x80");

	send_txt(fd, NULL, "exit");
	return rv;
}

static int
test_tput(int fd, pid_t p)
{
	int rv = 0;
	/* vpa: move cursor to row (0 based), hpa: move cursor to column */
	send_txt(fd, "xyz", "%s", "tput vpa 7; tput hpa 18; echo x'y'z");
	rv |= validate_row(p, 8, "%18sxyz%59s", "", "");

	/* ed: clear to end of screen */
	send_txt(fd, "uniq", "%s; %s; %s",
		"yes abcdefghijklmnopqrstuvzxyz | sed 25q", /* Fill screen */
		"tput cup 5 10; tput ed", /* Move and delete to end of screen */
		"printf 'uniq\\n'"
	);
	rv |= validate_row(p, 6, "%-80s", "abcdefghijuniq");
	for( int i = 8; i < 23; i++ ) {
		rv |= validate_row(p, i, "%80s", "");
	}
	/* bel: alert user*/
	send_txt(fd, NULL, "tput bel; kill $SMTX");
	return rv;
}

static int
test_width(int fd, pid_t p)
{
	int rv = 0;
	char buf[161];
	send_cmd(fd, "uniq01", "cCCCj\rprintf 'uniq%%s' 01");
	rv |= check_layout(p, 0x11, "%s; %s; %s; %s; %s",
		"11x20@0,0",
		"*11x80@12,0",
		"11x19@0,21",
		"11x19@0,41",
		"11x19@0,61"
	);
	/* Move up to a window that is now only 20 columns wide and
	print a string of 50 chars */
	send_cmd(fd, NULL, "k");
	send_str(fd, NULL, "for i in 1 2 3 4 5; do ");
	send_str(fd, NULL, "printf '%%s' ${i}123456789;");
	send_str(fd, NULL, "test $i = 5 && printf '\\n  uniq%%s\\n' 02;");
	send_str(fd, "uniq02", "done\r");
	rv |= validate_row(p, 3, "%-20s", "11234567892123456789");

	/* Shift right 15 chars */
	send_cmd(fd, "dedef", "15>\rprintf '%%20sded%%s' '' ef");
	rv |= validate_row(p, 3, "%-20s", "56789312345678941234");

	for( unsigned i = 0; i < sizeof buf - 1; i++ ) {
		buf[i] = 'a' + i % 26;
	}
	buf[sizeof buf - 1] = '\0';
	/* Clear screen, print 2 rows (160 chars) of alphabet */
	send_txt(fd, NULL, "clear; printf '%s\\n'", buf);
	/* Shift right 75 chars ( now looking at last 20 chars of pty)*/
	send_cmd(fd, NULL, "75>");
	/* Print 60 blanks then a string to sync*/
	send_txt(fd, "de3dbeef", "printf '%%60sde3d%%s' '' beef");
	/* Verify that the 160 chars of repeated alphabet is at end of rows */
	rv |= validate_row(p, 1, "%-20s", "ijklmnopqrstuvwxyzab");
	rv |= validate_row(p, 2, "%-20s", "klmnopqrstuvwxyzabcd");

	/* Change width of underlying pty to 180 */
	send_cmd(fd, NULL, "180W\rclear; printf '%s\\n'", buf);
	send_txt(fd, "de4dbeef", "printf '%%68sde4d%%s' '' beef");
	rv |= validate_row(p, 1, "%-20s", "ijklmnopqrstuvwxyzab");
	send_cmd(fd, NULL, "1>");
	send_txt(fd, "de5dbeef", "printf '%%68sde5d%%s' '' beef");
	rv |= validate_row(p, 1, "%-20s", "jklmnopqrstuvwxyzabc");

	/* Change width of underlying pty to match canvas and scroll to start */
	send_cmd(fd, NULL, "W180<");
	send_txt(fd, NULL, "clear; printf '%s\\n'", buf);
	send_txt(fd, "de6dbeef", "printf '%%sde6d%%s' '' beef");
	rv |= validate_row(p, 1, "%-20s", "abcdefghijklmnopqrst");
	rv |= validate_row(p, 2, "%-20s", "uvwxyzabcdefghijklmn");

	send_str(fd, NULL, "kill $SMTX\r");
	return rv;
}

static void
handler(int s)
{
	char buf[256];
	union param p;
	int len = 0;
	switch(s) {
	case SIGHUP:
		timed_read(p2c[0], &p.hup, sizeof p.hup, "sighup");
		len = describe_layout(buf, sizeof buf, p.hup.flag);
		break;
	case SIGUSR1:
		timed_read(p2c[0], &p.usr1, sizeof p.usr1, "sigusr1");
		len = describe_row(buf, sizeof buf, p.usr1.row - 1);
		break;
	case SIGUSR2:
		len = describe_state(buf, sizeof buf);
		break;
	}
	if( len > 0 ) {
		write(c2p[1], buf, len);
	}
}

static void *
xrealloc(void *buf, size_t num, size_t siz)
{
	buf = realloc(buf, num * siz);
	if( buf == NULL ) {
		perror("realloc");
		exit(EXIT_FAILURE);
	}
	return buf;
}

typedef int test(int, pid_t);
struct st {
	char *name;
	test *f;
	int envc;
	char ** env;
	int argc;
	char ** argv;
	struct st *next;
};
static int execute_test(struct st *, const char *);
static int spawn_test(struct st *v, const char *argv0);
static void
new_test(char *name, test *f, struct st **h, ...)
{
	char *arg;
	struct st *tmp = *h;
	struct st *a = *h = xrealloc(NULL, 1, sizeof *a);
	struct {
		int *c;
		char ***v;
	} cv;
	a->next = tmp;
	a->name = name;
	a->f = f;
	a->envc = 0;
	a->argc = 1;
	a->env = xrealloc(NULL, 1, sizeof *a->env);
	a->argv = xrealloc(NULL, 2, sizeof *a->argv);
	a->argv[0] = "smtx";
	cv.c = &a->envc;
	cv.v = &a->env;
	va_list ap;
	va_start(ap, h);
	while( (arg = va_arg(ap, char *)) != NULL ) {
		if( strcmp(arg, "args") == 0 ) {
			cv.c = &a->argc;
			cv.v = &a->argv;
			continue;
		}
		*cv.v = xrealloc(*cv.v, *cv.c + 2, sizeof **cv.v);
		(*cv.v)[(*cv.c)++] = arg;
	}
	va_end(ap);
	a->env[a->envc] = NULL;
	a->argv[a->argc] = NULL;
}

static int
match_name(const char *a, const char *b)
{
	const char *under = strchr(a, '_');
	return strcmp(a, b) && (!under || strcmp(under + 1, b));
}

#define F(x, ...) new_test(#x, x, &tab, ##__VA_ARGS__, NULL)
int
main(int argc, char *const argv[])
{
	int fail_count = 0;
	int total_count = 0;
	const char *argv0 = argv[0];
	struct st *tab = NULL, *v;
	F(test_ack);
	F(test_attach);
	F(test_cols, "TERM", "smtx", "COLUMNS", "92", "args", "-w", "97");
	F(test_csr);
	F(test_cup);
	F(test_cursor);
	F(test_dashc, "args", "-c", "l");
	F(test_dasht, "args", "-t", "uninstalled_terminal_type");
	F(test_dch);
	F(test_decaln);
	F(test_decid);
	F(test_dsr);
	F(test_ech);
	F(test_ed);
	F(test_el);
	F(test_equalize);
	F(test_hpr);
	F(test_ich);
	F(test_insert);
	F(test_layout);
	F(test_lnm);
	F(test_navigate);
	F(test_nel, "TERM", "smtx");
	F(test_pager ,"MORE", "");
	F(test_pnm);
	F(test_quit);
	F(test_reset);
	F(test_resize);
	F(test_resizepty, "args", "-s", "10");
	F(test_ri);
	F(test_row);
	F(test_scrollback);
	F(test_swap);
	F(test_tabstop);
	F(test_title);
	F(test_tput);
	F(test_vis);
	F(test_width);
	for( v = tab; v && ( argc < 2 || *++argv ); v = v ? v->next : NULL ) {
		const char *name = *argv;
		if( argc > 1 ) {
			for( v = tab; v && match_name(v->name, name); )
				v = v->next;
		}
		total_count += 1;
		if( v && v->f ) {
			int (*f)(struct st *, const char *);
			f = strcmp(v->name, argv0) ? spawn_test : execute_test;
			fail_count += f(v, argv0);
		} else {
			fprintf(stderr, "unknown function: %s\n", name);
			fail_count += 1;
		}
	}
	if( fail_count ) {
		fprintf(stderr, "%d test%s (of %d) failed\n", fail_count,
			fail_count > 1 ? "s" : "", total_count);
	}
	return fail_count ? EXIT_FAILURE : EXIT_SUCCESS;
}

/* Initialize a child to run a test.  We re-exec to force argv[0] to
 * the name of the test so any error messages generated using err()
 * will have the test name, and to make it easier to pick them out
 * in the output of ps.
 */
static int
exit_status(int status)
{
	return WIFEXITED(status) ? WEXITSTATUS(status) : EXIT_FAILURE;
}

static int
spawn_test(struct st *v, const char *argv0)
{
	pid_t pid[3];
	int status;
	char *const args[] = { v->name, v->name, NULL };
	switch( pid[0] = fork() ) {
	case -1:
		err(EXIT_FAILURE, "fork");
	case 0:
		switch( pid[1] = fork() ) {
		case -1:
			err(EXIT_FAILURE, "fork");
		case 0:
			execv(argv0, args);
			err(EXIT_FAILURE, "execv");
		}
		switch( pid[2] = fork() ) {
		case -1:
			err(EXIT_FAILURE, "fork");
		case 0:
			sleep(main_timeout);
			_exit(0);
		}

		pid_t died = wait(&status);
		if( died == pid[1] ) {
			if( kill(pid[2], SIGKILL) )  {
				perror("kill");
			}
			wait(NULL);
		} else {
			fprintf(stderr, "%s timed out\n", v->name);
			if( kill(pid[1], SIGINT) )  {
				perror("kill");
			}
			wait(&status);
		}
		_exit(exit_status(status));
	}
	waitpid(pid[0], &status, 0);
	return exit_status(status);
}

static void
set_window_size(struct winsize *ws)
{
	char *t = getenv("LINES");
	char *c = getenv("COLUMNS");
	ws->ws_row = t ? strtol(t, NULL, 10) : 24;
	ws->ws_col = c ? strtol(c, NULL, 10) : 80;
	unsetenv("LINES");
	unsetenv("COLUMNS");
}

static int
execute_test(struct st *v, const char *name)
{
	int fd[2]; /* primary/secondary fd of pty */
	int status;
	struct winsize ws;
	int rv = 1;
	pid_t pid;

	assert( strcmp(name, v->name) == 0 );
	unsetenv("ENV");  /* Suppress all shell initializtion */
	setenv("SHELL", "/bin/sh", 1);
	setenv("PS1", PROMPT, 1);
	unsetenv("LINES");
	unsetenv("COLUMNS");
	for( char **a = v->env; a && *a; a += 2 ) {
		setenv(a[0], a[1], 1);
	}
	set_window_size(&ws);
	if( pipe(c2p) || pipe(p2c) ) {
		err(EXIT_FAILURE, "pipe");
	}
	if( openpty(fd, fd + 1, NULL, NULL, &ws) ) {
		err(EXIT_FAILURE, "openpty");
	}
	switch( pid = fork() ) {
	case -1:
		err(1, "fork");
		break;
	case 0:
		if( close(fd[0]) ) {
			err(EXIT_FAILURE, "close");
		}
		if( login_tty(fd[1]) ) {
			err(EXIT_FAILURE, "login_tty");
		}
		rv = 0;
		struct sigaction sa;
		memset(&sa, 0, sizeof sa);
		sa.sa_flags = 0;
		sigemptyset(&sa.sa_mask);
		sa.sa_handler = handler;
		sigaction(SIGHUP, &sa, NULL);
		sigaction(SIGUSR1, &sa, NULL);
		sigaction(SIGUSR2, &sa, NULL);
		if( close(c2p[0]) || close(p2c[1]) ) {
			err(EXIT_FAILURE, "close");
		}
		exit(smtx_main(v->argc, v->argv));
	default:
		if( close(fd[1]) ) {
			err(EXIT_FAILURE, "close secondary");
		}
		if( close(c2p[1]) || close(p2c[0]) ) {
			err(EXIT_FAILURE, "close");
		}
		grep(fd[0], PROMPT); /* Wait for shell to initialize */
		rv = v->f(fd[0], pid);
		send_txt(fd[0], NULL, "kill $SMTX");
		wait(&status);
	}
	status = check_test_status(rv, status, fd[0], v->name);

	char *verbosity = getenv("V");
	if( verbosity && strtol(verbosity, NULL, 10) > 0 ) {
		printf("%s: %s\n", v->name, status ? "FAIL" : "pass" );
	}
	fclose(stderr); /* Prevent redundant output of failures */
	return status;
}

static int
check_test_status(int rv, int status, int pty, const char *name)
{
	if( WIFEXITED(status) && WEXITSTATUS(status) != 0 ) {
		char iobuf[BUFSIZ];
		ssize_t r = read(pty, iobuf, sizeof iobuf - 1);
		if( r > 0 ) {
			iobuf[r] = '\0';
			for( char *s = iobuf; *s; s++ ) {
				if( isprint(*s) || *s == '\n' ) {
					fputc(*s, stderr);
				}
			}
		}
	} else if( WIFSIGNALED(status) ) {
		fprintf(stderr, "test %s caught signal %d\n",
			name, WTERMSIG(status));
	}
	if( rv ) {
		fprintf(stderr, "%s FAILED\n", name);
	}
	return (!rv && WIFEXITED(status)) ? WEXITSTATUS(status) : EXIT_FAILURE;
}
