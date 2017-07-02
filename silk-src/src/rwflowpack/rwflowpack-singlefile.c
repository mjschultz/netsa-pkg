/*
** Copyright (C) 2003-2017 by Carnegie Mellon University.
**
** @OPENSOURCE_LICENSE_START@
** See license information in ../../LICENSE.txt
** @OPENSOURCE_LICENSE_END@
*/

/*
**  rwflowpack-singlefile.c
**
**    Helper file for rwflowpack that implements the 'single-file'
**    input-mode.
**
**    Specify the functions that are used to read
**    records from a single file.
**
*/

#include <silk/silk.h>

RCSIDENT("$SiLK: rwflowpack-singlefile.c efd886457770 2017-06-21 18:43:23Z mthomas $");

#include "rwflowpack_priv.h"


/* LOCAL MACROS AND TYPEDEFS */

#define INPUT_MODE_TYPE_NAME "Single File Input Mode"


/* LOCAL VARIABLES */

/* The probe to use as the source of the flow records */
static skpc_probe_t *probe = NULL;

/* True as long as we are reading. */
static volatile int reading = 0;


/* FUNCTION DEFINITIONS */

/*
 *  status = input_start()
 *
 *    Invoked by input_mode_type->start_fn();
 */
static int
input_start(
    void)
{
    int rv;

    INFOMSG("Starting " INPUT_MODE_TYPE_NAME "...");

    /* Create the converter */
    switch (skpcProbeGetType(probe)) {
      case PROBE_ENUM_NETFLOW_V5:
        rv = sk_conv_pdu_create(probe);
        break;
      case PROBE_ENUM_IPFIX:
        rv = sk_conv_ipfix_create(probe);
        break;
      case PROBE_ENUM_SILK:
        rv = sk_conv_silk_create(probe);
        break;
      default:
        CRITMSG("'%s': Unsupported probe type id '%d'",
                probe->probe_name, (int)probe->probe_type);
        skAbortBadCase(probe->probe_type);
    }
    if (rv) {
        return -1;
    }

    if (NULL == skpcProbeGetFileSource(probe)) {
        skAbort();
    }

    rv = sk_coll_create(probe);

    /* Create the collector */
    if (sk_coll_start(probe)) {
        ERRMSG("Could not create %s source from file '%s'",
               skpcProbetypeEnumtoName(skpcProbeGetType(probe)),
               skpcProbeGetFileSource(probe));
        return -1;
    }

    reading = 1;

    INFOMSG("Started " INPUT_MODE_TYPE_NAME ".");

    return 0;
}


/*
 *  input_stop();
 *
 *    Invoked by input_mode_type->stop_fn();
 */
static void
input_stop(
    void)
{
    INFOMSG("Stopping " INPUT_MODE_TYPE_NAME "...");

    if (reading) {
        reading = 0;
        sk_coll_stop(probe);
        /* decrement_thread_count(0); */
    }

    INFOMSG("Stopped " INPUT_MODE_TYPE_NAME ".");
}


/*
 *  status = input_setup();
 *
 *    Invoked by input_mode_type->setup_fn();
 */
static int
input_setup(
    void)
{
    skpc_probe_iter_t p_iter;
    const skpc_probe_t *p;

    /* There should be a single probe */
    skpcProbeIteratorBind(&p_iter);
    if (skpcProbeIteratorNext(&p_iter, &p) != 1) {
        skAppPrintErr("No valid probes were found");
        return -1;
    }

    probe = (skpc_probe_t *)p;

    if (skpcProbeIteratorNext(&p_iter, &p) != 0) {
        skAppPrintErr(("Multiple probes specified.  %s requires"
                       " a single probe that reads from a file"),
                          INPUT_MODE_TYPE_NAME);
        return -1;
    }

    /* Make certain probe reads from a file */
    if (NULL == skpcProbeGetFileSource(probe)) {
        skAppPrintErr("Probe %s does not read from a file",
                      skpcProbeGetName(probe));
        return -1;
    }

    return 0;
}


/*
 *  input_teardown();
 *
 *    Invoked by input_mode_type->teardown_fn();
 */
static void
input_teardown(
    void)
{
    if (probe) {
        /* destroy the collector */
        sk_coll_destroy(probe);

        /* destroy the converter */
        switch (skpcProbeGetType(probe)) {
          case PROBE_ENUM_NETFLOW_V5:
            sk_conv_pdu_destroy(probe);
            break;
          case PROBE_ENUM_IPFIX:
            sk_conv_ipfix_destroy(probe);
            break;
          case PROBE_ENUM_SILK:
            sk_conv_silk_destroy(probe);
            break;
          default:
            CRITMSG("'%s': Unsupported probe type id '%d'",
                    probe->probe_name, (int)probe->probe_type);
            skAbortBadCase(probe->probe_type);
        }
    }
}


/*
 *  status = singlefile_initialize(input_mode_fn_table);
 *
 *    Fill in the function pointers for the input_mode_type.
 */
int
singlefile_initialize(
    input_mode_type_t  *input_mode_fn_table)
{
    /* Set function pointers */
    input_mode_fn_table->setup_fn       = &input_setup;
    input_mode_fn_table->start_fn       = &input_start;
    input_mode_fn_table->print_stats_fn = NULL;
    input_mode_fn_table->stop_fn        = &input_stop;
    input_mode_fn_table->teardown_fn    = &input_teardown;

    return 0;
}


/*
** Local Variables:
** mode:c
** indent-tabs-mode:nil
** c-basic-offset:4
** End:
*/
