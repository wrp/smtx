
#include <assert.h>
#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <signal.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/wait.h>
#include <unistd.h>
#if HAVE_PTY_H
# include <pty.h>
# include <utmp.h>
#elif HAVE_LIBUTIL_H
# include <libutil.h>
#elif HAVE_UTIL_H
# include <util.h>
#endif

extern int ctlkey;

void __attribute__((format(printf,3,4)))
send_txt(int fd, const char *wait, const char *fmt, ...);

void __attribute__((format(printf,3,4)))
send_cmd(int fd, const char *wait, const char *fmt, ...);

void __attribute__((format(printf,3,4)))
send_str(int fd, const char *wait, const char *fmt, ...);

int __attribute__((format(printf,3,4)))
validate_row(int fd, int row, const char *fmt, ... );

int
get_layout(int fd, int flag, char *layout, size_t siz);

int __attribute__((format(printf,3,4)))
check_layout(int fd, int flag, const char *fmt, ...);

typedef int(test)(int fd);
test test_ack;
test test_attach;
test test_cols;
test test_csr;
test test_resend;
