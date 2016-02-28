TARGET_ROOT = $(shell pwd)/root
YAF_PREFIX = ${TARGET_ROOT}/opt/yaf
SILK_PREFIX = ${TARGET_ROOT}/opt/silk
LIBFIXBUF_PREFIX = ${TARGET_ROOT}/opt/libfixbuf

libfixbuf:
	mkdir -p ${LIBFIXBUF_PREFIX}
	(cd libfixbuf-src; ./configure --prefix=${LIBFIXBUF_PREFIX})
	make -C libfixbuf-src
	make -C libfixbuf-src install

yaf:
	;

silk:
	;

deb:
	;

clean:
	git clean -dxf
