#include "smtx.h"
#include <err.h>
#include <sys/wait.h>


static void
send_cmd(int fd, const char *fmt, ...)
{
	char cmd[1024];
	size_t n;
	va_list ap;
	va_start(ap, fmt);
	n = vsnprintf(cmd, sizeof cmd, fmt, ap);
	va_end(ap);
	assert( n < sizeof cmd );
	safewrite(fd, cmd, n);
	safewrite(fd, "\r", 1);
}

static void
vsend_cmd(int fd, const char *fmt, va_list ap)
{
	char cmd[1024];
	size_t n;
	n = vsnprintf(cmd, sizeof cmd, fmt, ap);
	assert( n < sizeof cmd );
	safewrite(fd, cmd, n);
	safewrite(fd, "\r", 1);
}

static void
expect_layout(const struct canvas *c, const char *expect)
{
	char actual[1024];
	const char *a = actual, *b = expect;
	describe_layout(actual, sizeof actual, c);
	while( *a && ( *a++ == *b || *b == '?' ) ) {
		b += 1;
	}
	if( *b || *a ) {
		errx(1, "\nExpected \"%s\", but got \"%s\"\n", expect, actual);
	}
}

static int
test_description(int fd)
{
	(void)fd;
	struct canvas *r = init(24, 80);
	expect_layout(r, "*23x80@0,0(0,0)");
	create(r, "c");
	expect_layout(r, "*11x80@0,0(0,0); 11x80@12,0(0,0)");
	mov(r, "j");
	expect_layout(r, "11x80@0,0(0,0); *11x80@12,0(0,0)");
	create(r->c[0], "C");
	expect_layout(r, "11x80@0,0(0,0); *11x40@12,0(0,0); 11x39@12,41(0,0)");
	mov(r->c[0], "l");
	expect_layout(r, "11x80@0,0(0,0); 11x40@12,0(0,0); *11x39@12,41(0,0)");
	return 0;
}

static void
read_until(FILE *fp, const char *s, VTPARSER *vp)
{
	const char *t = s;
	while( *t ) {
		int c = fgetc(fp);
		vtwrite(vp, (char *)&c, 1);
		t = (c == *t) ? t + 1 : s;
	}
}

struct test_canvas {
	struct canvas *c;
	const char *ps1;
	FILE *fp;
	VTPARSER *vp;
};

static void
check_cmd(struct test_canvas *T, const char *expect, const char *cmd, ...)
{
	va_list ap;
	va_start(ap, cmd);
	vsend_cmd(fileno(T->fp), cmd, ap);
	va_end(ap);
	read_until(T->fp, T->ps1, T->vp);
	expect_layout(T->c, expect);
}

static int
test_cursor(int fd)
{
	struct test_canvas T;
	T.c = init(24, 80);
	T.fp = fdopen(fd = T.c->p->pt, "r");
	T.ps1 = "uniq> ";
	T.vp = &T.c->p->vp;

	if( T.fp == NULL ) {
		err(1, "Unable to fdopen master pty\n");
	}
	expect_layout(T.c, "*23x80@0,0(0,0)");
	send_cmd(fd, "PS1='%s'; tput cud 5", T.ps1);
	read_until(T.fp, T.ps1, T.vp); /* discard first line */
	read_until(T.fp, T.ps1, T.vp);
#if 0
	/* (1) */
	expect_layout(root, "*23x80@0,0(6,?)");
#endif
	check_cmd(&T, "*23x80@0,0(7,14)", "printf '0123456789ab'; tput cub 4");
	check_cmd(&T, "*23x80@0,0(8,6)", "tput sc");
	check_cmd(&T, "*23x80@0,0(8,6)", "tput rc");
	/* Hmmm. It seems weird that we start at y == 0 but after
	tput cup 15 we jump down to y = scroll_back_buffer - size + 15 */
	check_cmd(&T, "*23x80@0,0(1016,56)", "tput cup 15 50;");
	check_cmd(&T, "*23x80@0,0(1001,6)", "tput clear");
	check_cmd(&T, "*23x80@0,0(1002,14)", "tput ht");
	check_cmd(&T, "*23x80@0,0(1003,22)", "printf '\\t\\t\\t'; tput cbt");

	return 0;
}
/* (1) I expect the x coordinate of this test to be 6 (the length
of the prompt, but it consistently comes back 8.  Need to understand
where the extra 2 characters come from.  This same behavior was
observed when the prompt was only one character long.
*/

static int
test1(int fd)
{
	char *cmds[] = {
		"echo err >&2;",
		"tput cud 2; tput cuu 2; tput cuf 1",
		"tput ed; tput bel",
		"tput hpa 5; tput ri",
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

typedef int test(int);
#define F(x, y) { .name = #x, .f = (x), .main = y }
int
main(int argc, char *const argv[])
{
	int rv = EXIT_SUCCESS;
	char *const args[] = { "smtx-test", NULL };
	struct { char *name; test *f; int main; } tab[] = {
		F(test1, 1),
		F(test_cursor, 0),
		F(test_description, 0),
		{ NULL, NULL, 0 }
	}, *v;
	setenv("SHELL", "/bin/sh", 1);
	for( v = tab; ( v->f && argc < 2 ) || *++argv; v += 1 ) {
		const char *name = *argv;
		if( argc > 1 ) {
			for( v = tab; v->name && strcmp(v->name, name); v++ )
				;
		}
		if( v->f ) {
			int fd;
			int status;

			switch( forkpty(&fd, NULL, NULL, NULL) ) {
			case -1:
				err(1, "forkpty");
				break;
			case 0:
				if( v->main ) {
					exit(smtx_main(1, args));
				} else {
					exit(v->f(fd));
				}
			default:
				if( v->main ) {
					v->f(fd);
				}
				wait(&status);
			}
			if( ! WIFEXITED(status) || WEXITSTATUS(status) != 0 ) {
				char iobuf[BUFSIZ], *s;
				rv = EXIT_FAILURE;
				fprintf(stderr, "test %s FAILED\n", v->name);
				ssize_t r = read(fd, s = iobuf, sizeof iobuf);
				if( r > 0 ) for( ; *s; s++ ) {
					if( isprint(*s) || *s == '\n' ) {
						fputc(*s, stderr);
					}
				}
			}
		} else {
			fprintf(stderr, "unknown function: %s\n", name);
			rv = EXIT_FAILURE;
		}
	}
	return rv;
}
