
bin_PROGRAMS = mtm
AM_CPPFLAGS = -D_POSIX_C_SOURCE=200809L -D_XOPEN_SOURCE=600 -D_XOPEN_SOURCE_EXTENDED
LIBS = -lncursesw -lutil

CLEANFILES = *.gcda *.gcno *.gcov version *.1
EXTRA_DIST = build-aux/package-version

.PHONY: version
version:
	{ cd $(top_srcdir) && build-aux/package-version; } | { read v; \
	echo "$$v" > version; \
	awk '/VERSION/{$$3 = sprintf("\"%s\"", v)}1' v=$$v config.h > config.h.new \
	&& mv config.h.new config.h; }

mtm_SOURCES = vtparser.c mtm.c cset.c handler.c
