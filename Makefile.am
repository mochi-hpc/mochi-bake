AUTOMAKE_OPTIONS = foreign
ACLOCAL_AMFLAGS = -I m4

bin_PROGRAMS =
bin_SCRIPTS =
noinst_PROGRAMS =
noinst_HEADERS =
TESTS =
XFAIL_TESTS =
check_PROGRAMS =
EXTRA_PROGRAMS =
CLEANFILES = $(bin_SCRIPTS)
MAINTAINERCLEANFILES =
EXTRA_DIST =
BUILT_SOURCES =
include_HEADERS = include/bake.h\
		  include/bake-client.h\
		  include/bake-server.h\
		  include/bake.hpp\
		  include/bake-client.hpp\
		  include/bake-server.hpp

TESTS_ENVIRONMENT =

EXTRA_DIST += \
 prepare.sh

AM_CPPFLAGS = -I$(top_srcdir)/include

AM_CFLAGS =

AM_LIBS =

AM_CXXFLAGS = $(AM_CFLAGS)

lib_LTLIBRARIES = src/libbake-client.la src/libbake-server.la
src_libbake_client_la_SOURCES =
src_libbake_server_la_SOURCES =

LDADD = src/libbake-client.la src/libbake-server.la

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = \
 maint/bake-client.pc \
 maint/bake-server.pc


include Make.rules

include $(top_srcdir)/src/Makefile.subdir
include $(top_srcdir)/tests/Makefile.subdir
include $(top_srcdir)/tests/proxy/Makefile.subdir
