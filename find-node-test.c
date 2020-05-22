#include "sttm.h"
#include <err.h>

static void
expect(const char * name, struct node *returned, struct node *expected)
{
	if( expected != returned ) {
		err(EXIT_FAILURE, "Test %s failed!!", name);
	}
}

int
main(int argc, char **argv)
{
	(void) argc;
	(void) argv;
	struct node a = { .id = 1 };
	struct node b = { .id = 2 };
	struct node p = { .id = 0, .c = { &a, &b } };
	expect("NULL", find_node(NULL, 0), NULL);
	expect("find 1", find_node(&p, 2), &b);
	return 0;
}
