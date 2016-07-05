FROM ubuntu:12.04
MAINTAINER Bo Bayles <bbayles@gmail.com>

RUN apt-get update && apt-get install -y \
    autoconf \
    automake \
    build-essential \
    git \
    libglib2.0-dev \
    libpcap0.8-dev \
    libtool \
    libltdl-dev \
    liblzo2-dev \
    make \
    rpm \
    ruby \
    ruby-dev \
    rubygems \
    zlib1g \
    zlib1g-dev

RUN gem install json -v 1.8.3
RUN gem install fpm -v 1.4.0

ADD ["./libfixbuf-src/", "/netsa-pkg/libfixbuf-src/"]
ADD ["./yaf-src/", "/netsa-pkg/yaf-src/"]
ADD ["./silk-src/", "/netsa-pkg/silk-src/"]
ADD ["./packaging/", "/netsa-pkg/packaging/"]
ADD ["./Makefile", "/netsa-pkg/"]

WORKDIR /netsa-pkg
