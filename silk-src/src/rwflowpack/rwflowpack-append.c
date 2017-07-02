/*
** Copyright (C) 2006-2017 by Carnegie Mellon University.
**
** @OPENSOURCE_LICENSE_START@
** See license information in ../../LICENSE.txt
** @OPENSOURCE_LICENSE_END@
*/

/*
**  rwflowpack-append.c
**
**    Helper file for rwflowpack that implements the
**    'append-incremental' input-mode.
**
**    Specify the functions that are used to poll a directory for SiLK
**    incremental files created by a previous invocation of
**    rwflowpack.  These files are moved to the processing-directory
**    and their names are added to the output_deque so that the
**    output_thread will process them.
**
**    The flowtype and sensor of each flow record will be unchanged.
**    The format of the hourly files will be taken from the format of
**    the incremental files.
**
**    For an input_mode_type that either modifies the file format or
**    recategorizes the records to modify the flowtype and sensor, see
**    rwflowpack-stream.c.
**
**
*/

#include <silk/silk.h>

RCSIDENT("$SiLK: rwflowpack-append.c efd886457770 2017-06-21 18:43:23Z mthomas $");

#include <silk/skpolldir.h>
#include "stream-cache.h"
#include "rwflowpack_priv.h"

/* use TRACEMSG_LEVEL as our tracing variable */
#define TRACEMSG(lvl, msg) TRACEMSG_TO_TRACEMSGLVL(lvl, msg)
#include <silk/sktracemsg.h>


/* MACROS AND TYPEDEFS */

/* A name for this input_mode_type. */
#define INPUT_MODE_TYPE_NAME  "Append Incremental File Input Mode"


/* LOCAL VARIABLES */

/* There is a single processing thread for this input_mode */
static pthread_t reader_thread;

/* The directory polling object */
static skPollDir_t *polldir = NULL;

/* True as long as we are reading. */
static volatile int reading = 0;


/* FUNCTION DEFINITIONS */

/*
 *  input_reader();
 *
 *  THREAD ENTRY POINT for the 'reader_thread'.
 *
 *    The 'reader_thread' is created in input_start().
 *
 *    Get a file from the incoming_directory.  Verify that the name of
 *    the file contains a valid flowtype, sensor, and time; if not,
 *    move the file to the error directory.  If time-window
 *    verification was requested, verify that the file is within that
 *    time window; if not, move the file to the error directory.  Move
 *    the file to the processing directory and add the file's name and
 *    information to the output_deque.  Repeat until the file-scoped
 *    variable 'reading' is false or an error occurs.
 */
static void*
input_reader(
    void        UNUSED(*dummy))
{
    char in_path[PATH_MAX];
    char proc_path[PATH_MAX];
    char *in_basename;
    cache_closed_file_t *incr_path;
    skPollDirErr_t pderr;
    sk_flowtype_id_t ft;

    DEBUGMSG("Started reader thread");

    while (reading) {
        /* Get next file from the directory poller */
        pderr = skPollDirGetNextFile(polldir, in_path, &in_basename);
        if (PDERR_NONE != pderr) {
            if (PDERR_STOPPED != pderr) {
                CRITMSG("Error polling append incoming directory '%s': %s",
                        skPollDirGetDir(polldir),((PDERR_SYSTEM == pderr)
                                                  ? strerror(errno)
                                                  : skPollDirStrError(pderr)));
            }
            reading = 0;
        }
        if (!reading) {
            break;
        }

        incr_path = sk_alloc(cache_closed_file_t);

        /* Yay progress!  Go back to the SiLK 0.x days when we used
         * the file name to determine the flowtype, sensor, and
         * start-time. */
        ft = sksiteParseFilename(in_basename, &incr_path->key, NULL);
        if (SK_INVALID_FLOWTYPE == ft
            || SK_INVALID_SENSOR == incr_path->key.sensor_id)
        {
            WARNINGMSG(("Unable to parse incremental pathname for"
                        " sensor, flowtype, time '%s'. Moving to error-dir"),
                       in_path);
            dispose_incoming_file(in_path, incoming_directory, 1);
            free(incr_path);
            continue;
        }

        /* Check for incremental files outside of the time window */
        if (check_time_window) {
            int64_t diff;
            time_t t = time(NULL);

            diff = (((int64_t)t /3600) - (incr_path->key.timestamp /3600000));
            if (diff > reject_hours_past) {
                NOTICEMSG(("Skipping incremental file: First record's"
                           " timestamp occurs %" PRId64 " hours in the"
                           " past: '%s'. Repository unchanged"),
                          diff, in_path);
                dispose_incoming_file(in_path, incoming_directory, 1);
                free(incr_path);
                continue;
            }
            if (-diff > reject_hours_future) {
                dispose_incoming_file(in_path, incoming_directory, 1);
                NOTICEMSG(("Skipping incremental file: First record's"
                           " timestamp occurs %" PRId64 " hours in the"
                           " future: '%s'. Repository unchanged"),
                          -diff, in_path);
                free(incr_path);
                continue;
            }
        }

        TRACEMSG(1, ("Moving to processing-dir file '%s'", in_basename));

        if (move_to_directory(in_path, processing_directory,
                              in_basename, proc_path, sizeof(proc_path)))
        {
            INFOMSG("Ignoring file '%s'\n", in_path);
            free(incr_path);
            continue;
        }

        /* Queue this file. */
        incr_path->filename = strdup(proc_path);
        if (NULL == incr_path->filename) {
            skAppPrintOutOfMemory(NULL);
            free(incr_path);
            reading = 0;
            break;
        }
        if (skDequePushBack(output_deque, (void*)incr_path) == SKDQ_ERROR) {
            skAppPrintOutOfMemory(NULL);
            cache_closed_file_destroy(incr_path);
            reading = 0;
            break;
        }
    }

    DEBUGMSG("Finishing reader thread...");

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
 *  status = append_initialize(input_mode_fn_table);
 *
 *    Fill in the function pointers for the input_mode_type.
 */
int
append_initialize(
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
