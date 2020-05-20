
bin_PROGRAMS = sttm
AM_CPPFLAGS = -D_POSIX_C_SOURCE=200809L -D_XOPEN_SOURCE=600 -D_XOPEN_SOURCE_EXTENDED
LIBS = -lncursesw -lutil

LDADD = libsttm.la
noinst_LTLIBRARIES = libsttm.la
libsttm_la_SOURCES = vtparser.c main.c cset.c handler.c
check_PROGRAMS = find-node-test
TESTS = find-node-test

CLEANFILES = *.gcda *.gcno *.gcov version *.1
EXTRA_DIST = build-aux/package-version version
noinst_HEADERS = vtparser.h main.h

.PHONY: version
version:
	cd $(top_srcdir) && autoreconf -ivf
	( cd $(top_srcdir) && build-aux/package-version ) > version;

install-terminfo: $(top_srcdir)/sttm.ti
	tic -s -x $(top_srcdir)/sttm.ti
