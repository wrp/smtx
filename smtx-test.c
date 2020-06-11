#include "smtx.h"
#include <err.h>
#include <sys/wait.h>

static int
test_cuu(int fd)
{
	(void) fd;
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
	char *const args[] = { "smtx-test", "test_cuu", NULL };
	char *defaults[] = { "test1", NULL };
	struct { char *name; test *f; int main; } tab[] = {
		F(test1, 1),
		F(test_cuu, 0),
		{ NULL, NULL, 0 }
	}, *v;
	for( argv = argc < 2 ? defaults : argv + 1; *argv; argv++ ) {
		for( v = tab; v->name && strcmp(v->name, *argv); v++ )
			;
		if( v->f ) {
			int fd;
			int status;

			switch( forkpty(&fd, NULL, NULL, NULL) ) {
			case -1:
				fprintf(stderr, "forkpty: %s", strerror(errno));
				rv = EXIT_FAILURE;
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
				rv = EXIT_FAILURE;
				fprintf(stderr, "test %s FAILED", *argv);
			}
		} else {
			fprintf(stderr, "unknown function: %s", *argv);
			rv = EXIT_FAILURE;
		}
	}
	return rv;
}
