#include "smtx.h"
#include <err.h>
#include <sys/wait.h>

const char *args[10];

static int
test1() {
	int fd;
	int status;

	switch( forkpty(&fd, NULL, NULL, NULL) ) {
	case -1:
		err(1, "forkpty");
	case 0:
		status = smtx_main(1, (char *const[]) { "smtx-test", NULL });
		exit(status);
	}
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
	wait(&status);
	return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

typedef int test(void);
#define F(x) { .name = #x, .f = (x) }
int
main(int argc, char **argv)
{
	char *defaults[] = { "test1", NULL };
	struct { char *name; test *f; } tab[] = {
		F(test1),
		{ NULL, NULL }
	}, *v;
	for( argv = argc < 2 ? defaults : argv + 1; *argv; argv++ ) {
		for( v = tab; v->name && strcmp(v->name, *argv); v++ )
			;
		if( v->f ) {
			if( !v->f()) {
				errx(EXIT_FAILURE, "test %s FAILED", *argv);
			}
		} else {
			errx(EXIT_FAILURE, "unknown function: %s", *argv);
		}
	}
	return EXIT_SUCCESS;
}
