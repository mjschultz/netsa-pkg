/*
** Copyright (C) 2011-2017 by Carnegie Mellon University.
**
** @OPENSOURCE_LICENSE_START@
** See license information in ../../LICENSE.txt
** @OPENSOURCE_LICENSE_END@
*/

/*
**  Common functions for pysilk and silkpython
**
*/


#include <Python.h>
#include <silk/silk.h>

RCSIDENT("$SiLK: pysilk_common.c efd886457770 2017-06-21 18:43:23Z mthomas $");

#include "pysilk_common.h"

/* FUNCTION DEFINITIONS */

PyObject *
bytes_from_string(
    PyObject           *obj)
{
    PyObject *bytes;

    if (PyBytes_Check(obj)) {
        Py_INCREF(obj);
        return obj;
    }
    bytes = PyUnicode_AsASCIIString(obj);
    return bytes;
}

PyObject *
bytes_from_wchar(
    const wchar_t      *wc)
{
    PyObject *bytes;
    PyObject *pstr = PyUnicode_FromWideChar(wc, -1);
    if (pstr == NULL) {
        return NULL;
    }
    bytes = bytes_from_string(pstr);
    Py_DECREF(pstr);

    return bytes;
}


/*
** Local Variables:
** mode:c
** indent-tabs-mode:nil
** c-basic-offset:4
** End:
*/
