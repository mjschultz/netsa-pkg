# RCSIDENT("$SiLK: Makefile.am 17098e373e5f 2018-03-22 21:54:11Z mthomas $");

BUILT_SOURCES = $(INFOMODEL_REGISTRY_INCLUDE_FILES)
EXTRA_DIST = $(INFOMODEL_REGISTRIES) $(INFOMODEL_REGISTRY_INCLUDE_FILES)
XML2FIXBUF = $(srcdir)/../xml2fixbuf.xslt

RUN_XSLTPROC =  \
    $(AM_V_GEN) \
    $(XSLTPROC) --stringparam array-name 'infomodel_array_static_$*' \
        -o $@  $(XML2FIXBUF) $< || { rm -f $@ ; exit 1 ; }

$(INFOMODEL_REGISTRY_INCLUDE_FILES): $(XML2FIXBUF)
SUFFIXES = .xml .i
.xml.i:
	$(RUN_XSLTPROC)
