#include "main.h"
#include <sys/wait.h>

const char *args[10];

static const char **
make_args(const char *a, ...)
{
	va_list ap;
	int i = 0;
	args[i++] = a;
	va_start(ap, a);
	while( args[i - 1] != NULL ) {
		args[i++] = va_arg(ap, const char *);
	}
	va_end(ap);
	return args;
}

void
basic1(void)
{
	FD_SET(STDIN_FILENO, &fds);
	setlocale(LC_ALL, "");
	signal(SIGCHLD, SIG_IGN); /* automatically reap children */
	build_bindings();

	if( initscr() == NULL ) {
		exit(EXIT_FAILURE);
	}
	raw();
	noecho();
	nonl();
	intrflush(NULL, FALSE);
	start_color();
	use_default_colors();

	view_root = root = newnode(0, 0, LINES, COLS, ++id);
	if( root == NULL || !new_screens(root) || !new_pty(root) ) {
		err(EXIT_FAILURE, "Unable to create root window");
	}
	focus(view_root);
	draw(view_root);

	struct node *c[3];

	create(root, make_args(NULL));
	c[0] = find_node(root, 1);
	c[1] = find_node(root, 2);
	assert(c[1] == focused);
	assert(c[0] == root->c[0]);
	assert(c[1] == root->c[1]);
	mov(c[1], make_args("j", NULL));
	assert(c[1] == focused);
	mov(c[1], make_args("k", NULL));
	assert(c[0] == focused);
	reorient(c[0], NULL);
	assert(c[0] == focused);
	mov(c[0], make_args("l", NULL));
	assert(c[1] == focused);
	mov(c[1], make_args("h", NULL));
	assert(c[0] == focused);
	redrawroot(c[0], NULL);
	equalize(c[0], NULL);
	digit(c[0], make_args("2", NULL));
	swap(c[0], NULL);
	cmd_count = -1;
	assert(c[0] == focused);
	assert(c[1] == root->c[0]);
	assert(c[0] == root->c[1]);
	create(c[0], make_args(NULL));
	c[2] = find_node(root, 3);
	assert(c[2] == root->c[1]->c[1]);
	prune(root->c[1]->c[0]);
	assert(c[2] == root->c[1]);
	assert(c[2] == focused);

	/* Test equalize */
	cmd_count = 20;
	resize(c[2], make_args(">"));
	assert(root->split_point == 20.0 / 100.0);
	equalize(c[2], NULL);
	assert(root->split_point == 50.0 / 100.0);

	reshape_root(focused, NULL);
	cmd_count = -1;
	new_tabstop(focused, NULL);
	mov(focused, make_args("V", NULL));
	mov(focused, make_args("v", NULL));
	cmd_count = 7;
	new_tabstop(focused, NULL);

	endwin();
}

static void
test1() {
	int fd;
	switch( forkpty(&fd, NULL, NULL, NULL) ) {
	case -1:
		err(1, "forkpty");
	case 0:
		/* TODO: remove all this boiler plate */
		FD_SET(STDIN_FILENO, &fds);
		setlocale(LC_ALL, "");
		signal(SIGCHLD, SIG_IGN);
		build_bindings();
		if( initscr() == NULL ) {
			exit(EXIT_FAILURE);
		}
		raw();
		noecho();
		nonl();
		intrflush(NULL, FALSE);
		start_color();
		use_default_colors();

		view_root = root = newnode(0, 0, LINES, COLS, ++id);
		if( root == NULL || !new_screens(root) || !new_pty(root) ) {
			err(EXIT_FAILURE, "Unable to create root window");
		}
		focus(view_root);
		draw(view_root);
		main_loop();
		endwin();
		exit(0);
	default: {
		char cmd[] = "tput cuu\rtput cud\rexit\r";
		int status;
		write(fd, cmd, strlen(cmd));
		wait(&status);
		assert( WIFEXITED(status) && WEXITSTATUS(status) == 0 );
		}
	}
}
int
main(int argc, char **argv)
{
	(void) argc;
	(void) argv;
	test1();
	basic1();
	return EXIT_SUCCESS;
}
