## Introduction

This repository contains scripts to build packages for the [CERT NetSA Security Suite](https://tools.netsa.cert.org/)
tools [YAF](https://tools.netsa.cert.org/yaf/index.html) and [SiLK](https://tools.netsa.cert.org/silk/index.html).

## Building

In order to build the packages you will need:

Ubuntu systems | RHEL systems
-------------|-------------
autoconf | autoconf
automake | automake
build-essential | gcc
libglib2.0-dev | glib2-devel
libpcap-dev | libpcap-devel
libtool | libtool
libltdl-dev | libtool-ltdl-devel
liblzo2-dev | lzo-devel
make | make
rpm | rpm-build
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
* __[SiLK](silk-src/COPYING)__: GNU General Public License, version 2
* __[libfixbuf](libfixbuf-src/COPYING)__: GNU Lesser General Public License, version 2.1

The packaging scripts are governed by the [Apache License, version 2.0](http://www.apache.org/licenses/LICENSE-2.0).
