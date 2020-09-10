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
	int rv = validate_row(p, 3, "%-80s", "sync");

	send_cmd(fd, "other", "j\rprintf '\\no't'h'er'\\n'");

	get_layout(p, 5, desc, sizeof desc);
	if( sscanf(desc, "7x80(id=%*d); *7x80(id=%d);", &id) != 1 ) {
		fprintf(stderr, "received unexpected: '%s'\n", desc);
		rv = 1;
	} else {
		send_cmd(fd, "uniq", "k%da\recho 'u'ni'q'", id);
	}
	rv |= check_layout(p, 0x1, "*7x80; 7x80; 7x80");

	/* 2nd row of the first window should now be different */
	rv |= validate_row(p, 3, "%-80s", "other");
	return rv;
}
