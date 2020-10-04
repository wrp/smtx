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
#include "unit-test.h"
#include <limits.h>

/* TODO: always use the p2c/c2p pipes for synchornization. */

static int read_timeout = 1;  /* Set to 0 when interactively debugging */
static int main_timeout = 2;
int c2p[2];
int p2c[2];
static int check_test_status(int rv, int status, int pty, const char *name);
static void grep(int fd, const char *needle);
int ctlkey = CTL('g');

union param {
	struct { unsigned flag; } hup;
	struct { int row; unsigned flag; } usr1;
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

void __attribute__((format(printf,3,4)))
send_cmd(int fd, const char *wait, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	write_pty(fd, 0x3, wait, fmt, ap);
	va_end(ap);
}

void __attribute__((format(printf,3,4)))
send_txt(int fd, const char *wait, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	write_pty(fd, 0x2, wait, fmt, ap);
	va_end(ap);
}

void __attribute__((format(printf,3,4)))
send_raw(int fd, const char *wait, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	write_pty(fd, 0, wait, fmt, ap);
	va_end(ap);
}
ssize_t timed_read(int, void *, size_t, const char *);

int
get_layout(int fd, int flag, char *buf, size_t siz)
{
	int len;
	ssize_t s = 0;
	const char *end = buf + siz;
	len = snprintf(buf, siz, "%c:show_layout %d\r", ctlkey, flag);
	write(fd, buf, len);
	grep(fd, "layout: ");
	while( buf < end && s != -1 ) {
		s = timed_read(fd, buf, 1, "layout");
		if( buf[0] == ':' ) {
			buf[0] = '\0';
			break;
		}
		buf += s;
	}
	if( s == -1 ) {
		fprintf(stderr, "reading from child: %s\n", strerror(errno));
		return -1;
	}
	return 0;
}

int
get_state(int fd, char *state, size_t siz)
{
	char buf[64];
	int len;
	ssize_t s;
	len = snprintf(buf, sizeof buf, "%c:show_state\r", ctlkey);
	write(fd, buf, len);
	grep(fd, "state: ");
	do s = timed_read(fd, state, siz, "state"); while( s == 0 );
	if( s == -1 ) {
		fprintf(stderr, "reading from child: %s\n", strerror(errno));
		return -1;
	}
	return 0;
}

int __attribute__((format(printf,3,4)))
check_layout(int fd, int flag, const char *fmt, ...)
{
	char buf[1024];
	va_list ap;
	char expect[1024];
	int rv = -1;

	va_start(ap, fmt);
	(void)vsnprintf(expect, sizeof expect, fmt, ap);
	va_end(ap);

	if( get_layout(fd, flag, buf, sizeof buf) == 0 ) {
		if( strcmp( buf, expect ) ) {
			fprintf(stderr, "unexpected layout:\n");
			fputs("received: ", stderr);
			for( const char *b = buf; *b; b++ ) {
				fputc(isprint(*b) ? *b : '?', stderr);
			}
			fprintf(stderr, "\nexpected: %s\n", expect);
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

	ssize_t rc;
	char c;
	const char *n = needle;
	while( *n != '\0' ) {
		do rc = timed_read(fd, &c, 1, needle); while( rc == 0 );
		if( rc == -1 ) {
			err(EXIT_FAILURE, "read from pty");
		}
		if( c != *n++ ) {
			n = needle;
		}
	}
}

/* TODO: allow alternatives.  That is, sometimes there is a race, and
 * a row has one of two values (depending on if the shell has printed
 * a prompt).  It might be nice to check that.
 */
int
validate_row(int fd, int row, const char *fmt, ... )
{
	int len;
	size_t count = 0;
	char expect[1024];
	char buf[1024];
	char *r = buf;
	int status = 0;
	va_list ap;
	va_start(ap, fmt);
	(void)vsnprintf(expect, sizeof expect, fmt, ap);
	va_end(ap);

	len = snprintf(buf, sizeof buf, "%c:show_row %d\r", ctlkey, row - 1);
	write(fd, buf, len);
	sprintf(buf, "row %d:(", row - 1);
	grep(fd, buf);
	/* We expect to see "row N:(len)" on the fd, where len is the width of
	 * the row.  The above grep discards "row N:(".  Now read the length. */

	while( *r != ')' ) {
		if( timed_read(fd, r, 1, "count in row validation") != 1 ) {
			err(1, "Invalid read in row validation");
		} else if( *r == ')' ) {
			;
		} else if( ! isdigit(*r) ) {
			err(1, "Expected count, got '%c'\n", *r);
		} else {
			count = 10 * count + *r - '0';
		}
	}
	if( count > sizeof buf - 1 ) {
		err(1, "Row is too long");
	}
	ssize_t s = timed_read(fd, buf, count, "Reading row");
	if( s == -1 ) {
		fprintf(stderr, "reading from child: %s\n", strerror(errno));
		return -1;
	}
	buf[s] = 0;

	if( strcmp( buf, expect ) ) {
		fprintf(stderr, "unexpected content in row %d\n", row);
		fputs("received: '", stderr);
		for( const char *b = buf; *b; b++ ) {
			fputc(isprint(*b) ? *b : '?', stderr);
		}
		fprintf(stderr, "'\nexpected: '%s'\n", expect);
		status = 1;
	}
	return status;
	return 0;
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
	char bigint[128];
	int fail_count = 0;
	int total_count = 0;
	const char *argv0 = argv[0];
	struct st *tab = NULL, *v;

	snprintf(bigint, sizeof bigint, "%d", INT_MAX);
	F(test_ack);
	F(test_attach);
	F(test_bighist, "NOWAIT", "1", "args", "-s", bigint);
	F(test_changehist, "args", "-s", "128");
	F(test_cols, "COLUMNS", "92", "args", "-w", "97");
	F(test_command);
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
	F(test_resend);
	F(test_resize);
	F(test_resizepty, "args", "-s", "10");
	F(test_ri);
	F(test_row);
	F(test_scrollback);
	F(test_scrollh, "COLUMNS", "26", "args", "-w", "78");
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

	assert( strcmp(name, v->name) == 0 );
	unsetenv("ENV");  /* Suppress all shell initializtion */
	setenv("SHELL", "/bin/sh", 1);
	setenv("PS1", PROMPT, 1);
	unsetenv("LINES");
	unsetenv("COLUMNS");
	unsetenv("NOWAIT");
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
	switch( fork() ) {
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
		if( close(c2p[0]) || close(p2c[1]) ) {
			err(EXIT_FAILURE, "close");
		}
		execv("./smtx", v->argv);
		err(EXIT_FAILURE, "execv");
	default:
		if( close(fd[1]) ) {
			err(EXIT_FAILURE, "close secondary");
		}
		if( close(c2p[1]) || close(p2c[0]) ) {
			err(EXIT_FAILURE, "close");
		}
		if( getenv("NOWAIT") == NULL ) {
			grep(fd[0], PROMPT); /* Wait for shell to initialize */
		}
		rv = v->f(fd[0]);
		write(fd[0], "kill $SMTX\r", 11);
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
		fprintf(stderr, "FAILED: %s\n", name);
	}
	return (!rv && WIFEXITED(status)) ? WEXITSTATUS(status) : EXIT_FAILURE;
}
