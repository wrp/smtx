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

#define PROMPT "ps1>"

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
	assert( n < sizeof cmd - 3);
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

static int __attribute__((format(printf,3,4)))
check_layout(pid_t pid, int flag, const char *fmt, ...)
{
	va_list ap;
	char buf[1024];
	char expect[1024];
	int rv = 0;
	ssize_t s;
	union param p = { .hup.flag = flag };

	va_start(ap, fmt);
	(void)vsnprintf(expect, sizeof expect, fmt, ap);
	va_end(ap);

	write(p2c[1], &p, sizeof p);
	kill(pid, SIGHUP);
	s = read(c2p[0], buf, sizeof buf - 1);
	if( s == -1 ) {
		fprintf(stderr, "reading from child: %s", strerror(errno));
		rv = -1;
	} else {
		buf[s] = 0;
		if( strcmp( buf, expect ) ) {
			fprintf(stderr, "unexpected layout:\n");
			fprintf(stderr, "received: %s\n", buf);
			fprintf(stderr, "expected: %s\n", expect);
			rv = -1;
		}
	}
	return rv;
}

ssize_t
timed_read(int fd, void *buf, size_t count, int seconds)
{
	fd_set set;
	struct timeval timeout;
	ssize_t rv = -1;

	FD_ZERO(&set);
	FD_SET(fd, &set);
	timeout.tv_sec = seconds;
	timeout.tv_usec = 0;

	switch( select(fd + 1, &set, NULL, NULL, &timeout) ) {
	case -1:
		err(EXIT_FAILURE, "select %d", fd);
	case 0:
		errx(EXIT_FAILURE, "timedout");
	default:
		rv = read(fd, buf, count);
	}
	return rv;
}
/* Read from fd until needle is seen or end of file.
 * This is not intended to really check anything,
 * but is merely a synchronization device to delay the test until
 * data is seen to verify that the underlying shell has processed
 * input.
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
			size_t rc = timed_read(fd, buf + d, sizeof buf - d, 1);
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
	ssize_t s = read(c2p[0], buf, sizeof buf - 1);
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
test_attach(int fd, pid_t pid)
{
	int id;
	char desc[1024];
	union param p = { .hup.flag = 5 };
	int status = 0;
	send_cmd(fd, NULL, "cc3a");
	send_txt(fd, NULL, "kill -HUP $SMTX");
	write(p2c[1], &p, sizeof p);
	read(c2p[0], desc, sizeof desc);
	if( sscanf(desc, "*7x80(id=%*d); 7x80(id=%d);", &id) != 1 ) {
		fprintf(stderr, "received unexpected: '%s'\n", desc);
		status = 1;
	} else {
		send_cmd(fd, NULL, "%da", id);
	}
	send_txt(fd, NULL, "kill -TERM %d", pid);
	return status;
}

static int
test_cols(int fd, pid_t p)
{
	/* Ensure that tput correctly identifies the width */
	int rv;
	send_txt(fd, "97", "tput cols");
	rv = validate_row(p, 2, "%-97s", "97");
	send_txt(fd, NULL, "exit");
	return rv;
}

static int
test_csr(int fd, pid_t p)
{
	int rv = 0;
	/* Change scroll region */
	send_str(fd, PROMPT, "tput csr 6 12\r");
	send_str(fd, PROMPT, "yes | nl | sed 25q\r");
	for(int i = 2; i <= 6; i++ ) {
		rv |= validate_row(p, i, "     %d  %-72s", i - 1, "y");
	}
	for(int i = 7; i <= 12; i++ ) {
		rv |= validate_row(p, i, "    %d  %-72s", i + 13, "y");
	}
	rv |= validate_row(p, 13, "%-80s", PROMPT);
	send_str(fd, NULL, "exit\r");
	return rv;
}

static int
test_cup(int fd, pid_t p)
{
	int status = 0;
	send_str(fd, PROMPT, "tput cup 5 50; echo foo\r"); /* down 5 lines */
	char *cmd = "printf '0123456'; tput cub 4; printf '789\\n'";
	send_str(fd, PROMPT, "%s\r", cmd);
	char *cmd2 = "printf abc; tput cuf 73; echo 12345678wrapped";
	send_str(fd, PROMPT, "%s\r", cmd2);

	assert( strlen(PROMPT) == 4 ); /* TODO: compute with this */

	status |= validate_row(p, 6, "%50s%-30s", "", "foo");
	status |= validate_row(p, 7, "%s%-76s", PROMPT, cmd);
	status |= validate_row(p, 8, "%-80s", "0127896");
	status |= validate_row(p, 9, "%s%-76s", PROMPT, cmd2);
	status |= validate_row(p, 10, "abc%73s1234", "");
	status |= validate_row(p, 11, "%-80s", "5678wrapped");
	send_str(fd, NULL, "kill $SMTX\r");
	return status;
}

static int
test_cursor(int fd, pid_t p)
{
	int rv = 0;
	send_txt(fd, "un01", "printf '0123456'; tput cub 4; printf 'un%%s' 01");
	rv |= validate_row(p, 2, "012un01%-73s", PROMPT);

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
	check_cmd(T, "printf foobar; tput cub 3; tput dch 1; echo",
		"*23x80@0,0(%d,6)", y += 2);
	expect_row(y - 1001 - 1, T, "fooar%75s", " ");
	expect_row(y - 1001, T, "%-80s", T->ps1);

	check_cmd(T, "printf 012; tput cub 2; tput ich 2; echo",
		"*23x80@0,0(%d,6)", y += 2);
	expect_row(y - 1001 - 1, T, "0  12%75s", " ");

	check_cmd(T, "tput cud 6", "*23x80@0,0(%d,6)", y += 1 + 6);
	check_cmd(T, ":", "*23x80@0,0(%d,6)", ++y);
	check_cmd(T, ":", "*23x80@0,0(%d,6)", ++y);
	assert( y == 1023 );
	check_cmd(T, ":", "*23x80@0,0(%d,6)", y);
#endif
	send_str(fd, NULL, "kill $SMTX\r");

	return rv;
}

static int
test_resize(int fd, pid_t p)
{
	int status = 0;
	send_cmd(fd, PROMPT, "JccC\r:");
	status |= check_layout(p, 0x1, "*7x40; 7x80; 7x80; 7x39");
	send_cmd(fd, PROMPT, "5J\r:");
	status |= check_layout(p, 0x1, "*12x40; 4x80; 5x80; 12x39");
	send_cmd(fd, PROMPT, "jj10K\r:");
	status |= check_layout(p, 0x1, "*12x40; 0x80; 10x80; 12x39");
	send_cmd(fd, PROMPT, "kkl20H\r:");
	status |= check_layout(p, 0x1, "12x20; 0x80; 10x80; *12x59");
	send_str(fd, NULL, "kill -TERM $SMTX\r");
	return status;
}

static int
test_ech(int fd, pid_t p)
{
	int rv = 0;
	send_str(fd, PROMPT, "printf 012345; tput cub 3; tput ech 1; echo\r");
	rv |= validate_row(p, 2, "%-80s", "012 45");
	send_str(fd, NULL, "exit\r");
	return rv;
}

static int
test_el(int fd, pid_t p)
{
	int rv = 0;
	send_str(fd, PROMPT, "printf 01234; tput cub 3; tput el\r");
	rv |= validate_row(p, 2, "01%-78s", PROMPT);

	send_str(fd, PROMPT, "printf 01234; tput cub 3; tput el1; echo\r");
	rv |= validate_row(p, 3, "   34%75s", "");

	send_str(fd, NULL, "exit\r");
	return rv;
}

static int
test_equalize(int fd, pid_t p)
{
	int status = 0;
	send_str(fd, PROMPT, "%ccc5J\recho foo\r", CTL('g'));
	status |= check_layout(p, 0x1, "*12x80; 4x80; 5x80");
	send_str(fd, PROMPT, "%c=\recho\r", CTL('g'));
	status |= check_layout(p, 0x1, "*7x80; 7x80; 7x80");
	send_str(fd, NULL, "kill -TERM $SMTX\r");
	return status;
}

static int
test_ich(int fd, pid_t p)
{
	int rv = 0;
	const char *cmd = "printf abcdefg; tput cub 3; tput ich 5; echo";
	send_str(fd, PROMPT, "%s\r", cmd);
	rv |= validate_row(p, 2, "%-80s", "abcd     efg");

	cmd = "yes | nl | sed 6q; tput cuu 3; tput il 3; tput cud 6";
	send_str(fd, PROMPT, "%s\r", cmd);
	rv |= validate_row(p, 3, "%s%-*s", PROMPT, 80 - strlen(PROMPT), cmd);
	for( int i=1; i < 4; i++ ) {
		rv |= validate_row(p, 3 + i, "%6d  y%71s", i, "");
	}
	for( int i=4; i < 7; i++ ) {
		rv |= validate_row(p, 3 + i, "%80s", "");
	}
	for( int i=7; i < 10; i++ ) {
		rv |= validate_row(p, 3 + i, "%6d  y%71s", i - 3, "");
	}
	cmd = "yes | nl | sed 6q; tput cuu 5; tput dl 4; tput cud 1";
	send_str(fd, PROMPT, "%s\r", cmd);
	rv |= validate_row(p, 14, "     %d  y%71s", 1, "");
	rv |= validate_row(p, 15, "     %d  y%71s", 6, "");

	send_str(fd, NULL, "exit\r");
	return rv;
}

static int
test_insert(int fd, pid_t p)
{
	int rc = 0;
	/* smir -- begin insert mode;  rmir -- end insert mode */
	send_str(fd, PROMPT, "printf 0123456; tput cub 3; tput smir; "
		"echo foo; tput rmir\r");
	rc |= validate_row(p, 2, "%-80s", "0123foo456");
	rc |= validate_row(p, 3, "%-80s", PROMPT);
	send_str(fd, NULL, "exit\r");
	return rc;
}

static int
test_layout(int fd, pid_t p)
{
	int rv = check_layout(p, 0x13, "*23x80@0,0(1,%zd)", strlen(PROMPT));

	send_str(fd, "uniq01", "%c\rprintf 'uniq%%s' 01\r", CTL('g'));
	rv |= check_layout(p, 0x11, "*23x80@0,0");

	send_str(fd, "gnat", "%cc\rprintf 'gn%%s' at\r", CTL('g'));
	rv |= check_layout(p, 0x11, "*11x80@0,0; 11x80@12,0");

	send_str(fd, "foobar", "%cj\rprintf 'foo%%s' bar\r", CTL('g'));
	rv |= check_layout(p, 0x11, "11x80@0,0; *11x80@12,0");

	send_str(fd, "uniq02", "%cC\rprintf 'uniq%%s' 02\r", CTL('g'));
	rv |= check_layout(p, 0x11, "11x80@0,0; *11x40@12,0; 11x39@12,41");

	send_str(fd, "foobaz", "%cl\rprintf 'foo%%s' baz\r", CTL('g'));
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
	send_cmd(fd, NULL, "cjkhlCCjkhlc");
	send_txt(fd, "foobar", "printf 'foo%%s' bar");
	status |= check_layout(p, 0x11, "%s; %s; %s; %s; %s",
		"11x26@0,0",
		"11x80@12,0",
		"*5x26@0,27",
		"5x26@6,27",
		"11x26@0,54"
	);
	send_cmd(fd, "foobaz", "cccccccchhk\rprintf 'foo%%s' baz");
	status |= check_layout(p, 0x11, "%s; %s; %s",
		"*11x26@0,0; 11x80@12,0; 0x26@0,27",
		"0x26@1,27; 0x26@2,27; 0x26@3,27; 0x26@4,27; 0x26@5,27",
		"0x26@6,27; 0x26@7,27; 1x26@8,27; 1x26@10,27; 11x26@0,54"
	);
	send_txt(fd, NULL, "kill $SMTX");
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
	cmd = "printf foobar; tput cub 3; tput el; echo blah; "
		"printf 'uniq%s\\n' 02";
	send_txt(fd, "uniq02", "%s", cmd);
	rv |= validate_row(p, 7, "%s%-*s", PROMPT, 80 - strlen(PROMPT), cmd);
	rv |= validate_row(p, 8, "%-80s", "fooblah");
	send_str(fd, NULL, "exit\r");
	return rv;
}

static int
test_pager(int fd, pid_t p)
{
	int rv = 0;

	send_str(fd, "22", "yes | nl | sed 500q | more\r");
	rv |= validate_row(p, 2, "     2%-74s", "  y");
	rv |= validate_row(p, 10, "    10%-74s", "  y");
	rv |= validate_row(p, 22, "    22%-74s", "  y");
	rv |= check_layout(p, 0x1, "*23x80");
	send_str(fd, "44", " ");
	rv |= validate_row(p, 1,  "    23%-74s", "  y");
	rv |= validate_row(p, 10, "    32%-74s", "  y");
	rv |= validate_row(p, 22, "    44%-74s", "  y");
	send_str(fd, NULL, "qexit\r");
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
test_row(int fd, pid_t p)
{
	int status = 0;
	send_str(fd, PROMPT, "yes | nl -ba | sed 400q\r");

	status |= validate_row(p, 21, "%6d%-74s", 399, "  y");
	status |= validate_row(p, 22, "%6d%-74s", 400, "  y");
	status |= validate_row(p, 23, "%-80s", PROMPT);
	send_str(fd, NULL, "kill $SMTX\r");
	return status;
}

static int
test_scrollback(int fd, pid_t p)
{
	int status = 0;
	const char *string = "This is a relatively long string!";
	char trunc[128];

	send_str(fd, PROMPT ":", "%cCC\r:\r", CTL('g'));
	status |= check_layout(p, 0x1, "*23x26; 23x26; 23x26");

	send_str(fd, NULL, "a='%s'\rPS1=$(printf 'un%%s>' iq)\r", string);
	send_str(fd, "uniq>", "%c100<\r", CTL('g'));
	send_str(fd, "uniq>", "yes \"$a\" | nl |\rsed 50q\r");
	snprintf(trunc, 19, "%s", string);
	status |= validate_row(p, 1, "%6d  %-18s", 29, trunc);
	status |= validate_row(p, 22, "%6d  %-18s", 50, trunc);

	/* Scrollback 3, then move to another term and write a unique string */
	send_str(fd, "foobar", "%c3bl\rprintf 'foo%%s' bar\r", CTL('g'));
	status |= validate_row(p, 22, "%6d  %-18s", 47, trunc);

	/* Scrollright 8, then move to another term and write a unique string */
	snprintf(trunc, 27, "%s", string);
	send_str(fd, "foobaz", "%ch8>l\rprintf 'foo%%s' baz\r", CTL('g'));
	status |= validate_row(p, 14, "%-26s", trunc);

	/* Exit all pty instead of killing.  This was triggering a segfault
	 * on macos.  The test still times out whether we kill the SMTX
	 * or exit. */
	status |= check_layout(p, 0x1, "23x26; *23x26; 23x26");
	send_str(fd, NULL, "exit\r%1$cl\rexit\r%1$chh\rexit\r", CTL('g'));

	return status;
}

static int
test_swap(int fd, pid_t pid)
{
	int rv = 0;
	int id;
	char desc[1024];
	union param p = { .hup.flag = 5 };
	send_cmd(fd, NULL, "cCjC");
	send_txt(fd, "uniq02", "printf 'uniq%%s\\n' 02");
	/* Write string2 into lower left canvas */
	send_txt(fd, NULL, "printf 'str%%s\\n' ing2; kill -HUP $SMTX");
	write(p2c[1], &p, sizeof p);
	read(c2p[0], desc, sizeof desc);
	if( sscanf(desc, "11x40(id=%*d); *11x40(id=%d); "
			"11x39(id=%*d); 11x39(id=%*d)", &id) != 1 ) {
		fprintf(stderr, "received unexpected: '%s'\n", desc);
		rv = 1;
	}
	rv |= validate_row(pid, 4, "%-40s", "");
	/* Move focus to upper left */
	send_cmd(fd, NULL, "k");
	send_txt(fd, "uniq01", "printf 'uniq%%s\\n' 01");
	/* Write string1 to upper left canvas */
	send_txt(fd, NULL, "printf 'string%%s\\n' 1");

	/* Swap upper left and lower left */
	send_cmd(fd, NULL, "%ds", id);
	send_txt(fd, "uniq03", "printf 'uniq%%s\\n' 03");
	rv |= validate_row(pid, 4, "%-40s", "string2");
	rv |= validate_row(pid, 6, "%-40s", "uniq03");
	send_cmd(fd, NULL, "s");

	/* Swap back */
	send_txt(fd, "uniq04", "printf 'uniq%%s' 04");
	rv |= validate_row(pid, 4, "%-40s", "string1");
	send_txt(fd, NULL, "kill -TERM %d", pid);
	return rv;
}

static int
test_vis(int fd, pid_t p)
{
	int rv = 0;
	send_str(fd, PROMPT, "tput civis;\r");
	rv |= validate_row(p, 1, "%-80s", PROMPT "tput civis;");
	rv |= check_layout(p, 0x3, "*23x80(2,4)!");
	rv |= validate_row(p, 2, "%-80s", PROMPT);

	send_str(fd, PROMPT, "tput cvvis;\r");
	rv |= check_layout(p, 0x3, "*23x80(3,4)");

	send_str(fd, NULL, "exit\r");
	return rv;
}

static int
test_vpa(int fd, pid_t p)
{
	int rv = 0;
	send_str(fd, PROMPT, "tput vpa 7; tput hpa 18; echo foo\r");
	rv |= validate_row(p, 8, "%18sfoo%59s", "", "");
	rv |= validate_row(p, 9, "%-80s", PROMPT);
	for( int i = 10; i < 23; i++ ) {
		rv |= validate_row(p, i, "%80s", "");
	}
	send_str(fd, NULL, "kill $SMTX\r");
	return rv;
}

static int
test_width(int fd, pid_t p)
{
	int rv = 0;
	char buf[161];
	send_str(fd, "uniq01", "%ccCCCj\rprintf 'uniq%%s' 01\r", CTL('g'));
	rv |= check_layout(p, 0x11, "%s; %s; %s; %s; %s",
		"11x20@0,0",
		"*11x80@12,0",
		"11x19@0,21",
		"11x19@0,41",
		"11x19@0,61"
	);
	/* Move up to a window that is now only 20 columns wide and
	print a string of 50 chars */
	send_str(fd, NULL, "%ck\rfor i in 1 2 3 4 5; do ", CTL('g'));
	send_str(fd, NULL, "printf '%%s' \"${i}123456789\";");
	send_str(fd, NULL, "test \"$i\" = 5 && printf '\\n  uniq%%s\\n' 02;");
	send_str(fd, "uniq02", "done\r");
	rv |= validate_row(p, 3, "%-20s", "11234567892123456789");

	send_str(fd, "dedef", "%c15>\rprintf '%%20sded%%s' '' ef\r", CTL('g'));
	rv |= validate_row(p, 3, "%-20s", "56789312345678941234");

	for( unsigned i = 0; i < sizeof buf - 1; i++ ) {
		buf[i] = 'a' + i % 26;
	}
	buf[sizeof buf - 1] = '\0';
	send_str(fd, NULL, "clear; printf '%s\\n'\r%c75>\r", buf, CTL('g'));
	send_str(fd, "de3dbeef", "printf '%%68sde3d%%s' '' beef\\n\r");
	rv |= validate_row(p, 1, "%-20s", "ijklmnopqrstuvwxyzab");
	rv |= validate_row(p, 2, "%-20s", "klmnopqrstuvwxyzabcd");

	send_str(fd, NULL, "%c180W\rclear; printf '%s\\n'\r", CTL('g'), buf);
	send_str(fd, "de4dbeef", "printf '%%68sde4d%%s' '' beef\\n\r");
	rv |= validate_row(p, 1, "%-20s", "ijklmnopqrstuvwxyzab");

	send_str(fd, NULL, "kill $SMTX\r");
	return rv;
}

static int
check_ps1(int fd, pid_t p)
{
	int s = 0;
	s |= validate_row(p, 1, "%-80s", PROMPT);
	send_str(fd, NULL, "kill $SMTX\r");

	return s;
}

static int
test1(int fd, pid_t p)
{
	(void)p;
	char *cmds[] = {
		"echo err >&2",
		"tput cud 2; tput cuu 2; tput cuf 1",
		"tput ed; tput bel",
		"tput hpa 5; tput ri",
		"tput tsl; tput fsl; tput dsl",
		"tput cub 1; tput dch 1; tput ack",
		"tput civis; tput cvvis; tput ack",
		"tabs -5",
		"kill $SMTX",
		NULL
	};
	for( char **cmd = cmds; *cmd; cmd++ ) {
		send_str(fd, NULL, "%s\r", *cmd);
	}
	return 0;
}

static void
handler(int s)
{
	char buf[256];
	union param p;
	unsigned len = 0;
	switch(s) {
	case SIGHUP:
		read(p2c[0], &p.hup, sizeof p.hup);
		len = describe_layout(buf, sizeof buf, p.hup.flag);
		break;
	case SIGUSR1:
		read(p2c[0], &p.usr1, sizeof p.usr1);
		len = describe_row(buf, sizeof buf, p.usr1.row - 1);
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
struct st { char *name; test *f; const char **env; struct st *next; };
static int execute_test(struct st *, const char *);
static int spawn_test(struct st *v, const char *argv0);
static void
new_test(char *name, test *f, struct st **h, ...)
{
	char *env;
	int env_count = 0;
	struct st *tmp = *h;
	struct st *a = *h = xrealloc(NULL, 1, sizeof *a);
	a->next = tmp;
	a->name = name;
	a->f = f;
	a->env = xrealloc(a->env, 1, sizeof *a->env);
	va_list ap;
	va_start(ap, h);
	while( (env = va_arg(ap, char *)) != NULL ) {
		a->env = xrealloc(a->env, env_count + 2, sizeof *a->env);
		a->env[env_count++] = env;
	}
	a->env[env_count] = NULL;
	va_end(ap);
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
	F(check_ps1);
	F(test1);
	F(test_attach);
	F(test_cols, "TERM", "smtx", "COLUMNS", "97");
	F(test_csr);
	F(test_cup);
	F(test_cursor);
	F(test_ech);
	F(test_el);
	F(test_equalize);
	F(test_ich);
	F(test_insert);
	F(test_layout);
	F(test_lnm);
	F(test_navigate);
	F(test_nel, "TERM", "smtx");
	F(test_pager);
	F(test_quit);
	F(test_reset);
	F(test_resize);
	F(test_row);
	F(test_scrollback);
	F(test_swap);
	F(test_vis);
	F(test_vpa);
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
			sleep(2);
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
			if( kill(pid[1], SIGKILL) )  {
				perror("kill");
			}
			wait(&status);
		}
		_exit(exit_status(status));
	}
	waitpid(pid[0], &status, 0);
	return exit_status(status);
}

static int
execute_test(struct st *v, const char *name)
{
	char *const args[] = { "smtx", NULL };
	int fd[2]; /* primary/secondary fd of pty */
	int status;
	int rv = 1;
	pid_t pid;

	assert( strcmp(name, v->name) == 0 );
	unsetenv("ENV");  /* Suppress all shell initializtion */
	setenv("SHELL", "/bin/sh", 1);
	setenv("PS1", PROMPT, 1);
	setenv("LINES", "24", 1);
	setenv("COLUMNS", "80", 1);
	for( const char **a = v->env; a && *a; a += 2 ) {
		setenv(a[0], a[1], 1);
	}
	if( pipe(c2p) || pipe(p2c) ) {
		err(EXIT_FAILURE, "pipe");
	}
	if( openpty(fd, fd + 1, NULL, NULL, NULL) ) {
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
		if( close(c2p[0]) || close(p2c[1]) ) {
			err(EXIT_FAILURE, "close");
		}
		exit(smtx_main(1, args));
	default:
		if( close(fd[1]) ) {
			err(EXIT_FAILURE, "close secondary");
		}
		if( close(c2p[1]) || close(p2c[0]) ) {
			err(EXIT_FAILURE, "close");
		}
		grep(fd[0], PROMPT); /* Wait for shell to initialize */
		rv = v->f(fd[0], pid);
		wait(&status);
	}
	status = check_test_status(rv, status, fd[0], v->name);
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
