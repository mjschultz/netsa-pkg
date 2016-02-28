## Introduction

This repository contains scripts to build packages for the [CERT NetSA Security Suite](https://tools.netsa.cert.org/)
tools [YAF](https://tools.netsa.cert.org/yaf/index.html) and [SiLK](https://tools.netsa.cert.org/silk/index.html).

As of 2016-02-28 this project is experimental and not recommended for use.

## Building

In order to build the .deb package you will need:

* The usual build tools (e.g. the `build-essential` package)
* The GLib 2.0, libpcap, and LZO libraries (the `libglib2.0-dev`, `libpcap-dev` and `liblzo2-dev` packages)
* `ruby` and the `fpm` gem

## Credits and licenses

Contained here are mirrors of the YAF, SiLK, and libfixbuf packages.
These packages are governed by the following licenses:

* __[YAF](yaf-src/COPYING)__: GNU General Public License, version 2
* __[SiLK](silk-src/COPYING)__:GNU General Public License, version 2
* __[libfixbuf](libfixbuf-src/COPYING)__: GNU Lesser General Public License, version 2.1
