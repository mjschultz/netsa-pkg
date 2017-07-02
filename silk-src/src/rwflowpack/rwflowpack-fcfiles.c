/*
** Copyright (C) 2004-2017 by Carnegie Mellon University.
**
** @OPENSOURCE_LICENSE_START@
** See license information in ../../LICENSE.txt
** @OPENSOURCE_LICENSE_END@
*/

/*
**  rwflowpack-fcfiles.c
**
**    Helper file for rwflowpack that implements the 'fcfiles'
**    input-mode.
**
**    This file is for flowcap-style input.  For flowcap-style output,
**    see rwflowpack-flowcap.c.
**
**    Specify the functions that are used to poll a directory for SiLK
**    Flow files that were created by an instance of rwflowpack
**    running with the 'flowcap' output mode.  These files contain a
**    header that specifies the probe name where the flows were
**    collected.  The name of the probe is used to find the probe
**    object defined in the sensor.conf file.
**
**    Given the probe, the SiLK records in the files can be read and
**    processed as if they were collected by a directory poller in the
**    'stream' input-mode.
**
*/

#include <silk/silk.h>

RCSIDENT("$SiLK: rwflowpack-fcfiles.c efd886457770 2017-06-21 18:43:23Z mthomas $");

#include <silk/skpolldir.h>
#include "rwflowpack_priv.h"


/* MACROS AND TYPEDEFS */

/* A name for this input_mode_type. */
#define INPUT_MODE_TYPE_NAME  "Flowcap Files Input Mode"


/* LOCAL VARIABLES */

/* There is a single processing thread for this input_mode */
static pthread_t reader_thread;

/* The directory polling object */
static skPollDir_t *polldir = NULL;

/* True as long as we are reading. */
static volatile int reading = 0;


/* FUNCTION DEFINITIONS */

/*
 *  status = open_pathname(pathname, &stream, &packer);
 *
 *    Open the SiLK file at 'pathname' as a stream of flow records and
 *    store the result in 'stream'.
 *
 *    Read the name of the probe from the header of the stream and
 *    find that probe in the list of probes.
 *
 *    Return 0 on success.  Return an error code of -1 on the
 *    following error conditions:
 *
 *    -- file is not a valid SiLK file
 *    -- file does not contain a Probename Header
 *    -- the probe_name does not map to a valid probe in config file
 *    -- allocation error
 *    -- the application is told to stop while waiting for file handle
 */
static int
open_pathname(
    const char         *pathname,
    skstream_t        **stream_parm,
    skpc_probe_t      **probe)
{
    const char *probe_name;
    sk_hentry_probename_t *probe_hdr;
    sk_file_header_t *hdr;
    skstream_t *stream;
    ssize_t rv;

    assert(pathname);
    *stream_parm = NULL;
    *probe = NULL;
    stream = NULL;

    /* Get a file handle. Check return status in case we started
     * shutting down while waiting for a handle. */
    if (flowpackAcquireFileHandle()) {
        reading = 0;
    }
    if (!reading) {
        flowpackReleaseFileHandle();
        return -1;
    }

    /* Open the file */
    if ((rv = skStreamCreate(&stream, SK_IO_READ, SK_CONTENT_SILK_FLOW))
        || (rv = skStreamBind(stream, pathname))
        || (rv = skStreamOpen(stream))
        || (rv = skStreamReadSilkHeader(stream, &hdr)))
    {
        /* Error opening the file */
        skStreamPrintLastErr(stream, rv, &WARNINGMSG);
        NOTICEMSG("File '%s' does not appear to be a valid Flow file",
                  pathname);
        goto ERROR;
    }

    /* Get the probe_name from the stream's header */
    probe_hdr = ((sk_hentry_probename_t*)
                 skHeaderGetFirstMatch(hdr, SK_HENTRY_PROBENAME_ID));
    if (probe_hdr == NULL) {
        WARNINGMSG("No probename header in file '%s'", pathname);
        goto ERROR;
    }
    probe_name = skHentryProbenameGetProbeName(probe_hdr);
    if (probe_name == NULL || probe_name[0] == '\0') {
        CRITMSG("Unable to get probename from flowcap file '%s'", pathname);
        goto ERROR;
    }

    /* Use the probe_name to find the skpc_probe_t object. */
    *probe = (skpc_probe_t*)skpcProbeLookupByName(probe_name);
    if (*probe == NULL) {
        WARNINGMSG("The sensor configuration file does not define probe '%s'",
                   probe_name);
        goto ERROR;
    }

    /* File has been validated.  We're done. */
    *stream_parm = stream;
    return 0;

  ERROR:
    skStreamDestroy(&stream);
    flowpackReleaseFileHandle();
    if (dispose_incoming_file(pathname, incoming_directory, 1)) {
        reading = 0;
    }
    return -1;
}


/*
 *  input_reader();
 *
 *  THREAD ENTRY POINT for the 'reader_thread'.
 *
 *    The 'reader_thread' is created in input_start().
 *
 *    Get a file from the incoming_directory, read and process its
 *    records, archive the file, and repeat until the file-scoped
 *    variable 'reading' is false or an error occurs.
 */
static void*
input_reader(
    void        UNUSED(*dummy))
{
    skpc_probe_t *probe;
    char path[PATH_MAX];
    char *filename;
    skPollDirErr_t pderr;
    skstream_t *stream = NULL;
    rwRec rwrec;
    lua_State *L;
    int rv = SKSTREAM_ERR_EOF;

    DEBUGMSG("Started reader thread");

    L = sk_lua_newstate();

    rwRecInitialize(&rwrec, L);

    while (reading) {
        /* Get next file from the directory poller */
        pderr = skPollDirGetNextFile(polldir, path, &filename);
        if (PDERR_NONE != pderr) {
            if (PDERR_STOPPED != pderr) {
                CRITMSG("Error polling incoming directory '%s': %s",
                        skPollDirGetDir(polldir),((PDERR_SYSTEM == pderr)
                                                  ? strerror(errno)
                                                  : skPollDirStrError(pderr)));
            }
            reading = 0;
        }
        if (!reading) {
            break;
        }

        INFOMSG((INPUT_MODE_TYPE_NAME " processing file '%s'"), filename);

        /* Open the file and get the probe object using the probe name
         * in the file's header. */
        if (open_pathname(path, &stream, &probe) != 0) {
            continue;
        }

        /* Process the records in the file */
        while (SKSTREAM_OK == (rv = skStreamReadRecord(stream, &rwrec))) {
            rv = skpcProbePackRecord(probe, &rwrec, NULL);
            if (-1 == rv) {
                reading = 0;
                skStreamDestroy(&stream);
                flowpackReleaseFileHandle();
                goto END;
            }
        }
        /* Report any unexpected error.  */
        if (SKSTREAM_ERR_EOF != rv) {
            skStreamPrintLastErr(stream, rv, &WARNINGMSG);
        }

        INFOMSG("Processed file '%s', %" PRIu64 " records.",
                filename, skStreamGetRecordCount(stream));
        skStreamDestroy(&stream);
        flowpackReleaseFileHandle();

        dispose_incoming_file(path, incoming_directory, 0);
    }

  END:
    DEBUGMSG("Finishing reader thread...");

    sk_lua_closestate(L);

    /* thread is ending, decrement the count and tell the main thread
     * to check the thread count */
    decrement_thread_count(1);

    return NULL;
}


/*
 *  status = inputStart();
 *
 *    Invoked by input_mode_type->start_fn();
 */
static int
input_start(
    void)
{
    skPollDirErr_t pderr;
    int rv;

    INFOMSG("Starting " INPUT_MODE_TYPE_NAME "...");

    /* Start the polldir object for directory polling */
    DEBUGMSG("Starting directory poller on '%s'", skPollDirGetDir(polldir));
    pderr = skPollDirStart(polldir);
    if (PDERR_NONE != pderr) {
        CRITMSG("Failed to start polling for directory '%s': %s",
                skPollDirGetDir(polldir), ((PDERR_SYSTEM == pderr)
                                           ? strerror(errno)
                                           : skPollDirStrError(pderr)));
        skPollDirDestroy(polldir);
        polldir = NULL;
        return -1;
    }

    reading = 1;

    increment_thread_count();

    rv = skthread_create(
        INPUT_MODE_TYPE_NAME, &reader_thread, &input_reader, NULL);
    if (rv) {
        ERRMSG("Unable to create reader thread: %s", strerror(rv));
        decrement_thread_count(0);
        skPollDirStop(polldir);
        reader_thread = pthread_self();
        reading = 0;
        return -1;
    }

    INFOMSG("Started " INPUT_MODE_TYPE_NAME ".");

    return 0;
}


/*
 *  inputStop();
 *
 *    Invoked by input_mode_type->stop_fn();
 */
static void
input_stop(
    void)
{
    if (pthread_equal(pthread_self(), reader_thread)) {
        return;
    }

    INFOMSG("Stopping " INPUT_MODE_TYPE_NAME "...");

    reading = 0;
    if (polldir) {
        DEBUGMSG("Stopping directory poller");
        skPollDirStop(polldir);
    }

    DEBUGMSG("Waiting for reader thread to finish...");
    pthread_join(reader_thread, NULL);

    INFOMSG("Stopped " INPUT_MODE_TYPE_NAME ".");
}


/*
 *  status = inputSetup();
 *
 *    Invoked by input_mode_type->setup_fn();
 */
static int
input_setup(
    void)
{
    /* Create the polldir object for directory polling */
    polldir = skPollDirCreate(incoming_directory->d_poll_directory,
                              incoming_directory->d_poll_interval);
    if (NULL == polldir) {
        skAppPrintErr("Error creating directory poller on '%s'",
                      incoming_directory->d_poll_directory);
        return -1;
    }

    return 0;
}


/*
 *  inputTeardown();
 *
 *    Invoked by input_mode_type->teardown_fn();
 */
static void
input_teardown(
    void)
{
    if (polldir) {
        DEBUGMSG("Destroying directory poller");
        skPollDirDestroy(polldir);
        polldir = NULL;
    }
}


/*
 *  status = fcfiles_initialize(input_mode_fn_table);
 *
 *    Fill in the function pointers for the input_mode_type.
 */
int
fcfiles_initialize(
    input_mode_type_t  *input_mode_fn_table)
{
    /* Set function pointers */
    input_mode_fn_table->setup_fn       = &input_setup;
    input_mode_fn_table->start_fn       = &input_start;
    input_mode_fn_table->print_stats_fn = NULL;
    input_mode_fn_table->stop_fn        = &input_stop;
    input_mode_fn_table->teardown_fn    = &input_teardown;

    /* initialize reader_thread to the main thread */
    reader_thread = pthread_self();

    return 0;                     /* OK */
}


/*
** Local Variables:
** mode:c
** indent-tabs-mode:nil
** c-basic-offset:4
** End:
*/
