#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.63])
AC_INIT([bake], [0.1], [],[],[]) 
AC_CONFIG_MACRO_DIR([m4])
LT_INIT

AC_CANONICAL_TARGET
AC_CANONICAL_SYSTEM
AC_CANONICAL_HOST

AM_INIT_AUTOMAKE([foreign subdir-objects -Wall])

# we should remove this soon, only needed for automake 1.10 and older
m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES([yes])])

AC_CONFIG_SRCDIR([README.md])
AC_CONFIG_HEADERS([bake-config.h])

# Checks for programs.
AC_PROG_CC
AM_PROG_CC_C_O
AC_PROG_CXX
AC_PROG_CXXCPP

AC_PROG_MKDIR_P

AC_REQUIRE_CPP

AC_CHECK_SIZEOF([long int])

dnl
dnl Verify pkg-config
dnl
PKG_PROG_PKG_CONFIG
if test "x$PKG_CONFIG" == "x"; then
   AC_MSG_ERROR([Could not find pkg-config utility!])
fi

# coreutils checks for OSX
AC_ARG_VAR([TIMEOUT], timeout program)
AC_ARG_VAR([MKTEMP], mktemp program)

if test -z "$TIMEOUT" ; then
    AC_CHECK_PROGS(TIMEOUT, [timeout gtimeout])
    if test -z "$TIMEOUT" ; then
        AC_MSG_ERROR([Could not find timeout command (can optionally provide via the TIMEOUT variable)])
    fi
else
    AC_SUBST([TIMEOUT], ["$TIMEOUT"])
fi

if test -z "$MKTEMP" ; then
    AC_CHECK_PROGS(MKTEMP, [mktemp gmktemp])
    if test -z "$MKTEMP" ; then
        AC_MSG_ERROR([Could not find mktemp command (can optionally provide via the MKTEMP variable)])
    fi
else
    AC_SUBST([MKTEMP], ["$MKTEMP"])
fi

PKG_CHECK_MODULES([MARGO],[margo],[],
   [AC_MSG_ERROR([Could not find working margo installation!])])
LIBS="$MARGO_LIBS $LIBS"
CPPFLAGS="$MARGO_CFLAGS $CPPFLAGS"
CFLAGS="$MARGO_CFLAGS $CFLAGS"

PKG_CHECK_MODULES([UUID],[uuid],[],
   [AC_MSG_ERROR([Could not find working uuid installation!])])
LIBS="$UUID_LIBS $LIBS"
CPPFLAGS="$UUID_CFLAGS $CPPFLAGS"
CFLAGS="$UUID_CFLAGS $CFLAGS"

PKG_CHECK_MODULES([REMI],[remi],[],
   [AC_MSG_ERROR([Could not find working remi installation!])])
LIBS="$REMI_LIBS $LIBS"
CPPFLAGS="$REMI_CFLAGS $CPPFLAGS"
CFLAGS="$REMI_CFLAGS $CFLAGS"

AC_ARG_ENABLE([sizecheck],
    AS_HELP_STRING([--enable-sizecheck], [Adds a header in regions to keep track of region sizes (may degrade performance) @<:@default=no@:>@]),
    [AS_CASE(${enableval}, [yes], [AC_DEFINE([USE_SIZECHECK_HEADERS], 1, [Enable sizecheck])], [no], [],
    [AC_MSG_ERROR([bad value ${enableval} for --enable-sizecheck])])],
    [enable_sizecheck=no])

AC_ARG_ENABLE([timers],
    AS_HELP_STRING([--enable-timers], [Print detailed timers from each function on stdout @<:@default=no@:>@]),
    [AS_CASE(${enableval}, [yes], [AC_DEFINE([ENABLE_TIMING], 1, [Enable timers])], [no], [],
    [AC_MSG_ERROR([bad value ${enableval} for --enable-timers])])],
    [enable_timers=no])

AC_CONFIG_FILES([Makefile maint/bake-client.pc maint/bake-server.pc])
AC_OUTPUT
