/*
** Copyright (C) 2014-2017 by Carnegie Mellon University.
**
** @OPENSOURCE_LICENSE_START@
** See license information in ../../LICENSE.txt
** @OPENSOURCE_LICENSE_END@
*/

/*
**  Add CERT IEs to an information model
**
*/


#include <silk/silk.h>

RCSIDENT("$SiLK: skipfixcert.c efd886457770 2017-06-21 18:43:23Z mthomas $");

#include <silk/skipfixcert.h>
#include <silk/sklog.h>
#include <silk/utils.h>


/* LOCAL DEFINES AND TYPEDEFS */

/* Define the IPFIX information elements in IPFIX_CERT_PEN space */

/* Include our local copy of CERT_IE.h taken from the yaf 2.8.0
 * sources as of 2015-12-23. */
#define CERT_PEN  IPFIX_CERT_PEN
#define YAF_ENABLE_HOOKS   1

#include "CERT_IE.h"


/* Name of environment variable that, when set, cause SiLK to ignore
 * any G_LOG_LEVEL_WARNING messages. */
#define SK_ENV_FIXBUF_SUPPRESS_WARNING "SILK_LIBFIXBUF_SUPPRESS_WARNINGS"


/* LOCAL VARIABLE DEFINITIONS */

static fbInfoModel_t *shared_model = NULL;
static pthread_mutex_t shared_model_mutex = PTHREAD_MUTEX_INITIALIZER;
static unsigned int shared_model_count = 0;

static fbInfoElement_t skipfix_cert_info_elements[] = {
    /* Extra fields produced by SiLK records */
    FB_IE_INIT_FULL("silkFlowType",                 IPFIX_CERT_PEN, 30,  1,
                    FB_IE_F_ENDIAN | FB_IE_IDENTIFIER,
                    0, 0, FB_UINT_8, NULL),
    FB_IE_INIT_FULL("silkFlowSensor",               IPFIX_CERT_PEN, 31,  2,
                    FB_IE_F_ENDIAN | FB_IE_IDENTIFIER,
                    0, 0, FB_UINT_16, NULL),
    FB_IE_INIT_FULL("silkTCPState",                 IPFIX_CERT_PEN, 32,  1,
                    FB_IE_F_ENDIAN | FB_IE_FLAGS,
                    0, 0, FB_UINT_8, NULL),
    FB_IE_NULL
};



/* FUNCTION DEFINITIONS */

void
skipfixCERTAugmentInfoModel(
    fbInfoModel_t      *model)
{
    fbInfoModelAddElementArray(model, yaf_info_elements);
    fbInfoModelAddElementArray(model, yaf_dpi_info_elements);
    fbInfoModelAddElementArray(model, yaf_dhcp_info_elements);

    fbInfoModelAddElementArray(model, skipfix_cert_info_elements);
}


fbInfoModel_t *
skipfix_information_model_create(
    unsigned int        flags)
{
    if (flags & SK_INFOMODEL_UNIQUE) {
        fbInfoModel_t *model;

        model = fbInfoModelAlloc();
        skipfixCERTAugmentInfoModel(model);
        return model;
    }

    pthread_mutex_lock(&shared_model_mutex);
    if (0 == shared_model_count) {
        assert(NULL == shared_model);
        shared_model = fbInfoModelAlloc();
        skipfixCERTAugmentInfoModel(shared_model);
    }
    assert(shared_model);

    ++shared_model_count;
    pthread_mutex_unlock(&shared_model_mutex);
    return shared_model;
}


void
skipfix_information_model_destroy(
    fbInfoModel_t      *model)
{
    if (model) {
        pthread_mutex_lock(&shared_model_mutex);
        if (model == shared_model) {
            assert(shared_model_count);
            --shared_model_count;
            if (shared_model_count) {
                pthread_mutex_unlock(&shared_model_mutex);
                return;
            }
            shared_model = NULL;
        }
        fbInfoModelFree(model);
        pthread_mutex_unlock(&shared_model_mutex);
    }
}



/*
 *    Handler to print log messages using skAppPrintErr().  This may
 *    be invoked by g_log() and the other logging functions from
 *    GLib2.
 */
static void
skipfix_glog_handler_app(
    const gchar            *log_domain,
    GLogLevelFlags   UNUSED(log_level),
    const gchar            *message,
    gpointer         UNUSED(user_data))
{
    if (log_domain) {
        skAppPrintErr("%s: %s", log_domain, message);
    } else {
        skAppPrintErr("%s", message);
    }
}


/*
 *    Handler to write log messages to the log file using the
 *    functions in sklog.h..  This may be invoked by g_log() and the
 *    other logging functions from GLib2.
 */
static void
skipfix_glog_handler_sklog(
    const gchar     UNUSED(*log_domain),
    GLogLevelFlags          log_level,
    const gchar            *message,
    gpointer         UNUSED(user_data))
{
    /* In syslog, CRIT is worse than ERR; in Glib2 ERROR is worse than
     * CRITICAL. */

    switch (log_level & G_LOG_LEVEL_MASK) {
      case G_LOG_LEVEL_CRITICAL:
        ERRMSG("%s", message);
        break;
      case G_LOG_LEVEL_WARNING:
        WARNINGMSG("%s", message);
        break;
      case G_LOG_LEVEL_MESSAGE:
        NOTICEMSG("%s", message);
        break;
      case G_LOG_LEVEL_INFO:
        INFOMSG("%s", message);
        break;
      case G_LOG_LEVEL_DEBUG:
        DEBUGMSG("%s", message);
        break;
      default:
        CRITMSG("%s", message);
        break;
    }
}


/*
 *    Handler to discard messages.
 */
static void
skipfix_glog_handler_void(
    const gchar     UNUSED(*log_domain),
    GLogLevelFlags   UNUSED(log_level),
    const gchar     UNUSED(*message),
    gpointer         UNUSED(user_data))
{
    return;
}


/*
 *    Use 'log_handler' as the function that handles (most) messages
 *    generated by GLib and libfixbuf.  If the environmnet variable
 *    specified by SK_ENV_FIXBUF_SUPPRESS_WARNING is set, then warning
 *    messages generated by are ignored.
 *
 *    For more information, see
 *    http://developer.gnome.org/glib/stable/glib-Message-Logging.html
 */
static void
skipfix_set_glog_handlers(
    GLogFunc            log_handler)
{
    const char *env;
    GLogLevelFlags log_levels = (GLogLevelFlags)(G_LOG_LEVEL_CRITICAL
                                                 | G_LOG_LEVEL_WARNING
                                                 | G_LOG_LEVEL_MESSAGE
                                                 | G_LOG_LEVEL_INFO
                                                 | G_LOG_LEVEL_DEBUG);

    /* set a log handler for messages from glib, which we always want
     * to include in our log file. */
    g_log_set_handler("GLib", log_levels, log_handler, NULL);

    /* set a log handler for messages from fixbuf, maybe using a void
     * handler for warnings. */
    env = getenv(SK_ENV_FIXBUF_SUPPRESS_WARNING);
    if (env && *env && 0 == strcmp("1", env)) {
        /* suppress warnings by setting a void handler */
        log_levels = (GLogLevelFlags)((unsigned int)log_levels
                                      & ~(unsigned int)G_LOG_LEVEL_WARNING);
        g_log_set_handler(
            NULL, G_LOG_LEVEL_WARNING, &skipfix_glog_handler_void, NULL);
    }
    g_log_set_handler(NULL, log_levels, log_handler, NULL);
}


void
skipfix_initialize(
    unsigned int        flags)
{
    static int first_call = 1;

#if (GLIB_MAJOR_VERSION == 2 && GLIB_MINOR_VERSION >= 10)
#define MEMORY_SIZE 128
    gpointer memory;

    /* initialize the slice allocator */
    memory = g_slice_alloc(MEMORY_SIZE);
    g_slice_free1(MEMORY_SIZE, memory);
#endif

    /* As of glib 2.32, g_thread_init() is deprecated. */
#if (GLIB_MAJOR_VERSION == 2 && GLIB_MINOR_VERSION < 32)
    /* tell fixbuf (glib) we are a threaded program.  this will abort
     * if glib does not have thread support. */
    if (!g_thread_supported()) {
        g_thread_init(NULL);
    }
#endif

    if (first_call || (flags & SKIPFIX_INITIALIZE_FLAG_APPERROR)) {
        /* write messages using skAppPrintErr() */
        skipfix_set_glog_handlers(&skipfix_glog_handler_app);
    }
    if (flags & SKIPFIX_INITIALIZE_FLAG_LOG) {
        /* write messages using INFOMSG(),etc, from sklog.c */
        skipfix_set_glog_handlers(&skipfix_glog_handler_sklog);
    }

    first_call = 0;
}


/*
** Local Variables:
** mode:c
** indent-tabs-mode:nil
** c-basic-offset:4
** End:
*/
