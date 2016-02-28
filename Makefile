TARGET_ROOT = $(shell pwd)/root
YAF_PREFIX = ${TARGET_ROOT}/opt/yaf
SILK_PREFIX = ${TARGET_ROOT}/opt/silk
LIBFIXBUF_PREFIX = ${TARGET_ROOT}/opt/libfixbuf

libfixbuf:
	mkdir -p ${LIBFIXBUF_PREFIX}
	(cd libfixbuf-src; ./configure --prefix="${LIBFIXBUF_PREFIX}")
	make -C libfixbuf-src
	make -C libfixbuf-src install

yaf:
	mkdir -p ${YAF_PREFIX}
	(cd yaf-src; \
	./configure --prefix=${LIBFIXBUF_PREFIX})
	(cd yaf-src; \
	PKG_CONFIG_PATH="${LIBFIXBUF_PREFIX}/lib/pkgconfig" ./configure \
		--prefix="${YAF_PREFIX}")
	make -C yaf-src
	make -C yaf-src install

silk:
	;

deb:
	;

clean:
	git clean -dxf
