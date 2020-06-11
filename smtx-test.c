#include "smtx.h"
#include <err.h>
#include <sys/wait.h>


static void
send_cmd(int fd, char *cmd)
{
	safewrite(fd, cmd, strlen(cmd));
}

static int
test_cuu(int fd)
{
	struct canvas *root = init(24, 80);
	int y, x;
	FILE *ofp = fdopen(fd = root->p->pt, "r");

	if( ofp == NULL ) {
		err(1, "Unable to fdopen master pty\n");
	}
	getyx(root->p->s->win, y, x);
	if( y || x ) {
		errx(1, "Cursor position in root window is (%d,%d)\n", y, x);
	}
	send_cmd(fd, "PS1='X'; tput cud 5\r");
	sleep(1);  /* TODO: synchronize reasonably */
	int c;
	while( ( c = fgetc(ofp)) != 'X' && c != EOF ) {
		; /* discard first line */
	}
	while( ( c = fgetc(ofp)) != 'X' && c != EOF ) {
		vtwrite(&root->p->vp, (char *)&c, 1);
	}
	getyx(root->p->pri.win, y, x);
	if( y != 6 ) {
		errx(1, "Cursor position after cud 5 is (%d, %d)\n", y, x);
		return 1;
	}

	send_cmd(fd, "printf '012345678'; tput cub 4\r");
	sleep(1);  /* TODO: synchronize reasonably */
	while( ( c = fgetc(ofp)) != 'X' && c != EOF ) {
		vtwrite(&root->p->vp, (char *)&c, 1);
	}
	getyx(root->p->pri.win, y, x);
	if( x != 5 ) {
		errx(1, "Cursor position after cub 4 is (%d, %d)\n", y, x);
		return 1;
	}
	return 0;
}

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
main(int argc, char **argv)
{
	int rv = EXIT_SUCCESS;
	char *const args[] = { "smtx-test", NULL };
	char *defaults[] = { "test1", "test_cuu", NULL };
	struct { char *name; test *f; int main; } tab[] = {
		F(test1, 1),
		F(test_cuu, 0),
		{ NULL, NULL, 0 }
	}, *v;
	setenv("SHELL", "/bin/sh", 1);
	for( argv = argc < 2 ? defaults : argv + 1; *argv; argv++ ) {
		for( v = tab; v->name && strcmp(v->name, *argv); v++ )
			;
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
				fprintf(stderr, "test %s FAILED\n", *argv);
				ssize_t r = read(fd, s = iobuf, sizeof iobuf);
				if( r > 0 ) for( ; *s; s++ ) {
					if( isprint(*s) || *s == '\n' ) {
						fputc(*s, stderr);
					}
				}
			}
		} else {
			fprintf(stderr, "unknown function: %s\n", *argv);
			rv = EXIT_FAILURE;
		}
	}
	return rv;
}
