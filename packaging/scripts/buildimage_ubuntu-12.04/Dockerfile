FROM ubuntu:12.04
MAINTAINER Bo Bayles <bbayles@gmail.com>

RUN apt-get update && apt-get install -y \
    autoconf \
    automake \
    build-essential \
    git \
    libglib2.0-dev \
    libpcap-dev \
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

RUN gem install fpm

RUN git clone https://github.com/bbayles/netsa-pkg.git

WORKDIR /netsa-pkg

RUN make libfixbuf && make yaf && make silk && make deb

ENTRYPOINT ["/bin/bash"]
