/*
** Copyright (C) 2011-2020 by Carnegie Mellon University.
**
** @OPENSOURCE_LICENSE_START@
** See license information in ../../LICENSE.txt
** @OPENSOURCE_LICENSE_END@
*/

/*
**  int-ext-fields.c
**
**    A plug-in to define key fields for rwcut, rwuniq, etc, to print,
**    group by the {internal,external} {IP,port}.
**
**    Mark Thomas
**    January 2011
**
*/

#include <silk/silk.h>

RCSIDENT("$SiLK: int-ext-fields.c ef14e54179be 2020-04-14 21:57:45Z mthomas $");

#include <silk/rwrec.h>
#include <silk/skipaddr.h>
#include <silk/skplugin.h>
#include <silk/sksite.h>
#include <silk/skvector.h>
#include <silk/utils.h>


/* DEFINES AND TYPEDEFS */

/*
 *    These variables specify the version of the SiLK plug-in API.
 *    They are used in the call to skpinSimpleCheckVersion() below.
 *    See the description of that function for their meaning.
 */
#define PLUGIN_API_VERSION_MAJOR 1
#define PLUGIN_API_VERSION_MINOR 0


/* LOCAL VARIABLES */

sk_bitmap_t *incoming_flowtypes = NULL;
sk_bitmap_t *outgoing_flowtypes = NULL;


/* LOCAL FUNCTION PROTOTYPES */

static void
internalIp(
    skipaddr_t         *return_value,
    const rwRec        *rec);
static void
externalIp(
    skipaddr_t         *return_value,
    const rwRec        *rec);
static uint64_t
internalPort(
    const rwRec        *rec);
static uint64_t
externalPort(
    const rwRec        *rec);
static void cleanup(void);
static skplugin_err_t
parseFlowtypes(
    const char         *opt_arg,
    void               *v_bitmap);


/* FUNCTION DEFINITIONS */

/*
 *    This is the registration function.
 *
 *    When you provide "--plugin=my-plugin.so" on the command line to
 *    an application, the application calls this function to determine
 *    the new switches and/or fields that "my-plugin" provides.
 *
 *    This function is called with three arguments: the first two
 *    describe the version of the plug-in API, and the third is a
 *    pointer that is currently unused.
 */
skplugin_err_t
SKPLUGIN_SETUP_FN(
    uint16_t            major_version,
    uint16_t            minor_version,
    void        UNUSED(*plug_in_data))
{
    skplugin_err_t err;
    const char *env;

    /* Check the plug-in API version */
    err = skpinSimpleCheckVersion(major_version, minor_version,
                                  PLUGIN_API_VERSION_MAJOR,
                                  PLUGIN_API_VERSION_MINOR,
                                  skAppPrintErr);
    if (err != SKPLUGIN_OK) {
        return err;
    }

    env = getenv("INCOMING_FLOWTYPES");
    if (env && *env) {
        parseFlowtypes(env, (void*)&incoming_flowtypes);
    }
    env = getenv("OUTGOING_FLOWTYPES");
    if (env && *env) {
        parseFlowtypes(env, (void*)&outgoing_flowtypes);
    }

    /* register options for all apps that use key-field */
    err = skpinRegOption2("incoming-flowtypes", REQUIRED_ARG,
                          "List of flowtypes representing incoming flows",
                          NULL, parseFlowtypes, (void*)&incoming_flowtypes,
                          5, SKPLUGIN_APP_CUT, SKPLUGIN_APP_GROUP,
                          SKPLUGIN_APP_SORT, SKPLUGIN_APP_STATS_FIELD,
                          SKPLUGIN_APP_UNIQ_FIELD);
    if (err != SKPLUGIN_OK
        && err != SKPLUGIN_ERR_DID_NOT_REGISTER)
    {
        return err;
    }

    err = skpinRegOption2("outgoing-flowtypes", REQUIRED_ARG,
                          "List of flowtypes representing outgoing flows",
                          NULL, parseFlowtypes, (void*)&outgoing_flowtypes,
                          5, SKPLUGIN_APP_CUT, SKPLUGIN_APP_GROUP,
                          SKPLUGIN_APP_SORT, SKPLUGIN_APP_STATS_FIELD,
                          SKPLUGIN_APP_UNIQ_FIELD);
    if (err != SKPLUGIN_OK
        && err != SKPLUGIN_ERR_DID_NOT_REGISTER)
    {
        return err;
    }

    err = skpinRegCleanup(cleanup);
    if (SKPLUGIN_OK != err) {
        return err;
    }

    return SKPLUGIN_OK;
}


static void
internalIp(
    skipaddr_t         *return_value,
    const rwRec        *rec)
{
    if (skBitmapGetBit(incoming_flowtypes, rwRecGetFlowType(rec))) {
        /* flow is incoming, so destination is internal */
        rwRecMemGetDIP(rec, return_value);
        return;
    }
    if (skBitmapGetBit(outgoing_flowtypes, rwRecGetFlowType(rec))) {
        /* flow is outgoing, so source is internal */
        rwRecMemGetSIP(rec, return_value);
        return;
    }
    /* set value to 0. */
    skipaddrClear(return_value);
}


static void
externalIp(
    skipaddr_t         *return_value,
    const rwRec        *rec)
{
    if (skBitmapGetBit(incoming_flowtypes, rwRecGetFlowType(rec))) {
        /* flow is incoming, so source is external */
        rwRecMemGetSIP(rec, return_value);
        return;
    }
    if (skBitmapGetBit(outgoing_flowtypes, rwRecGetFlowType(rec))) {
        /* flow is outgoing, so destination is external */
        rwRecMemGetDIP(rec, return_value);
        return;
    }
    /* set value to 0. */
    skipaddrClear(return_value);
}


static uint64_t
internalPort(
    const rwRec        *rec)
{
    /* just return 0 for ICMP, since the port fields hold the type/code */
    if (rwRecIsICMP(rec)) {
        return 0;
    }

    if (skBitmapGetBit(incoming_flowtypes, rwRecGetFlowType(rec))) {
        /* flow is incoming, so destination is internal */
        return rwRecGetDPort(rec);
    }
    if (skBitmapGetBit(outgoing_flowtypes, rwRecGetFlowType(rec))) {
        /* flow is outgoing, so source is internal */
        return rwRecGetSPort(rec);
    }
    return 0;
}


static uint64_t
externalPort(
    const rwRec        *rec)
{
    /* just return 0 for ICMP, since the port fields hold the type/code */
    if (rwRecIsICMP(rec)) {
        return 0;
    }

    if (skBitmapGetBit(incoming_flowtypes, rwRecGetFlowType(rec))) {
        /* flow is incoming, so source is external */
        return rwRecGetSPort(rec);
    }
    if (skBitmapGetBit(outgoing_flowtypes, rwRecGetFlowType(rec))) {
        /* flow is outgoing, so destination is external */
        return rwRecGetDPort(rec);
    }
    return 0;
}


static void
cleanup(
    void)
{
    if (incoming_flowtypes) {
        skBitmapDestroy(&incoming_flowtypes);
    }
    if (outgoing_flowtypes) {
        skBitmapDestroy(&outgoing_flowtypes);
    }
}


static skplugin_err_t
parseFlowtypes(
    const char         *opt_arg,
    void               *v_bitmap)
{
    static int registered_fields = 0;
    sk_bitmap_t **ft_bitmap;
    sksite_error_iterator_t *err_iter = NULL;
    sk_vector_t *ft_vec = NULL;
    sk_flowtype_id_t ft;
    int i = 0;
    skplugin_err_t err = SKPLUGIN_OK;
    int rv;

    assert(v_bitmap);
    ft_bitmap = (sk_bitmap_t**)v_bitmap;

    if (NULL != *ft_bitmap) {
        /* this is an attempt to re-create a list of flowtypes, which
         * could happen if the first pass was generated by parsing an
         * environment variable.  clear the bitmap */
        skBitmapClearAllBits(*ft_bitmap);

    } else if (skBitmapCreate(ft_bitmap, SK_MAX_NUM_FLOWTYPES)) {
        skAppPrintErr("Unable to create bitmap");
        err = SKPLUGIN_ERR;
        goto END;
    }

    ft_vec = skVectorNew(sizeof(sk_flowtype_id_t));
    if (NULL == ft_vec) {
        skAppPrintErr("Unable to create vector");
        err = SKPLUGIN_ERR;
        goto END;
    }
    rv = sksiteParseFlowtypeList(ft_vec, opt_arg, NULL, NULL, NULL, NULL,
                                 &err_iter);
    if (rv) {
        if (rv < 0) {
            skAppPrintErr("Memory or internal error while parsing flowtypes");
        } else if (1 == rv) {
            sksiteErrorIteratorNext(err_iter);
            skAppPrintErr("Invalid flowtypes '%s': %s",
                          opt_arg, sksiteErrorIteratorGetMessage(err_iter));
            assert(sksiteErrorIteratorNext(err_iter)
                   == SK_ITERATOR_NO_MORE_ENTRIES);
        } else {
            skAppPrintErr("Invalid flowtypes '%s': Found multiple errors:",
                          opt_arg);
            while (sksiteErrorIteratorNext(err_iter) == SK_ITERATOR_OK) {
                skAppPrintErr("%s", sksiteErrorIteratorGetMessage(err_iter));
            }
        }
        err = SKPLUGIN_ERR;
        goto END;
    }
    if (0 == skVectorGetCount(ft_vec)) {
        skAppPrintErr("Invalid flowtypes '%s': No valid flowtypes found",
                      opt_arg);
        err = SKPLUGIN_ERR;
        goto END;
    }

    for (i = 0; 0 == skVectorGetValue(&ft, ft_vec, i); ++i) {
        skBitmapSetBit(*ft_bitmap, ft);
    }

    if (incoming_flowtypes && outgoing_flowtypes && !registered_fields) {
        registered_fields = 1;

        err = skpinRegIPAddressField("int-ip", &internalIp, 0);
        if (SKPLUGIN_OK != err) {
            goto END;
        }
        err = skpinRegIPAddressField("ext-ip", &externalIp, 0);
        if (SKPLUGIN_OK != err) {
            goto END;
        }
        err = skpinRegIntField("int-port", 0, UINT16_MAX, &internalPort, 0);
        if (SKPLUGIN_OK != err) {
            goto END;
        }
        err = skpinRegIntField("ext-port", 0, UINT16_MAX, &externalPort, 0);
        if (SKPLUGIN_OK != err) {
            goto END;
        }
    }

  END:
    skVectorDestroy(ft_vec);
    sksiteErrorIteratorFree(err_iter);
    if (*ft_bitmap && err != SKPLUGIN_OK) {
        skBitmapDestroy(ft_bitmap);
        *ft_bitmap = NULL;
    }
    return err;
}




/*
** Local Variables:
** mode:c
** indent-tabs-mode:nil
** c-basic-offset:4
** End:
*/
