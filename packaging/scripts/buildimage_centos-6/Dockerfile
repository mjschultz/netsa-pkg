FROM centos:6
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
    lzo-devel \
    make \
    rpm-build \
    ruby \
    ruby-devel \
    rubygems \
    zlib-devel

RUN gem install json -v 1.8.3
RUN gem install ffi -v 1.9.18
RUN gem install fpm -v 1.4.0

ADD ["./libfixbuf-src/", "/netsa-pkg/libfixbuf-src/"]
ADD ["./yaf-src/", "/netsa-pkg/yaf-src/"]
ADD ["./silk-src/", "/netsa-pkg/silk-src/"]
ADD ["./packaging/", "/netsa-pkg/packaging/"]
ADD ["./Makefile", "/netsa-pkg/"]

WORKDIR /netsa-pkg
