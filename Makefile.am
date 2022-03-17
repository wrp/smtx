
bin_PROGRAMS = smtx
man1_MANS = smtx.1
ACLOCAL_AMFLAGS = -I m4
LIBS = -lncursesw -lutil

LDADD = libsmtx.la
noinst_LTLIBRARIES = libsmtx.la
libsmtx_la_SOURCES = vtparser.c smtx-main.c cset.c handler.c action.c test-describe.c \
	bindings.c
check_PROGRAMS = test-main
AM_TESTS_ENVIRONMENT = LC_ALL=en_US.UTF-8; export LC_ALL;
TESTS = test-shell test-main test-coverage
test_main_SOURCES = test-main.c test-unit.c
test_main_DEPENDENCIES = smtx

CLEANFILES = *.gcda *.gcno *.gcov version *.1
EXTRA_DIST = build-aux/package-version version test-shell test-coverage smtx.ti \
	smtx.1 smtx.txt
noinst_HEADERS = vtparser.h smtx.h test-unit.h

version: $(DIST_SOURCES) $(DIST_COMMON)
	{ cd $(top_srcdir) && ./autogen.sh && build-aux/package-version >&3; } 3> version

install-data-hook:
	mkdir -p $(DESTDIR)$(sysconfdir)/terminfo
	tic -o $(DESTDIR)$(sysconfdir)/terminfo -s -x $(top_srcdir)/smtx.ti

uninstall-hook:
	rm -rf $(DESTDIR)$(sysconfdir)/terminfo/s/smtx
	rm -rf $(DESTDIR)$(sysconfdir)/terminfo/s/smtx-256color

smtx.1: smtx.txt
	@asciidoctor -a ver=${PACKAGE_VERSION} -b manpage $< 2> /dev/null \
	|| echo 'This page blank since asciidoctor was not available' > $@
