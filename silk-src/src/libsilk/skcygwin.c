/*
** Copyright (C) 2011-2020 by Carnegie Mellon University.
**
** @OPENSOURCE_LICENSE_START@
** See license information in ../../LICENSE.txt
** @OPENSOURCE_LICENSE_END@
*/

/*
**  skcygwin.c
**
**    Support for getting the default SiLK Data Root directory from
**    the Windows Registry
**
**    July 2011
**
*/

#include <silk/silk.h>

RCSIDENT("$SiLK: skcygwin.c ef14e54179be 2020-04-14 21:57:45Z mthomas $");

#ifdef __CYGWIN__
/*
 *    Microsoft says to define _WIN32_WINNT to get the right windows
 *    API version, but under Cygwin, you need to define WINVER - which
 *    are related, (but not the same?).  They are believed to be the
 *    same under Cygwin
 */
#define _WIN32_WINNT 0x0600
#define WINVER 0x0600
#include <windows.h>
#include "skcygwin.h"

/* LOCAL DEFINES AND TYPEDEFS */

/* used for converting a Windows path to a Cygwin path */
#define CYGWIN_PATH_PREFIX              "/cygdrive/"

/* path to return when the registry key does not exist */
#define SILK_DEFAULT_CYGWIN_DATA_DIR    CYGWIN_PATH_PREFIX "c/data"


/* sizes for buffers when querying the registry */
#define BUFFER_SIZE_INITIAL     8192
#define BUFFER_SIZE_INCREMENT   4096


/* FUNCTION DEFINITIONS */

/**
 *  windowsToCygwinPath
 *
 *    converts a "normal" windows path "C:\Windows\" into an
 *    equivalent cygwin path "/cygdrive/c/Windows/"
 *
 *    @param buf a character buffer to be filled with the cygwin path
 *
 *    @param bufsize the size of the buffer
 *
 *    @param win_path a character string containing a windows path
 *
 *    @return a pointer to buf on success; on error this function
 *    returns NULL
 */
static char *
windowsToCygwinPath(
    char               *buf,
    size_t              bufsize,
    const char         *win_path)
{
    int len;
    char *cp;
    char *ep;

    assert(buf);
    assert(win_path);

    len = snprintf(buf, bufsize, "%s%s",
                   CYGWIN_PATH_PREFIX, win_path);
    if ((size_t)len > bufsize) {
        /* registry value too large to fit in 'buf' */
        return NULL;
    }

    /* skip over the cygwin prefix so we're at the start of the windows path */
    cp = buf + strlen(CYGWIN_PATH_PREFIX);

    /* try to find the drive prefix, e.g. c: or d: or z: */
    ep = strchr(cp, ':');
    if (NULL == ep) {
        /* it's a relative path, run with it? */
        return NULL;
    }

    /* downcase drive letter */
    while (cp < ep) {
        *cp = tolower((int)*cp);
        ++cp;
    }

    /* change ':' to '/' */
    *cp = '/';

    /* convert remaining '\' to '/' */
    while ((cp = strchr(cp, '\\')) != NULL) {
        *cp = '/';
    }

    return buf;
}



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
    size_t              bufsize)
{
    char *data_buffer;
    char *rv;
    DWORD buffer_size;
    DWORD rc;

    buffer_size = BUFFER_SIZE_INITIAL;
    data_buffer = (char*)malloc(sizeof(char) * buffer_size);
    if (NULL == data_buffer) {
        /* error couldn't allocate memory */
        return NULL;
    }

    /* keeping asking the registry for the value until we have a
     * buffer big enough to hold it */
    do {
        rc = RegQueryValueEx(HKEY_LOCAL_MACHINE,
                             SILK_WINDOWSREG_DATA_DIR_KEY_PATH, NULL,
                             NULL, (PVOID)data_buffer, &buffer_size);
        if (ERROR_MORE_DATA == rc) {
            /* buffer is not large enough; grow it.  also, since
             * 'buffer_size' contains the required size, I'm not sure
             * why we add additional space onto it.... */
            char *oldbuf = data_buffer;
            buffer_size += BUFFER_SIZE_INCREMENT;
            data_buffer = (char*)realloc(data_buffer, buffer_size);
            if (NULL == data_buffer) {
                /* realloc failed */
                free(oldbuf);
                return NULL;
            }
        }
    } while (ERROR_MORE_DATA == rc);

    if (ERROR_SUCCESS != rc) {
        free(data_buffer);
        return NULL;
    }

    if (0 == buffer_size) {
        /* What makes sense to do when we can't find the registry
         * entry?  In this case, we return a "sane" default for
         * windows. */
        free(data_buffer);
        strncpy(buf, SILK_DEFAULT_CYGWIN_DATA_DIR, bufsize);
        if ('\0' != buf[bufsize-1]) {
            return NULL;
        }
        return buf;
    }

    rv = windowsToCygwinPath(buf, bufsize, data_buffer);
    free(data_buffer);
    return rv;
}


#ifdef STANDALONE_TEST_HARNESS
/*
 */
int main(int UNUSED(argc), char UNUSED(**argv))
{
    char buf[PATH_MAX];
    char *b;

    b = skCygwinGetDataRootDir(buf, sizeof(buf));

    printf("registry string is\n    %s => \"%s\"\n",
           NETSA_WINDOWSREG_REGHOME, (b ? b : "NULL"));
    return 0;
}

#endif  /* STANDALONE_TEST_HARNESS */
#endif  /* __CYGWIN__ */

/*
** Local Variables:
** mode:c
** indent-tabs-mode:nil
** c-basic-offset:4
** End:
*/
