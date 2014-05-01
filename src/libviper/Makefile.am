## Process this file with automake to produce Makefile.in

INCLUDES = $(INCLTDL) \
           -I$(top_builddir) \
           -I$(top_srcdir)/include

AM_CFLAGS =

# Libraries to build
lib_LTLIBRARIES = libviper.la

noinst_HEADERS = entity_config.h viper_internal.h

libviper_la_SOURCES = \
	entity_config.c shvio_compat.c util.c

libviper_la_CFLAGS = $(UIOMUX_CFLAGS)
libviper_la_LDFLAGS = -version-info @SHARED_VERSION_INFO@ @SHLIB_VERSION_ARG@
libviper_la_LIBADD = $(UIOMUX_LIBS)
