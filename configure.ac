#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.63])
AC_INIT([bake], [0.3.4], [],[],[]) 
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

PKG_CHECK_MODULES([ABTIO],[abt-io],[],
   [AC_MSG_ERROR([Could not find working abt-io installation!])])
LIBS="$ABTIO_LIBS $LIBS"
CPPFLAGS="$ABTIO_CFLAGS $CPPFLAGS"
CFLAGS="$ABTIO_CFLAGS $CFLAGS"

# NOTE: See README.md if the following does not work for you; some versions of
# nvml/pmem install broken .pc files
PKG_CHECK_MODULES([LIBPMEMOBJ],[libpmemobj],[],
   [AC_MSG_ERROR([Could not find working libpmemobj installation!])])
LIBS="$LIBPMEMOBJ_LIBS $LIBS"
CPPFLAGS="$LIBPMEMOBJ_CFLAGS $CPPFLAGS"
CFLAGS="$LIBPMEMOBJ_CFLAGS $CFLAGS"

PKG_CHECK_MODULES([UUID],[uuid],[],
   [AC_MSG_ERROR([Could not find working uuid installation!])])
LIBS="$UUID_LIBS $LIBS"
CPPFLAGS="$UUID_CFLAGS $CPPFLAGS"
CFLAGS="$UUID_CFLAGS $CFLAGS"

PKG_CHECK_MODULES([JSONC],[json-c],[],
   [AC_MSG_ERROR([Could not find working json-c installation!])])
LIBS="$JSONC_LIBS $LIBS"
CPPFLAGS="$JSONC_CFLAGS $CPPFLAGS"
CFLAGS="$JSONC_CFLAGS $CFLAGS"

AC_ARG_ENABLE(remi,
        [AS_HELP_STRING([--enable-remi],[Enable REMI (migration) support @<:@default=no@:>@])],
        [case "${enableval}" in
         yes) enable_remi="yes" ;;
         no) enable_remi="no" ;;
         *) AC_MSG_ERROR(bad value ${enableval} for --enable-remi) ;;
 esac],
 [enable_remi="no"]
 )
AM_CONDITIONAL(ENABLE_REMI, test x$enable_remi = xyes)
if test "$enable_remi" = "yes"; then
        PKG_CHECK_MODULES(REMI, remi)
        AC_DEFINE(USE_REMI, 1, [REMI support enabled.])
        USE_REMI=1
        LIBS="$REMI_LIBS $LIBS"
        CPPFLAGS="$REMI_CFLAGS $CPPFLAGS"
        CFLAGS="$REMI_CFLAGS $CFLAGS"
        REMI_PKG="remi"
else
        USE_REMI=0
        REMI_PKG=""
fi
AC_SUBST(USE_REMI)
AC_SUBST(REMI_PKG)

AC_ARG_ENABLE(benchmark,
              [AS_HELP_STRING([--enable-benchmark],[Build Bake benchmark @<:@default=no@:>@])],
              [case "${enableval}" in
                yes) enable_benchmark="yes" ;;
                no) enable_benchmark="no" ;;
                *) AC_MSG_ERROR(bad value ${enableval} for --enable-benchmark) ;;
               esac],
              [enable_benchmark="no"]
)
AM_CONDITIONAL(BUILD_BENCHMARK, test x$enable_benchmark = xyes)

if [test "$enable_benchmark" = "yes"]; then
        PKG_CHECK_MODULES(JSONCPP, jsoncpp)
        LIBS="$JSONCPP_LIBS $LIBS"
        CPPFLAGS="$JSONCPP_CFLAGS $CPPFLAGS"
        CFLAGS="$JSONCPP_CFLAGS $CFLAGS"
fi

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
