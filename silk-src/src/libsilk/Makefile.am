# RCSIDENT("$SiLK: Makefile.am d14ab78e4b11 2016-12-07 20:31:25Z mthomas $");

# Installed Targets

bin_PROGRAMS = silk_config silklua

lib_LTLIBRARIES = libsilk.la
libsilk_version = 44:0:0
# At previous release: libsilk_version = 44:0:0


EXTRA_DIST = silk_config.pod silk.conf.pod silk.pod silklua.pod
if HAVE_POD2MAN
man1_MANS = silk_config.1 silklua.1
man5_MANS = silk.conf.5
man7_MANS = silk.7
endif

pkginclude_HEADERS = hashlib.h rwrec.h silk.h				\
	 silk_files.h silk_types.h skbag.h skcircbuf.h skcmdline.h	\
	 skcountry.h skdaemon.h skdeque.h skdllist.h skfixbuf.h		\
	 skfixformatter.h skfixstream.h skflowiter.h skformat.h		\
	 skheader.h skheap.h  skipaddr.h skipfixcert.h skipset.h	\
	 skleb128.h sklog.h skmempool.h skplugin.h skpolldir.h		\
	 skprefixmap.h skprintnets.h skredblack.h skschema.h sksite.h	\
	 skstream.h skstringmap.h sktempfile.h skthread.h sktimer.h	\
	 sktracemsg.h skvector.h utils.h redblack/redblack.h		\
	 $(LEGACY_HDRS) $(EXTRA_HDRS)

if NEED_GETOPT_LONG_ONLY
EXTRA_HDRS = gnu_getopt.h
endif

dist_pkgdata_DATA = silk.magic silk-templ.conf


# Additional Targets

EXTRA_PROGRAMS = hashlib_metrics hashlib_tests \
	 options-parse-test parse-tests rwreadonly \
	 skbitmap-test skcircbuf-test skdeque-test \
	 skheader-test skheap-test \
	 skmempool-test sklog-test skpolldir-test \
	 skprefixmap-test skredblack-test skschema-test \
	 skschema-test-computed skschema-test-lists \
	 skschema-test-times sksidecar-test \
	 sksiteconfig-test skstream-test \
	 skstringmap-test sktimer-test skvector-test
# $(EXTRA_PROGRAMS) only need to appear in one of bin_PROGRAMS,
# noinst_PROGRAMS, or check_PROGRAMS
#check_PROGRAMS = $(EXTRA_PROGRAMS)
# Swap the comment character between bin_PROGRAMS and noinst_PROGRAMS
# to determine whether to install the test programs in $(bindir)
#bin_PROGRAMS += $(EXTRA_PROGRAMS)
noinst_PROGRAMS = $(EXTRA_PROGRAMS)


# Build Rules

AM_CPPFLAGS = $(SK_SRC_INCLUDES) $(SK_CPPFLAGS) \
    -DSILK_DATA_ROOTDIR='"${SILK_DATA_ROOTDIR}"' \
    -DSILK_PYTHON_SITE_PKG='"${PYTHON_SITE_PKG}"'
AM_CFLAGS = $(WARN_CFLAGS) $(SK_CFLAGS)
LDADD = libsilk.la

# Sources are maintained in a separate file
include Makefile-sources
EXTRA_DIST += getopt.c hashlib-lookup2.c hashlib-lookup3.c

libsilk_la_SOURCES = $(SOURCES_LIBSILK)
libsilk_la_LIBADD = ../../lua/src/liblua.la
libsilk_la_LDFLAGS = -version-info $(libsilk_version)

nodist_silk_config_SOURCES = silk_config.c
silk_config_CPPFLAGS = -DINCLUDEDIR='"$(includedir)"' -DLIBDIR='"$(libdir)"' $(AM_CPPFLAGS)

silklua_SOURCES = silklua.c
silklua_CPPFLAGS = $(SK_SRC_INCLUDES) $(READLINE_CFLAGS) $(AM_CPPFLAGS)
silklua_LDFLAGS = libsilk.la $(READLINE_LDFLAGS)

# The script to compile Lua code and write it as ASCII hex codes
EXTRA_DIST += lua2hex.pl

# Output of hashlib_metrics depends on whether HASHLIB_RECORD_STATS
# was defined when hashlib.c and hashlib_metrics.c were compiled.
# Output includes timing info, so not good for "make check" test.
hashlib_metrics_SOURCES = hashlib_metrics.c

hashlib_tests_SOURCES = hashlib_tests.c

options_parse_test_SOURCES = options-parse-test.c 

parse_tests_SOURCES = parse-tests.c

rwreadonly_SOURCES = rwreadonly.c

skbitmap_test_SOURCES = skbitmap-test.c

skcircbuf_test_SOURCES = skcircbuf-test.c

skdeque_test_SOURCES = skdeque-test.c

skheader_test_SOURCES = skheader-test.c

skheap_test_SOURCES = skheap-test.c

skmempool_test_SOURCES = skmempool-test.c

sklog_test_SOURCES = sklog-test.c

skpolldir_test_SOURCES = skpolldir-test.c

skprefixmap_test_SOURCES = skprefixmap-test.c

skredblack_test_SOURCES = skredblack-test.c

skschema_test_SOURCES = skschema-test.c

skschema_test_computed_SOURCES = skschema-test-computed.c

skschema_test_lists_SOURCES = skschema-test-lists.c

skschema_test_times_SOURCES = skschema-test-times.c

sksidecar_test_SOURCES = sksidecar-test.c sksidecar.h

sksiteconfig_test_SOURCES = sksiteconfig-test.c sksiteconfig.h

skstream_test_SOURCES = skstream-test.c

skstringmap_test_SOURCES = skstringmap-test.c

sktimer_test_SOURCES = sktimer-test.c

skvector_test_SOURCES = skvector-test.c

# add switches to flex that remove unused functions
AM_LFLAGS = $(FLEX_NOFUNS)

AM_YFLAGS = -d
sksiteconfig_lex.$(OBJEXT) sksiteconfig_lex.lo: sksiteconfig_parse.h

# Include the redblack docs in the distribution
EXTRA_DIST += redblack/AUTHORS redblack/COPYING

CLEANFILES = $(LUA_OUTPUT_FILES)
DISTCLEANFILES = lua/$(am__dirstamp)



# Global Rules
include $(top_srcdir)/build.mk
include $(top_srcdir)/lib-deps.mk

# Tests

# Required files; variables defined in ../../build.mk
check_DATA = $(SILK_TESTDATA_IPFIX) $(SILK_TESTPMAPS) $(SILK_TESTSDIR)

EXTRA_DIST += $(TESTS)
EXTRA_DIST += \
	tests/luaunit.lua \
	tests/sklua-schema-test.lua \
	tests/sklua-site-test.lua \
	tests/sklua-test.lua

TESTS = \
	tests/run-hashlib-tests.pl \
	tests/run-skheap-test.pl \
	tests/run-skmempool-test.pl \
	tests/run-skschema-test.pl \
	tests/run-skschema-test-times.pl \
	tests/run-skdeque-test.pl \
	tests/run-skstringmap-test.pl \
	tests/run-skvector-test.pl \
	tests/run-parse-tests-numbers.pl \
	tests/run-parse-tests-lists.pl \
	tests/run-parse-tests-dates.pl \
	tests/run-parse-tests-tcp-flags.pl \
	tests/run-parse-tests-signals.pl \
	tests/run-parse-tests-ip-addresses.pl \
	tests/run-parse-tests-host-port-pairs.pl \
	tests/run-skbitmap-test.pl \
	tests/silklua-sklua-test.pl \
	tests/silklua-sklua-schema-test.pl \
	tests/silklua-sklua-site-test.pl
