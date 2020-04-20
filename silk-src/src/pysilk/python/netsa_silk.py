# Copyright 2008-2020 by Carnegie Mellon University

# @OPENSOURCE_LICENSE_START@
# See license information in ../../../LICENSE.txt
# @OPENSOURCE_LICENSE_END@

"""
The netsa_silk module contains a shared API for working with common
Internet data in both netsa-python and PySiLK.  If netsa-python is
installed but PySiLK is not, the less efficient but more portable
pure-Python version of this functionality that is included in
netsa-python is used.  If PySiLK is installed, then the
high-performance C version of this functionality that is included in
PySiLK is used.
"""

# This module exists only to import the needed functionality from
# either PySiLK (via the internal silk._netsa_silk module), or
# netsa-python (via the internal netsa._netsa_silk module) and then
# re-export it.  All information (including the list of symbols to
# export) comes from the module providing the functionality, so that
# this module will never have to change and may remain common to both
# PySiLK and netsa-python installs.  (That is: Since it's always the
# same, it's acceptable for both sources to install the file without
# fear of overwriting each other.

import os

try:
    if os.getenv("NETSA_SILK_DISABLE_PYSILK"):
        raise ImportError("netsa_silk PySiLK support disabled")
    import silk._netsa_silk
    from silk._netsa_silk import *
    __all__ = silk._netsa_silk.__all__
except ImportError:
    try:
        if os.getenv("NETSA_SILK_DISABLE_NETSA_PYTHON"):
            raise ImportError("netsa_silk netsa-python support disabled")
        import netsa._netsa_silk
        from netsa._netsa_silk import *
        __all__ = netsa._netsa_silk.__all__
    except ImportError:
        import_error = ImportError(
            "Cannot locate netsa_silk implementation during import")
        raise import_error
