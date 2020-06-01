
bin_PROGRAMS = smtx
AM_CPPFLAGS = -D_POSIX_C_SOURCE=200809L -D_XOPEN_SOURCE=600 -D_XOPEN_SOURCE_EXTENDED
LIBS = -lncursesw -lutil

LDADD = libsmtx.la
noinst_LTLIBRARIES = libsmtx.la
libsmtx_la_SOURCES = vtparser.c smtx-main.c cset.c handler.c
check_PROGRAMS = smtx-test
TESTS = main-test smtx-test coverage-test

CLEANFILES = *.gcda *.gcno *.gcov version *.1
EXTRA_DIST = build-aux/package-version version main-test coverage-test
noinst_HEADERS = vtparser.h smtx.h

.PHONY: version
version:
	cd $(top_srcdir) && autoreconf -ivf
	( cd $(top_srcdir) && build-aux/package-version ) > version;

install-terminfo: $(top_srcdir)/smtx.ti
	tic -s -x $(top_srcdir)/smtx.ti
