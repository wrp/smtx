CC        ?= gcc
CFLAGS    ?= -std=c99 -Wall -Wextra -pedantic -Os
FEATURES  ?= -D_POSIX_C_SOURCE=200809L -D_XOPEN_SOURCE=600 -D_XOPEN_SOURCE_EXTENDED
HEADERS   ?=
LIBPATH   ?=
DESTDIR   ?= /usr/local
MANDIR    ?= $(DESTDIR)/man/man1
CURSESLIB ?= ncursesw
LIBS      ?= -l$(CURSESLIB) -lutil

all: mtm

FILES = vtparser.c mtm.c cset.c handler.c
mtm: config.h $(FILES)
	$(CC) $(CFLAGS) $(FEATURES) -o $@ $(HEADERS) $(FILES) $(LIBPATH) $(LIBS)

config.h: config.def.h
	cp config.def.h config.h

install: mtm
	cp mtm $(DESTDIR)/bin
	cp mtm.1 $(MANDIR)

install-terminfo: mtm.ti
	tic -s -x mtm.ti

clean:
	rm -f *.o mtm
