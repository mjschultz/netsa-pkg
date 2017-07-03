MAINTAINER = "Bo Bayles <bbayles+netsa@gmail.com>"
VERSION = 20170325
PROC_ARC ?= amd64
TARGET_ROOT = $(shell pwd)/packaging/root
OUTPUT_DIR = $(shell pwd)/packaging/output

libfixbuf:
	mkdir -p ${TARGET_ROOT}
	(cd libfixbuf-src; \
		autoreconf -if; \
		./configure)
	make -C libfixbuf-src
	make -C libfixbuf-src install DESTDIR="${TARGET_ROOT}"

yaf:
	mkdir -p ${TARGET_ROOT}
	(cd yaf-src; \
		autoreconf -if; \
		./configure \
			PKG_CONFIG_PATH="${TARGET_ROOT}/usr/local/lib/pkgconfig" \
			CPPFLAGS="-I${TARGET_ROOT}/usr/local/include" \
			CFLAGS="-I${TARGET_ROOT}/usr/local/include" \
			LDFLAGS="-L${TARGET_ROOT}/usr/local/lib" \
			--enable-applabel \
			--enable-plugins)
	make -C yaf-src
	make -C yaf-src install DESTDIR="${TARGET_ROOT}"

silk:
	mkdir -p ${TARGET_ROOT}
	(cd silk-src; \
		autoreconf -if; \
		./configure \
			--with-libfixbuf="${TARGET_ROOT}/usr/local/lib/pkgconfig" \
			--enable-ipv6)
	make -C silk-src
	make -C silk-src install DESTDIR="${TARGET_ROOT}"

deb:
	mkdir -p ${OUTPUT_DIR}
	fpm \
		-s dir \
		-t deb \
		-n netsa-pkg \
		-v ${VERSION} \
		-p packaging/output/netsa-pkg.deb \
		-a ${PROC_ARC} \
		--maintainer ${MAINTAINER} \
		--category admin \
		--force \
		--deb-compression bzip2 \
		--description "YAF and SiLK from CERT NetSA Security Suite" \
		--license "GPL version 2, LGPL version 2.1" \
		--depends libglib2.0-0 \
		--depends liblzo2-2 \
		--depends libltdl7 \
		--depends libpcap0.8 \
		--depends zlib1g \
		--after-install packaging/scripts/postinst.sh \
		--after-remove packaging/scripts/postrm.sh \
		--deb-no-default-config-files \
		packaging/root/=/

rpm:
	mkdir -p ${OUTPUT_DIR}
	fpm \
		-s dir \
		-t rpm \
		-n netsa-pkg \
		-v ${VERSION} \
		-p packaging/output/netsa-pkg.rpm \
		-a ${PROC_ARC} \
		--maintainer ${MAINTAINER} \
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

build_deb:
	make libfixbuf
	make yaf silk
	make silk
	make deb

build_rpm:
	make libfixbuf
	make yaf silk
	make silk
	make rpm

build_ubuntu:
	$(eval IMAGE_ID = $(shell docker build --force-rm -q -f "packaging/scripts/buildimage_ubuntu-12.04/Dockerfile" .))
	docker run -v "${OUTPUT_DIR}:/netsa-pkg/packaging/output" $(IMAGE_ID) /usr/bin/make build_deb
	chmod -R 776 ${OUTPUT_DIR}/*

build_centos:
	$(eval IMAGE_ID = $(shell docker build --force-rm -q -f "packaging/scripts/buildimage_centos-6/Dockerfile" .))
	docker run -v "${OUTPUT_DIR}:/netsa-pkg/packaging/output" $(IMAGE_ID) /usr/bin/make build_rpm
	chmod -R 776 ${OUTPUT_DIR}/*

.PHONY: clean
clean:
	git clean -dxf
