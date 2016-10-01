# RCSIDENT("$SiLK: Makefile.am 2d7b43e7a886 2016-09-23 14:04:15Z mthomas $");

# Installed Targets

bin_PROGRAMS = rwfileinfo

EXTRA_DIST = rwfileinfo.pod
if HAVE_POD2MAN
man1_MANS = rwfileinfo.1
endif


# Build Rules

AM_CPPFLAGS = $(SK_SRC_INCLUDES) $(SK_CPPFLAGS)
AM_CFLAGS = $(WARN_CFLAGS) $(SK_CFLAGS)
AM_LDFLAGS = $(SK_LDFLAGS) $(STATIC_APPLICATIONS)
LDADD = ../libsilk/libsilk.la

rwfileinfo_SOURCES = rwfileinfo.c


# Global Rules
include $(top_srcdir)/build.mk
include $(top_srcdir)/lib-deps.mk


# Tests

# Required files; variables defined in ../../build.mk
check_DATA = $(SILK_TESTSDIR) $(SILK_TESTDATA)

EXTRA_DIST += $(TESTS)

TESTS = \
	tests/rwfileinfo-help.pl \
	tests/rwfileinfo-version.pl \
	tests/rwfileinfo-lone-command.pl \
	tests/rwfileinfo-recs-data.pl \
	tests/rwfileinfo-recs-empty.pl \
	tests/rwfileinfo-recs-stdin.pl \
	tests/rwfileinfo-vers-cmd-lines.pl \
	tests/rwfileinfo-length.pl \
	tests/rwfileinfo-byte-order.pl

# above tests are automatically generated;
# those below are written by hand
TESTS += \
	tests/rwfileinfo-compression.pl
