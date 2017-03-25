# RCSIDENT("$SiLK: Makefile.am d82586e0bb8f 2016-12-13 22:17:26Z mthomas $");

# Installed Targets

pkglib_LTLIBRARIES = app-mismatch.la conficker-c.la cutmatch.la \
	 flowkey.la flowrate.la int-ext-fields.la uniq-distproto.la

EXTRA_DIST = flowkey.pod flowrate.pod int-ext-fields.pod silk-plugin.pod
if HAVE_POD2MAN
man3_MANS = flowkey.3 flowrate.3 int-ext-fields.3 silk-plugin.3
endif

noinst_LTLIBRARIES = skplugin-test.la rwslammer.la rwsynackfin.la

check_PROGRAMS =


# Flags used when building plug-ins to be installed.  (This variable
# must have some non-empty value when automake is run, so include the
# "-module" switch here.)
INSTALLED_PLUGIN_FLAGS = -module $(SILK_PLUGIN_LIBTOOL_FLAGS)

# For plug-ins that are "noinst" we need to specify an -rpath so that
# libtool will build the *.so file.
NOINST_PLUGIN_FLAGS = -module $(SILK_PLUGIN_LIBTOOL_FLAGS) -rpath $(pkglibdir)


# Build Rules

AM_CPPFLAGS = $(SK_SRC_INCLUDES) $(SK_CPPFLAGS)
AM_CFLAGS = $(WARN_CFLAGS) $(SK_CFLAGS)

app_mismatch_la_SOURCES = app-mismatch.c
app_mismatch_la_LDFLAGS = $(INSTALLED_PLUGIN_FLAGS)

conficker_c_la_SOURCES = conficker-c.c
conficker_c_la_LDFLAGS = $(INSTALLED_PLUGIN_FLAGS)

cutmatch_la_SOURCES = cutmatch.c
cutmatch_la_LDFLAGS = $(INSTALLED_PLUGIN_FLAGS)

flowkey_la_SOURCES = flowkey.c
flowkey_la_LDFLAGS = $(INSTALLED_PLUGIN_FLAGS)

flowrate_la_SOURCES = flowrate.c
flowrate_la_LDFLAGS = $(INSTALLED_PLUGIN_FLAGS)

int_ext_fields_la_SOURCES = int-ext-fields.c
int_ext_fields_la_LDFLAGS = $(INSTALLED_PLUGIN_FLAGS)

uniq_distproto_la_SOURCES = uniq-distproto.c
uniq_distproto_la_LDFLAGS = $(INSTALLED_PLUGIN_FLAGS)

rwslammer_la_SOURCES = rwslammer.c
rwslammer_la_LDFLAGS = $(NOINST_PLUGIN_FLAGS)

rwsynackfin_la_SOURCES = rwsynackfin.c
rwsynackfin_la_LDFLAGS = $(NOINST_PLUGIN_FLAGS)

skplugin_test_la_SOURCES = skplugin-test.c
skplugin_test_la_LDFLAGS = $(NOINST_PLUGIN_FLAGS)



# Global Rules
include $(top_srcdir)/build.mk
include $(top_srcdir)/lib-deps.mk
