VERSION = 20160228
PROC_ARC ?= amd64
TARGET_ROOT = $(shell pwd)/packaging/root
YAF_PREFIX = ${TARGET_ROOT}/opt/yaf
SILK_PREFIX = ${TARGET_ROOT}/opt/silk
LIBFIXBUF_PREFIX = ${TARGET_ROOT}/opt/libfixbuf

libfixbuf:
	mkdir -p ${LIBFIXBUF_PREFIX}
	(cd libfixbuf-src; \
		autoreconf -if; \
		./configure --prefix="${LIBFIXBUF_PREFIX}")
	make -C libfixbuf-src
	make -C libfixbuf-src install

yaf:
	mkdir -p ${YAF_PREFIX}
	(cd yaf-src; \
		autoreconf -if; \
		PKG_CONFIG_PATH="${LIBFIXBUF_PREFIX}/lib/pkgconfig" ./configure \
			--prefix="${YAF_PREFIX}")
	make -C yaf-src
	make -C yaf-src install

silk:
	mkdir -p ${SILK_PREFIX}
	(cd silk-src; \
		autoreconf -if; \
		./configure \
			--with-libfixbuf="${LIBFIXBUF_PREFIX}/lib/pkgconfig" \
			--enable-ipv6 \
			--prefix="${SILK_PREFIX}")
	make -C silk-src
	make -C silk-src install

deb:
	mkdir -p packaging/output
	fpm \
		-s dir \
		-t deb \
		-n netsa-pkg \
		-v ${VERSION} \
		-p packaging/output/netsa-pkg.deb \
		-a ${PROC_ARC} \
		--category admin \
		--force \
		--deb-compression bzip2 \
		--description "YAF and SiLK from CERT NetSA Security Suite" \
		--license "GPL version 2, LGPL version 2.1" \
		--depends libglib2.0-0 \
		--depends liblzo2-2 \
		--depends libpcap0.8 \
		--depends zlib1g \
		--after-install packaging/scripts/postinst.sh \
		--after-remove packaging/scripts/postrm.sh \
		--deb-no-default-config-files \
		packaging/root/=/

rpm:
	mkdir -p packaging/output
	fpm \
		-s dir \
		-t rpm \
		-n netsa-pkg \
		-v ${VERSION} \
		-p packaging/output/netsa-pkg.rpm \
		-a ${PROC_ARC} \
		--category admin \
		--force \
		--rpm-compression bzip2 \
		--description "YAF and SiLK from CERT NetSA Security Suite" \
		--license "GPL version 2, LGPL version 2.1" \
		--depends glib2 \
		--depends libpcap \
		--depends libtool-ltdl \
		--depends lzo \
		--depends zlib \
		--after-install packaging/scripts/postinst.sh \
		--after-remove packaging/scripts/postrm.sh \
		packaging/root/=/

clean:
	git clean -dxf
