FROM centos:7
MAINTAINER Bo Bayles <bbayles@gmail.com>

RUN yum install -y \
    autoconf \
    automake \
    gcc \
    git \
    glib2-devel \
    libpcap-devel \
    libtool \
    libtool-ltdl-devel \
    libxslt \
    lzo-devel \
    make \
    rpm-build \
    ruby \
    ruby-devel \
    rubygems \
    snappy-devel \
    zlib-devel

RUN gem install ffi -v 1.9.18
RUN gem install fpm

ADD ["./libfixbuf-src/", "/netsa-pkg/libfixbuf-src/"]
ADD ["./yaf-src/", "/netsa-pkg/yaf-src/"]
ADD ["./silk-src/", "/netsa-pkg/silk-src/"]
ADD ["./packaging/", "/netsa-pkg/packaging/"]
ADD ["./Makefile", "/netsa-pkg/"]

WORKDIR /netsa-pkg
