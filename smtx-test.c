#include "smtx.h"
#include <err.h>
#include <sys/wait.h>

#define PROMPT "ps1>"

/* Non-intrusive tests that manipulate the master pty. */

int rv = EXIT_SUCCESS;
int c2p[2];
int p2c[2];
static unsigned describe_layout(char *, ptrdiff_t, const struct canvas *,
	unsigned);
static unsigned describe_row(char *, size_t, const struct canvas *, int);
static int check_test_status(int rv, int status, int pty, const char *name);

union param {
	struct { unsigned flag; } hup;
	struct { int row; } usr1;
};

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
	rewrite(fd, cmd, n);
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
	grep(fd, PROMPT, 1);
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
	grep(fd, PROMPT, 1);
	fdprintf(fd, "tput cup 5 50; echo foo\r"); /* Move down 5 lines */
	grep(fd, PROMPT, 1);
	char *cmd = "printf '0123456'; tput cub 4; printf '789\\n'";
	fdprintf(fd, "%s\r", cmd);
	grep(fd, PROMPT, 1);
	char *cmd2 = "printf abc; tput cuf 73; echo 12345678wrapped";
	fdprintf(fd, "%s\r", cmd2);
	grep(fd, PROMPT, 1);

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
test_resize(int fd, pid_t p)
{
	int status = 0;
	grep(fd, PROMPT, 1);
	fdprintf(fd, "%cJccC\r:\r", CTL('g'));
	grep(fd, PROMPT, 1);
	status |= check_layout(p, 0x1, "*7x40; 7x80; 7x80; 7x39");
	fdprintf(fd, "%c5J\r:\r", CTL('g'));
	grep(fd, PROMPT, 1);
	status |= check_layout(p, 0x1, "*12x40; 4x80; 5x80; 12x39");
	fdprintf(fd, "%cjj10K\r:\r", CTL('g'));
	grep(fd, PROMPT, 1);
	status |= check_layout(p, 0x1, "*12x40; 0x80; 10x80; 12x39");
	fdprintf(fd, "%ckkl20H\r:\r", CTL('g'));
	grep(fd, PROMPT, 1);
	status |= check_layout(p, 0x1, "12x20; 0x80; 10x80; *12x59");
	fdprintf(fd, "kill -TERM $SMTX\r");
	return status;
}

static int
test_equalize(int fd, pid_t p)
{
	int status = 0;
	grep(fd, PROMPT, 1);
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
	fdprintf(fd, "%ccjkhlC4tCvjkh2slc\r", CTL('g'));
	grep(fd, PROMPT, 1);
	status |= check_layout(p, 0x11, "%s; %s; %s; %s; %s",
		"11x26@0,0",
		"11x80@12,0",
		"*5x26@0,27",
		"5x26@6,27",
		"11x26@0,54"
	);
	fdprintf(fd, "%ccccccccchhk\rprintf 'foo%%s' bar\r", CTL('g'));
	grep(fd, "foobar", 1);
	status |= check_layout(p, 0x11, "%s; %s; %s",
		"*11x26@0,0; 11x80@12,0; 0x26@0,27",
		"0x26@1,27; 0x26@2,27; 0x26@3,27; 0x26@4,27; 0x26@5,27",
		"0x26@6,27; 0x26@7,27; 1x26@8,27; 1x26@10,27; 11x26@0,54"
	);
	fdprintf(fd, "kill $SMTX\r");
	return status;
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

	status |= validate_row(p, 20, "%6d%-74s", 399, "  y");
	status |= validate_row(p, 21, "%6d%-74s", 400, "  y");
	fdprintf(fd, "kill $SMTX\r");
	return 0;
	return status;
}

static int
test_scrollback(int fd, pid_t p)
{
	int status = 0;
	const char *string = "This is a relatively long string!";
	char trunc[128];

	grep(fd, PROMPT, 1);
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
test_width(int fd, pid_t p)
{
	int rv = 0;
	char buf[161];
	fdprintf(fd, "%ccCCCj\r", CTL('g'));
	grep(fd, PROMPT, 1);
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
	grep(fd, PROMPT, 1);
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
	grep(fd, PROMPT, 1);
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
	const struct canvas *c;
	switch(s) {
	case SIGHUP:
		read(p2c[0], &p.hup, sizeof p.hup);
		c = p.hup.flag & 0x1 ? S.c : S.f;
		len = describe_layout(buf, sizeof buf, c, p.hup.flag);
		break;
	case SIGUSR1:
		read(p2c[0], &p.usr1, sizeof p.usr1);
		len = describe_row(buf, sizeof buf, S.c,
			S.c->offset.y + p.usr1.row - 1);
	}
	if( len > 0 ) {
		write(c2p[1], buf, len);
	}
}

/* Describe a layout. This may be called in a signal handler */
static unsigned
describe_layout(char *d, ptrdiff_t siz, const struct canvas *c, unsigned flags)
{
	const char * const e = d + siz;
	int recurse = flags & 0x1;
	int cursor = flags & 0x2;
	int show_id = flags & 0x4;
	int show_pid = flags & 0x8;
	int show_pos = flags & 0x10;
	char *isfocus = recurse && c == get_focus() ? "*" : "";
	d += snprintf(d, e - d, "%s%dx%d", isfocus, c->extent.y, c->extent.x);

	if( show_pos) {
		d += snprintf(d, e - d, "@%d,%d", c->origin.y, c->origin.x);
	}
	if( show_pid && c->p ) {
		d += snprintf(d, e - d, "(pid=%d)", c->p->pid);
	}
	if( show_id && c->p ) {
		d += snprintf(d, e - d, "(id=%d)", c->p->id);
	}
	if( cursor && c->p->s ) {
		int y = 0, x = 0;
		getyx(c->p->s->win, y, x);
		d += snprintf(d, e - d, "(%d,%d)%s", y, x,
			c->p->s->vis ? "" : "!");
	}
	for( int i = 0; i < 2; i ++ ) {
		if( recurse && e - d > 3 && c->c[i] ) {
			*d++ = ';';
			*d++ = ' ';
			d += describe_layout(d, e - d, c->c[i], flags);
		}
	}
	return siz - ( e - d );
}

static unsigned
describe_row(char *desc, size_t siz, const struct canvas *c, int row)
{
	WINDOW *w = c->p->s->win;
	unsigned rv;
	int y, x, mx;
	getmaxyx(w, y, mx);
	mx = MIN3(mx, c->extent.x + c->offset.x, (int)siz - 1);
	getyx(w, y, x);
	desc[rv = mx - c->offset.x] = '\0';
	for( ; mx >= c->offset.x; mx-- ) {
		desc[mx - c->offset.x] = mvwinch(w, row, mx) & A_CHARTEXT;
	}
	wmove(w, y, x);
	return rv;
}

typedef int test(int, pid_t);
struct st { char *name; test *f; };
static int execute_test(struct st *v);
static int spawn_test(struct st *v, const char *argv0);
#define F(x) { .name = #x, .f = (x) }
int
main(int argc, char *const argv[])
{
	int status = 0;
	const char *argv0 = argv[0];
	struct st tab[] = {
		F(check_ps1),
		F(test1),
		F(test_attach),
		F(test_cup),
		F(test_equalize),
		F(test_lnm),
		F(test_navigate),
		F(test_reset),
		F(test_resize),
		F(test_row),
		F(test_scrollback),
		F(test_width),
		{ NULL, NULL }
	}, *v;
	for( v = tab; ( v->f && argc < 2 ) || *++argv; v += 1 ) {
		const char *name = *argv;
		if( argc > 1 ) {
			for( v = tab; v->name && strcmp(v->name, name); v++ )
				;
		}
		if( v->f ) {
			if( strcmp(v->name, argv0) != 0 ) {
				status |= spawn_test(v, argv0);
			} else {
				status = execute_test(v);
			}
		} else {
			fprintf(stderr, "unknown function: %s\n", name);
			status = EXIT_FAILURE;
		}
	}
	return status;
}

/* Initialize a child to run a test.  We re-exec to force argv[0] to
 * the name of the test so any error messages generated using err()
 * will have the test name, and to make it easier to pick them out
 * in the output of ps.
 */
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
		_exit(status);
	}
	waitpid(pid[0], &status, 0);
	if( status ) {
		fprintf(stderr, "%s FAILED\n", v->name);
	}
	return status;
}

static int
execute_test(struct st *v)
{
	char *const args[] = { v->name, v->name, NULL };
	int fd[2]; /* primary/secondary fd of pty */
	int status;
	pid_t pid;

	unsetenv("ENV");  /* Suppress all shell initializtion */
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
		exit(smtx_main(1, args + 1));
	default:
		if( close(fd[1]) ) {
			err(EXIT_FAILURE, "close secondary");
		}
		if( close(c2p[1]) || close(p2c[0]) ) {
			err(EXIT_FAILURE, "close");
		}
		rv = v->f(fd[0], pid);
		wait(&status);
	}
	return check_test_status(rv, status, fd[0], v->name);
}

static int
check_test_status(int rv, int status, int pty, const char *name)
{
	if( WIFEXITED(status) && WEXITSTATUS(status) != 0 ) {
		char iobuf[BUFSIZ];
		fprintf(stderr, "%s child FAILED\n", name);
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
		fprintf(stderr, "%s FAILED", name);
	}
	return !rv && WIFEXITED(status) ? WEXITSTATUS(status) : EXIT_FAILURE;
}
