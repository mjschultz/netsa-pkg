## Introduction

This repository contains scripts to build packages for the [CERT NetSA Security Suite](https://tools.netsa.cert.org/)
tools [YAF](https://tools.netsa.cert.org/yaf/index.html) and [SiLK](https://tools.netsa.cert.org/silk/index.html).

## Quick build

If you have [Docker 1.10.2 or later](https://docs.docker.com/engine/installation/linux/ubuntulinux/) installed then you can easily generate a .deb or a .rpm package. From the source directory:
* `make build_ubuntu` will generate a .deb using a Ubuntu 16.04 container
* `make build_centos` will generate a .rpm using a CentOS 6 contianer

The package files will go to the `packaging/output/` directory. You should be able to distribute them to other machines with compatible libraries.

## Building for your system

In order to build the packages you will need:

Ubuntu systems | RHEL systems
-------------|-------------
autoconf | autoconf
automake | automake
build-essential | gcc
libglib2.0-dev | glib2-devel
libpcap-dev | libpcap-devel
libsnappy-dev | snappy-devel
libtool | libtool
libltdl-dev | libtool-ltdl-devel
liblzo2-dev | lzo-devel
make | make
rpm | rpm-build
xsltproc | libxslt
zlib1g, zlib1g-dev | zlib-devel

To build the .deb and .rpm files you will need a working Ruby installation
capable of installing the [`fpm`](https://github.com/jordansissel/fpm/wiki) gem.

From the project directory issue these commands:

* `make libfixbuf`
* `make yaf`
* `make silk`
* `make deb` or `make rpm`

## Credits and licenses

Contained here are mirrors of the YAF, SiLK, and libfixbuf packages.
These packages are governed by the following licenses:

* __[YAF](yaf-src/COPYING)__: GNU General Public License, version 2
* __[SiLK](silk-src/LICENSE.txt)__: GNU General Public License, version 2
* __[libfixbuf](libfixbuf-src/COPYING)__: GNU Lesser General Public License, version 2.1

The packaging scripts are governed by the [Apache License, version 2.0](http://www.apache.org/licenses/LICENSE-2.0).
