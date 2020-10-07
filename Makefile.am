
bin_PROGRAMS = smtx
AM_CPPFLAGS = -D_POSIX_C_SOURCE=200809L -D_XOPEN_SOURCE=600 -D_XOPEN_SOURCE_EXTENDED
ACLOCAL_AMFLAGS = -I m4
LIBS = -lncursesw -lutil

LDADD = libsmtx.la
noinst_LTLIBRARIES = libsmtx.la
libsmtx_la_SOURCES = vtparser.c smtx-main.c cset.c handler.c action.c show.c
check_PROGRAMS = test-main
TESTS = main-test test-main coverage-test
test_main_SOURCES = test-main.c test-unit.c
test_main_DEPENDENCIES = smtx

CLEANFILES = *.gcda *.gcno *.gcov version *.1
EXTRA_DIST = build-aux/package-version version main-test coverage-test smtx.ti
noinst_HEADERS = vtparser.h smtx.h unit-test.h

version: $(DIST_SOURCES) $(DIST_COMMON)
	{ cd $(top_srcdir) && ./autogen.sh && build-aux/package-version >&3; } 3> version

install-data-hook:
	mkdir -p $(DESTDIR)$(sysconfdir)/terminfo
	tic -o $(DESTDIR)$(sysconfdir)/terminfo -s -x $(top_srcdir)/smtx.ti
