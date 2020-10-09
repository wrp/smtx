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
#include "test-unit.h"

int
test_alt(int fd)
{
	/* Emit csi sequence to use alternate screen */

	int rv = 0;

	/* Write some lines of text on primary screen */
	send_txt(fd, "primary", "echo 'p'rimar'y'");
	rv |= validate_row(fd, 2, "%-80s", "primary");
	send_txt(fd, "un1>", "PS1='u'n'1>'; echo primary 4");
	rv |= validate_row(fd, 4, "%-80s", "primary 4");

	/* Go to alternate screen */
	send_txt(fd, NULL, "printf '\\033[47h'");
	send_txt(fd, "secondary", "echo 's'econdar'y'");
	rv |= validate_row(fd, 2, "%-80s", "secondary");
	rv |= validate_row(fd, 4, "%-80s", "");

	/* Return to primary screen */
	send_txt(fd, NULL, "printf '\\033[47l'");
	send_txt(fd, "ab1>", "PS1='a'b1'>'");
	rv |= validate_row(fd, 4, "%-80s", "primary 4");
	rv |= validate_row(fd, 7, "%-80s", "ab1>");

	/* Go back to alternate screen using 1047 */
	send_txt(fd, "zephyr", "printf '\\033[1047h'\recho z'e'ph'y'r");
	rv |= validate_row(fd, 2, "%-80s", "zephyr");
	rv |= validate_row(fd, 4, "%-80s", "");
	return rv;
}

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

	/* Write "other" in lower left canvas */
	send_cmd(fd, "other", "j\rprintf '\\no't'h'er'\\n'");

	get_layout(fd, 5, desc, sizeof desc);
	if( sscanf(desc, "7x80(id=%*d); *7x80(id=%d);", &id) != 1 ) {
		fprintf(stderr, "received unexpected: '%s'\n", desc);
		rv = 1;
	} else {
		/* Move to upper left and attach to the pane with "other" */
		send_cmd(fd, "uniq", "k%da\recho 'u'ni'q'", id);
	}
	rv |= check_layout(fd, 0x1, "*7x80; 7x80; 7x80");

	/* 2nd row of the first window should now be "other" */
	rv |= validate_row(fd, 3, "%-80s", "other");
	return rv;
}

int
test_bighist(int fd)
{
	/* Use -s INT_MAX to trigger a memory allocation error. */
	(void)fd;
	return 0;
}

int
test_changehist(int fd)
{
	int rv = 0;
	/* Test begins with -s 128 */
	send_cmd(fd, NULL, "120Z"); /* Invalid (too small) */
	send_txt(fd, "un1>", "PS1=un'1>'; yes | nl -s '' | sed 127q");
	for( int i = -104; i < 23; i++ ) {
		rv |= validate_row(fd, i, "%6dy%73s", i + 105, "");
	}
	rv |= validate_row(fd, 23, "%-80s", "un1>");

	send_cmd(fd, NULL, "200Z"); /* Increase history to 200 */
	const char *cmd = "PS1=un'2>'; yes | nl -s '' | sed 127q";
	send_txt(fd, "un2>", "%s", cmd);
	rv |= validate_row(fd, -176, "    57%-74s", "y");
	rv |= validate_row(fd, -106, "   127%-74s", "y");
	rv |= validate_row(fd, -105, "un1>%-76s", cmd);
	rv |= validate_row(fd, -104, "     1%-74s", "y");
	rv |= validate_row(fd, 23, "%-80s", "un2>");

	return rv;
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
	send_cmd(fd, NULL, ":bad_key");
	send_cmd(fd, "unknown function: xaqx", ":xaqx   with arg");
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
	return rv;
}

int
test_cup(int fd)
{
	int rv = 0;
	/* cup: move to N, M;  cub: back N;  cuf: forward N */
	send_txt(fd, "un2>", "PS1=un'2>'; tput cup 5 50; printf 'row6\\n'");
	char *cmd = "printf '0123456'; tput cub 4; printf '789\\n\\n'";
	send_txt(fd, "un3>", "PS1='u'n'3>'; %s", cmd);
	cmd = "printf abc; tput cuf 73; printf '12345678%s\\n' wrapped";
	send_txt(fd, "5678wrapped", "%s", cmd);

	rv |= validate_row(fd, 6, "%50s%-30s", "", "row6");
	rv |= validate_row(fd, 8, "%-80s", "0127896");
	rv |= validate_row(fd, 11, "abc%73s1234", "");
	rv |= validate_row(fd, 12, "%-80s", "5678wrapped");
	return rv;
}

int
test_cursor(int fd)
{
	int d = 1;
	const char *cmd = "PS1=un'%d>'; %s";
	send_txt(fd, "un1>", cmd, d++, "printf '012345'; tput cub 4; "
		"printf 'xyz\\n'");
	int rv = validate_row(fd, 2, "%-80s", "01xyz5");

	/* sc == save cursor position;  rc == restore cursor position */
	send_txt(fd, "un2>", cmd, d++,
		"tput rc; tput sc; echo abcd; tput rc; echo xyz");
	rv |= validate_row(fd, 4, "%-80s", "xyzd");

	send_txt(fd, "un3>", cmd, d++, "tput cup 15 50; printf 'foobaz\\n'");
	rv |= validate_row(fd, 16, "%-50sfoobaz%24s", "", "");

	send_txt(fd, "un4>", cmd, d++, "tput clear; echo foo37");
	rv |= validate_row(fd, 1, "%-80s", "foo37");

	/* ht == next hard tab */
	send_txt(fd, "un5>", cmd, d++, "printf foo; tput ht; echo bar38");
	rv |= validate_row(fd, 3, "%-80s", "foo     bar38");

	/* cbt == backtab */
	send_txt(fd, "un6>", cmd, d++, "printf 'a\\tb\\tc\\t'; tput cbt; "
		"tput cbt; echo foo39");
	rv |= validate_row(fd, 5, "%-80s", "a       foo39   c");

	/* Cursor down 3 */
	send_txt(fd, "un7>", cmd, d++, "tput cud 3; echo uniq7");
	rv |= validate_row(fd, 7, "%-80s", "");
	rv |= validate_row(fd, 8, "%-80s", "");
	rv |= validate_row(fd, 9, "%-80s", "");
	rv |= validate_row(fd, 10, "%-80s", "uniq7");
	send_txt(fd, NULL, "kill $SMTX");

	return rv;
}

int
test_dashc(int fd)
{
	int rv;
	ctlkey = CTL('l');
	send_cmd(fd, "un>", "c\rPS1='un>'");
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
	return rv;
}

int
test_decid(int fd)
{
	/* Abort test if file named 1 exists */
	send_txt(fd, NULL, "test -f 1 && exit 1");
	send_txt(fd, "^[[>1;10;0c", "%s", "printf '\\033[>c'");
	send_txt(fd, "^[[?1;2c", "%s", "\rprintf '\\033[c'");
	send_txt(fd, "^[[?6c", "%s", "\rprintf '\\033Z'");
	/* WTF? The first test creates a file named 1.  This is craptastic */
	send_txt(fd, NULL, "\rtest -f 1 && ! test -s 1 && rm 1");
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
	return rv;
}

int
test_ed(int fd)
{
	int rv = 0;
	/* cuu: cursor up N lines */
	send_txt(fd, "un1>", "PS1='u'n'1>'; yes | sed 15q; tput cuu 8");
	rv |= validate_row(fd, 8, "%-80s", "y");
	rv |= validate_row(fd, 9, "%-80s", "un1>");
	rv |= validate_row(fd, 10, "%-80s", "y");

	/* \\033[J : clear to end of line */
	send_txt(fd, "xy2>", "PS1='x'y'2>'; printf '\\033[J\\n'");
	rv |= validate_row(fd, 8, "%-80s", "y");
	rv |= validate_row(fd, 10, "%-80s", "");
	rv |= validate_row(fd, 11, "%-80s", "xy2>");

	/* \\033[2J : clear screen */
	send_txt(fd, "un3>", "PS1='u'n'3>'; printf '\\033[2J'");
	rv |= validate_row(fd, 8, "%-80s", "");
	rv |= validate_row(fd, 12, "%-80s", "un3>");
	rv |= validate_row(fd, 13, "%-80s", "");
	send_txt(fd, "un4>", "PS1='u'n'4>'; clear");
	rv |= validate_row(fd, 1, "%-80s", "un4>");
	rv |= validate_row(fd, 12, "%-80s", "");
	send_txt(fd, "un5>", "PS1='u'n'5>'; yes | sed 15q; tput cuu 8");
	for(int i = 2; i < 9; i++ ) {
		rv |= validate_row(fd, i, "%-80s", "y");
	}
	/* \\03[1J : Clear to top of screen */
	send_txt(fd, "un6>", "PS1='u'n'6>'; printf '\\033[1J'");
	for(int i = 2; i < 9; i++ ) {
		rv |= validate_row(fd, i, "%-80s", "");
	}
	for(int i = 12; i < 15; i++ ) {
		rv |= validate_row(fd, i, "%-80s", "y");
	}
	send_txt(fd, "un7>", "PS1='u'n'7>'; printf '\\033[3J\\033[1;1H'");
	for(int i = 2; i < 15; i++ ) {
		rv |= validate_row(fd, i, "%-80s", "");
	}
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
	send_cmd(fd, "un1>", "%s", "cc5J\rPS1=un'1>'");
	int status = check_layout(fd, 0x1, "*12x80; 4x80; 5x80");
	send_cmd(fd, "un2>", "%s", "=\rPS1=un'2>'");
	status |= check_layout(fd, 0x1, "*7x80; 7x80; 7x80");
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
	/* ich: insert N characters, cub: move back N, nel: insert newline */
	const char *cmd = "printf abcdefg; tput cub 3; tput ich 5; tput nel";
	send_txt(fd, PROMPT, "%s", cmd);
	rv |= validate_row(fd, 2, "%-80s", "abcd     efg");

	cmd = "yes | nl | sed 6q; tput cuu 3; tput il 3; tput cud 6";
	send_txt(fd, "un1>", "PS1=u'n'1'> '; %s", cmd);
	for( int i=1; i < 4; i++ ) {
		rv |= validate_row(fd, 3 + i, "%6d  y%71s", i, "");
	}
	for( int i=4; i < 7; i++ ) {
		rv |= validate_row(fd, 3 + i, "%80s", "");
	}
	for( int i=7; i < 10; i++ ) {
		rv |= validate_row(fd, 3 + i, "%6d  y%71s", i - 3, "");
	}
	/* dl: delete n lines */
	cmd = "yes | nl | sed 6q; tput cuu 5; tput dl 4; tput cud 1; tput nel";
	send_txt(fd, "un2>", "PS1=un'2> '; %s", cmd);
	rv |= validate_row(fd, 14, "     %d  y%71s", 1, "");
	rv |= validate_row(fd, 15, "     %d  y%71s", 6, "");
	rv |= validate_row(fd, 16, "%-80s", "");
	rv |= validate_row(fd, 17, "%-80s", "un2>");

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

	return rv;
}

int
test_lnm(int fd)
{
	int rv = validate_row(fd, 1, "%-80s", PROMPT);
	send_txt(fd, "a1>", "PS1=a1\\>; printf '\\033[20h\\n' ");
	rv = validate_row(fd, 2, "%-80s", "");

	send_txt(fd, "b2>", "PS1=b'2>'; printf 'foobaz\\rbar\\n'");
	/* Line 4 is blank because lnm is on and a newline was inserted */
	rv |= validate_row(fd, 4, "%-80s", "");
	rv |= validate_row(fd, 5, "%-80s", "barbaz");

	send_txt(fd, "c3>", "PS1=c3\\>; printf '\\033[20l'");
	rv |= validate_row(fd, 7, "%-80s", "");  /* Inserted newline (1)*/
	send_txt(fd, "c4>", "PS1=c4\\>; echo syn");
	rv |= validate_row(fd, 9, "%-80s", "syn");

	send_txt(fd, "u4>", "PS1=u4\\>; printf 'foo\\rabcdef\\n'");
	rv |= validate_row(fd, 11, "%-80s", "abcdef");
	rv |= validate_row(fd, 12, "%-80s", "u4>");
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
		"yes abcd | nl -s: | sed -e '23,43y/abcd/wxyz/' "
		"-e '44y/abcd/klmn/' -e 500q | more",
		"PS1=u'ni'q\\>"
	);
	for( int i = 1; i < 23; i++ ) {
		rv |= validate_row(fd, i, "    %2d:%-73s", i, "abcd");
	}
	rv |= validate_row(fd, 23, "%-91s", "<rev>--More--</rev>");
	rv |= check_layout(fd, 0x1, "*23x80");
	send_raw(fd, "44:klmn", " ");
	send_raw(fd, "uniq>", "q");
	for( int i = 1; i < 22; i++ ) {
		rv |= validate_row(fd, i, "    %2d:%-73s", i + 22, "wxyz");
	}
	rv |= validate_row(fd, 22, "%-80s", "    44:klmn");
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
	send_cmd(fd, "exited", "c\rexit"); /* (1) */
	send_cmd(fd, NULL, "%dq", SIGTERM);  /* Invalid window */
	send_cmd(fd, NULL, "%dq", SIGTERM + 128);  /* Terminate SMTX */
	return 0;
}
/*
(1) The string "exited" is expected to appear in the title line.
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
		const char *fmt =  "printf '\\033[%d%c";
		send_txt(fd, NULL, fmt, v, 'l');
		send_txt(fd, NULL, fmt, v, 'h');
	}
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

static int
sgr_background(int fd)
{
	int rv = 0;
	char *fmt = "printf '%s\\033[%sm%s\\033[m%s\\n'; ";
	send_raw(fd, NULL, fmt, "c", "32;42", "d", "e");
	send_txt(fd, "xy1>", "PS1='xy''1>'");
	rv |= validate_row(fd, 2, "%-107s", "c<green/green>d</green/green>e");

	send_raw(fd, NULL, fmt, "x", "31;42;7", "y", "z");
	send_txt(fd, "xy2>", "PS1='xy''2>'");
	rv |= validate_row(fd, 4, "%-114s",
		"x<rev><red/green>y</rev></red/green>z");
	return rv;
}

int
test_sgr(int fd)
{
	int rv = 0;
	int d = 0;

	char fmt[1024] = "PS1='un''%d>'; clear; ";
	char *cmd = fmt + strlen(fmt);
	sprintf(cmd, "%s", "printf 'foo\\033[%smbar\\033[%sm\\n'");
	struct { char *sgr; char *name; } *attrp, attrs[] = {
		{ "1", "bold" },
		{ "2", "dim" },
		{ "4", "ul" },  /* underline */
		{ "5", "blink" },
		{ "7", "rev" },
		{ "8", "inv" },
		{ NULL, NULL }
	};

	for( attrp = attrs; attrp->sgr; attrp++ ) {
		char ps[32];
		char lenfmt[32];
		char expect[128];
		size_t len = strlen(attrp->name);
		sprintf(ps, "un%d>", ++d);
		sprintf(lenfmt, "%%-%zds", 80 + 5 + len * 2);
		sprintf(expect, "foo<%1$s>bar</%1$s>", attrp->name);
		send_txt(fd, ps, fmt, d, attrp->sgr, "0");
		rv |= validate_row(fd, 1, lenfmt, expect);
	}

	/* test that 22 disables bold */
	send_txt(fd, "un7>", fmt, ++d, "1", "22");
	rv |= validate_row(fd, 1, "%-93s", "foo<bold>bar</bold>");

	/* test that 24 disables underline */
	send_txt(fd, "un8>", fmt, ++d, "4", "24");
	rv |= validate_row(fd, 1, "%-89s", "foo<ul>bar</ul>");

	/* test that 25 disables blink */
	send_txt(fd, "un9>", fmt, ++d, "5", "25");
	rv |= validate_row(fd, 1, "%-95s", "foo<blink>bar</blink>");

	/* test that 27 disables reverse */
	send_txt(fd, "un10>", fmt, ++d, "7", "27");
	rv |= validate_row(fd, 1, "%-91s", "foo<rev>bar</rev>");

	/* test colors */
	send_txt(fd, "un9>", fmt, d = 9, "31", "");
	rv |= validate_row(fd, 1, "%-91s", "foo<red>bar</red>");

	send_txt(fd, "un10>", fmt, ++d, "30", "");
	rv |= validate_row(fd, 1, "%-95s", "foo<black>bar</black>");

	send_txt(fd, "un11>", fmt, ++d, "32", "");
	rv |= validate_row(fd, 1, "%-95s", "foo<green>bar</green>");

	send_txt(fd, "un12>", fmt, ++d, "33", "");
	rv |= validate_row(fd, 1, "%-97s", "foo<yellow>bar</yellow>");

	send_txt(fd, "un13>", fmt, ++d, "37", "");
	rv |= validate_row(fd, 1, "%-95s", "foo<white>bar</white>");

	for( int i = 34; i < 38; i++ ) {
		cmd = "PS1='a'x'%d>'; printf 'ax%d\\033[%dmend\\033[mt\\n'";
		char *colors[] = { "blue", "magenta", "cyan", "white" };
		char buf[128];
		char ps[16];
		char width[10];
		sprintf(width, "%%-%zds", 80 + 2 * strlen(colors[i-34]) + 5);
		sprintf(ps, "ax%d>", i);
		sprintf(buf, "ax%1$d<%2$s>end</%2$s>t", i, colors[ i - 34]);
		send_txt(fd, ps, cmd, i, i, i);
		rv |= validate_row(fd, 3 + 2 * (i - 34), width, buf);
	}
	rv |= validate_row(fd, 3, "%-93s", "ax34<blue>end</blue>t");
	rv |= validate_row(fd, 5, "%-99s", "ax35<magenta>end</magenta>t");
	rv |= validate_row(fd, 7, "%-93s", "ax36<cyan>end</cyan>t");
	rv |= validate_row(fd, 9, "%-95s", "ax37<white>end</white>t");
	send_txt(fd, "aw1>", "PS1='a'w1'>'; clear");
	rv |= sgr_background(fd);

	return rv;
}

int
test_su(int fd)
{
	int rv = 0;
	send_txt(fd, "un1>", "PS1=un'1>'; yes | nl -s '' | sed 50q");
	rv |= validate_row(fd, 22, "    50y%73s", "");
	rv |= validate_row(fd, 23, "%-80s", "un1>");

	/* Use csi escape sequence S to scroll up 10 lines */
	char *cmd = "PS1=ab'2>'; printf '\\033[10S'";
	send_txt(fd, "ab2>", cmd);
	rv |= validate_row(fd, 11, "    50y%73s", "");
	rv |= validate_row(fd, 12, "un1>%-76s", cmd);
	for( int i = 13; i < 23; i++ ) {
		rv |= validate_row(fd, i, "%-80s", "");
	}
	rv |= validate_row(fd, 23, "%-80s", "ab2>");

	/* Use csi escape sequence T to scroll down 15 lines */
	cmd = "PS1=ws'3>'; printf '\\033[15T'";
	send_txt(fd, "ws3>", cmd);
	rv |= validate_row(fd, 10, "    35y%73s", "");

	/* Use csi escape sequence ^ to scroll down 4 lines */
	cmd = "PS1=xy'4>'; printf '\\033[4^'";
	send_txt(fd, "xy4>", cmd);
	rv |= validate_row(fd, 10, "    32y%73s", "");
	return rv;
}

int
test_swap(int fd)
{
	int rv = 0;
	int id[3];
	char desc[1024];

	/* Create several canvasses and move to lower left */
	send_cmd(fd, NULL, "cCjC");

	/* Write lowerleft into lower left canvas */
	send_txt(fd, "ll0>", "PS1='l'l'0>'; echo; echo lowerleft");

	get_layout(fd, 5, desc, sizeof desc);
	if( sscanf(desc, "11x40(id=%*d); *11x40(id=%d); "
			"11x39(id=%*d); 11x39(id=%d)", id, id + 1) != 2 ) {
		fprintf(stderr, "received unexpected: '%s'\n", desc);
		rv = 1;
	}
	rv |= validate_row(fd, 4, "%-40s", "");
	send_cmd(fd, NULL, "k"); /* Move to upper left */
	send_txt(fd, "ul0>", "PS1='u'l'0>'; echo; echo upperleft");
	rv |= validate_row(fd, 3, "%-40s", "upperleft");

	send_cmd(fd, NULL, "1024s");   /* Invalid swap */
	send_cmd(fd, NULL, "%ds", id[0]); /* Swap upper left and lower left */
	send_txt(fd, "ll1>", "PS1='l'l'1>'; echo s2 line 5");
	rv |= validate_row(fd, 3, "%-40s", "lowerleft");

	/* Swap back (with no count, s swaps with parent or primary child) */
	send_cmd(fd, NULL, "s");
	send_txt(fd, "ul1>", "PS1='u'l'1>'");
	rv |= validate_row(fd, 3, "%-40s", "upperleft");

	send_cmd(fd, NULL, "jl"); /* Move to lower right */
	send_txt(fd, "lr1>", "PS1='l'r'1>'; echo; echo lowerright");
	send_cmd(fd, NULL, "kh"); /* Move to upper left */
	send_txt(fd, "ul2>", "PS1='u'l'2>'");
	rv |= validate_row(fd, 3, "%-40s", "upperleft");
	send_cmd(fd, NULL, "%ds", id[1]); /* Swap upper left and lower rt */
	send_txt(fd, "ul3>", "PS1='u'l'3>'");
	/*
	TODO: why does this not work?  The attach seems to clear screen
	rv |= validate_row(fd, 3, "%-40s", "lowerright");
	*/

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
	const char *cmd = "PS1=un'%d>'; printf 'this\\tis\\ta\\ttest\\n'";
	send_txt(fd, "un0>", cmd, d++);
	rv |= validate_row(fd, 2, "%-80s", "this    is      a       test");

	send_cmd(fd, NULL, "3t");
	send_txt(fd, "un1>", cmd, d++);
	rv |= validate_row(fd, 4, "%-80s", "this  is a  test");

	send_cmd(fd, NULL, "t");
	rv |= validate_row(fd, 6, "%-80s", "");
	send_txt(fd, "un2>", cmd, d++);
	rv |= validate_row(fd, 6, "%-80s", "this    is      a       test");

	send_txt(fd, "un3>", "%s; %s", "tabs -5", "PS1=un'3>'");
	send_txt(fd, "un4>", cmd, ++d);
	rv |= validate_row(fd, 9, "%-80s", "this is   a    test");

	/* Clear tab in position 10 */
	send_txt(fd, "un5>", "PS1=un'5>'; tput hpa 10; printf '\\033[0g'");
	send_txt(fd, "un6>", cmd, d += 2);
	rv |= validate_row(fd, 12, "%-80s", "this is        a    test");

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
	send_txt(fd, "un1>", "PS1=un'1> '; tput bel");
	rv |= validate_row(fd, 8, "%-80s", "un1>");

	/* Use vpr to move down 5 lines */
	char *cmd = "PS1=un'2>'; printf '\\033[5e'; echo foo";
	send_txt(fd, "un2>", cmd);
	rv |= validate_row(fd, 8, "un1> %-75s", cmd);

	for( int i = 10; i < 14; i++ ) {
		rv |= validate_row(fd, i, "%80s", "");
	}
	rv |= validate_row(fd, 14, "%-80s", "foo");
	rv |= validate_row(fd, 15, "%-80s", "un2>");

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
	send_txt(fd, "un1>",
		"PS1='u'n'1>'; for i in 1 2 3 4 5; do "
		"printf '%%s' ${i}123456789; done; echo"
	);
	rv |= validate_row(fd, 2, "%-20s", "11234567892123456789");

	/* Shift right 15 chars */
	send_cmd(fd, "un2>", "15>\rPS1='%15su'n'2>'", "");
	rv |= validate_row(fd, 2, "%-20s", "56789312345678941234");

	for( unsigned i = 0; i < sizeof buf - 1; i++ ) {
		buf[i] = 'a' + i % 26;
	}
	buf[sizeof buf - 1] = '\0';
	/* Clear screen, print 2 rows (160 chars) of alphabet */
	send_txt(fd, "un3>", "PS1='%15su'n'3>'; clear; printf '%s\\n'",
		"", buf
	);
	rv |= validate_row(fd, 1, "%-20s", "pqrstuvwxyzabcdefghi");

	/* Shift right 75 chars ( now looking at last 20 chars of pty)*/
	send_cmd(fd, NULL, "75>");

	send_txt(fd, "un3>", "PS1='%60su'n'3>'", "");
	/* Verify that the 160 chars of repeated alphabet is at end of rows */
	rv |= validate_row(fd, 1, "%-20s", "ijklmnopqrstuvwxyzab");
	rv |= validate_row(fd, 2, "%-20s", "klmnopqrstuvwxyzabcd");

	/* Change width of underlying pty to 180 */
	send_cmd(fd, NULL, "180W");
	send_txt(fd, "un4>", "PS1='%60su'n'4>'; clear; echo '%s'", "", buf);
	rv |= validate_row(fd, 1, "%-20s", "ijklmnopqrstuvwxyzab");
	send_cmd(fd, NULL, "1>");
	send_txt(fd, "un5>", "PS1='%61su'n'5>'; clear; echo '%s'", "", buf);
	rv |= validate_row(fd, 1, "%-20s", "jklmnopqrstuvwxyzabc");

	/* Change width of underlying pty to match canvas and scroll to start */
	send_cmd(fd, NULL, "W180<");
	send_txt(fd, "un6>", "PS1='u'n'6>'; clear; printf '%s\\n'", buf);
	rv |= validate_row(fd, 1, "%-20s", "abcdefghijklmnopqrst");
	rv |= validate_row(fd, 2, "%-20s", "uvwxyzabcdefghijklmn");

	return rv;
}
