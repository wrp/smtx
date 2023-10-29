#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.69])
AC_INIT([smtx],
	m4_esyscmd([./build-aux/package-version]),
	[william.r.pursell@gmail.com])
AM_INIT_AUTOMAKE([-Wall -Werror foreign])
AM_PROG_AR
LT_INIT
AC_CONFIG_MACRO_DIRS([m4])
AC_CONFIG_HEADERS([config.h])

AC_PROG_CC
AC_PROG_CC_STDC
AC_CHECK_HEADERS([unistd.h util.h libutil.h termios.h pty.h wchar.h wctype.h])
AC_CHECK_HEADERS([curses.h ncursesw/curses.h])
AC_CHECK_DECL([A_ITALIC],AC_DEFINE([HAVE_A_ITALIC],[1],[ ]),[],[[#include <curses.h>]])
AC_TYPE_SIZE_T
AC_TYPE_SSIZE_T

AC_FUNC_REALLOC
AC_SEARCH_LIBS([endwin],[ncursesw ncurses],[],AC_MSG_ERROR([unable to find ncurses library]))
AC_SEARCH_LIBS([forkpty],[util],[],AC_MSG_ERROR([unable to find util library]))
AC_CHECK_FUNC([alloc_pair],AC_DEFINE([HAVE_ALLOC_PAIR],[1],[ ]))


AM_CPPFLAGS='-D_POSIX_C_SOURCE=200809L -D_XOPEN_SOURCE=600 -D_XOPEN_SOURCE_EXTENDED'
AC_CANONICAL_HOST
case "${host_os}" in
darwin*) AM_CPPFLAGS="${AM_CPPFLAGS} -D_DARWIN_C_SOURCE" ;;
esac


AC_SUBST([AM_CPPFLAGS])
AC_CONFIG_FILES([Makefile])
AC_OUTPUT
