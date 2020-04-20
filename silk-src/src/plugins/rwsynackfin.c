/*
** Copyright (C) 2001-2020 by Carnegie Mellon University.
**
** @OPENSOURCE_LICENSE_START@
** See license information in ../../LICENSE.txt
** @OPENSOURCE_LICENSE_END@
*/

/*
**  rwsynackfin.c
**
**    Pass web traffic and fail all other traffic.  For web traffic,
**    keep a count of the number/types of flags seen, and print
**    summary to stderr once processing is complete.
*/

#include <silk/silk.h>

RCSIDENT("$SiLK: rwsynackfin.c ef14e54179be 2020-04-14 21:57:45Z mthomas $");

#include <silk/skplugin.h>
#include <silk/rwrec.h>


/* DEFINES AND TYPEDEFS */

/* Plugin protocol version */
#define PLUGIN_API_VERSION_MAJOR 1
#define PLUGIN_API_VERSION_MINOR 0


/* LOCAL VARIABLES */

static uint32_t fin_count, ack_count, rst_count, syn_count;


/* FUNCTION DECLARATIONS */

static skplugin_err_t check(const rwRec *rwrec, void *cbdata, void **extra);
static skplugin_err_t summary(void *cbdata);


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

    syn_count = fin_count = ack_count = rst_count = 0;

    /* Register the function to use for filtering */
    memset(&regdata, 0, sizeof(regdata));
    regdata.cleanup = summary;
    regdata.filter = check;
    return skpinRegFilter(NULL, &regdata, NULL);
}


/*
 *  status = check(rwrec, data, NULL);
 *
 *    Check whether 'rwrec' passes the filter.  Return
 *    SKPLUGIN_FILTER_PASS if it does; SKPLUGIN_FILTER_FAIL otherwise.
 *
 *    Pass the filter if 80/tcp or 443/tcp
 */
static skplugin_err_t
check(
    const rwRec            *rwrec,
    void            UNUSED(*cbdata),
    void           UNUSED(**extra))
{
    if (rwRecGetProto(rwrec) != 6) {
        return SKPLUGIN_FILTER_FAIL;
    }
    if (rwRecGetDPort(rwrec) != 80 && rwRecGetDPort(rwrec) != 443) {
        return SKPLUGIN_FILTER_FAIL;
    }

    if (rwRecGetFlags(rwrec) == ACK_FLAG
        && rwRecGetPkts(rwrec) == 1
        && rwRecGetBytes(rwrec) == 40)
    {
        ack_count++;
        return SKPLUGIN_FILTER_PASS;
    }

    if (rwRecGetFlags(rwrec) & FIN_FLAG) {
        fin_count++;
    }
    if (rwRecGetFlags(rwrec) & RST_FLAG) {
        rst_count++;
    }
    if (rwRecGetFlags(rwrec) & SYN_FLAG) {
        syn_count++;
    }
    return SKPLUGIN_FILTER_PASS;
}


/*
 *  status = summary(cbdata);
 *
 *    Print a summary of the flows we've seen to stderr.
 */
static skplugin_err_t
summary(
    void        UNUSED(*cbdata))
{
    fprintf(stderr, ("WEB SYN %" PRIu32 "  FIN %" PRIu32
                     "  RST %" PRIu32 "  ACK %" PRIu32 "\n"),
            syn_count, fin_count, rst_count, ack_count);

    return SKPLUGIN_OK;
}


/*
** Local Variables:
** mode:c
** indent-tabs-mode:nil
** c-basic-offset:4
** End:
*/
