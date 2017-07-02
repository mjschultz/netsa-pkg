# RCSIDENT("$SiLK: Makefile.am 13c86c07b9e7 2016-09-23 19:59:04Z mthomas $");

# Installed Targets

noinst_LTLIBRARIES = libflowsource.la
#libflowsource_version = 8:2:0
# At previous release: libflowsource_version = 8:1:0


# Additional Targets

EXTRA_PROGRAMS = check-struct
# $(EXTRA_PROGRAMS) only need to appear in one of bin_PROGRAMS,
# noinst_PROGRAMS, or check_PROGRAMS
#check_PROGRAMS = $(EXTRA_PROGRAMS)
# Swap the comment character between bin_PROGRAMS and noinst_PROGRAMS
# to determine whether to install the test programs in $(bindir)
#bin_PROGRAMS += $(EXTRA_PROGRAMS)
noinst_PROGRAMS = $(EXTRA_PROGRAMS)


# Build Rules

AM_CPPFLAGS = $(SK_SRC_INCLUDES) $(SK_CPPFLAGS)
AM_CFLAGS = $(WARN_CFLAGS) $(SK_CFLAGS)
LDADD =	libflowsource.la ../libsilk/libsilk.la

libflowsource_la_SOURCES = libflowsource.h ipfixsource.c pdusource.c \
	 probeconf.c probeconf.h \
	 skipfix.c skipfix.h

#libflowsource_la_LDFLAGS = -version-info $(libflowsource_version)
#libflowsource_la_LIBADD  = $(LDADD)

check_struct_SOURCES = check-struct.c

CLEANFILES = $(EXTRA_PROGRAMS)

# Global Rules
include $(top_srcdir)/build.mk
