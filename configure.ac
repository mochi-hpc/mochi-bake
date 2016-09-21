#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.63])
AC_INIT([bake-bulk], [0.1], [],[],[]) 
AC_CONFIG_MACRO_DIR([m4])
LT_INIT

AC_CANONICAL_TARGET
AC_CANONICAL_SYSTEM
AC_CANONICAL_HOST

AM_INIT_AUTOMAKE([foreign subdir-objects -Wall])

# we should remove this soon, only needed for automake 1.10 and older
m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES([yes])])

AC_CONFIG_SRCDIR([README.md])
AC_CONFIG_HEADERS([bake-bulk-config.h])

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

PKG_CHECK_MODULES([MARGO],[margo],[],
   [AC_MSG_ERROR([Could not find working margo installation!])])
LIBS="$MARGO_LIBS $LIBS"
CPPFLAGS="$MARGO_CFLAGS $CPPFLAGS"
CFLAGS="$MARGO_CFLAGS $CFLAGS"

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

PKG_CHECK_MODULES([HG_BULK_POOL],[hg-bulk-pool],[],
   [AC_MSG_ERROR([Could not find working hg bulk pool installation!])])
LIBS="$HG_BULK_POOL_LIBS $LIBS"
CPPFLAGS="$HG_BULK_POOL_CFLAGS $CPPFLAGS"
CFLAGS="$HG_BULK_POOL_CFLAGS $CFLAGS"

AC_CONFIG_FILES([Makefile maint/bake-bulk.pc])
AC_OUTPUT
 
