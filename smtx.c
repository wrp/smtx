#include "smtx.h"

int
main(int argc, char **argv)
{
	unsetenv("LINES");
	unsetenv("COLUMNS");
	return smtx_main(argc, argv);
}
