#include "sttm.h"
#include <err.h>

static void
expect(const char * name, struct canvas *returned, struct canvas *expected)
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
	struct canvas a = { .id = 1 };
	struct canvas b = { .id = 2 };
	struct canvas p = { .id = 0, .c = { &a, &b } };
	expect("NULL", find_canvas(NULL, 0), NULL);
	expect("find 1", find_canvas(&p, 2), &b);
	return 0;
}
