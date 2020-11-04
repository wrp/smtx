/*
 * Copyright 2020 - William Pursell <william.r.pursell@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include "smtx.h"

static void
parse_args(int argc, char *const*argv)
{
	int c;
	char *name = strrchr(argv[0], '/');
	while( (c = getopt(argc, argv, ":c:hs:t:vw:")) != -1 ) {
		switch( c ) {
		case 'h':
			printf("usage: %s", name ? name + 1 : argv[0]);
			puts(""
				" [-c ctrl-key]"
				" [-s history-size]"
				" [-t terminal-type]"
				" [-v]"
				" [-w width]"
			);
			exit(EXIT_SUCCESS);
		case 'c':
			S.ctlkey = CTL(optarg[0]);
			break;
		case 's':
			S.history = strtol(optarg, NULL, 10);
			break;
		case 't':
			S.term = optarg;
			break;
		case 'v':
			printf("%s-%s (%s %s)\n", PACKAGE_NAME, PACKAGE_VERSION,
				__DATE__, __TIME__);
			exit(EXIT_SUCCESS);
		case 'w':
			S.width = strtol(optarg, NULL, 10);
			break;
		default:
			fprintf(stderr, "Unknown option: %c", optopt);
			exit(EXIT_FAILURE);
		}
	}
}

int
main(int argc, char **argv)
{
	char buf[16];
	parse_args(argc, argv);
	snprintf(buf, sizeof buf - 1, "%d", getpid());
	setenv("SMTX", buf, 1);
	setenv("SMTX_VERSION", VERSION, 1);
	unsetenv("LINES");
	unsetenv("COLUMNS");
	setenv("TERM", S.term, 1);
	return smtx_main();
}
