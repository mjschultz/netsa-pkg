/*
** Copyright (C) 2009-2020 by Carnegie Mellon University.
**
** @OPENSOURCE_LICENSE_START@
** See license information in ../../LICENSE.txt
** @OPENSOURCE_LICENSE_END@
*/

#include <silk/silk.h>

RCSIDENT("$SiLK: app-mismatch.c ef14e54179be 2020-04-14 21:57:45Z mthomas $");

#include <silk/skplugin.h>
#include <silk/rwrec.h>


/*
 *    This is a simple plug-in that can be used by rwfilter to find
 *    flows where the 'application' field does not match the value in
 *    the source or destination port.
 *
 *    Note that this plug-in will FAIL traffic where the application
 *    field is 0, and it will FAIL traffic that is neither TCP or UDP.
 *
 *    Mark Thomas
 *    September 2009
 */


/* DEFINES AND TYPEDEFS */

/* Plugin protocol version */
#define PLUGIN_API_VERSION_MAJOR 1
#define PLUGIN_API_VERSION_MINOR 0


/* FUNCTION DECLARATIONS */

static skplugin_err_t check(const rwRec *rwrec, void *cbdata, void **extra);


/* FUNCTION DEFINITIONS */

/* the registration function called by skplugin.c */
skplugin_err_t
SKPLUGIN_SETUP_FN(
    uint16_t            major_version,
    uint16_t            minor_version,
    void        UNUSED(*pi_data))
{
    skplugin_err_t rv;
    skplugin_callbacks_t regdata;

    /* Check API version */
    rv = skpinSimpleCheckVersion(major_version, minor_version,
                                 PLUGIN_API_VERSION_MAJOR,
                                 PLUGIN_API_VERSION_MINOR,
                                 skAppPrintErr);
    if (rv != SKPLUGIN_OK) {
        return rv;
    }

    /* Register the function to use for filtering */
    memset(&regdata, 0, sizeof(regdata));
    regdata.filter = check;
    return skpinRegFilter(NULL, &regdata, NULL);
}


/*
 *  status = check(rwrec, data, NULL);
 *
 *    Check whether 'rwrec' passes the filter.  Return
 *    SKPLUGIN_FILTER_PASS if it does; SKPLUGIN_FILTER_FAIL otherwise.
 *
 *    Pass when application is non-zero and protocol is TCP or UDP and
 *    application is not equal to the sPort and the dPort
 */
static skplugin_err_t
check(
    const rwRec            *rwrec,
    void            UNUSED(*cbdata),
    void           UNUSED(**extra))
{
    if ((rwRecGetApplication(rwrec) != 0)
        && ((rwRecGetProto(rwrec) == 17)
            || (rwRecGetProto(rwrec) == 6))
        && (rwRecGetApplication(rwrec) != rwRecGetSPort(rwrec))
        && (rwRecGetApplication(rwrec) != rwRecGetDPort(rwrec)))
    {
        return SKPLUGIN_FILTER_PASS;
    }
    return SKPLUGIN_FILTER_FAIL;
}


/*
** Local Variables:
** mode:c
** indent-tabs-mode:nil
** c-basic-offset:4
** End:
*/
