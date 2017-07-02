# RCSIDENT("$SiLK: Makefile.am 7dc2e093f1e4 2016-12-07 17:40:42Z mthomas $");

# Installed Targets

bin_PROGRAMS = rwpdu2silk
sbin_PROGRAMS = rwflowpack rwpackchecker

EXTRA_DIST = rwflowpack.pod rwpackchecker.pod rwpdu2silk.pod
if HAVE_POD2MAN
man1_MANS = rwpdu2silk.1
man8_MANS = rwflowpack.8 rwpackchecker.8
endif

dist_pkgdata_DATA = packer-templ.lua


conf_files = rwflowpack.conf
EXTRA_DIST += rwflowpack.conf.in

init_d_scripts = rwflowpack.init.d
EXTRA_DIST += rwflowpack.init.d.in


# Build Rules

AM_CPPFLAGS = $(SK_SRC_INCLUDES) $(SK_CPPFLAGS)
AM_CFLAGS = $(WARN_CFLAGS) $(SK_CFLAGS)
AM_LDFLAGS = $(SK_LDFLAGS) $(STATIC_APPLICATIONS)

rwflowpack_SOURCES = rwflowpack.c rwflowpack.h rwflowpack-append.c \
	 rwflowpack-config.c rwflowpack-flowcap.c rwflowpack_priv.h \
	 stream-cache.c stream-cache.h \
	 rwflowpack-stream.c rwflowpack-fcfiles.c \
	 rwflowpack-singlefile.c \
	 rwflowpack-local.c rwflowpack-onedest.c \
	 rwflowpack-ipfixsource.c rwflowpack-pdusource.c \
	 rwflowpack-probe.c packlogic.lua
rwflowpack_LDFLAGS = -export-dynamic
rwflowpack_LDADD = ../libsilk/libsilk.la

rwpackchecker_SOURCES = rwpackchecker.c
rwpackchecker_LDADD = ../libsilk/libsilk.la

rwpdu2silk_SOURCES = rwpdu2silk.c 
rwpdu2silk_LDADD = ../libflowsource/libflowsource.la \
	 ../libsilk/libsilk.la

LUA_OUTPUT_FILES = packlogic.i

BUILT_SOURCES = $(LUA_OUTPUT_FILES)

rwflowpack.conf: Makefile rwflowpack.conf.in
	$(MAKE_CONF_FILE)
rwflowpack.init.d: Makefile rwflowpack.init.d.in
	$(MAKE_INIT_D_SCRIPT)

all-local: $(conf_files) $(init_d_scripts)
install-data-local: install-conf-files install-init-d-scripts
uninstall-local: uninstall-conf-files uninstall-init-d-scripts

CLEANFILES = $(conf_files) $(init_d_scripts) $(LUA_OUTPUT_FILES) \
	rwguess rwguess.1

# Global Rules
include $(top_srcdir)/build.mk
include $(top_srcdir)/lib-deps.mk


# Tests

# Required files; variables defined in ../../build.mk
check_DATA = $(SILK_TESTSDIR) $(SILK_TESTDATA) $(SILK_TESTPDU)

EXTRA_DIST += $(TESTS)
EXTRA_DIST += \
	tests/flowcap-daemon.py \
	tests/rwflowappend-daemon.py \
	tests/rwflowpack-daemon.py \
	tests/rwflowpack-pack-ipfix-ipv6.pl.txt \
	tests/rwflowpack-pack-multiple.pl.txt \
	tests/rwflowpack-pack-pdu-dir.pl.txt \
	tests/rwflowpack-pack-silk-discard.pl.txt \
	tests/rwflowpack-pack-silk-ipv6.pl.txt \
	tests/rwflowpack-pack-silk-send.pl.txt \
	tests/rwflowpack-pack-silk.pl.txt

MOSTLYCLEANFILES = tests/rwflowappend-daemon.pyc tests/rwflowappend-daemon.pyc

TESTS = \
	tests/rwflowpack-help.pl \
	tests/rwflowpack-version.pl \
	tests/rwflowpack-lone-command.pl \
	tests/rwpackchecker-help.pl \
	tests/rwpackchecker-version.pl \
	tests/rwpackchecker-lone-command.pl \
	tests/rwpdu2silk-help.pl \
	tests/rwpdu2silk-version.pl \
	tests/rwpdu2silk-lone-command.pl \
	tests/rwpackchecker-all-tests.pl \
	tests/rwpackchecker-port-123.pl \
	tests/rwpackchecker-bpp-allow-1.pl \
	tests/rwpackchecker-bpp-allow-2.pl \
	tests/rwpackchecker-sipset.pl \
	tests/rwpdu2silk-small-input.pl

# above tests are automatically generated;
# those below are written by hand
TESTS += \
	tests/rwflowpack-init-d.pl \
	tests/rwflowpack-pack-silk.pl \
	tests/rwflowpack-pack-silk-ipv6.pl \
	tests/rwflowpack-pack-silk-send.pl \
	tests/rwflowpack-pack-silk-after.pl \
	tests/rwflowpack-pack-silk-cmd.pl \
	tests/rwflowpack-pack-silk-lua.pl \
	tests/rwflowpack-pack-silk-lua-rwrec.pl \
	tests/rwflowpack-pack-fcfile.pl \
	tests/rwflowpack-pack-respool.pl \
	tests/rwflowpack-pack-pdu-dir.pl \
	tests/rwflowpack-pack-pdu-file.pl \
	tests/rwflowpack-pack-pdu-file-lua.pl \
	tests/rwflowpack-pack-ipfix-file.pl \
	tests/rwflowpack-pack-ipfix.pl \
	tests/rwflowpack-pack-ipfix-ipv6.pl \
	tests/rwflowpack-pack-ipfix-net-v4.pl \
	tests/rwflowpack-pack-ipfix-net-v6.pl \
	tests/rwflowpack-pack-multiple.pl \
	tests/rwflowpack-pack-multiple2.pl \
	tests/rwflowpack-pack-silk-discard-when.pl \
	tests/rwflowpack-pack-silk-discard-unless.pl \
	tests/rwflowpack-pack-bad-silk.pl \
	tests/rwflowpack-pack-bad-fcfile.pl \
	tests/rwflowpack-pack-bad-pdu.pl \
	tests/rwflowpack-pack-bad-ipfix.pl \
	tests/rwflowpack-append-ipv4.pl \
	tests/rwflowpack-append-ipv6.pl \
	tests/rwflowpack-append-cmd.pl \
	tests/rwflowpack-append-hours.pl \
	tests/rwflowpack-append-bad.pl \
	tests/rwflowpack-flowcap-netflowv5-v4.pl \
	tests/rwflowpack-flowcap-netflowv5-any-v4.pl \
	tests/rwflowpack-flowcap-netflowv5-v6.pl \
	tests/rwflowpack-flowcap-ipfix-v4.pl \
	tests/rwflowpack-flowcap-ipfix-any-v4.pl \
	tests/rwflowpack-flowcap-ipfix-v6.pl \
	tests/rwflowpack-flowcap-ipfixv6-v6.pl

