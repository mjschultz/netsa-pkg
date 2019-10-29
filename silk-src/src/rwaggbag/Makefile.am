# RCSIDENT("$SiLK: Makefile.am c56bd724bfc8 2019-10-11 19:38:12Z mthomas $");

# Installed Targets

bin_PROGRAMS = rwaggbag rwaggbagbuild rwaggbagcat rwaggbagtool

EXTRA_DIST = rwaggbag.pod rwaggbagbuild.pod rwaggbagcat.pod rwaggbagtool.pod
if HAVE_POD2MAN
man1_MANS = rwaggbag.1 rwaggbagbuild.1 rwaggbagcat.1 rwaggbagtool.1
endif


# Build Rules

AM_CPPFLAGS = $(SK_SRC_INCLUDES) $(SK_CPPFLAGS)
AM_CFLAGS = $(WARN_CFLAGS) $(SK_CFLAGS)
AM_LDFLAGS = $(SK_LDFLAGS) $(STATIC_APPLICATIONS)
LDADD = ../libsilk/libsilk.la

rwaggbag_SOURCES = rwaggbag.c

rwaggbagbuild_SOURCES = rwaggbagbuild.c

rwaggbagcat_SOURCES = rwaggbagcat.c

rwaggbagtool_SOURCES = rwaggbagtool.c


# Global Rules
include $(top_srcdir)/build.mk
include $(top_srcdir)/lib-deps.mk


# Tests

# Required files; variables defined in ../../build.mk
check_DATA = $(SILK_TESTSDIR) $(SILK_TESTDATA) $(SILK_TESTBAGS)

EXTRA_DIST += $(TESTS)

TESTS = \
	tests/rwaggbag-help.pl \
	tests/rwaggbag-version.pl \
	tests/rwaggbag-lone-command.pl \
	tests/rwaggbagbuild-help.pl \
	tests/rwaggbagbuild-version.pl \
	tests/rwaggbagbuild-lone-command.pl \
	tests/rwaggbagcat-help.pl \
	tests/rwaggbagcat-version.pl \
	tests/rwaggbagcat-lone-command.pl \
	tests/rwaggbagtool-help.pl \
	tests/rwaggbagtool-version.pl \
	tests/rwaggbagtool-lone-command.pl \
	tests/rwaggbag-no-switches.pl \
	tests/rwaggbag-null-input.pl \
	tests/rwaggbag-no-files.pl \
	tests/rwaggbag-ports-proto.pl \
	tests/rwaggbag-ports-proto-v6.pl \
	tests/rwaggbag-ports-proto-multi.pl \
	tests/rwaggbag-sipv4-bytes.pl \
	tests/rwaggbag-dipv4-bytes.pl \
	tests/rwaggbag-dipv4-packets.pl \
	tests/rwaggbag-sipv6-bytes.pl \
	tests/rwaggbag-dipv6-packets.pl \
	tests/rwaggbag-dport-all.pl \
	tests/rwaggbag-stime-packets-records.pl \
	tests/rwaggbag-elapsed-bytes.pl \
	tests/rwaggbag-etime.pl \
	tests/rwaggbag-stime-proto.pl \
	tests/rwaggbag-collection-point.pl \
	tests/rwaggbag-proto.pl \
	tests/rwaggbag-no-title.pl \
	tests/rwaggbag-no-column.pl \
	tests/rwaggbag-delimited.pl \
	tests/rwaggbag-empty-input.pl \
	tests/rwaggbag-empty-input-xargs.pl \
	tests/rwaggbag-multiple-inputs.pl \
	tests/rwaggbag-copy-input.pl \
	tests/rwaggbag-stdin.pl \
	tests/rwaggbag-icmpTypeCode.pl \
	tests/rwaggbag-dport-icmp.pl \
	tests/rwaggbag-icmp-dport.pl \
	tests/rwaggbag-country-code-sip.pl \
	tests/rwaggbag-country-code-dip.pl \
	tests/rwaggbag-country-code-sip-v6.pl \
	tests/rwaggbag-country-code-dip-v6.pl \
	tests/rwaggbagbuild-no-switches.pl \
	tests/rwaggbagbuild-null-input.pl \
	tests/rwaggbagbuild-no-files.pl \
	tests/rwaggbagbuild-ports-proto.pl \
	tests/rwaggbagbuild-ports-proto-v6.pl \
	tests/rwaggbagbuild-sipv4-bytes.pl \
	tests/rwaggbagbuild-dipv4-bytes.pl \
	tests/rwaggbagbuild-dipv4-packets.pl \
	tests/rwaggbagbuild-sipv6-bytes.pl \
	tests/rwaggbagbuild-stime-packets-records.pl \
	tests/rwaggbagbuild-etime.pl \
	tests/rwaggbagbuild-stime-proto.pl \
	tests/rwaggbagbuild-collection-point.pl \
	tests/rwaggbagbuild-country-code-sip.pl \
	tests/rwaggbagbuild-country-code-dip.pl \
	tests/rwaggbagbuild-country-code-aggbagcat.pl \
	tests/rwaggbagcat-null-input.pl \
	tests/rwaggbagtool-null-input.pl \
	tests/rwaggbagtool-ports-proto-remove.pl \
	tests/rwaggbagtool-ports-proto-select.pl \
	tests/rwaggbagtool-to-ipset-sipv4.pl \
	tests/rwaggbagtool-to-ipset-dipv6.pl \
	tests/rwaggbagtool-to-bag-sport-byte.pl \
	tests/rwaggbagtool-add-bags.pl \
	tests/rwaggbagtool-subtract-bags.pl \
	tests/rwaggbagtool-sip-dip-select-insert.pl \
	tests/rwaggbagtool-sip-dip-remove-insert.pl \
	tests/rwaggbagtool-subtract-self-aggbag.pl \
	tests/rwaggbagtool-subtract-self-to-bag.pl \
	tests/rwaggbagtool-subtract-self-to-ipset.pl \
	tests/rwaggbagtool-min-max-empty.pl \
	tests/rwaggbagtool-min-sport-1024.pl \
	tests/rwaggbagtool-max-ports-1024.pl \
	tests/rwaggbagtool-min-bytes-100000.pl \
	tests/rwaggbagtool-sip-intersect-10.pl \
	tests/rwaggbagtool-dip-complement-10.pl \
	tests/rwaggbagtool-country-code-sip-filter.pl \
	tests/rwaggbagtool-country-code-dip.pl \
	tests/rwaggbagtool-country-code-sip-v4v6.pl

