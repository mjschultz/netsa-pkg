# RCSIDENT("$SiLK: Makefile.am 33debcf4a95d 2016-09-20 20:19:03Z mthomas $");

# Installed Targets

bin_PROGRAMS = rwallformats

EXTRA_DIST =


# Additional Targets

noinst_PROGRAMS = rwrtd2split


# Build Rules

AM_CPPFLAGS = $(SK_SRC_INCLUDES) $(SK_CPPFLAGS)
AM_CFLAGS = $(WARN_CFLAGS) $(SK_CFLAGS)
AM_LDFLAGS = $(SK_LDFLAGS) $(STATIC_APPLICATIONS)
LDADD = ../libsilk/libsilk.la

rwallformats_SOURCES = rwallformats.c

rwrtd2split_SOURCES = rwrtd2split.c


# Global Rules
include $(top_srcdir)/build.mk
include $(top_srcdir)/lib-deps.mk


# Tests

# Required files; variables defined in ../../build.mk
check_DATA = $(SILK_TESTSDIR) $(SILK_TESTDATA)

EXTRA_DIST += $(TESTS)

TESTS = \
	tests/rwallformats-help.pl \
	tests/rwallformats-version.pl \
	tests/rwallformats-lone-command.pl \
	tests/rwrtd2split-help.pl \
	tests/rwrtd2split-version.pl \
	tests/rwrtd2split-lone-command.pl

# Manually generated test
TESTS += \
	tests/rwallformats-all.pl
