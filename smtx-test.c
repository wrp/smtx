#include "smtx.h"
#include <err.h>
#include <sys/wait.h>

const char *args[10];

static void
test1() {
	int fd;

	switch( forkpty(&fd, NULL, NULL, NULL) ) {
	case -1:
		err(1, "forkpty");
	case 0:
		smtx_main(1, (char *const[]) { "smtx-test", NULL });
		exit(0);
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
	int status;
	for( char **cmd = cmds; *cmd; cmd++ ) {
		write(fd, *cmd, strlen(*cmd));
		write(fd, "\r", 1);
	}
	wait(&status);
	assert( WIFEXITED(status) && WEXITSTATUS(status) == 0 );
}

int
main(int argc, char **argv)
{
	(void) argc;
	(void) argv;
	test1();
	return EXIT_SUCCESS;
}
