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
#include "unit-test.h"

int
test_ack(int fd)
{
	/* Expect an \x06 in response to \x05
	 * I don't completely understand how the \x06 is getting converted
	 * to "^F"
	 */
	send_txt(fd, "^F", "printf '\\005'");
	send_cmd(fd, NULL, "143q");
	return 0; /* Test will timeout if it fails */
}

int
test_attach(int fd)
{
	int id;
	char desc[1024];

	send_cmd(fd, NULL, "cc1000a");  /* Invalid attach */
	send_txt(fd, "sync", "printf '\\ns'y'n'c'\\n'");
	int rv = validate_row(fd, 3, "%-80s", "sync");

	send_cmd(fd, "other", "j\rprintf '\\no't'h'er'\\n'");

	get_layout(fd, 5, desc, sizeof desc);
	if( sscanf(desc, "7x80(id=%*d); *7x80(id=%d);", &id) != 1 ) {
		fprintf(stderr, "received unexpected: '%s'\n", desc);
		rv = 1;
	} else {
		send_cmd(fd, "uniq", "k%da\recho 'u'ni'q'", id);
	}
	rv |= check_layout(fd, 0x1, "*7x80; 7x80; 7x80");

	/* 2nd row of the first window should now be different */
	rv |= validate_row(fd, 3, "%-80s", "other");
	return rv;
}

int
test_bighist(int fd)
{
	/* Use -s 9999999999 to trigger a memory allocation error */
	/* Not really sure what else to do here; just call it to get
	code coverage I guess */
	(void)fd;
	return 0;
}

int
test_cols(int fd)
{
	/* Ensure that tput correctly identifies the width */
	int rv;
	send_txt(fd, "uniq1", "%s", "tput cols; printf 'uniq%s\\n' 1");
	rv = validate_row(fd, 2, "%-92s", "97");
	send_txt(fd, NULL, "exit");
	return rv;
}

int
test_command(int fd)
{
	send_cmd(fd, NULL, ":bad_key   with arg");
	send_cmd(fd, "unknown function: xaqx", ":xaqx   with arg");
	send_txt(fd, NULL, "exit");
	return 0;
}

int
test_csr(int fd)
{
	int rv = 0;
	/* Change scroll region */
	send_txt(fd, PROMPT, "%s;%s", "tput csr 6 12; yes | nl -s: | sed 25q",
		"printf 'uni%s\\n' q1");
	for(int i = 2; i <= 6; i++ ) {
		rv |= validate_row(fd, i, "     %d:%-73s", i, "y");
	}
	for(int i = 7; i <= 11; i++ ) {
		rv |= validate_row(fd, i, "    %2d:%-73s", i + 14, "y");
	}
	send_str(fd, NULL, "exit\r");
	return rv;
}

int
test_cup(int fd)
{
	int status = 0;
	/* cup: move to n, m;  cub: back n;  buf: forward n */
	send_txt(fd, "uniq1", "%s", "tput cup 5 50; printf 'uniq%s\\n' 1");
	char *cmd = "printf '0123456'; tput cub 4; printf '789\\nuniq%s\\n' 2";
	send_txt(fd, "uniq2", "%s", cmd);
	cmd = "printf abc; tput cuf 73; printf '12345678%s\\n' wrapped";
	send_txt(fd, "5678wrapped", "%s", cmd);

	status |= validate_row(fd, 6, "%50s%-30s", "", "uniq1");
	status |= validate_row(fd, 8, "%-80s", "0127896");
	status |= validate_row(fd, 11, "abc%73s1234", "");
	status |= validate_row(fd, 12, "%-80s", "5678wrapped");
	send_txt(fd, NULL, "kill $SMTX");
	return status;
}

int
test_cursor(int fd)
{
	int rv = 0;
	send_txt(fd, "un01", "printf '0123456'; tput cub 4; printf 'un%%s' 01");

	send_txt(fd, NULL, "tput sc; echo abcdefg; tput rc; echo bar");
	send_txt(fd, "uniq01", "printf 'uniq%%s' 01");
	rv |= validate_row(fd, 4, "%-80s", "bardefg");

	send_txt(fd, "foobaz", "tput cup 15 50; printf 'foo%%s\\n' baz");
	rv |= validate_row(fd, 16, "%-50sfoobaz%24s", "", "");

	send_txt(fd, "foo37", "tput clear; printf 'foo%%s\n' 37");
	rv |= validate_row(fd, 1, "%-80s", "foo37");

	send_txt(fd, "bar38", "printf foo; tput ht; printf 'bar%%s\\n' 38");
	rv |= validate_row(fd, 3, "%-80s", "foo     bar38");

	send_txt(fd, "foo39", "printf 'a\\tb\\tc\\t'; tput cbt; tput cbt; "
		"printf 'foo%%s\\n' 39");
	rv |= validate_row(fd, 5, "%-80s", "a       foo39   c");

	/* Cursor down 3 */
	send_txt(fd, "uniq7", "tput cud 3; echo 'u'n'i'q7");
	rv |= validate_row(fd, 10, "%-80s", "uniq7");
	send_txt(fd, NULL, "kill $SMTX");

	return rv;
}

int
test_dashc(int fd)
{
	int rv;
	ctlkey = CTL('l');
	send_str(fd, "uniq", "%cc\recho u'n'i'q'\r", CTL('l'));
	rv = check_layout(fd, 0x1, "*11x80; 11x80");
	send_txt(fd, NULL, "kill $SMTX");
	return rv;
}

int
test_dasht(int fd)
{
	/* This test exercises -t with a terminal type that should not
	 * exist in order to test the code path that uses initscr() */
	send_cmd(fd, "uniq", "c\recho u'n'i'q'");
	int rv = check_layout(fd, 0x1, "*11x80; 11x80");
	rv |= validate_row(fd, 2, "%-80s", "uniq");
	send_txt(fd, NULL, "kill $SMTX");
	return rv;
}

int
test_dch(int fd)
{
	int rv = 0;
	/* Print string, move back 3, delete 2, forward 3 */
	send_txt(fd, "uniq", "printf '%s%s%s%su'n'i'q'\\n'",
		"1234567",  /* Print a string */
		"\\033[3D", /* Cursor back 3 */
		"\\033[2P", /* Delete 2 */
		"\\033[1C"  /* Forward 1 */
	);
	rv |= validate_row(fd, 2, "%-80s", "12347uniq");
	send_txt(fd, NULL, "kill $SMTX");
	return rv;
}

int
test_decaln(int fd)
{
	char e[81] = "EEE";
	int rv = 0;
	memset(e, 'E', 80);
	send_txt(fd, "uniq", "printf '\\033[1048#u'; echo 'u'n'i'q;");
	rv |= validate_row(fd, 1, "%s", e);
	for( int i = 4; i < 24; i++ ) {
		rv |= validate_row(fd, i, "%s", e);
	}
	memcpy(e, "uniq", 4);
	rv |= validate_row(fd, 2, "%s", e);
	send_str(fd, NULL, "kill $SMTX\r");
	return rv;
}

int
test_decid(int fd)
{
	send_txt(fd, "^[[>1;10;0c", "%s", "printf '\\033[>c'");
	send_txt(fd, "^[[?1;2c", "%s", "\rprintf '\\033[c'");
	send_txt(fd, "^[[?6c", "%s", "\rprintf '\\033Z'");
	send_txt(fd, NULL, "\rkill $SMTX");
	return 0;
}

int
test_dsr(int fd)
{
	send_txt(fd, "^[[2;1R", "%s", "printf '\\033[6n'");
	send_txt(fd, "^[[0n", "%s", "\rprintf '\\033[n'");
	send_txt(fd, NULL, "\rkill $SMTX");
	return 0;
}

int
test_ech(int fd)
{
	int rv = 0;
	/* ech: erase N characters */
	send_txt(fd, "uniq1", "%s%s",
		"printf 012345; tput cub 3; tput ech 1;",
		"printf '\\nuniq%s\\n' 1"
	);
	rv |= validate_row(fd, 2, "%-80s", "012 45");
	send_str(fd, NULL, "exit\r");
	return rv;
}

int
test_ed(int fd)
{
	int rv = 0;
	send_txt(fd, "uniq", "yes | sed 15q; tput cuu 8; echo u'n'i'q'");
	rv |= validate_row(fd, 8, "%-80s", "y");
	rv |= validate_row(fd, 12, "%-80s", "y");
	send_txt(fd, "uniq2", "printf '\\033[J'u'n'i'q'2"); /* Clear to end */
	rv |= validate_row(fd, 8, "%-80s", "y");
	rv |= validate_row(fd, 12, "%-80s", "");
	send_txt(fd, "uniq3", "printf '\\033[2J'u'n'i'q'3"); /* Clear all */
	rv |= validate_row(fd, 8, "%-80s", "");
	rv |= validate_row(fd, 13, "%-80s", "");
	send_txt(fd, "uniq6", "clear; printf 'u'n'i'q'6'");
	send_txt(fd, "uniq4", "yes | sed 15q; tput cuu 8; echo u'n'i'q4'");
	for(int i = 2; i < 9; i++ ) {
		rv |= validate_row(fd, i, "%-80s", "y");
	}
	send_txt(fd, "uniq5", "printf '\\033[1J'u'n'i'q'5"); /* Clear to top */
	for(int i = 2; i < 9; i++ ) {
		rv |= validate_row(fd, i, "%-80s", "");
	}
	for(int i = 12; i < 15; i++ ) {
		rv |= validate_row(fd, i, "%-80s", "y");
	}
	send_txt(fd, "uniq7", "printf '\\033[3J\\033[1;1H'u'n'i'q'7");
	for(int i = 2; i < 15; i++ ) {
		rv |= validate_row(fd, i, "%-80s", "");
	}
	send_txt(fd, NULL, "exit");
	return rv;
}

int
test_el(int fd)
{
	int rv = 0;
	send_txt(fd, "uniq01", "%s", "printf 01234; tput cub 3; tput el; "
		"printf 'uniq%s\\n' 01");
	rv |= validate_row(fd, 2, "%-80s", "01uniq01");

	send_txt(fd, "uniq02", "%s", "printf 01234; tput cub 3; tput el1; "
		"printf '\\nuniq%s' 02");
	rv |= validate_row(fd, 4, "%-80s", "   34");

	/* Delete full line with csi 2K */
	send_txt(fd, "uniq03", "%s", "printf '01234\\033[2Ku'ni'q03\\n'");
	rv |= validate_row(fd, 6, "%-80s", "     uniq03");

	send_txt(fd, NULL, "exit");
	return rv;
}

int
test_equalize(int fd)
{
	send_cmd(fd, "uniq1", "%s", "cc5J\rprintf uniq%s 1");
	int status = check_layout(fd, 0x1, "*12x80; 4x80; 5x80");
	send_cmd(fd, "uniq2", "%s", "=\rprintf uniq%s 2");
	status |= check_layout(fd, 0x1, "*7x80; 7x80; 7x80");
	send_str(fd, NULL, "kill -TERM $SMTX\r");
	return status;
}

int
test_hpr(int fd)
{
	int rv = 0;
	const char *cmd = "printf 'abcd\\033[5aef'gh'ij\\n'";
	send_txt(fd, PROMPT, "%s", cmd);
	rv |= validate_row(fd, 2, "%-80s", "abcd     efghij");
	send_txt(fd, NULL, "exit");
	return rv;
}

int
test_ich(int fd)
{
	int rv = 0;
	/* ich: insert N characters, cub: move back N */
	const char *cmd = "printf abcdefg; tput cub 3; tput ich 5";
	send_txt(fd, "uniq1", "%s; %s", cmd, "printf '\\nuni%s\\n' q1");
	rv |= validate_row(fd, 2, "%-80s", "abcd     efg");

	cmd = "yes | nl | sed 6q; tput cuu 3; tput il 3; tput cud 6";
	send_txt(fd, "uniq2", "%s; %s", cmd, "printf uni'%s\\n' q2");
	for( int i=1; i < 4; i++ ) {
		rv |= validate_row(fd, 4 + i, "%6d  y%71s", i, "");
	}
	for( int i=4; i < 7; i++ ) {
		rv |= validate_row(fd, 4 + i, "%80s", "");
	}
	for( int i=7; i < 10; i++ ) {
		rv |= validate_row(fd, 4 + i, "%6d  y%71s", i - 3, "");
	}
	/* dl: delete n lines */
	cmd = "yes | nl | sed 6q; tput cuu 5; tput dl 4; tput cud 1";
	send_txt(fd, "uniq3", "%s; %s", cmd, "printf uni'%s\\n' q3");
	rv |= validate_row(fd, 16, "     %d  y%71s", 1, "");
	rv |= validate_row(fd, 17, "     %d  y%71s", 6, "");

	send_txt(fd, NULL, "exit");
	return rv;
}

int
test_insert(int fd)
{
	int rc = 0;
	/* smir -- begin insert mode;  rmir -- end insert mode */
	send_txt(fd, "sync01", "%s", "printf 0123456; tput cub 3; tput smir; "
		"echo foo; tput rmir; printf 'sync%s\\n' 01");
	rc |= validate_row(fd, 3, "%-80s", "0123foo456");
	rc |= validate_row(fd, 4, "%-80s", "sync01");
	send_txt(fd, NULL, "exit");
	return rc;
}

int
test_layout(int fd)
{
	int rv = check_layout(fd, 0x13, "%s", "*23x80@0,0");

	send_cmd(fd, "uniq01", "\rprintf 'uniq%%s' 01\r");
	rv |= check_layout(fd, 0x11, "*23x80@0,0");

	send_cmd(fd, "gnat", "c\rprintf 'gn%%s' at\r");
	rv |= check_layout(fd, 0x11, "*11x80@0,0; 11x80@12,0");

	send_cmd(fd, "foobar", "j\rprintf 'foo%%s' bar\r");
	rv |= check_layout(fd, 0x11, "11x80@0,0; *11x80@12,0");

	send_cmd(fd, "uniq02", "C\rprintf 'uniq%%s' 02\r");
	rv |= check_layout(fd, 0x11, "11x80@0,0; *11x40@12,0; 11x39@12,41");

	send_cmd(fd, "foobaz", "l\rprintf 'foo%%s' baz\r");
	rv |= check_layout(fd, 0x11, "11x80@0,0; 11x40@12,0; *11x39@12,41");

	send_str(fd, NULL, "kill $SMTX\r");
	return rv;
}

int
test_lnm(int fd)
{
	send_txt(fd, PROMPT, "printf '\\033[20h\\n' "); /* line 1 */
	int rv = validate_row(fd, 2, "%-80s", "");

	send_txt(fd, PROMPT, "printf 'foobaz\\rbar\\n'"); /* line 3 */
	/* Line 4 is blank because lnm is on and a newline was inserted */
	rv |= validate_row(fd, 4, "%-80s", "");
	rv |= validate_row(fd, 5, "%-80s", "barbaz");

	send_txt(fd, PROMPT, "printf '\\033[20lsy'n'c2\\n'"); /* line 6 */
	rv |= validate_row(fd, 7, "%-80s", "");  /* Inserted newline (1)*/
	rv |= validate_row(fd, 8, "%-80s", "sync2");

	send_txt(fd, PROMPT, "printf 'foo\\rch''eck3\\n'");
	rv |= validate_row(fd, 10, "%-80s", "check3");
	return rv;
}
/*
 * (1) This is a bit confusing.  The newlines printed by printf do *not*
 * get manipulated.  The \r inserted by send_txt does.  Since the \r is
 * written to terminate the printf command, it is replaced with \n\r before
 * printf is run to disable the insertions.  It is probably just confusing
 * to retain the \r in the printfs, since they are not really the point, but
 * we should verify that it is correct behavior to *not* expand them.
 */

int
test_navigate(int fd)
{
	send_cmd(fd, NULL, "cjkhl2Cjkhlc");
	send_txt(fd, "foobar", "%s", "printf 'foo%s\\n' bar");
	int status = check_layout(fd, 0x11, "%s; %s; %s; %s; %s",
		"11x26@0,0",
		"11x80@12,0",
		"*5x26@0,27",
		"5x26@6,27",
		"11x26@0,54"
	);
	send_cmd(fd, NULL, "8chhk");
	send_txt(fd, "foobaz", "%s", "printf 'foo%s\\n' baz");
	status |= check_layout(fd, 0x11, "%s; %s; %s",
		"*11x26@0,0; 11x80@12,0; 0x26@0,27",
		"0x26@1,27; 0x26@2,27; 0x26@3,27; 0x26@4,27; 0x26@5,27",
		"0x26@6,27; 0x26@7,27; 1x26@8,27; 1x26@10,27; 11x26@0,54"
	);
	send_cmd(fd, NULL, "%dq", SIGTERM + 128);  /* Terminate SMTX */
	return status;
}

int
test_nel(int fd)
{
	int rv = 0;
	/* nel is a newline */
	const char *cmd = "tput cud 3; printf foo; tput nel; "
		"printf 'uniq%s\\n' 01";
	send_txt(fd, "uniq01", "%s", cmd);
	rv |= validate_row(fd, 5, "%-80s", "foo");
	rv |= validate_row(fd, 6, "%-80s", "uniq01");
	cmd = "printf foobarz012; tput cub 7; echo blah; "
		"printf 'uniq%s\\n' 02";
	send_txt(fd, "uniq02", "%s", cmd);
	rv |= validate_row(fd, 8, "%-80s", "fooblah012");
	send_txt(fd, NULL, "exit");
	return rv;
}

int
test_pager(int fd)
{
	int rv = 0;

	send_txt(fd, "--More--", "%s; %s",
		"yes abcd | nl -s: | sed -e '23,44y/abcd/wxyz/' -e 500q | more",
		"PS1=u'ni'q\\>"
	);
	rv |= validate_row(fd, 02, "%-80s", "     2:abcd");
	rv |= validate_row(fd, 10, "%-80s", "    10:abcd");
	rv |= validate_row(fd, 22, "%-80s", "    22:abcd");
	rv |= validate_row(fd, 23, "%-80s", "--More--");
	rv |= check_layout(fd, 0x1, "*23x80");
	send_str(fd, "uniq>", "%s", " q");
	rv |= validate_row(fd, 1,  "%-80s", "    23:wxyz");
	rv |= validate_row(fd, 10, "%-80s", "    32:wxyz");
	rv |= validate_row(fd, 22, "%-80s", "    44:wxyz");
	rv |= validate_row(fd, 23, "%-80s", "uniq>");
	send_txt(fd, NULL, "exit");
	return rv;
}

int
test_pnm(int fd)
{
	send_txt(fd, "uniq", "printf '\\033>'u'n'i'q\\n'"); /* numkp */
	int rv = check_layout(fd, 0, "23x80#");
	send_txt(fd, "uniq2", "\rprintf '\\033='u'n'i'q2\\n'"); /* numkp */
	rv |= check_layout(fd, 0, "23x80");
	send_txt(fd, "uniq3", "\rprintf '\\033[1l'u'n'i'q3\\n'"); /* csi 1l */
	rv |= check_layout(fd, 0, "23x80#");
	send_txt(fd, "uniq4", "\rprintf '\\033[1h'u'n'i'q4\\n'"); /* csi 1h */
	rv |= check_layout(fd, 0, "23x80");
	send_txt(fd, NULL, "exit");
	return rv;
}

int
test_quit(int fd)
{
	send_cmd(fd, NULL, "%dq", SIGBUS); /* Invalid signal */
	send_cmd(fd, "exited", "c\rexit"); /* (2) */
	send_cmd(fd, NULL, "%dq", SIGTERM);  /* Invalid window */
	send_cmd(fd, NULL, "j");
	send_txt(fd, PROMPT, "trap \"printf 'uniq%%s' 01\" HUP");
	send_cmd(fd, "uniq01", "%dq\r", SIGHUP);  /* (1) */
	send_cmd(fd, NULL, "%dq", SIGTERM + 128);  /* Terminate SMTX */
	return 0;
}
/*
(1) The extra return seems necessary, as the shell on debian is not
    firing the trap until the newline is processed
(2) The string "exited" is expected to appear in the title line
*/

int
test_resend(int fd)
{
	send_txt(fd, "uniq", "%1$c%1$c\recho u'n'i'q'", ctlkey);
	int rv = validate_row(fd, 1, "%-80s", "ps1>^G");
	send_txt(fd, NULL, "exit");
	return rv;
}

int
test_reset(int fd)
{
	int k[] = { 1, 3, 4, 6, 7, 20, 25, 34, 1048, 1049, 47, 1047 };

	for( unsigned long i = 0; i < sizeof k / sizeof *k; i++ ) {
		int v = k[i];
		const char *fmt =  "printf '\\e[%d%c\r";
		send_str(fd, NULL, fmt, v, 'l');
		send_str(fd, NULL, fmt, v, 'h');
	}
	send_str(fd, NULL, "kill -TERM $SMTX\r");
	return 0;
}

int
test_resize(int fd)
{
	int status = 0;
	send_cmd(fd, "uniq1", "%s\r%s", "JccC", "printf 'uniq%s\\n' 1");
	status |= check_layout(fd, 0x1, "*7x40; 7x80; 7x80; 7x39");
	send_cmd(fd, "uniq2", "%s\r%s", "5J", "printf 'uniq%s\\n' 2");
	status |= check_layout(fd, 0x1, "*12x40; 4x80; 5x80; 12x39");
	send_cmd(fd, "uniq3", "%s\r%s", "jj10K", "printf 'uniq%s\\n' 3");
	status |= check_layout(fd, 0x1, "*12x40; 0x80; 10x80; 12x39");
	send_cmd(fd, "uniq4", "%s\r%s", "kkl20H", "printf 'uniq%s\\n' 4");
	status |= check_layout(fd, 0x1, "12x20; 0x80; 10x80; *12x59");
	send_str(fd, NULL, "kill -TERM $SMTX\r");
	return status;
}

/* test_resizepty() is called with -s 10 to trigger minimal history.
 * The main program sets the history to the screen size,
 * and this test resizes the screen to be larger to test
 * increase of history.
 */
int
test_resizepty(int fd)
{
	int rc = 0;
	struct winsize ws = { .ws_row = 67, .ws_col = 80 };
	char buf[1024];
	int history = 24;

	while( history != 67 ) {
		if( ioctl(fd, TIOCSWINSZ, &ws) ) {
			err(EXIT_FAILURE, "ioctl");
		}
		get_state(fd, buf, sizeof buf);
		if( sscanf(buf, "history=%d", &history) != 1
			|| ( history != 24 && history != 67 )
		) {
			rc = 1;
			fprintf(stderr, "unexpected response: %s\n", buf);
			break;
		}
	}
	send_cmd(fd, NULL, "143q"); /* Send SIGTERM */
	return rc;
}

int
test_ri(int fd)
{
	send_txt(fd, "sync", "printf '%s%s%s%s'",
		"012345678\\n", /* Print a string */
		"\\033M",        /* ri to move up one line */
		"abc\\n",       /* overwrite */
		"s'y'nc"
	);
	int rc = validate_row(fd, 2, "%-80s", "abc345678");
	return rc;
}

int
test_row(int fd)
{
	int status = 0;
	send_txt(fd, "uniq1", "%s; %s", "yes | nl -ba | sed 400q",
		"printf 'uniq%s\\n' 1");

	status |= validate_row(fd, 20, "%6d%-74s", 399, "  y");
	status |= validate_row(fd, 21, "%6d%-74s", 400, "  y");
	status |= validate_row(fd, 22, "%-80s", "uniq1");
	send_txt(fd, NULL, "kill $SMTX");
	return status;
}

int
test_scrollback(int fd)
{
	int status = 0;
	const char *string = "This is a relatively long string!";
	char trunc[128];

	send_cmd(fd, "uniq", "CC\recho u'n'iq");
	status |= check_layout(fd, 0x1, "*23x26; 23x26; 23x26");

	send_txt(fd, "uniq>", "a='%s'\rPS1='un'i'q>'", string);
	send_cmd(fd, NULL, "100<"); /* Cover invalid scroll left */
	send_txt(fd, "uniq>", "yes \"$a\" | nl |\rsed 50q");
	snprintf(trunc, 19, "%s", string);
	status |= validate_row(fd, 1, "%6d  %-18s", 29, trunc);
	status |= validate_row(fd, 22, "%6d  %-18s", 50, trunc);

	/* Scrollback 3, then move to another term and write a unique string */
	send_cmd(fd, "foobar", "3bl\rprintf 'foo%%s' bar");
	status |= validate_row(fd, 22, "%6d  %-18s", 47, trunc);

	/* Scrollright 8, then move to another term and write a unique string */
	snprintf(trunc, 27, "%s", string);
	send_cmd(fd, "foobaz", "h8>l\rprintf 'foo%%s' baz");
	status |= validate_row(fd, 14, "%-26s", trunc);

	/* Exit all pty instead of killing.  This was triggering a segfault
	 * on macos.  The test still times out whether we kill the SMTX
	 * or exit. */
	status |= check_layout(fd, 0x1, "23x26; *23x26; 23x26");
	send_txt(fd, NULL, "kill $SMTX");

	return status;
}

int
test_scrollh(int fd)
{
	char buf[79];
	for(unsigned i = 0; i < sizeof buf - 1; i++) {
		buf[i] = 'a' + i % 26;
	}
	buf[sizeof buf - 1] = 0;

	send_txt(fd, "uniq", "tput cols; echo %s'u'n'i'q", buf);
	int rv = validate_row(fd, 3, "%-26s", "78");

	buf[26] = 0;
	rv |= validate_row(fd, 4, "%-26s", buf);
	buf[26] = 'a';

	/* Scroll one tabstop to the right */
	buf[26 + 8 ] = 0;  /* 8 is default tabstop */
	send_cmd(fd, "foo", ">\recho '        f'o'o'");
	rv |= validate_row(fd, 4, "%-26s", buf + 8);
	buf[26 + 8 ] = 'a' + 8;

	/* Scroll one screen width (26) to the right */
	send_cmd(fd, "uniq1", "%s", "0>\rprintf '%34s'u'n'i'q'1\\n");
	buf[26 + 34] = 0;
	rv |= validate_row(fd, 4, "%-26s", buf + 34);
	buf[26 + 34] = 'a' + 8;

	/* Scroll 2 to the left */
	send_cmd(fd, "uniq2", "%s", "2<\rprintf '%32s'u'n'i'q'2\\n");
	buf[26 + 32] = 0;
	rv |= validate_row(fd, 4, "%-26s", buf + 32);
	buf[26 + 32] = 'a' + 6;

	/* Scroll 200 to the right (should stop at 52) */
	send_cmd(fd, "uniq3", "%s", "200>\rprintf '%52s'u'n'i'q'3\\n");
	rv |= validate_row(fd, 4, "%-26s", buf + 52);

	send_txt(fd, NULL, "exit");
	return rv;
}

int
test_swap(int fd)
{
	int rv = 0;
	int id[3];
	char desc[1024];

	send_cmd(fd, NULL, "cCjC");
	send_txt(fd, "uniq02", "printf 'uniq%%s\\n' 02");
	/* Write string2 into lower left canvas */
	send_txt(fd, "string2", "printf 'str%%s\\n' ing2");

	get_layout(fd, 5, desc, sizeof desc);
	if( sscanf(desc, "11x40(id=%*d); *11x40(id=%d); "
			"11x39(id=%*d); 11x39(id=%d)", id, id + 1) != 2 ) {
		fprintf(stderr, "received unexpected: '%s'\n", desc);
		rv = 1;
	}
	rv |= validate_row(fd, 4, "%-40s", "");
	send_cmd(fd, NULL, "k"); /* Move to upper left */
	send_txt(fd, "uniq01", "printf 'uniq%%s\\n' 01");
	send_txt(fd, NULL, "printf 'string%%s\\n' 1");

	send_cmd(fd, NULL, "1024s");   /* Invalid swap */
	send_cmd(fd, NULL, "%ds", id[0]); /* Swap upper left and lower left */
	send_txt(fd, "uniq03", "printf 'uniq%%s\\n' 03");
	rv |= validate_row(fd, 4, "%-40s", "string2");
	rv |= validate_row(fd, 6, "%-40s", "uniq03");

	/* Swap back */
	send_cmd(fd, NULL, "s");
	send_txt(fd, "uniq04", "printf 'uniq%%s\\n' 04");
	rv |= validate_row(fd, 4, "%-40s", "string1");
	rv |= validate_row(fd, 6, "%-40s", "uniq04");

	send_cmd(fd, NULL, "jl");
	send_txt(fd, "uniq05", "printf '\\nuniq%%s\\n' 05");
	send_cmd(fd, NULL, "kh");
	send_txt(fd, "uniq06", "printf 'uniq%%s\\n' 06");
	rv |= validate_row(fd, 4, "%-40s", "string1");
	send_cmd(fd, NULL, "%ds", id[1]); /* Swap upper left and lower rt */
	send_txt(fd, "uniq07", "printf 'uniq%%s\\n' 07");

	get_layout(fd, 5, desc, sizeof desc);
	if( sscanf(desc, "*11x40(id=%d); 11x40(id=%*d); "
			"11x39(id=%*d); 11x39(id=%*d)", id + 2) != 1 ) {
		fprintf(stderr, "received unexpected: '%s'\n", desc);
		rv = 1;
	}
	if( id[2] != id[1] ) {
		fprintf(stderr, "unexpected id in first window: %s\n", desc);
		rv = 1;
	}
	send_txt(fd, NULL, "kill -TERM $SMTX");
	return rv;
}

int
test_tabstop(int fd)
{
	int rv = 0;
	int d = 0;
	const char *cmd = "printf 'this\\tis\\ta\\ttest%%d\\n' %d";
	send_txt(fd, "test0", cmd, d);
	rv |= validate_row(fd, 2, "%-80s", "this    is      a       test0");

	send_cmd(fd, NULL, "3t");
	send_txt(fd, "test1", cmd, ++d);
	rv |= validate_row(fd, 4, "%-80s", "this  is a  test1");

	send_cmd(fd, NULL, "t");
	rv |= validate_row(fd, 6, "%-80s", "");
	send_txt(fd, "test2", cmd, ++d);
	rv |= validate_row(fd, 6, "%-80s", "this    is      a       test2");

	send_txt(fd, "uniq:", "%s; %s", "tabs -5", "PS1=un'iq:'");
	send_txt(fd, "test3", cmd, ++d);
	rv |= validate_row(fd, 9, "%-80s", "this is   a    test3");

	send_txt(fd, NULL, "kill -TERM $SMTX");
	return rv;
}

int
test_title(int fd)
{
	int rv = 0;
	char buf[76];

	/* The tail of the title should be ACS_HLINE.
	 * When the locale is wrong, ACS_HLINE == 'q', but we set the
	 * locale in smtx-main, so it seems we should set buf to have
	 * ACS_HLINE & A_CHARTEXT, but that is a space.
	 * Not entirely sure why the title comes back with 'q', but the
	 * purpose of this test is to check that the string "foobar" is
	 * set in the beginning of the title, so I'm going to punt on the
	 * 'q' for now and just hack this.
	 */
	memset(buf, 'q', 76);
	buf[75] = '\0';
	rv |= validate_row(fd, 24, "1 sh %s", buf);
	send_txt(fd, "uniq", "printf '\\033]2foobar\\007'; echo u'n'iq");
	buf[71] = '\0';
	rv |= validate_row(fd, 24, "1 foobar %s", buf);
	send_cmd(fd, "unIq", "200W\rprintf '\\033]2qux\\007u'n'I'q");

	buf[65] = '\0';
	rv |= validate_row(fd, 24, "1 qux 1-80/200 %s", buf);

	send_txt(fd, NULL, "exit");
	return rv;
}

int
test_tput(int fd)
{
	int rv = 0;
	/* vpa: move cursor to row (0 based), hpa: move cursor to column */
	send_txt(fd, "xyz", "%s", "tput vpa 7; tput hpa 18; echo x'y'z");
	rv |= validate_row(fd, 8, "%18sxyz%59s", "", "");

	/* ed: clear to end of screen */
	send_txt(fd, "uniq", "%s; %s; %s",
		"yes abcdefghijklmnopqrstuvzxyz | sed 25q", /* Fill screen */
		"tput cup 5 10; tput ed", /* Move and delete to end of screen */
		"printf 'uniq\\n'"
	);
	rv |= validate_row(fd, 6, "%-80s", "abcdefghijuniq");
	for( int i = 8; i < 23; i++ ) {
		rv |= validate_row(fd, i, "%80s", "");
	}
	/* bel: alert user*/
	send_txt(fd, NULL, "tput bel; kill $SMTX");
	return rv;
}

int
test_vis(int fd)
{
	int rv = 0;
	/* tput civis to hide cursor */
	send_txt(fd, "uniq1", "%s; %s", "tput civis", "printf 'uniq%s\\n' 1");
	rv |= check_layout(fd, 0, "23x80!");

	/* tput cvvis to show cursor */
	send_txt(fd, "uniq2", "%s; %s", "tput cvvis", "printf 'uniq%s\\n' 2");
	rv |= check_layout(fd, 0, "23x80");

	/* CSI 25l to hide cursor */
	send_txt(fd, "uniq3", "%s", "printf '\\033[?25l u'n'iq3'");
	rv |= check_layout(fd, 0, "23x80!");

	/* CSI 25h to show cursor */
	send_txt(fd, "uniq4", "%s", "printf '\\033[?25h u'n'iq4'");
	rv |= check_layout(fd, 0, "23x80");

	/* CSI 25l to hide cursor */
	send_txt(fd, "uniq5", "%s", "printf '\\033[?25l u'n'iq5'");
	rv |= check_layout(fd, 0, "23x80!");

	/* esc p to show cursor */
	send_txt(fd, "uniq6", "%s", "printf '\\033p u'n'iq6'");
	rv |= check_layout(fd, 0, "23x80");

	send_txt(fd, NULL, "exit");
	return rv;
}

int
test_width(int fd)
{
	int rv = 0;
	char buf[161];
	send_cmd(fd, "uniq01", "cCCCj\rprintf 'uniq%%s' 01");
	rv |= check_layout(fd, 0x11, "%s; %s; %s; %s; %s",
		"11x20@0,0",
		"*11x80@12,0",
		"11x19@0,21",
		"11x19@0,41",
		"11x19@0,61"
	);
	/* Move up to a window that is now only 20 columns wide and
	print a string of 50 chars */
	send_cmd(fd, NULL, "k");
	send_str(fd, NULL, "for i in 1 2 3 4 5; do ");
	send_str(fd, NULL, "printf '%%s' ${i}123456789;");
	send_str(fd, NULL, "test $i = 5 && printf '\\n  uniq%%s\\n' 02;");
	send_str(fd, "uniq02", "done\r");
	rv |= validate_row(fd, 3, "%-20s", "11234567892123456789");

	/* Shift right 15 chars */
	send_cmd(fd, "dedef", "15>\rprintf '%%20sded%%s' '' ef");
	rv |= validate_row(fd, 3, "%-20s", "56789312345678941234");

	for( unsigned i = 0; i < sizeof buf - 1; i++ ) {
		buf[i] = 'a' + i % 26;
	}
	buf[sizeof buf - 1] = '\0';
	/* Clear screen, print 2 rows (160 chars) of alphabet */
	send_txt(fd, NULL, "clear; printf '%s\\n'", buf);
	/* Shift right 75 chars ( now looking at last 20 chars of pty)*/
	send_cmd(fd, NULL, "75>");
	/* Print 60 blanks then a string to sync*/
	send_txt(fd, "de3dbeef", "printf '%%60sde3d%%s' '' beef");
	/* Verify that the 160 chars of repeated alphabet is at end of rows */
	rv |= validate_row(fd, 1, "%-20s", "ijklmnopqrstuvwxyzab");
	rv |= validate_row(fd, 2, "%-20s", "klmnopqrstuvwxyzabcd");

	/* Change width of underlying pty to 180 */
	send_cmd(fd, NULL, "180W\rclear; printf '%s\\n'", buf);
	send_txt(fd, "de4dbeef", "printf '%%68sde4d%%s' '' beef");
	rv |= validate_row(fd, 1, "%-20s", "ijklmnopqrstuvwxyzab");
	send_cmd(fd, NULL, "1>");
	send_txt(fd, "de5dbeef", "printf '%%68sde5d%%s' '' beef");
	rv |= validate_row(fd, 1, "%-20s", "jklmnopqrstuvwxyzabc");

	/* Change width of underlying pty to match canvas and scroll to start */
	send_cmd(fd, NULL, "W180<");
	send_txt(fd, NULL, "clear; printf '%s\\n'", buf);
	send_txt(fd, "de6dbeef", "printf '%%sde6d%%s' '' beef");
	rv |= validate_row(fd, 1, "%-20s", "abcdefghijklmnopqrst");
	rv |= validate_row(fd, 2, "%-20s", "uvwxyzabcdefghijklmn");

	send_str(fd, NULL, "kill $SMTX\r");
	return rv;
}
