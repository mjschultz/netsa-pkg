/*
** Copyright (C) 2006-2020 by Carnegie Mellon University.
**
** @OPENSOURCE_LICENSE_START@
** See license information in ../../LICENSE.txt
** @OPENSOURCE_LICENSE_END@
*/

/*
**  rwp2f_minbytes.c
**
**    An example of a simple plug-in that can be used with rwptoflow.
**
**  Mark Thomas
**  September 2006
*/

#include <silk/silk.h>

RCSIDENT("$SiLK: rwp2f_minbytes.c ef14e54179be 2020-04-14 21:57:45Z mthomas $");

#include <silk/utils.h>
#include <silk/skplugin.h>
#include <silk/skstream.h>
#include "rwppacketheaders.h"

/* Plugin protocol version */
#define PLUGIN_API_VERSION_MAJOR 1
#define PLUGIN_API_VERSION_MINOR 0


/* LOCAL VARIABLE DEFINITIONS */

/*
 * rwptoflow hands the packet to the plugin as an "extra argument".
 * rwptoflow and its plugins must agree on the name of this argument.
 * The extra argument is specified in a NULL-terminated array of
 * argument names defined in rwppacketheaders.h.
 */
static const char *plugin_extra_args[] = RWP2F_EXTRA_ARGUMENTS;

/* the minimum number of bytes a packet must have to pass, as entered
 * by the user */
static uint32_t byte_limit = 0;


/* OPTIONS SETUP */

typedef enum {
    OPT_BYTE_LIMIT
} plugin_options_enum;

static struct option plugin_options[] = {
    {"byte-limit",      REQUIRED_ARG, 0, OPT_BYTE_LIMIT},
    {0,0,0,0}           /* sentinel */
};

static const char *plugin_help[] = {
    ("Reject the packet if its length (hdr+payload) is less\n"
     "\tthan this value"),
    (char*)NULL
};


/* LOCAL FUNCTION PROTOTYPES */

static skplugin_err_t optionsHandler(const char *opt_arg, void *cbdata);
static skplugin_err_t
p2f_minbytes(
    rwRec              *rwrec,
    void               *cbdata,
    void              **extra_args);


/* FUNCTION DEFINITIONS */

/* the registration function called by skplugin.c */
skplugin_err_t
SKPLUGIN_SETUP_FN(
    uint16_t            major_version,
    uint16_t            minor_version,
    void        UNUSED(*pi_data))
{
    int i;
    skplugin_err_t rv;

    /* Check API version */
    rv = skpinSimpleCheckVersion(major_version, minor_version,
                                 PLUGIN_API_VERSION_MAJOR,
                                 PLUGIN_API_VERSION_MINOR,
                                 skAppPrintErr);
    if (rv != SKPLUGIN_OK) {
        return rv;
    }

    assert((sizeof(plugin_options)/sizeof(struct option))
           == (sizeof(plugin_help)/sizeof(char*)));

    /* register the options to use for rwptoflow.  when the option is
     * given, we will call skpinRegTransform() to register the
     * transformation function. */
    for (i = 0; plugin_options[i].name; ++i) {
        rv = skpinRegOption2(plugin_options[i].name, plugin_options[i].has_arg,
                             plugin_help[i], NULL, &optionsHandler,
                             (void*)&plugin_options[i].val,
                             1, SKPLUGIN_APP_TRANSFORM);
        if (SKPLUGIN_OK != rv && SKPLUGIN_ERR_DID_NOT_REGISTER != rv) {
            return rv;
        }
    }

    return SKPLUGIN_OK;
}


/*
 *  status = optionsHandler(opt_arg, &index);
 *
 *    Handles options for the plugin.  'opt_arg' is the argument, or
 *    NULL if no argument was given.  'index' is the enum passed in
 *    when the option was registered.
 *
 *    Returns SKPLUGIN_OK on success, or SKPLUGIN_ERR if there was a
 *    problem.
 */
static skplugin_err_t
optionsHandler(
    const char         *opt_arg,
    void               *cbdata)
{
    plugin_options_enum opt_index = *((plugin_options_enum*)cbdata);
    skplugin_callbacks_t regdata;
    int rv;

    switch (opt_index) {
      case OPT_BYTE_LIMIT:
        rv = skStringParseUint32(&byte_limit, opt_arg, 0, 0);
        if (rv) {
            skAppPrintErr("Invalid %s '%s': %s",
                          plugin_options[opt_index].name, opt_arg,
                          skStringParseStrerror(rv));
            return SKPLUGIN_ERR;
        }
        break;
    }

    /* register the transform function */
    memset(&regdata, 0, sizeof(regdata));
    regdata.transform = p2f_minbytes;
    regdata.extra = plugin_extra_args;
    return skpinRegTransformer(NULL, &regdata, NULL);
}


skplugin_err_t
p2f_minbytes(
    rwRec       UNUSED(*rwrec),
    void        UNUSED(*cbdata),
    void              **extra_args)
{
    sk_pktsrc_t *pktsrc = (sk_pktsrc_t*)extra_args[0];
    ip_header_t *iph;

    iph = (ip_header_t*)(pktsrc->pcap_data + sizeof(eth_header_t));
    if (ntohs(iph->tlen) < byte_limit) {
        return SKPLUGIN_FILTER_FAIL;
    }

    return SKPLUGIN_FILTER_PASS;
}




/*
** Local Variables:
** mode:c
** indent-tabs-mode:nil
** c-basic-offset:4
** End:
*/
