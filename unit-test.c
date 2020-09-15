#include "unit-test.h"

int
test_ack(int fd, pid_t pid)
{
	/* Expect an \x06 in response to \x05
	 * I don't completely understand how the \x06 is getting converted
	 * to "^F"
	 */
	(void)pid;
	send_txt(fd, "^F", "printf '\\005'");
	send_cmd(fd, NULL, "143q");
	return 0; /* Test will timeout if it fails */
}

int
test_attach(int fd, pid_t p)
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
test_cols(int fd, pid_t p)
{
	/* Ensure that tput correctly identifies the width */
	int rv;
	send_txt(fd, "uniq1", "%s", "tput cols; printf 'uniq%s\\n' 1");
	rv = validate_row(fd, 2, "%-92s", "97");
	send_txt(fd, NULL, "exit");
	return rv;
}

int
test_csr(int fd, pid_t p)
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
test_resend(int fd, pid_t p)
{
	send_txt(fd, "uniq", "%1$c%1$c\recho u'n'i'q'", commandkey);
	int rv = validate_row(fd, 1, "%-80s", "ps1>^G");
	send_txt(fd, NULL, "exit");
	return rv;
}
