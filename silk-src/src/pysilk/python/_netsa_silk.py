#######################################################################
# Copyright (C) 2011-2020 by Carnegie Mellon University.
#
# @OPENSOURCE_LICENSE_START@
# See license information in ../../../LICENSE.txt
# @OPENSOURCE_LICENSE_END@
#
#######################################################################

#######################################################################
# $SiLK: _netsa_silk.py ef14e54179be 2020-04-14 21:57:45Z mthomas $
#######################################################################

"""
The netsa_silk module contains a shared API for working with common
Internet data in both netsa-python and PySiLK.  If netsa-python is
installed but PySiLK is not, the less efficient but more portable
pure-Python version of this functionality that is included in
netsa-python is used.  If PySiLK is installed, then the
high-performance C version of this functionality that is included in
PySiLK is used.
"""

# This module provides the symbols exported by PySiLK for the
# netsa_silk API.  It exists to rename PySiLK symbols that have a
# different name from the netsa_silk symbols, and to constrain the set
# of PySiLK symbols that are exported.  If a new symbol is added (to
# provide a new feature), it need only be added here and it will
# automatically be exported by netsa_silk.

from silk import (
    ipv6_enabled as has_IPv6Addr,
    IPAddr, IPv4Addr, IPv6Addr,
    IPSet as ip_set,
    IPWildcard,
    TCPFlags,

    TCP_FIN, TCP_SYN, TCP_RST, TCP_PSH, TCP_ACK, TCP_URG, TCP_ECE, TCP_CWR,
    silk_version
)

# PySiLK API version
__version__ = "1.0"

# Implementation version
__impl_version__ = " ".join(["SiLK", silk_version()])

__all__ = """
    has_IPv6Addr
    IPAddr IPv4Addr IPv6Addr
    ip_set
    IPWildcard
    TCPFlags

    TCP_FIN TCP_SYN TCP_RST TCP_PSH TCP_ACK TCP_URG TCP_ECE TCP_CWR

    __version__
    __impl_version__
""".split()
