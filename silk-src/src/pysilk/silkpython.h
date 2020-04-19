/*
** Copyright (C) 2011-2020 by Carnegie Mellon University.
**
** @OPENSOURCE_LICENSE_START@
** See license information in ../../LICENSE.txt
** @OPENSOURCE_LICENSE_END@
*/
#ifndef _SILKPYTHON_H
#define _SILKPYTHON_H
#ifdef __cplusplus
extern "C" {
#endif

#include <silk/silk.h>

RCSIDENTVAR(rcsID_SILKPYTHON_H, "$SiLK: silkpython.h ef14e54179be 2020-04-14 21:57:45Z mthomas $");

/*
 *  silkpython.h
 *
 *    Declare the function that applications which use PySiLK as a
 *    plug-in must call to initialize Python.
 *
 *    This is a private header required for building.  It is not to be
 *    installed.
 */

#include <silk/skplugin.h>


/*
 *    Function defined in silkpython.c that is used by SiLK
 *    applications to load the PySiLK plugin.  Nothing in this
 *    directory calls this, but it is here to avoid a gcc warning.
 */
skplugin_err_t
skSilkPythonAddFields(
    uint16_t            major_version,
    uint16_t            minor_version,
    void               *data);

#ifdef __cplusplus
}
#endif
#endif /* _SILKPYTHON_H */

/*
** Local Variables:
** mode:c
** indent-tabs-mode:nil
** c-basic-offset:4
** End:
*/
