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

/* Non-intrusive tests that manipulate the master pty. */

int rv = EXIT_SUCCESS;
int c2p[2];
int p2c[2];
static int check_test_status(int rv, int status, int pty, const char *name);
static void grep(int fd, const char *needle, int count);

union param {
	struct { unsigned flag; } hup;
	struct { int row; } usr1;
};

static void
retrywrite(int fd, const char *b, size_t n)
{
	const char *e = b + n;
	while( b < e ) {
		ssize_t s = write(fd, b, e - b);
		if( s < 0 && errno != EINTR ) {
			err(EXIT_FAILURE, "write to pty");
		}
		b += s < 0 ? 0 : s;
	}
}

static void __attribute__((format(printf,2,3)))
fdprintf(int fd, const char *fmt, ...)
{
	char cmd[1024];
	size_t n;
	va_list ap;
	va_start(ap, fmt);
	n = vsnprintf(cmd, sizeof cmd, fmt, ap);
	va_end(ap);
	assert( n < sizeof cmd );
	retrywrite(fd, cmd, n);
}

static void __attribute__((format(printf,2,3)))
send_cmd(int fd, const char *fmt, ...)
{
	char cmd[1024];
	size_t n;
	va_list ap;
	va_start(ap, fmt);
	n = vsnprintf(cmd, sizeof cmd, fmt, ap);
	va_end(ap);
	assert( n < sizeof cmd );
	retrywrite(fd, cmd, n);
	grep(fd, PROMPT, 1);
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
/* Read from fd until needle is seen count non-overlapping times
 * or end of file.  This is not intended to really check anything,
 * but is merely a synchronization device to delay the test until
 * data is seen to verify that the underlying shell has processed
 * input.
 */
static void
grep(int fd, const char *needle, int count)
{
	assert( needle != NULL );

	char buf[BUFSIZ];
	const char *end = buf;
	const char *b = buf;
	const char *n = needle;
	while( count > 0 ) {
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
		if( *b++ == *n ) {
			if( *++n == '\0' ) {
				count -= 1;
				n = needle;
			}
		} else {
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
	fdprintf(fd, "%ccc3a\r", CTL('g'));
	fdprintf(fd, "kill -HUP $SMTX\r");
	write(p2c[1], &p, sizeof p);
	read(c2p[0], desc, sizeof desc);
	if( sscanf(desc, "*7x80(id=%*d); 7x80(id=%d);", &id) != 1 ) {
		fprintf(stderr, "received unexpected: '%s'\n", desc);
		status = 1;
	} else {
		fdprintf(fd, "%c%da\r", CTL('g'), id);
	}
	fdprintf(fd, "kill -TERM %d\r", pid);
	return status;
}

static int
test_cup(int fd, pid_t p)
{
	int status = 0;
	send_cmd(fd, "tput cup 5 50; echo foo\r"); /* Move down 5 lines */
	char *cmd = "printf '0123456'; tput cub 4; printf '789\\n'";
	send_cmd(fd, "%s\r", cmd);
	char *cmd2 = "printf abc; tput cuf 73; echo 12345678wrapped";
	send_cmd(fd, "%s\r", cmd2);

	assert( strlen(PROMPT) == 4 ); /* TODO: compute with this */

	status |= validate_row(p, 6, "%50s%-30s", "", "foo");
	status |= validate_row(p, 7, "%s%-76s", PROMPT, cmd);
	status |= validate_row(p, 8, "%-80s", "0127896");
	status |= validate_row(p, 9, "%s%-76s", PROMPT, cmd2);
	status |= validate_row(p, 10, "abc%73s1234", "");
	status |= validate_row(p, 11, "%-80s", "5678wrapped");
	fdprintf(fd, "kill $SMTX\r");
	return status;
}

static int
test_cursor(int fd, pid_t p)
{
	int rv = 0;
	send_cmd(fd, "printf '0123456'; tput cub 4\r");
	rv |= validate_row(p, 2, "012%-77s", PROMPT);

	send_cmd(fd, "tput sc; echo abcdefg; tput rc; echo bar\r");
	rv |= validate_row(p, 3, "%-80s", "bardefg");

	fdprintf(fd, "tput cup 15 50; printf 'foo%%s\\n' baz\r");
	grep(fd, "foobaz", 1);
	rv |= validate_row(p, 16, "%-50sfoobaz%24s", "", "");

	fdprintf(fd, "tput clear; printf 'foo%%s\n' 37\r");
	grep(fd, "foo37", 1);
	rv |= validate_row(p, 1, "%-80s", "foo37");

	fdprintf(fd, "printf foo; tput ht; printf 'bar%%s\\n' 38\r");
	grep(fd, "bar38", 1);
	rv |= validate_row(p, 3, "%-80s", "foo     bar38");

	fdprintf(fd, "printf 'a\\tb\\tc\\t'; tput cbt; tput cbt; "
		"printf 'foo%%s\\n' 39\r");
	grep(fd, "foo39", 1);
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
	fdprintf(fd, "kill $SMTX\r");

	return rv;
}

static int
test_resize(int fd, pid_t p)
{
	int status = 0;
	send_cmd(fd, "%cJccC\r:\r", CTL('g'));
	status |= check_layout(p, 0x1, "*7x40; 7x80; 7x80; 7x39");
	send_cmd(fd, "%c5J\r:\r", CTL('g'));
	status |= check_layout(p, 0x1, "*12x40; 4x80; 5x80; 12x39");
	send_cmd(fd, "%cjj10K\r:\r", CTL('g'));
	status |= check_layout(p, 0x1, "*12x40; 0x80; 10x80; 12x39");
	send_cmd(fd, "%ckkl20H\r:\r", CTL('g'));
	status |= check_layout(p, 0x1, "12x20; 0x80; 10x80; *12x59");
	fdprintf(fd, "kill -TERM $SMTX\r");
	return status;
}

static int
test_ech(int fd, pid_t p)
{
	int rv = 0;
	send_cmd(fd, "printf 012345; tput cub 3; tput ech 1; echo\r");
	rv |= validate_row(p, 2, "%-80s", "012 45");
	fdprintf(fd, "exit\r");
	return rv;
}

static int
test_el(int fd, pid_t p)
{
	int rv = 0;
	send_cmd(fd, "printf 01234; tput cub 3; tput el\r");
	rv |= validate_row(p, 2, "01%-78s", PROMPT);

	send_cmd(fd, "printf 01234; tput cub 3; tput el1; echo\r");
	rv |= validate_row(p, 3, "   34%75s", "");

	fdprintf(fd, "exit\r");
	return rv;
}

static int
test_equalize(int fd, pid_t p)
{
	int status = 0;
	fdprintf(fd, "%ccc5J\recho foo\r", CTL('g'));
	grep(fd, PROMPT, 1);
	status |= check_layout(p, 0x1, "*12x80; 4x80; 5x80");
	fdprintf(fd, "%c=\recho\r", CTL('g'));
	grep(fd, PROMPT, 1);
	status |= check_layout(p, 0x1, "*7x80; 7x80; 7x80");
	fdprintf(fd, "kill -TERM $SMTX\r");
	return status;
}

static int
test_ich(int fd, pid_t p)
{
	int rv = 0;
	const char *cmd = "printf abcdefg; tput cub 3; tput ich 5; echo";
	fdprintf(fd, "%s\r", cmd);
	grep(fd, PROMPT, 1);
	rv |= validate_row(p, 2, "%-80s", "abcd     efg");

	cmd = "yes | nl | sed 6q; tput cuu 3; tput il 3; tput cud 6";
	fdprintf(fd, "%s\r", cmd);
	grep(fd, PROMPT, 1);
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
	fdprintf(fd, "%s\r", cmd);
	grep(fd, PROMPT, 1);
	rv |= validate_row(p, 14, "     %d  y%71s", 1, "");
	rv |= validate_row(p, 15, "     %d  y%71s", 6, "");

	fdprintf(fd, "exit\r");
	return rv;
}

static int
test_insert(int fd, pid_t p)
{
	int rc = 0;
	/* smir -- begin insert mode;  rmir -- end insert mode */
	send_cmd(fd, "printf 0123456; tput cub 3; tput smir; "
		"echo foo; tput rmir\r");
	rc |= validate_row(p, 2, "%-80s", "0123foo456");
	rc |= validate_row(p, 3, "%-80s", PROMPT);
	fdprintf(fd, "exit\r");
	return rv;
}

static int
test_lnm(int fd, pid_t pid)
{
	union param p = { .hup.flag = 1 };
	fdprintf(fd, "printf '\\e[20h'\r");
	fdprintf(fd, "kill -HUP $SMTX\r");
	write(p2c[1], &p, sizeof p);
	read(c2p[0], &pid, 1); /* Read and discard */
	fdprintf(fd, "printf 'foo\\rbar\\r\\n'\r");
	fdprintf(fd, "printf '\\e[20l'\r");
	fdprintf(fd, "kill -TERM $SMTX\r");
	return 0;
}

static int
test_navigate(int fd, pid_t p)
{
	int status = 0;
	fdprintf(fd, "%ccjkhlC4tCvjkh2slc\rprintf 'foo%%s' bar\r", CTL('g'));
	grep(fd, "foobar", 1);
	status |= check_layout(p, 0x11, "%s; %s; %s; %s; %s",
		"11x26@0,0",
		"11x80@12,0",
		"*5x26@0,27",
		"5x26@6,27",
		"11x26@0,54"
	);
	fdprintf(fd, "%ccccccccchhk\rprintf 'foo%%s' baz\r", CTL('g'));
	grep(fd, "foobaz", 1);
	status |= check_layout(p, 0x11, "%s; %s; %s",
		"*11x26@0,0; 11x80@12,0; 0x26@0,27",
		"0x26@1,27; 0x26@2,27; 0x26@3,27; 0x26@4,27; 0x26@5,27",
		"0x26@6,27; 0x26@7,27; 1x26@8,27; 1x26@10,27; 11x26@0,54"
	);
	fdprintf(fd, "kill $SMTX\r");
	return status;
}

static int
test_nel(int fd, pid_t p)
{
	int rv = 0;
	/* nel is a newline */
	const char *cmd = "tput cud 3; printf foo; tput nel; "
		"printf 'blah%d\\n' 12";
	fdprintf(fd, "%s\r", cmd);
	grep(fd, "blah12", 1);
	rv |= validate_row(p, 5, "%-80s", "foo");
	rv |= validate_row(p, 6, "%-80s", "blah12");
	cmd = "printf foobar; tput cub 3; tput el; echo blah";
	send_cmd(fd, "%s\r", cmd);
	rv |= validate_row(p, 7, "%s%-*s", PROMPT, 80 - strlen(PROMPT), cmd);
	rv |= validate_row(p, 8, "%-80s", "fooblah");
	fdprintf(fd, "exit\r");
	return rv;
}

static int
test_reset(int fd, pid_t p)
{
	int k[] = { 1, 3, 4, 6, 7, 20, 25, 34, 1048, 1049, 47, 1047 };
	(void)p;

	for( unsigned long i = 0; i < sizeof k / sizeof *k; i++ ) {
		int v = k[i];
		fdprintf(fd, "printf '\\e[%dl'\rprintf '\\e[%dh'\r", v, v);
	}
	fdprintf(fd, "kill -TERM $SMTX\r");
	return 0;
}

static int
test_row(int fd, pid_t p)
{
	int status = 0;
	fdprintf(fd, "yes | nl -ba | sed 400q\r");
	grep(fd, PROMPT, 1);

	status |= validate_row(p, 21, "%6d%-74s", 399, "  y");
	status |= validate_row(p, 22, "%6d%-74s", 400, "  y");
	status |= validate_row(p, 23, "%-80s", PROMPT);
	fdprintf(fd, "kill $SMTX\r");
	return status;
}

static int
test_scrollback(int fd, pid_t p)
{
	int status = 0;
	const char *string = "This is a relatively long string!";
	char trunc[128];

	fdprintf(fd, "%cCC\r:\r", CTL('g'));
	grep(fd, PROMPT ":", 1);
	status |= check_layout(p, 0x1, "*23x26; 23x26; 23x26");

	fdprintf(fd, "a='%s'\rPS1=$(printf 'un%%s>' iq)\r", string);
	fdprintf(fd, "%c100<\r", CTL('g'));
	grep(fd, "uniq>", 1);
	fdprintf(fd, "yes \"$a\" | nl |\rsed 50q\r");
	grep(fd, "uniq>", 1);
	snprintf(trunc, 19, "%s", string);
	status |= validate_row(p, 1, "%6d  %-18s", 29, trunc);
	status |= validate_row(p, 22, "%6d  %-18s", 50, trunc);

	/* Scrollback 3, then move to another term and write a unique string */
	fdprintf(fd, "%c3bl\rprintf 'foo%%s' bar\r", CTL('g'));
	grep(fd, "foobar", 1);
	status |= validate_row(p, 22, "%6d  %-18s", 47, trunc);

	/* Scrollright 8, then move to another term and write a unique string */
	snprintf(trunc, 27, "%s", string);
	fdprintf(fd, "%ch8>l\rprintf 'foo%%s' baz\r", CTL('g'));
	grep(fd, "foobaz", 1);
	status |= validate_row(p, 14, "%-26s", trunc);

	/* Exit all pty instead of killing.  This was triggering a segfault
	 * on macos.  The test still times out whether we kill the SMTX
	 * or exit. */
	status |= check_layout(p, 0x1, "23x26; *23x26; 23x26");
	fdprintf(fd, "exit\r%1$cl\rexit\r%1$chh\rexit\r", CTL('g'));

	return status;
}

static int
test_vis(int fd, pid_t p)
{
	int rv = 0;
	fdprintf(fd, "tput civis;\r");
	grep(fd, PROMPT, 1);
	rv |= validate_row(p, 1, "%-80s", PROMPT "tput civis;");
	rv |= check_layout(p, 0x3, "*23x80(2,4)!");
	rv |= validate_row(p, 2, "%-80s", PROMPT);

	fdprintf(fd, "tput cvvis;\r");
	grep(fd, PROMPT, 1);
	rv |= check_layout(p, 0x3, "*23x80(3,4)");

	fdprintf(fd, "exit\r");
	return rv;
}

static int
test_vpa(int fd, pid_t p)
{
	int rv = 0;
	fdprintf(fd, "tput vpa 7; tput hpa 18; echo foo\r");
	grep(fd, PROMPT, 1);
	rv |= validate_row(p, 8, "%18sfoo%59s", "", "");
	rv |= validate_row(p, 9, "%-80s", PROMPT);
	for( int i = 10; i < 23; i++ ) {
		rv |= validate_row(p, i, "%80s", "");
	}
	fdprintf(fd, "kill $SMTX\r");
	return rv;
}

static int
test_width(int fd, pid_t p)
{
	int rv = 0;
	char buf[161];
	fdprintf(fd, "%ccCCCj\rprintf 'foo%%s' bar\r", CTL('g'));
	grep(fd, "foobar", 1);
	rv |= check_layout(p, 0x11, "%s; %s; %s; %s; %s",
		"11x20@0,0",
		"*11x80@12,0",
		"11x19@0,21",
		"11x19@0,41",
		"11x19@0,61"
	);
	/* Move up to a window that is now only 20 columns wide and
	print a string of 50 chars */
	fdprintf(fd, "%ck\rfor i in 1 2 3 4 5; do ", CTL('g'));
	fdprintf(fd, "printf '%%s' \"${i}123456789\";");
	fdprintf(fd, "test \"$i\" = 5 && printf '\\n  foo%%s\\n' bar; done\r");
	grep(fd, "foobar", 1);
	rv |= validate_row(p, 3, "%-20s", "11234567892123456789");

	fdprintf(fd, "%c15>\rprintf '%%20sdead%%s' '' beef\r", CTL('g'));
	grep(fd, "deadbeef", 1);
	rv |= validate_row(p, 3, "%-20s", "56789312345678941234");

	for( unsigned i = 0; i < sizeof buf - 1; i++ ) {
		buf[i] = 'a' + i % 26;
	}
	buf[sizeof buf - 1] = '\0';
	fdprintf(fd, "clear; printf '%s\\n'\r", buf);
	fdprintf(fd, "%c75>\rprintf '%%68sde3d%%s' '' beef\\n\r", CTL('g'));
	grep(fd, "de3dbeef", 1);
	rv |= validate_row(p, 1, "%-20s", "ijklmnopqrstuvwxyzab");
	rv |= validate_row(p, 2, "%-20s", "klmnopqrstuvwxyzabcd");

	fdprintf(fd, "%c180W\rclear; printf '%s\\n'\r", CTL('g'), buf);
	fdprintf(fd, "printf '%%68sde4d%%s' '' beef\\n\r");
	grep(fd, "de4dbeef", 1);
	rv |= validate_row(p, 1, "%-20s", "ijklmnopqrstuvwxyzab");

	fdprintf(fd, "kill $SMTX\r");
	return rv;
}

static int
check_ps1(int fd, pid_t p)
{
	int s = 0;
	s |= validate_row(p, 1, "%-80s", PROMPT);
	fdprintf(fd, "kill $SMTX\r");

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
		fdprintf(fd, "%s\r", *cmd);
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

typedef int test(int, pid_t);
struct st { char *name; test *f; const char *env; struct st *next; };
static int execute_test(struct st *, const char *);
static int spawn_test(struct st *v, const char *argv0);
static void
new_test(char *name, test *f, struct st **h, const char *env)
{
	struct st *tmp = *h;
	struct st *a = *h = malloc(sizeof *a);
	if( a == NULL ) {
		err(EXIT_FAILURE, "malloc");
	};
	a->next = tmp;
	a->name = name;
	a->f = f;
	a->env = env;
}

#define F(x) new_test(#x, x, &tab, NULL)
#define E(x, y) new_test(#x, x, &tab, y)
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
	F(test_cup);
	F(test_cursor);
	F(test_ech);
	F(test_el);
	F(test_equalize);
	F(test_ich);
	F(test_insert);
	F(test_lnm);
	F(test_navigate);
	F(test_nel);
	F(test_reset);
	F(test_resize);
	F(test_row);
	F(test_scrollback);
	F(test_vis);
	F(test_vpa);
	F(test_width);
	for( v = tab; v && ( argc < 2 || *++argv ); v = v ? v->next : NULL ) {
		const char *name = *argv;
		if( argc > 1 ) {
			for( v = tab; v && strcmp(v->name, name); )
				v = v->next;
		}
		if( v && v->f ) {
			int (*f)(struct st *, const char *);
			f = strcmp(v->name, argv0) ? spawn_test : execute_test;
			fail_count += f(v, argv0);
			total_count += 1;
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
	pid_t pid;

	assert( strcmp(name, v->name) == 0 );
	unsetenv("ENV");  /* Suppress all shell initializtion */
	if(strcmp(v->name, "test_nel") == 0) {
		setenv("TERM", "smtx", 1);
	} /* TODO: figure out reasonable way to handle environ in tests */
	setenv("SHELL", "/bin/sh", 1);
	setenv("PS1", PROMPT, 1);
	setenv("LINES", "24", 1);
	setenv("COLUMNS", "80", 1);
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
		grep(fd[0], PROMPT, 1); /* Wait for shell to initialize */
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
