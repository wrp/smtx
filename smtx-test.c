#include "smtx.h"
#include <err.h>
#include <sys/wait.h>

/* Non-intrusive tests that manipulate the master pty. */

int rv = EXIT_SUCCESS;
int child_pipe[2];
static unsigned describe_layout(char *, ptrdiff_t, const struct canvas *, int);
static unsigned describe_row(char *desc, size_t siz, WINDOW *w, int row);

static void
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

static int
check_layout(int fd, const char *fmt, ...)
{
	va_list ap;
	char buf[1024];
	char expect[1024];
	int rv = 0;
	ssize_t s;

	va_start(ap, fmt);
	vsnprintf(expect, sizeof expect, fmt, ap);
	va_end(ap);

	fdprintf(fd, "kill -HUP $SMTX\r");
	s = read(child_pipe[0], buf, sizeof buf - 1);
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

/* Read from fd until needle is seen count non-overlapping times
 * or end of file.  This is not intended to really check anything,
 * but is merely a synchronization device to delay the test until
 * data is seen to verify that the underlying shell has processed
 * input.
 */
static void
grep(int fd, const char *needle, int count)
{
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
			size_t rc = read(fd, buf + d, sizeof buf - d);
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
test_row(int fd, pid_t p)
{
	(void)p;
	ssize_t s;
	int status = 0;
	char expect[81];
	char buf[1024];
	fdprintf(fd, "yes | nl -ba | sed 400q\r");
	fdprintf(fd, "%c21\recho 123456789\n", CTL('g'));
	grep(fd, "123456789", 3);
	fdprintf(fd, "kill -USR1 $SMTX\r");
	s = read(child_pipe[0], buf, sizeof buf - 1);
	buf[s] = 0;
	snprintf( expect, sizeof expect, "%6d%-74s", 400, "  y");
	if( strcmp( buf, expect ) ) {
		fprintf(stderr, "unexpected row: '%s'\n", buf);
		status = 1;
	}
	fdprintf(fd, "kill $SMTX\r");
	return status;
}

static int
test_lnm(int fd, pid_t p)
{
	(void)p;
	int k;
	fdprintf(fd, "printf '\\e[20h'\r");
	fdprintf(fd, "kill -HUP $SMTX\r");
	read(child_pipe[0], &k, 1);
	fdprintf(fd, "printf 'foo\\rbar\\r\\n'\r");
	fdprintf(fd, "printf '\\e[20l'\r");
	fdprintf(fd, "kill -TERM $SMTX\r");
	return 0;
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
test_attach(int fd, pid_t p)
{
	int k;
	(void)p;
	fdprintf(fd, "%ccc3a\r", CTL('g'));
	fdprintf(fd, "kill -HUP $SMTX\r");
	read(child_pipe[0], &k, 1);
	fdprintf(fd, "kill -TERM $SMTX\r");
	return 0;
}

static int
test_navigate(int fd, pid_t p)
{
	(void)p;
	int status = 0;
	fdprintf(fd, "%ccjkhlC4tCvjkh2slc\r", CTL('g'));
	status |= check_layout(fd, "%s; %s; %s; %s; %s",
		"11x26@0,0",
		"11x80@12,0",
		"*5x26@0,27",
		"5x26@6,27",
		"11x26@0,54"
	);
	fdprintf(fd, "\07cccccccc\r");
	status |= check_layout(fd, "%s; %s; %s; %s",
		"11x26@0,0; 11x80@12,0",
		"*0x26@0,27",
		"0x26@1,27; 0x26@2,27; 0x26@3,27; 0x26@4,27; 0x26@5,27",
		"0x26@6,27; 0x26@7,27; 1x26@8,27; 1x26@10,27; 11x26@0,54"
	);
	fdprintf(fd, "kill $$\r\007xv\r");
	fdprintf(fd, "kill -TERM $SMTX\r");
	return status;
}

static int
test1(int fd, pid_t p)
{
	(void)p;
	char *cmds[] = {
		"echo err >&2;",
		"tput cud 2; tput cuu 2; tput cuf 1",
		"tput ed; tput bel",
		"tput hpa 5; tput ri",
		"tput tsl; tput fsl; tput dsl",
		"tput cub 1; tput dch 1; tput ack",
		"tput civis; tput cvvis; tput ack",
		"tabs -5",
		"exit",
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
	int c = S.count == - 1 ? 1 : S.count;
	unsigned len = 0;
	switch(s) {
	case SIGHUP:
		len = describe_layout(buf, sizeof buf, S.c, c);
		break;
	case SIGUSR1:
		len = describe_row(buf, sizeof buf, S.c->p->s->win,
			S.c->offset.y + c - 1);
	}
	if( len > 0 ) {
		write(child_pipe[1], buf, len);
	}
}

/* Describe a layout. This may be called in a signal handler */
static unsigned
describe_layout(char *d, ptrdiff_t siz, const struct canvas *c, int flags)
{
	const char * const e = d + siz;
	int recurse = flags & 0x1;
	int cursor = flags & 0x2;
	int show_id = flags & 0x4;
	int show_pid = flags & 0x8;
	d += snprintf(d, e - d, "%s%dx%d@%d,%d",
		c == get_focus() ? "*" : "",
		c->extent.y, c->extent.x, c->origin.y, c->origin.x
	);
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
describe_row(char *desc, size_t siz, WINDOW *w, int row)
{
	unsigned rv;
	int y, x, mx;
	getmaxyx(w, y, mx);
	mx = mx < (int)siz ? mx : (int)siz - 1;
	getyx(w, y, x);
	desc[rv = mx] = '\0';
	for( ; mx >= 0; mx-- ) {
		desc[mx] = mvwinch(w, row, mx) & A_CHARTEXT;
	}
	wmove(w, y, x);
	return rv;
}

typedef int test(int, pid_t);
struct st { char *name; test *f; };
static int execute_test(struct st *v);
#define F(x) { .name = #x, .f = (x) }
int
main(int argc, char *const argv[])
{
	int status = 0;
	struct st tab[] = {
		F(test1),
		F(test_navigate),
		F(test_row),
		F(test_lnm),
		F(test_reset),
		F(test_attach),
		{ NULL, NULL }
	}, *v;
	setenv("SHELL", "/bin/sh", 1);
	setenv("LINES", "24", 1);
	setenv("COLUMNS", "80", 1);
	for( v = tab; ( v->f && argc < 2 ) || *++argv; v += 1 ) {
		const char *name = *argv;
		if( argc > 1 ) {
			for( v = tab; v->name && strcmp(v->name, name); v++ )
				;
		}
		if( v->f ) {
			status |= execute_test(v);
		} else {
			fprintf(stderr, "unknown function: %s\n", name);
			status = EXIT_FAILURE;
		}
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

	if( pipe(child_pipe) ) {
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
		if( close(child_pipe[0])) {
			err(EXIT_FAILURE, "close read side");
		}
		exit(smtx_main(1, args + 1));
	default:
		if( close(fd[1]) ) {
			err(EXIT_FAILURE, "close secondary");
		}
		if( close(child_pipe[1])) {
			err(EXIT_FAILURE, "close write side");
		}
		rv = v->f(fd[0], pid);
		wait(&status);
	}
	if( WIFEXITED(status) && WEXITSTATUS(status) != 0 ) {
		char iobuf[BUFSIZ], *s = iobuf;
		fprintf(stderr, "%s child FAILED\n", v->name);
		ssize_t r = read(fd[0], iobuf, sizeof iobuf - 1);
		if( r > 0 ) {
			iobuf[r] = '\0';
			for( ; *s; s++ ) {
				if( isprint(*s) || *s == '\n' ) {
					fputc(*s, stderr);
				}
			}
		}
	} else if( WIFSIGNALED(status) ) {
		fprintf(stderr, "test %s caught signal %d\n",
			v->name, WTERMSIG(status));
	}
	if( rv ) {
		fprintf(stderr, "%s FAILED", v->name);
	}
	return !rv && WIFEXITED(status) ? WEXITSTATUS(status) : EXIT_FAILURE;
}
