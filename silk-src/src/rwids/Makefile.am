# RCSIDENT("$SiLK: Makefile.am 9ebd52e51160 2014-05-27 13:15:06Z mthomas $")

# Installed Targets

if HAVE_PYTHONBIN
bin_SCRIPTS = rwidsquery
endif

EXTRA_DIST = rwidsquery.pod
if HAVE_POD2MAN
if HAVE_PYTHONBIN
man1_MANS = rwidsquery.1
endif
endif


# Build Rules

AM_CPPFLAGS = $(SK_SRC_INCLUDES) $(SK_CPPFLAGS)
AM_CFLAGS = $(WARN_CFLAGS) $(SK_CFLAGS)
AM_LDFLAGS = $(SK_LDFLAGS) $(STATIC_APPLICATIONS)

EXTRA_DIST += rwidsquery.in

make_rwidsquery_edit = sed \
  -e 's|@PYTHON[@]|$(PYTHON)|g' \
  -e 's|@PACKAGE_STRING[@]|$(PACKAGE_STRING)|g' \
  -e 's|@PACKAGE_BUGREPORT[@]|$(PACKAGE_BUGREPORT)|g' \
  -e 's|@rwfilter[@]|'$${my_rwfilter}'|g'

MAKE_RWIDSQUERY = $(AM_V_GEN) \
  rm -f $@ $@.tmp ; \
  srcdir='' ; \
  test -f ./$@.in || srcdir=$(srcdir)/ ; \
  my_rwfilter=`echo rwfilter | sed -e '$(transform)'` ; \
  $(make_rwidsquery_edit) "$${srcdir}$@.in" >$@.tmp && \
  chmod +x $@.tmp && \
  mv $@.tmp $@

rwidsquery: Makefile rwidsquery.in
	$(MAKE_RWIDSQUERY)

MOSTLYCLEANFILES = rwidsquery.tmp
CLEANFILES = rwidsquery

# Global Rules
include $(top_srcdir)/build.mk


# Tests

# Required files; variables defined in ../../build.mk
check_DATA = $(SILK_TESTSDIR)

EXTRA_DIST += $(TESTS)

TESTS = \
	tests/rwidsquery-help.pl \
	tests/rwidsquery-version.pl \
	tests/rwidsquery-lone-command.pl \
	tests/rwidsquery-no-input.pl \
	tests/rwidsquery-fast.pl \
	tests/rwidsquery-full.pl \
	tests/rwidsquery-rule.pl \
	tests/rwidsquery-rule-no-date.pl
