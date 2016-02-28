## Introduction

This repository contains scripts to build packages for the [CERT NetSA Security Suite](https://tools.netsa.cert.org/)
tools [YAF](https://tools.netsa.cert.org/yaf/index.html) and [SiLK](https://tools.netsa.cert.org/silk/index.html).

As of 2016-02-28 this project is experimental and not recommended for use.

## Building

In order to build the packages you will need:

Ubuntu systems | RHEL 6 systems
-------------|-------------
autoconf | autoconf
automake | automake
build-essential | gcc
libglib2.0-dev | glib2-devel
libpcap-dev | libpcap-devel
libtool | libtool, libtool-ltdl-devel
liblzo2-dev | lzo-devel
make | make
rpm | rpm-build
zlib1g, zlib1g-dev | zlib-devel

To build the .deb and .rpm files you will need a working Ruby installation
capable of installing the [`fpm`](https://github.com/jordansissel/fpm/wiki) gem.

## Credits and licenses

Contained here are mirrors of the YAF, SiLK, and libfixbuf packages.
These packages are governed by the following licenses:

* __[YAF](yaf-src/COPYING)__: GNU General Public License, version 2
* __[SiLK](silk-src/COPYING)__: GNU General Public License, version 2
* __[libfixbuf](libfixbuf-src/COPYING)__: GNU Lesser General Public License, version 2.1
