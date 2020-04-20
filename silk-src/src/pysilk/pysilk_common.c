/*
** Copyright (C) 2011-2020 by Carnegie Mellon University.
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

RCSIDENT("$SiLK: pysilk_common.c ef14e54179be 2020-04-14 21:57:45Z mthomas $");

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

#if PY_VERSION_HEX < 0x02060000

PyObject *
string_to_unicode(
    const char         *s)
{
    return PyUnicode_DecodeUTF8(s, strlen(s), "strict");
}

SK_DIAGNOSTIC_FORMAT_NONLITERAL_PUSH

PyObject *
format_to_unicode(
    const char         *s,
    ...)
{
    va_list ap;
    PyObject *str;
    PyObject *uni;

    va_start(ap, s);
    str = PyString_FromFormatV(s, ap);
    va_end(ap);

    if (str == NULL) {
        return NULL;
    }
    uni = PyUnicode_FromObject(str);
    Py_DECREF(str);

    return uni;
}

SK_DIAGNOSTIC_FORMAT_NONLITERAL_POP

#endif

/*
** Local Variables:
** mode:c
** indent-tabs-mode:nil
** c-basic-offset:4
** End:
*/
