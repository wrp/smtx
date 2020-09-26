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
	send_txt(fd, "uniq1", "%s;%s", "tput csr 6 12; yes | nl -s: | sed 25q",
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
test_resend(int fd)
{
	send_txt(fd, "uniq", "%1$c%1$c\recho u'n'i'q'", ctlkey);
	int rv = validate_row(fd, 1, "%-80s", "ps1>^G");
	send_txt(fd, NULL, "exit");
	return rv;
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
