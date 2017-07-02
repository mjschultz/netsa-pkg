# RCSIDENT($SiLK: Makefile.am b6bece4ab21f 2016-12-02 17:01:44Z mthomas $)

SILK_SRC_MAKE_BASE_LIBS = \
	 libsilk \
	 libflowsource

SILK_SRC_MAKE_RWFILTER = \
         rwfilter \
         plugins

SILK_SRC_MAKE_PYTHON_PLUGIN = \
         pysilk

SILK_SRC_MAKE_PACKING_TOOLS = \
         rwflowpack \
         rwpollexec \
         sendrcv

SILK_SRC_MAKE_ANALYSIS_TOOLS = \
         num2dot \
         rwaddrcount \
         rwappend \
         rwbag \
         rwcat \
         rwcompare \
         rwconvert \
         rwcount \
         rwcut \
         rwfileinfo \
         rwgroup \
         rwipfix \
         rwmatch \
         rwnetmask \
         rwpmap \
         rwrandomizeip \
         rwrecgenerator \
         rwresolve \
         rwscan \
         rwset \
         rwsiteinfo \
         rwsort \
         rwsplit \
         rwstats \
         rwswapbytes \
         rwtotal \
         rwtuc

# Conditional analysis tools
SILK_SRC_MAKE_ANALYSIS_IPA = rwipa
SILK_SRC_MAKE_ANALYSIS_PCAP = rwptoflow
SILK_SRC_MAKE_ANALYSIS_PYTHONBIN = rwids

# Experimental code



SUBDIRS = $(SILK_SRC_MAKE_BASE_LIBS)

if BUILD_ANALYSIS_TOOLS


if HAVE_PYTHON
SUBDIRS += $(SILK_SRC_MAKE_PYTHON_PLUGIN)
endif

SUBDIRS += $(SILK_SRC_MAKE_RWFILTER)

SUBDIRS += $(SILK_SRC_MAKE_ANALYSIS_TOOLS)

if HAVE_PCAP
SUBDIRS += $(SILK_SRC_MAKE_ANALYSIS_PCAP)
endif
if HAVE_PYTHONBIN
SUBDIRS += $(SILK_SRC_MAKE_ANALYSIS_PYTHONBIN)
endif
if HAVE_IPA
SUBDIRS += $(SILK_SRC_MAKE_ANALYSIS_IPA)
endif

if BUILD_PACKING_TOOLS
SUBDIRS += $(SILK_SRC_MAKE_PACKING_TOOLS)
endif

else !BUILD_ANALYSIS_TOOLS


if BUILD_PACKING_TOOLS
SUBDIRS += \
	$(SILK_SRC_MAKE_PACKING_TOOLS)
endif

endif !BUILD_ANALYSIS_TOOLS

SUBDIRS += include/silk

DIST_SUBDIRS = \
	 $(SILK_SRC_MAKE_BASE_LIBS) \
	 include/silk \
	 $(SILK_SRC_MAKE_RWFILTER) \
	 $(SILK_SRC_MAKE_PYTHON_PLUGIN) \
	 $(SILK_SRC_MAKE_PACKING_TOOLS) \
	 $(SILK_SRC_MAKE_ANALYSIS_TOOLS) \
	 $(SILK_SRC_MAKE_ANALYSIS_PCAP) \
	 $(SILK_SRC_MAKE_ANALYSIS_IPA) \
	 $(SILK_SRC_MAKE_ANALYSIS_PYTHONBIN)
