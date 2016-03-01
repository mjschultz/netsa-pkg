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

RUN gem install fpm

RUN git clone https://github.com/bbayles/netsa-pkg.git

WORKDIR /netsa-pkg

RUN make libfixbuf && make yaf && make silk && make rpm

ENTRYPOINT ["/bin/bash"]
