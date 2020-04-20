/*
** Copyright (C) 2011-2020 by Carnegie Mellon University.
**
** @OPENSOURCE_LICENSE_START@
** See license information in ../../LICENSE.txt
** @OPENSOURCE_LICENSE_END@
*/

/*
**  skcygwin.h
**
**    Support for getting the default SiLK Data Root directory from
**    the Windows Registry
**
**    July 2011
**
*/
#ifndef _SKCYGWIN_H
#define _SKCYGWIN_H
#ifdef __cplusplus
extern "C" {
#endif

#include <silk/silk.h>

RCSIDENTVAR(rcsID_SKCYGWIN_H, "$SiLK: skcygwin.h ef14e54179be 2020-04-14 21:57:45Z mthomas $");

#ifdef __CYGWIN__

/* registry location/key definitions */
#ifndef  NETSA_WINDOWSREG_REGHOME
#ifndef  SK_CYGWIN_TESTING

#define NETSA_WINDOWSREG_REGHOME        "Software\\CERT\\NetSATools"
#define SILK_WINDOWSREG_DATA_DIR_KEY    "SilkDataDir"
#define SILK_WINDOWSREG_DATA_DIR_KEY_PATH                               \
    (NETSA_WINDOWSREG_REGHOME "\\" SILK_WINDOWSREG_DATA_DIR_KEY)

#else  /* SK_CYGWIN_TESTING */

/* values for testing the code without having to modify registry */

#define NETSA_WINDOWSREG_REGHOME                        \
    "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion"
#define SILK_WINDOWSREG_DATA_DIR_KEY  "SystemRoot"
#define SILK_WINDOWSREG_DATA_DIR_KEY_PATH                               \
    (NETSA_WINDOWSREG_REGHOME "\\" SILK_WINDOWSREG_DATA_DIR_KEY)

#endif  /* SK_CYGWIN_TESTING */
#endif  /* NETSA_WINDOWSREG_REGHOME */


/**
 *  skCygwinGetDataRootDir
 *
 *    Gets the data directory defined at INSTALLATION time on Windows
 *    machines via reading the windows registry.  Caches the result in
 *    a file static.
 *
 *    @param buf a character buffer to be filled with the directory
 *
 *    @param bufsize the size of the buffer
 *
 *    @return a pointer to buf is returned on success; on error this
 *    function returns NULL
 *
 *    @note must call skCygwinClean to get rid of the memory for the
 *    cached result
 */
const char *
skCygwinGetDataRootDir(
    char               *buf,
    size_t              bufsize);

#endif /* __CYGWIN__ */
#ifdef __cplusplus
}
#endif
#endif /* _SKCYGWIN_H */

/*
** Local Variables:
** mode:c
** indent-tabs-mode:nil
** c-basic-offset:4
** End:
*/
