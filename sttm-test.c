#include "main.h"

int
main(int argc, char **argv)
{
	(void) argc;
	(void) argv;
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

	struct node *c[2];

	create(root, NULL);
	c[0] = find_node(root, 1);
	c[1] = find_node(root, 2);
	assert(c[1] == focused);
	assert(c[0] == root->c[0]);
	assert(c[1] == root->c[1]);
	mov(c[1], (const char *[]) { "j", NULL });
	assert(c[1] == focused);
	mov(c[1], (const char *[]) { "k", NULL });
	assert(c[0] == focused);
	reorient(c[0], NULL);
	assert(c[0] == focused);
	mov(c[0], (const char *[]) { "l", NULL });
	assert(c[1] == focused);
	mov(c[1], (const char *[]) { "h", NULL });
	assert(c[0] == focused);
	redrawroot(c[0], NULL);
	equalize(c[0], NULL);
	cmd_count = 2;
	swap(c[0],NULL);
	assert(c[0] == focused);
	assert(c[1] == root->c[0]);
	assert(c[0] == root->c[1]);
	create(c[0], NULL);
	prune(c[1]);
	endwin();
	return EXIT_SUCCESS;
}
