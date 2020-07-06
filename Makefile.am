
bin_PROGRAMS = smtx
AM_CPPFLAGS = -D_POSIX_C_SOURCE=200809L -D_XOPEN_SOURCE=600 -D_XOPEN_SOURCE_EXTENDED
ACLOCAL_AMFLAGS = -I m4
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
	( cd $(top_srcdir) && build-aux/package-version ) > version;

	## This is a terrible hack.  The proper approach (is to not do
	## this at all!) is to cd $(top_srcdir) && ./autogen.sh before running
	## build-aux/package-version, but that is too slow.  Just hack it for now:
	cat Makefile | awk 'match($$1,"^(PACKAGE_)*VERSION$$") && NF == 3{$$3 = v} 1' \
		v=$$(cat version) > Makefile.tmp
	cat config.h | awk 'match($$2,"^(PACKAGE_)*VERSION$$") && NF == 3{$$3 = v} 1' \
		v='"'$$(cat version)'"' > config.h.tmp
	mv Makefile.tmp Makefile
	mv config.h.tmp config.h
	make clean

install-terminfo: $(top_srcdir)/smtx.ti
	tic -s -x $(top_srcdir)/smtx.ti
