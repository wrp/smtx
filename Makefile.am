
bin_PROGRAMS = sttm
AM_CPPFLAGS = -D_POSIX_C_SOURCE=200809L -D_XOPEN_SOURCE=600 -D_XOPEN_SOURCE_EXTENDED
LIBS = -lncursesw -lutil

LDADD = libsttm.la
noinst_LTLIBRARIES = libsttm.la
libsttm_la_SOURCES = vtparser.c sttm-main.c cset.c handler.c
check_PROGRAMS = find-node-test sttm-test
TESTS = main-test find-node-test sttm-test coverage-test

CLEANFILES = *.gcda *.gcno *.gcov version *.1
EXTRA_DIST = build-aux/package-version version main-test coverage-test
noinst_HEADERS = vtparser.h main.h

.PHONY: version
version:
	cd $(top_srcdir) && autoreconf -ivf
	( cd $(top_srcdir) && build-aux/package-version ) > version;

install-terminfo: $(top_srcdir)/sttm.ti
	tic -s -x $(top_srcdir)/sttm.ti
