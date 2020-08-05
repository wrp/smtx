#include "smtx.h"
#include <err.h>
#include <sys/wait.h>

/* Non-intrusive tests that manipulate the master pty. */

int rv = EXIT_SUCCESS;
int child_pipe[2];
static unsigned describe_layout(char *, size_t, const struct canvas *,
	int, int);
static unsigned describe_row(char *desc, size_t siz, WINDOW *w, int row);

static void
write_string(int fd, const char *fmt, ...)
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
test_prompt(int fd)
{
	ssize_t s;
	int status = 0;
	char buf[1024] = "yes | nl -ba | sed 30q; kill -USR1 $SMTX\r";
	write(fd, buf, strlen(buf));
	s = read(child_pipe[0], buf, sizeof buf - 1);
	buf[s] = 0;
	char *expect = "foo";
	if( strcmp( buf, expect ) ) {
		fprintf(stderr, "unexpected row: %s\n", buf);
	}
	sprintf(buf, "\07%dq", SIGTERM + 128);
	write(fd, buf, strlen(buf));
	return status;
}

static int
test_lnm(int fd)
{
	/* I'm not exactly sure what happens here.  The whole point
	of \e[20h is that it should set p->lnm so that \r sequences
	are converted to \r\n.  But it seems that the master pty
	under which our main loop is running is receiving \n instead
	of the \r written here, so we never get to execute the code
	that is inserting \n after \r.  Probably need to change
	settings on the master pty
	*/
	write_string(fd, "printf '\\e[20h'\r");
	sleep(1);
	write_string(fd, "printf 'foo\\rbar\\r\\n'\r");
	write_string(fd, "printf '\\e[20l'\r");
	write_string(fd, "kill -TERM $SMTX\r");
	return 0;
}

static int
test_reset(int fd)
{
	int k[] = { 1, 3, 4, 6, 7, 20, 25, 34, 1048, 1049, 47, 1047 };
	char buf[1024];

	for( unsigned long i = 0; i < sizeof k / sizeof *k; i++ ) {
		int v = k[i];
		sprintf(buf, "printf '\\e[%dl'\rprintf '\\e[%dh'\r", v, v);
		write(fd, buf, strlen(buf));
	}
	sprintf(buf, "kill -TERM $SMTX\r");
	write(fd, buf, strlen(buf));
	return 0;
}

static int
test_navigate(int fd)
{
	ssize_t s;
	int status = 0;
	char buf[1024] = "\07cjkhlC4tCvjkh2slc\r";
	assert( buf[0] == CTL('g') );
	write(fd, buf, strlen(buf));

	sprintf(buf, "kill -HUP $SMTX\r");
	write(fd, buf, strlen(buf));
	s = read(child_pipe[0], buf, sizeof buf - 1);
	buf[s] = 0;
	char *expect = "11x26@0,0; 11x80@12,0; *5x26@0,27; "
		"5x26@6,27; 11x26@0,54";
	if( strcmp( buf, expect ) ) {
		fprintf(stderr, "unexpected layout: %s\n", buf);
		status = 1;
	}
	sprintf(buf, "\07cccccccc\r");
	write(fd, buf, strlen(buf));
	sprintf(buf, "kill -HUP $SMTX\r");
	write(fd, buf, strlen(buf));
	s = read(child_pipe[0], buf, sizeof buf - 1);
	expect = "11x26@0,0; 11x80@12,0; *0x26@0,27; 0x26@1,27; 0x26@2,27; "
		"0x26@3,27; 0x26@4,27; 0x26@5,27; 0x26@6,27; 0x26@7,27; "
		"1x26@8,27; 1x26@10,27; 11x26@0,54";

	if( strcmp( buf, expect ) ) {
		fprintf(stderr, "unexpected layout after squeeze: %s\n", buf);
		status = 1;
	}

	sprintf(buf, "kill $$\r\007xv\r");
	write(fd, buf, strlen(buf));
	sprintf(buf, "kill -TERM $SMTX\r");
	write(fd, buf, strlen(buf));
	return status;
}

static int
test1(int fd)
{
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
		write(fd, *cmd, strlen(*cmd));
		write(fd, "\r", 1);
	}
	return 0;
}

static void
handler(int s)
{
	char buf[256];
	unsigned len = 0;
	switch(s) {
	case SIGHUP:
		len = describe_layout(buf, sizeof buf, S.c, 1, 0);
		break;
	case SIGUSR1:
		len = describe_row(buf, sizeof buf, S.c->p->s->win,
			S.c->offset.y + S.c->extent.y - 1);
	}
	if( len > 0 ) {
		write(child_pipe[1], buf, len);
	}
}

/* Describe a layout. This may be called in a signal handler */
static unsigned
describe_layout(char *d, size_t siz, const struct canvas *c, int recurse,
	int cursor)
{
	unsigned len = snprintf(d, siz, "%s%dx%d@%d,%d",
		c == get_focus() ? "*" : "",
		c->extent.y, c->extent.x, c->origin.y, c->origin.x
	);
	if( cursor && c->p->s ) {
		int y = 0, x = 0;
		getyx(c->p->s->win, y, x);
		len += snprintf(d + len, siz - len, "(%d,%d)%s", y, x,
			c->p->s->vis ? "" : "!");
	}
	for( int i = 0; i < 2; i ++ ) {
		if( recurse && len + 3 < siz && c->c[i] ) {
			d[len++] = ';';
			d[len++] = ' ';
			len += describe_layout(d + len, siz - len, c->c[i], 1,
				cursor);
		}
	}
	return len;
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

typedef int test(int);
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
		F(test_prompt),
		F(test_lnm),
		F(test_reset),
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

	if( pipe(child_pipe) ) {
		err(EXIT_FAILURE, "pipe");
	}
	if( openpty(fd, fd + 1, NULL, NULL, NULL) ) {
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
		rv = v->f(fd[0]);
		wait(&status);
	}
	if( WIFEXITED(status) && WEXITSTATUS(status) != 0 ) {
		char iobuf[BUFSIZ], *s = iobuf;
		fprintf(stderr, "test %s FAILED\n", v->name);
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
	return !rv && WIFEXITED(status) ? WEXITSTATUS(status) : EXIT_FAILURE;
}
