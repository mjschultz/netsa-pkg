/*
** Copyright (C) 2003-2017 by Carnegie Mellon University.
**
** @OPENSOURCE_LICENSE_START@
** See license information in ../../LICENSE.txt
** @OPENSOURCE_LICENSE_END@
*/

/*
**  rwflowpack.c
**
**    rwflowpack is a daemon that runs as part of the SiLK flow
**    collection and packing tool-chain.  The primary job of
**    rwflowpack is to convert each incoming flow record to the SiLK
**    Flow format, categorize each incoming flow record (e.g., as
**    incoming or outgoing), set the sensor value for the record, and
**    determine which hourly file will ultimately store the record.
**
**    Assuming the required support libraries are present, rwflowpack
**    can process:
**
**        PDU records (NetFlow v5).  These can be read from a network
**        socket (UDP), from an individual file, or from a directory
**        where PDU files are continuously deposited.
**
**        IPFIX records.  These can be read from a network socket (TCP
**        or UDP) or from a directory where the files are continuously
**        deposited.  IPFIX support requires libfixbuf.
**
**        NetFlow v9 records.  These can be read from a network socket
**        (UDP).  NetFlow v9 support requires libfixbuf.
**
**        Existing SiLK Flow files.  These can be read from a
**        directory where the files are continuosly deposited.
**        rwflowpack will modify the flowtype and sensor information
**        on the records.
**
**        SiLK Flow files packaged by the flowcap daemon.  In this
**        case, rwflowpack does not do data collection; it simply
**        processes the records that flowcap collected.
**
**    Some of the input_mode_types alter the way rwflowpack operates.  For
**    example, when reading a single file of PDU records, the
**    input_mode_type will disable the daemonization of rwflowpack, so
**    that rwflowpack will process a single PDU file and exit.  These
**    changes to rwflowpack's operation are reflected in the
**    --input-mode switch.
**
**    For the output from rwflowpack, rwflowpack can create the hourly
**    files locally, or it can create "incremental files", which are
**    small files that the rwflowappend daemon can combine into hourly
**    files.  Using these incremental files allows rwflowpack to run
**    on a different machine than where the repository is located.
**
**    There are two concepts in rwflowpack that are easy to confuse.
**
**    The first concept is the "input_mode_type".  In general, a
**    input_mode_type contains the knowledge necessary to process one of
**    the types of inputs that rwflowpack supports.  (The IPFIX and
**    NetFlow v9 records are handled by the same input_mode_type.  The
**    input_mode_type to process PDU records from the network is different
**    from the input_mode_type to process PDU records from a file.  The
**    information for each input_mode_type is located in a separate file,
**    and file pointers are used to access the functions that each
**    input_mode_type provides.
**
**    The second concept is the "flow_processor".  The flow_processor
**    can be thought of as an "instance" of the input_mode_type.  A
**    flow_processor is associated with a specific collection point
**    for incoming flow records.
**
*/

#define RWFLOWPACK_SOURCE 1
#include <silk/silk.h>

RCSIDENT("$SiLK: rwflowpack.c 21315f05de74 2017-06-26 20:03:25Z mthomas $");

#include <dlfcn.h>

#include <silk/skdaemon.h>
#include <silk/sksite.h>
#include <silk/sktimer.h>
#include <silk/skvector.h>
#include "rwflowpack_priv.h"
#include "stream-cache.h"

/* use TRACEMSG_LEVEL as our tracing variable */
#define TRACEMSG(lvl, msg) TRACEMSG_TO_TRACEMSGLVL(lvl, msg)
#include <silk/sktracemsg.h>


/*
 *    MAX FILE HANDLE NOTES
 *
 *    In response to attempts to use 100+ probes that polled
 *    directories which caused us to run out of file handles, we tried
 *    to make some of the code smarter about the number of files
 *    handles we use.
 *
 *    However, currently we only look at polldir numbers, and we do
 *    not consider the number of file handles that we have open to
 *    read from the network.  One issue is we don't know how many that
 *    is until after we start.
 *
 *    We could be smarter and set the number of poll dir handles after
 *    we see how many polldirs we are actually using.
 *
 *    We could use sysconf(_SC_OPEN_MAX) to get the max number of file
 *    handles available and set our values based on that.
 */


/* MACROS AND DATA TYPES */

/* The signal the reader thread (the manageProcessor() function) sends
 * to main thread to indicate that the reader thread is done.
 * Normally SIGUSR2, but you may need to use a different value when
 * running under a debugger, valgrind, etc. */
#ifndef READER_DONE_SIGNAL
#  define READER_DONE_SIGNAL  SIGUSR2
#endif


/* EXPORTED VARIABLES */

/* structure of function pointers for the input_mode being used */
input_mode_type_t input_mode_type;

/* non-zero when file locking is disabled */
int no_file_locking = 0;

/* Size of the cache for writing output files.  Can be modified by
 * --file-cache-size switch. */
uint32_t stream_cache_size;

/* Number of seconds between cache flushes */
uint32_t flush_timeout;

/* In stream input mode, this holds the default settings for disposal
 * of files from directory-based probes.  In addition, this determines
 * the default polling interval for those probes. */
packconf_directory_t *stream_directory_defaults = NULL;

/* Incoming directory used by the input-modes append and fcfiles. */
packconf_directory_t *incoming_directory = NULL;

/* As of SiLK 4.0, rwflowpack always creates incremental files first.
 * This is the directory where the incremental files are created.
 * These files are flushed every flush_timeout seconds.  This is set
 * by the --processing-directory switch. */
const char *processing_directory = NULL;

/* When the output_mode is "local-storage" (which is the default),
 * every flush_timeout seconds rwflowpack appends the incremental
 * files is has created to the hourly files located under the root
 * directory (sksiteGetRootDir()). */

/* In the "incremental-files" output_mode, every flush_timeout seconds
 * rwflowpack closes the incremental files and moves them to this
 * destination directory.  This is set by the --incremental-directory
 * switch. */
const char *incremental_directory = NULL;

/* Command to run on newly created hourly files */
const char *hour_file_command = NULL;

/* deque that contains incremental files that need to be processed */
sk_deque_t *output_deque = NULL;

/* oldest file (in hours) that is considered acceptable.  incremental
 * files older than this will be put into the error directory. */
int64_t reject_hours_past = INT64_MAX;

/* how far into the future are incremental files accepted. */
int64_t reject_hours_future = INT64_MAX;

/* whether reject_hours_past and/or reject_hours_future differ from
 * their default values--meaning we need to run the tests */
int check_time_window = 0;

/* Where to write flowcap files */
const char *destination_directory = NULL;

/* for one-destination output mode, where to write the data */
const char *one_destination_path = NULL;

/* for one-destination output mode, the information about the format
 * and sidecar for that file */
const packer_fileinfo_t *one_destination_fileinfo = NULL;

/* To ensure records are sent along in a timely manner, the files are
 * closed when a timer fires or once they get to a certain size.
 * These variables define those values. */
uint64_t max_file_size = 0;

/* Timer base (0 if none) from which we calculate timeouts */
sktime_t clock_time = 0;

/* Amount of disk space to allow for a new file when determining
 * whether there is disk space available for flowcap stuff.  This will
 * be max_file_size plus some overhead should the compressed data be
 * larger than the raw data. */
uint64_t alloc_file_size = 0;

/* leave at least this much free space on the disk; specified by
 * --freespace-minimum.  Gets set to DEFAULT_FREESPACE_MINIMUM */
int64_t freespace_minimum_bytes = -1;

/* take no more that this amount of the disk; as a percentage.
 * specified by --space-maximum-percent */
double usedspace_maximum_percent = 0;

/* default input and output modes */
io_mode_t input_mode = INPUT_STREAM;
io_mode_t output_mode = OUTPUT_LOCAL_STORAGE;

/* number of appender threads to run */
uint32_t appender_count = 1;

/* Set to true once the input thread(s) have started.  This is used by
 * the output thread to know whether to check the thread count. */
int input_thread_started = 0;


/* LOCAL VARIABLES */

/* The number of processing threads current running */
static size_t thread_count = 0;

/* Mutex controlling access to 'thread_count' */
static pthread_mutex_t thread_count_mutex = PTHREAD_MUTEX_INITIALIZER;

/* This becomes non-zero when rwflowpack is shutting down */
static volatile int shutting_down = 0;

/* Set to true once skdaemonized() has been called---regardless of
 * whether the --no-daemon switch was given. */
static int daemonized = 0;

/* suffix used for mkstemp().  Note: we use sizeof() on this */
static const char temp_suffix[] = ".XXXXXX";

/* control thread */
static pthread_t main_thread;

/* when the output_mode is incremental-files, this is the thread that
 * handles reading the names of incremental files from the
 * output_deque and moving the files to the incremental_directory */
static pthread_t mover_thread;

/* Timer that flushes files every so often */
static sk_timer_t *timer_thread = NULL;

/* All open incremental files to which we are writing */
static stream_cache_t *stream_cache;

/* Maximum number of input file handles and the number remaining.
 * They are computed as a fraction of the stream_cache_size.  */
static int input_filehandles_max;
static int input_filehandles_left;

/* Mutex controlling access to the 'input_filehandles' variables */
static pthread_mutex_t input_filehandles_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  input_filehandles_cond  = PTHREAD_COND_INITIALIZER;


/* LOCAL FUNCTION PROTOTYPES */

static void stop_timer(void);
static void close_and_queue_files(void);


/* FUNCTION DEFINITIONS */

/*
 *  appTeardown()
 *
 *    Teardown all modules, close all files, and tidy up all
 *    application state.
 *
 *    This function is idempotent.
 */
void
appTeardown(
    void)
{
    static int teardownFlag = 0;
    cache_closed_file_t *incr_path;
    skpc_probe_iter_t iter;
    const skpc_probe_t *probe;

    if (teardownFlag) {
        return;
    }
    teardownFlag = 1;

    sk_ipset_cache_destroy(&ipset_cache);

    if (!daemonized) {
        free((void*)incremental_directory);
        incremental_directory = NULL;
        free((void*)processing_directory);
        processing_directory = NULL;
        free((void*)one_destination_path);
        one_destination_path = NULL;
        packconf_directory_destroy(stream_directory_defaults);
        stream_directory_defaults = NULL;
        packconf_directory_destroy(incoming_directory);
        incoming_directory = NULL;
        packer_fileinfo_destroy((packer_fileinfo_t *)one_destination_fileinfo);
        one_destination_fileinfo = NULL;

        if (input_mode_type.teardown_fn) {
            input_mode_type.teardown_fn();
        }
        appender_teardown();
        skpcTeardown();
        skdaemonTeardown();
        skAppUnregister();
        return;
    }

    if (input_mode == INPUT_SINGLEFILE) {
        INFOMSG("Finishing rwflowpack...");
    } else {
        INFOMSG("Begin shutting down...");
    }
    shutting_down = 1;

    /* broadcast so any poll-dir probes that are waiting for a file
     * handle will wake up and begin shutting down. */
    pthread_cond_broadcast(&input_filehandles_cond);

    input_mode_type.stop_fn();

    stop_timer();

    if (stream_cache) {
        /* Destroy the cache, flushing, closing and freeing all the
         * open streams.  We're in shutdown, so ignore the return
         * code. */
        sk_vector_t *vector = NULL;
        void *v;
        size_t i;

        INFOMSG("Closing incremental files...");
        (void)skCacheCloseAll(stream_cache, &vector);

        if (!vector) {
            CRITMSG("Error closing incremental files");
        } else if (0 == skVectorGetCount(vector)) {
            NOTICEMSG("No incremental files to close.");
            skVectorDestroy(vector);
        } else {
            /* Report number of records in each all files in the
             * vector; delete the entries as we process each one. */
            for (i = 0; (v = skVectorGetValuePointer(vector, i)); ++i) {
                incr_path = *((cache_closed_file_t**)v);

                INFOMSG(("%s: %" PRIu64 " recs"),
                        incr_path->filename,  incr_path->rec_count);
                cache_closed_file_destroy(incr_path);
            }
            skVectorDestroy(vector);
        }

        skCacheDestroy(stream_cache);
        stream_cache = NULL;
    }

    if (output_deque) {
        skDequeUnblock(output_deque);
    }

    input_mode_type.teardown_fn();

    if (OUTPUT_LOCAL_STORAGE == output_mode) {
        appender_stop();
        appender_teardown();
    }

    /* teardown the packing function on each probe */
    skpcProbeIteratorBind(&iter);
    while (skpcProbeIteratorNext(&iter, &probe) == 1) {
        skpcProbeTeardownPacker((skpc_probe_t *)probe);
    }

    if (mover_thread != pthread_self()) {
        INFOMSG("Waiting for mover thread to finish...");
        pthread_join(mover_thread, NULL);
        INFOMSG("Mover thread has finished.");
    }

    if (output_deque) {
        /* Clean up anything left in the deque */
        skDequeBlock(output_deque);
        while (skDequePopFrontNB(output_deque, (void**)&incr_path)
               == SKDQ_SUCCESS)
        {
            INFOMSG(("%s: %" PRIu64 " recs"),
                    incr_path->filename,  incr_path->rec_count);
            cache_closed_file_destroy(incr_path);
        }
        skDequeDestroy(output_deque);
    }

    free((void*)incremental_directory);
    incremental_directory = NULL;
    free((void*)processing_directory);
    processing_directory = NULL;
    free((void*)one_destination_path);
    one_destination_path = NULL;
    packconf_directory_destroy(stream_directory_defaults);
    stream_directory_defaults = NULL;
    packconf_directory_destroy(incoming_directory);
    incoming_directory = NULL;
    packer_fileinfo_destroy((packer_fileinfo_t *)one_destination_fileinfo);
    one_destination_fileinfo = NULL;

    /* teardown the probe configuration */
    skpcTeardown();

    if (input_mode == INPUT_SINGLEFILE) {
       INFOMSG("Finished processing file.");
    } else {
       INFOMSG("Finished shutting down.");
    }
    skdaemonTeardown();
    skthread_teardown();
    skAppUnregister();
}


/*
 *  empty_signal_handler(signal);
 *
 *    Do nothing.  This callback is invoked when the global
 *    thread_count goes to 0.  See also decrement_thread_count().
 */
static void
empty_signal_handler(
    int          UNUSED(s))
{
    return;
}


/*
 *  increment_thread_count();
 *
 *    Increase by one the count of the processing threads.
 */
void
increment_thread_count(
    void)
{
    pthread_mutex_lock(&thread_count_mutex);
    ++thread_count;
    pthread_mutex_unlock(&thread_count_mutex);
}


/*
 *  decrement_thread_count(send_signal_to_main);
 *
 *    Decrease by one the count of the processing threads.  If the
 *    argument is non-zero and the number of threads is 0, send a
 *    signal to the main thread.
 */
void
decrement_thread_count(
    int                 send_signal_to_main)
{
    int signal_main = 0;

    pthread_mutex_lock(&thread_count_mutex);
    if (thread_count) {
        --thread_count;
    }
    if (send_signal_to_main && 0 == thread_count) {
        signal_main = 1;
    }
    pthread_mutex_unlock(&thread_count_mutex);
    if (signal_main) {
        pthread_kill(main_thread, READER_DONE_SIGNAL);
    }
}


/*
 *  count = get_thread_count();
 *
 *    Return the number of input and output threads that are currently
 *    running.
 */
size_t
get_thread_count(
    void)
{
    size_t count_snapshot;

    pthread_mutex_lock(&thread_count_mutex);
    count_snapshot = thread_count;
    pthread_mutex_unlock(&thread_count_mutex);
    return count_snapshot;
}


/* Acquire a file handle.  Return 0 on success, or -1 if we have
 * started shutting down.  */
int
flowpackAcquireFileHandle(
    void)
{
    int rv = -1;

    pthread_mutex_lock(&input_filehandles_mutex);
    while (input_filehandles_left <= 0 && !shutting_down) {
        pthread_cond_wait(&input_filehandles_cond, &input_filehandles_mutex);
    }
    if (!shutting_down) {
        --input_filehandles_left;
        rv = 0;
    }
    pthread_mutex_unlock(&input_filehandles_mutex);
    return rv;
}


/* Release the file handle. */
void
flowpackReleaseFileHandle(
    void)
{
    pthread_mutex_lock(&input_filehandles_mutex);
    ++input_filehandles_left;
    pthread_cond_signal(&input_filehandles_cond);
    pthread_mutex_unlock(&input_filehandles_mutex);
}


/* Change the maximum number of input filehandles we can use. */
int
flowpackSetMaximumFileHandles(
    int                 new_max_fh)
{
    if (new_max_fh < 1) {
        return -1;
    }

    pthread_mutex_lock(&input_filehandles_mutex);
    input_filehandles_left += new_max_fh - input_filehandles_max;
    input_filehandles_max = new_max_fh;
    pthread_mutex_unlock(&input_filehandles_mutex);

    return 0;
}


/*
 *  repeat = timer_thread_main(NULL);
 *
 *  THREAD ENTRY POINT
 *
 *    This function is the callback function that is invoked every
 *    'flush_timeout' seconds by the skTimer_t timer_thread.
 *
 *    Calls the print_stats_fn() for the input_mode_type, then calls
 *    close_and_queue_files() to process any incremental files that
 *    exist in the processing-directory.
 */
static skTimerRepeat_t
timer_thread_main(
    void        UNUSED(*dummy))
{
    NOTICEMSG("Flushing files after %" PRIu32 " seconds...", flush_timeout);
    if (input_mode_type.print_stats_fn) {
        input_mode_type.print_stats_fn();
    }
    close_and_queue_files();

    return SK_TIMER_REPEAT;
}


/*
 *  stream = create_incremental_file(key, file_info);
 *
 *    Create an open stream in the processing-directory to store flow
 *    records for the 'flowtype', hourly 'timestamp', and 'sensor_id'
 *    contained in the structure 'key'.
 *
 *    Returns the stream on success, or NULL on error.
 *
 *    This a callback function that will be invoked by the stream
 *    cache function, skCacheLookupOrOpenAdd() when it needs to open a
 *    new file.
 */
static skstream_t*
create_incremental_file(
    const sksite_repo_key_t    *key,
    void                       *v_file_info)
{
    const packer_fileinfo_t *file_info = (packer_fileinfo_t*)v_file_info;
    char hourly_path[PATH_MAX];
    char process_path[PATH_MAX];
    char *hourly_basename;
    skstream_t *stream = NULL;
    sk_file_header_t *hdr;
    size_t sz;
    ssize_t rv;

    TRACEMSG(1, (("create_incremental_file() called for"
                  " {flowtype = %u, sensor = %u, time = %" PRId64 "}"),
                 key->flowtype_id, key->sensor_id, (int64_t)key->timestamp));

    /* initialize variables */
    process_path[0] = '\0';

    /* Build the file name--WHERE the records will be written onto
     * disk.  We need to get the basename of the hourly file, which we
     * do by generating the complete path to the hourly file. */
    if (!sksiteGeneratePathname(hourly_path, sizeof(hourly_path), key,
                                "", NULL, &hourly_basename))
    {
        CRITMSG(("Unable to generate pathname to file"
                 " {flowtype = %u, sensor = %u, time = %" PRId64 "}"),
                key->flowtype_id, key->sensor_id, (int64_t)key->timestamp);
        goto ERROR;
    }

    TRACEMSG(2, ("Incremental file basename is '%s'", hourly_basename));

    /* This is the pathname of the file we will open. */
    sz = (size_t)snprintf(process_path, sizeof(process_path), "%s/%s%s",
                          processing_directory, hourly_basename, temp_suffix);
    if (sz >= sizeof(process_path)) {
        CRITMSG("Placeholder pathname exceeds maximum size for '%s'",
                hourly_basename);
        process_path[0] = '\0';
        goto ERROR;
    }

    INFOMSG("Opening new incremental file '%s'", hourly_basename);

    /* Open the file; making sure its name is unique */
    if ((rv = skStreamCreate(&stream, SK_IO_WRITE, SK_CONTENT_SILK_FLOW))
        || (rv = skStreamBind(stream, process_path))
        || (rv = skStreamMakeTemp(stream)))
    {
        skStreamPrintLastErr(stream, rv, &CRITMSG);
        process_path[0] = '\0';
        goto ERROR;
    }
    if (file_info->sidecar) {
        skStreamSetSidecar(stream, file_info->sidecar);
    }

    TRACEMSG(1, ("Opened new incremental file '%s'",
                 skStreamGetPathname(stream)));

    {
        /* Get file's header and fill it in */
        hdr = skStreamGetSilkHeader(stream);
        if ((rv = skHeaderSetFileFormat(hdr, file_info->record_format))
            || (rv = skHeaderSetRecordVersion(hdr, file_info->record_version))
            || (rv = skHeaderSetCompressionMethod(hdr, file_info->comp_method))
            || (rv = skHeaderSetByteOrder(hdr, file_info->byte_order))
            || (rv = skHeaderAddPackedfile(hdr, key))
            || (rv = skStreamWriteSilkHeader(stream)))
        {
            skStreamPrintLastErr(stream, rv, &CRITMSG);
            goto ERROR;
        }

        TRACEMSG(2, ("Wrote header for incremental file '%s'",
                     hourly_basename));
    }

    return stream;

  ERROR:
    if (stream) {
        TRACEMSG(2, ("Destroying stream"));
        skStreamDestroy(&stream);
    }
    if (process_path[0]) {
        TRACEMSG(2, ("Unlinking incremental path '%s'", process_path));
        unlink(process_path);
    }
    return NULL;
}


int
dispose_incoming_file(
    const char                 *filepath,
    const packconf_directory_t *src_dir,
    int                         has_error)
{
    const char *filename;
    char dest_path[PATH_MAX];
    ssize_t rv;

    /* basename */
    filename = strrchr(filepath, '/');
    if (filename) {
        ++filename;
    } else {
        filename = filepath;
    }

    if (has_error) {
        /* fill 'dest_path' with name of the destination */
        rv = snprintf(dest_path, sizeof(dest_path), "%s/%s",
                      src_dir->d_error_directory, filename);
        if (sizeof(dest_path) <= (size_t)rv) {
            ERRMSG(("Error directory path too long for '%s'"
                    " (%" SK_PRIdZ "chars)"),
                   filename, rv);
            return -1;
        }
        /* move file */
        rv = skMoveFile(filepath, dest_path);
        if (rv != 0) {
            ERRMSG("Could not move '%s' to '%s': %s",
                   filepath, dest_path, strerror(rv));
            return -1;
        }
        return 0;
    }

    if (NULL == src_dir->d_archive_directory) {
        /* remove file */
        if (unlink(filepath) == -1) {
            ERRMSG("Could not remove '%s': %s",
                   filepath, strerror(errno));
            return -1;
        }
        return 1;
    }

    if (src_dir->d_flat_archive) {
        /* file goes directly into the archive_directory */
        rv = snprintf(dest_path, sizeof(dest_path), "%s/%s",
                      src_dir->d_archive_directory, filename);
        if (sizeof(dest_path) <= (size_t)rv) {
            WARNINGMSG(("Archive directory path too long for '%s'"
                        " (%" SK_PRIdZ "chars); trying error directory"),
                       filename, rv);
            return dispose_incoming_file(filepath, src_dir, 1);
        }
    } else {
        /* create archive path based on current UTC time:
         * ARCHIVE/YEAR/MONTH/DAY/HOUR/FILE */
        time_t curtime;
        struct tm ctm;
        char *s;

        curtime = time(NULL);
        gmtime_r(&curtime, &ctm);
        rv = snprintf(dest_path, sizeof(dest_path),"%s/%04d/%02d/%02d/%02d/%s",
                      src_dir->d_archive_directory, (ctm.tm_year + 1900),
                      (ctm.tm_mon + 1), ctm.tm_mday, ctm.tm_hour, filename);
        if (sizeof(dest_path) <= (size_t)rv) {
            WARNINGMSG(("Archive directory path too long for '%s'"
                        " (%" SK_PRIdZ "chars); trying error directory"),
                       filename, rv);
            return dispose_incoming_file(filepath, src_dir, 1);
        }

        /* make the destination directory */
        s = strrchr(dest_path, '/');
        *s = '\0';
        rv = skMakeDir(dest_path);
        if (rv != 0) {
            ERRMSG("Could not create directory '%s': %s",
                   dest_path, strerror(errno));
            WARNINGMSG("Trying error directory for file '%s'", filepath);
            return dispose_incoming_file(filepath, src_dir, 1);
        }
        *s = '/';
    }

    /* move file */
    rv = skMoveFile(filepath, dest_path);
    if (rv != 0) {
        ERRMSG("Could not move '%s' to '%s': %s",
               filepath, dest_path, strerror(rv));
        return -1;
    }

    if (src_dir->d_post_archive_command) {
        char *expanded_cmd;

        expanded_cmd = (skSubcommandStringFill(
                            src_dir->d_post_archive_command, "s", dest_path));
        if (NULL == expanded_cmd) {
            WARNINGMSG("Unable to allocate memory to create command string");
            return 0;
        }
        DEBUGMSG("Running post_archive_command: %s", expanded_cmd);
        rv = skSubcommandExecuteShell(expanded_cmd);
        switch (rv) {
          case -1:
            ERRMSG("Unable to fork to run post_archive_command: %s",
                   strerror(errno));
            break;
          case -2:
            NOTICEMSG("Error waiting for child: %s", strerror(errno));
            break;
          default:
            assert(rv > 0);
            break;
        }
        free(expanded_cmd);
    }

    return 0;
}


/*
 *  ok = write_record(rwrec, key, file_info);
 *
 *    Write the SiLK Flow record 'rwrec' to an incremental file.
 *
 *    This function is expected to be invoked by the pack_record_fn on
 *    the packing logic plug-in packlogic.
 */
static int
write_record(
    const rwRec                *rwrec,
    const sksite_repo_key_t    *key,
    const packer_fileinfo_t    *file_info)
{
    cache_entry_t *entry;
    int rv;

    assert(file_info != NULL);
    assert(key != NULL);

    /* Get the file from the cache, which may use an open file, open
     * an existing file, or create a new file as required.  If the
     * file does not exist, this function will invoke
     * create_incremental_file() to create the file.  */
    rv = skCacheLookupOrOpenAdd(stream_cache, key, (void*)file_info, &entry);
    if (rv) {
        if (-1 == rv) {
            /* problem opening file or adding file to cache */
            CRITMSG("Error opening file --  shutting down");
        } else if (1 == rv) {
            /* problem closing existing cache entry */
            CRITMSG("Error closing file -- shutting down");
        } else {
            CRITMSG(("Unexpected error code from stream cache %d"
                     " -- shutting down"),
                    rv);
        }
        return -1;
    }

    /* Write record */
    rv = skStreamWriteRecord(skCacheEntryGetStream(entry), rwrec);
    if (SKSTREAM_OK != rv) {
        if (SKSTREAM_ERROR_IS_FATAL(rv)) {
            skStreamPrintLastErr(skCacheEntryGetStream(entry), rv, &ERRMSG);
            CRITMSG("Error writing record --  shutting down");
            skCacheEntryRelease(entry);
            return -1;
        }
        skStreamPrintLastErr(skCacheEntryGetStream(entry), rv, &WARNINGMSG);
        /* Should we send a return code to the packer so they can try
         * again with a different file format?  That is probably
         * pointless---at this point the file exists, so there is not
         * really a way to try with a different format. */
    }

    /* unlock stream */
    skCacheEntryRelease(entry);

    return 0;
}


/*
 *    Implementation of the Lua function
 *
 *    write_rwrec(rec, file_info_table)
 *
 *    that is used when the record is being written to a data
 *    repository.  This is used in the OUTPUT_LOCAL_STORAGE and
 *    OUTPUT_INCREMENTAL_FILES output-modes.
 *
 *    The caller must provide the file_info_table that specifies the
 *    format of the file and any sidecar fields to enable in the
 *    file's header.
 *
 *    See also onedest_write_rwrec_lua().
 */
int
repo_write_rwrec_lua(
    lua_State          *L)
{
    sk_sidecar_t **sc;
    const rwRec *rec;
    const char *k;
    const char *str;
    lua_Integer i;
    sksite_repo_key_t key;
    packer_fileinfo_t file_info;
    int have_fileinfo;
    int isnum;

    file_info.sidecar = NULL;

    have_fileinfo = (lua_gettop(L) >= 2);

    /* record */
    rec = sk_lua_checkrwrec(L, 1);

    /* fill the repo-key based on values on the record */
    key.timestamp = rwRecGetStartTime(rec);
    key.timestamp -= key.timestamp % 3600000;

    key.sensor_id = rwRecGetSensor(rec);
    if (!sksiteSensorExists(key.sensor_id)) {
        if (key.sensor_id == SK_INVALID_SENSOR) {
            return luaL_error(L, "record's sensor id is invalid");
        }
        return luaL_error(L, "record's sensor id %u is not valid",
                          key.sensor_id);
    }

    key.flowtype_id = rwRecGetFlowType(rec);
    if (!sksiteFlowtypeExists(key.flowtype_id)) {
        if (key.flowtype_id == SK_INVALID_FLOWTYPE) {
            return luaL_error(L, "record's classtype_id is invalid");
        }
        return luaL_error(L, "record's classtype_id %u is not valid",
                          key.flowtype_id);
    }

    /*
     *  The file_info table is only needed when a new file is being
     *  opened.  Unfortunately, there is no way for this function to
     *  know whether a new file is being opened, and the function
     *  parses the table for each record.
     *
     *  Perhaps a file_info light user-data object visible to both Lua
     *  and C is needed to hold this information.  The object could
     *  then be created outside of the record processing loop and a
     *  handle to it could be passed into this function.
     *
     *  Having a separate object may also help when running in flowcap
     *  mode and there are multiple probes but they all use the same
     *  file_info.  The probes could share the object instead of each
     *  having its own table.
     */

    if (!have_fileinfo) {
        file_info.record_format = FT_RWIPV6ROUTING;
        file_info.record_version = SK_RECORD_VERSION_ANY;
        file_info.byte_order = SILK_ENDIAN_NATIVE;
        file_info.comp_method = skCompMethodGetDefault();
        file_info.sidecar = NULL;

        if (write_record(rec, &key, &file_info)) {
            return luaL_error(L, "write_record error");
        }
        return 0;
    }

    /* record format */
    k = "record_format";
    switch (lua_getfield(L, 2, k)) {
      case LUA_TNIL:
        return sk_lua_argerror(L, 2, "required key %s is not present", k);
      case LUA_TSTRING:
        str = lua_tostring(L, -1);
        file_info.record_format = skFileFormatFromName(str);
        if (!skFileFormatIsValid(file_info.record_format)) {
            return sk_lua_argerror(L, 2,
                                   "key %s '%s' is not a valid file format",
                                   k, str);
        }
        break;
      case LUA_TNUMBER:
        i = lua_tointegerx(L, -1, &isnum);
        if (isnum && i >= 0 && i < UINT8_MAX) {
            file_info.record_format = i;
            if (!skFileFormatIsValid(file_info.record_format)) {
                return (sk_lua_argerror(
                            L, 2, "key %s '%L' is not a valid file format id",
                            k, i));
            }
            break;
        }
        /* fall through */
      default:
        return sk_lua_argerror(L, 2,
                               "key %s is not a valid file format name or id",
                               k);
    }
    lua_pop(L, 1);

    /* record version */
    k = "record_version";
    switch (lua_getfield(L, 2, k)) {
      case LUA_TNIL:
      case LUA_TNONE:
        file_info.record_version = SK_RECORD_VERSION_ANY;
        break;
      case LUA_TNUMBER:
        i = lua_tointegerx(L, -1, &isnum);
        if (isnum) {
            file_info.record_version = i;
            break;
        }
        /* fall through */
      default:
        return sk_lua_argerror(L, 2, "key %s is not a valid file version",
                               k);
    }
    lua_pop(L, 1);

    /* byte_order */
    k = "byte_order";
    switch (lua_getfield(L, 2, k)) {
      case LUA_TNIL:
      case LUA_TNONE:
        file_info.byte_order = SILK_ENDIAN_ANY;
        break;
      case LUA_TSTRING:
        /* it would be nice to have some way for the caller to do the
         * parsing of the byte order one time */
        str = lua_tostring(L, -1);
        if (byteOrderParse(str, &file_info.byte_order)) {
            file_info.byte_order = SILK_ENDIAN_ANY;
            return sk_lua_argerror(L, 2,
                                   "key %s '%s' is not a valid byte order",
                                   k, str);
        }
        break;
      default:
        return sk_lua_argerror(L, 2, "key %s is not a valid byte_order", k);
    }
    lua_pop(L, 1);

    /* compression method */
    k = "compression_method";
    switch (lua_getfield(L, 2, k)) {
      case LUA_TNIL:
      case LUA_TNONE:
        file_info.comp_method = SK_COMPMETHOD_DEFAULT;
        break;
      case LUA_TSTRING:
        /* it would be nice to have some way for the caller to do the
         * parsing of the compression method one time */
        str = lua_tostring(L, -1);
        if (skCompMethodSetFromConfigFile(
                NULL, NULL, str, &file_info.comp_method))
        {
            file_info.comp_method = SK_COMPMETHOD_DEFAULT;
            return sk_lua_argerror(L, 2, "key %s '%s' is not a valid method",
                                   k, str);
        }
        break;
      default:
        return sk_lua_argerror(L, 2, "key %s is not a valid method", k);
    }
    lua_pop(L, 1);


    /* sidecar */
    k = "sidecar";
    switch (lua_getfield(L, 2, k)) {
      case LUA_TNIL:
      case LUA_TNONE:
        file_info.sidecar = NULL;
        break;
      case LUA_TUSERDATA:
        sc = sk_lua_tosidecar(L, -1);
        if (sc) {
            file_info.sidecar = *sc;
            break;
        }
        /* FALLTHROUGH */
      default:
        return sk_lua_argerror(L, 2, "key %s is not a valid sidecar", k);
    }
    lua_pop(L, 1);

    if (write_record(rec, &key, &file_info)) {
        return luaL_error(L, "write_record error");
    }
    return 0;
}


/*
 *  status = startTimer();
 *
 *    Start the timer thread.  Return 0 on success, or -1 on failure.
 */
static int
start_timer(
    void)
{
    int rv;

    if (input_mode != INPUT_SINGLEFILE) {
        INFOMSG("Starting flush timer");
        rv = skTimerCreate(
            &timer_thread, flush_timeout, timer_thread_main, NULL, 0);
        if (rv) {
            ERRMSG("Unable to start flush timer: %s", strerror(rv));
            return -1;
        }
    }
    return 0;
}


/*
 *  stop_timer();
 *
 *    Stop the timer thread if it is running.
 */
static void
stop_timer(
    void)
{
    if (timer_thread != NULL) {
        DEBUGMSG("Stopping timer");
        skTimerDestroy(timer_thread);
    }
}


/*
 *  status = move_to_directory(in_path,out_dir,out_basename,result,result_sz);
 *
 *    Move the file whose full path is 'in_path' into the directory
 *    named by 'out_dir' and attempt to name the file 'out_basename'.
 *    Return 0 on success, or -1 on failure.
 *
 *    The filename in 'out_basename' is expected to have a mkstemp()
 *    type extension on the end.  This function attempts to move the
 *    file into the 'out_dir' without changing the extension.
 *    However, if a file with that name already exists, an additional
 *    mkstemp()-type extension is added.
 *
 *    If 'result' is non-NULL, it must be a character array whose size
 *    is specified by 'result_sz', and 'result' will be filled with
 *    the complete path of resulting file.
 */
int
move_to_directory(
    const char         *in_path,
    const char         *out_dir,
    const char         *out_basename,
    char               *result,
    size_t              result_sz)
{
    char out_path[PATH_MAX];
    int fd;
    int rv;

    TRACEMSG(1, ("Moving file '%s'", in_path));

    if (!result || !result_sz) {
        result = out_path;
        result_sz = sizeof(out_path);
    }

    /* Generate template for the new path in the final location */
    if ((size_t)snprintf(result, result_sz, "%s/%s", out_dir, out_basename)
        >= result_sz)
    {
        WARNINGMSG(("Not moving file:"
                    " Destination path exceeds maximum size for '%s'"),
                   in_path);
        return -1;
    }

    /* Attempt to exclusively create the destination */
    fd = open(result, O_RDWR | O_CREAT | O_EXCL, 0644);
    if (-1 == fd) {
        TRACEMSG(1, ("Failed to create file '%s': %s",
                     result, strerror(errno)));

        /* Append an additional suffix to the file */
        if ((size_t)snprintf(result, result_sz, "%s/%s%s",
                             out_dir, out_basename, temp_suffix+1)
            >= result_sz)
        {
            WARNINGMSG(("Not moving file:"
                        " Destination path exceeds maximum size for '%s'"),
                       in_path);
            return -1;
        }

        /* Fill the template */
        fd = mkstemp(result);
        if (-1 == fd) {
            ERRMSG("Could not create and open temporary file '%s': %s",
                   result, strerror(errno));
            return -1;
        }
    }

    /* Opened the destination; we can simply copy the file */
    TRACEMSG(1, ("Opened destination file '%s'", result));
    close(fd);

    /* Move the in_path over the result */
    rv = skMoveFile(in_path, result);
    if (rv != 0) {
        ERRMSG("Could not move file '%s' to '%s': %s",
               in_path, result, strerror(rv));
        return -1;
    }

    return 0;
}


/*
 *  mover_thread_main(NULL);
 *
 *  THREAD ENTRY POINT
 *
 *    Thread entry point for the mover_thread.
 *
 *    Pops a file from the output_deque and passes the file name to
 *    the appropriate function---where the appropriate function
 *    depends on the output mode.
 *
 *    Once the input threads have started, this function awakes from
 *    waiting on the Deque every second to see if this thread is the
 *    only one that is running.  If it is, the function exits since
 *    there is no more data to process.
 */
static void*
mover_thread_main(
    void        UNUSED(*dummy))
{
    cache_closed_file_t *incr_path;
    const char *in_basename;
    skDQErr_t dqerr;

    assert(OUTPUT_INCREMENTAL_FILES == output_mode);

    INFOMSG("Started mover thread.");

    while (!shutting_down) {
        incr_path = NULL;

        /* Get the name of the next file to handle */
        dqerr = skDequePopFrontTimed(output_deque, (void**)&incr_path, 1);
        if (dqerr != SKDQ_SUCCESS) {
            /* deque probably timedout */
            if (SKDQ_TIMEDOUT == dqerr) {
                if (input_thread_started && get_thread_count() == 1) {
                    /* stop running once the inputs have been started
                     * and this is the only thread remaining */
                    break;
                }
                continue;
            }
            if (incr_path) {
                cache_closed_file_destroy(incr_path);
            }
            if (dqerr != SKDQ_UNBLOCKED) {
                CRITMSG("Unexpected error condition [%d] from deque at %s:%d",
                        (int)dqerr, __FILE__, __LINE__);
            }
            break;
        }

        if (incr_path->rec_count) {
            INFOMSG(("%s: %" PRIu64 " recs"),
                    incr_path->filename, incr_path->rec_count);
        }

        in_basename = strrchr(incr_path->filename, '/');
        if (!in_basename) {
            in_basename = incr_path->filename;
        } else {
            ++in_basename;
        }

        /* for now, ignore errors with incremental files */
        (void)move_to_directory(incr_path->filename, incremental_directory,
                                in_basename, NULL, 0);
        cache_closed_file_destroy(incr_path);
    }

    INFOMSG("Finishing mover thread.");

    decrement_thread_count(1);

    return NULL;
}


/*
 *  close_and_queue_files();
 *
 *    Called repeatedly by the timer_thread_main() function every
 *    flush_timeout seconds.  (In the single-file input-mode, called after
 *    the single file is procesed).
 *
 *    Flushes the files in the stream cache and adds the files to the
 *    output_deque for processing by the mover_thread or an
 *    appender_state[].thread.
 */
static void
close_and_queue_files(
    void)
{
    sk_vector_t *incr_path_vector = NULL;
    cache_closed_file_t *incr_path;
    void *v;
    size_t file_count = 0;
    size_t i;

    NOTICEMSG("Preparing to close incremental files"
              " and queue for output processing...");

    /* Close all the output files. */
    INFOMSG("Closing incremental files...");
    if (skCacheCloseAll(stream_cache, &incr_path_vector)
        || (NULL == incr_path_vector))
    {
        CRITMSG("Error closing incremental files -- shutting down");
        exit(EXIT_FAILURE);
    }

    if (0 == skVectorGetCount(incr_path_vector)) {
        NOTICEMSG("No incremental files to process.");
        skVectorDestroy(incr_path_vector);
        return;
    }

    /* Visit all files in the vector */
    INFOMSG("Queuing incremental files...");
    for (i = 0; (v = skVectorGetValuePointer(incr_path_vector, i)); ++i) {
        incr_path = *((cache_closed_file_t**)v);

        TRACEMSG(1, ("moveFiles(): Processing '%s'", incr_path->filename));

        if (skDequePushBack(output_deque, (void*)incr_path) == SKDQ_ERROR) {
            cache_closed_file_destroy(incr_path);
            skAppPrintOutOfMemory(NULL);
            exit(EXIT_FAILURE);
        }

        /* Queued the file. */
        ++file_count;
    }
    skVectorDestroy(incr_path_vector);

    /* Print status message */
    NOTICEMSG(("Successfully closed and queued %"  SK_PRIuZ " file%s."),
              file_count, CHECK_PLURAL(file_count));
    return;
}


/*
 *  check_processing_dir();
 *
 *    This function is used shortly after start-up.
 *
 *    The function checks the files in the processing-directory and
 *    "cleans up" any that appear to be left-over from a previous run
 *    of rwflowpack.
 *
 *    Any regular files that have a non-zero size and that do not
 *    begin with a dot are added to the output_deque so they can be
 *    processed by the mover_thread or an appender_state[].thread.
 */
static void
check_processing_dir(
    void)
{
    char in_path[PATH_MAX];
    cache_closed_file_t *incr_path;
    struct dirent *entry;
    DIR *dir;
    struct stat st;
    size_t file_count = 0;
    int rv;

    NOTICEMSG("Checking processing-directory for old incremental"
              " files to queue for processing...");

    /* Open the processing directory and loop over the files in the
     * directory */
    dir = opendir(processing_directory);
    if (NULL == dir) {
        CRITMSG("Fatal error: Unable to open processing directory '%s': %s",
                processing_directory, strerror(errno));
        exit(EXIT_FAILURE);
    }

    while ((entry = readdir(dir)) != NULL) {
        /* Ignore dot-files */
        if ('.' == entry->d_name[0]) {
            TRACEMSG(2, ("checkIncrDir(): Skipping '%s'", entry->d_name));
            continue;
        }

        /* Make complete path to file */
        if ((size_t)snprintf(in_path, sizeof(in_path), "%s/%s",
                             processing_directory, entry->d_name)
            >= sizeof(in_path))
        {
            WARNINGMSG("Pathname exceeds maximum size for '%s'",
                       entry->d_name);
            continue;
        }

        /* Ignore files that are not regular files */
        rv = stat(in_path, &st);
        if (-1 == rv) {
            if (EEXIST != errno) {
                WARNINGMSG("Unable to stat '%s': %s",
                           in_path, strerror(errno));
            }
            continue;
        }
        if (!(st.st_mode & S_IFREG)) {
            DEBUGMSG("Ignoring non-file '%s'", entry->d_name);
            continue;
        }

        /* Ignore zero-length files */
        if (st.st_size == 0) {
            DEBUGMSG("Ignoring file zero-length file '%s'", entry->d_name);
            continue;
        }

        /* Queue this file.  Ideally, we should open the file, get a
         * lock on the file, verify that it is a SiLK file, and then
         * move it.  Purpose of getting a lock is to avoid clashing
         * with a running rwflowpack. */
        incr_path = (cache_closed_file_t*)calloc(1, sizeof(*incr_path));
        if (NULL == incr_path) {
            skAppPrintOutOfMemory(NULL);
            exit(EXIT_FAILURE);
        }
        incr_path->filename = strdup(in_path);
        if (NULL == incr_path->filename) {
            skAppPrintOutOfMemory(NULL);
            free(incr_path);
            exit(EXIT_FAILURE);
        }
        if (skDequePushBack(output_deque, (void*)incr_path) == SKDQ_ERROR) {
            skAppPrintOutOfMemory(NULL);
            cache_closed_file_destroy(incr_path);
            exit(EXIT_FAILURE);
        }
        INFOMSG("Queued old incremental file '%s'", in_path);
        ++file_count;
    }

    closedir(dir);

    /* Print status message */
    if (file_count == 0) {
        NOTICEMSG("Found no old incremental files in processing-directory.");
    } else {
        NOTICEMSG(("Successfully queued %" SK_PRIuZ
                   " old incremental file%s for processing."),
                  file_count, CHECK_PLURAL(file_count));
    }
}


static void
start_output_mode(
    void)
{
    int rv;

    if (OUTPUT_LOCAL_STORAGE == output_mode) {
        if (appender_start()) {
            exit(EXIT_FAILURE);
        }
    } else if (OUTPUT_INCREMENTAL_FILES == output_mode) {
        /* Start the mover thread */
        NOTICEMSG("Starting mover thread...");
        increment_thread_count();
        rv = skthread_create(
            "mover_thread", &mover_thread, &mover_thread_main, NULL);
        if (rv) {
            decrement_thread_count(0);
            CRITMSG("Unable to create mover thread: '%s'", strerror(rv));
            exit(EXIT_FAILURE);
        }
    } else {
        skAbortBadCase(output_mode);
    }
}


static void
do_input_mode_single_file(
    void)
{
    /* For this mode, we run the input mode first.  When it is
     * finished, it sends a signal that it is complete.  When the
     * signal is received in the while() loop below, the files it
     * created are queued for processing and the output thread is
     * started.  Once the output thread empties the queue, it
     * sends another signal.  The while() loop catches this signal
     * and begins the shutdown process. */
    int output_started = 0;

    /* Check for partial files in the processing-directory from a
     * previous run. */
    check_processing_dir();

    if (input_mode_type.start_fn() != 0) {
        CRITMSG("Unable to start flow processor");
        exit(EXIT_FAILURE);
    }
    input_thread_started = 1;

    while (!shutting_down) {
        if (get_thread_count()) {
            pause();
        }
        if (output_started || shutting_down) {
            break;
        }
        output_started = 1;
        /* Queue the files for processing */
        close_and_queue_files();
        start_output_mode();
    }
}


int main(int argc, char **argv)
{
    struct sigaction action;
    skpc_probe_iter_t iter;
    const skpc_probe_t *probe;

    appSetup(argc, argv);                 /* never returns on error */

    /* handle any other initialization before we daemonize */
    main_thread = pthread_self();
    skthread_init("main");

    mover_thread = main_thread;

    if (OUTPUT_LOCAL_STORAGE == output_mode) {
        if (appender_setup()) {
            exit(EXIT_FAILURE);
        }
    }

    /* Provide a handler for the signal (normally SIGUSR2) that is
     * sent when the global thread_count variable goes to zero.  See
     * decrement_thread_count(). */
    memset(&action, 0, sizeof(action));
    /* mask any further signals while we're inside the handler */
    sigfillset(&action.sa_mask);
    action.sa_handler = &empty_signal_handler;
    if (sigaction(READER_DONE_SIGNAL, &action, NULL) == -1) {
        skAppPrintErr("Could not handle SIG%s: %s",
                      skSignalToName(READER_DONE_SIGNAL), strerror(errno));
        exit(EXIT_FAILURE);
    }

    /* start the logger and become a daemon */
    if (skdaemonize(&shutting_down, NULL) == -1) {
        exit(EXIT_FAILURE);
    }
    daemonized = 1;

    /* initialize the packing function on each probe */
    skpcProbeIteratorBind(&iter);
    while (skpcProbeIteratorNext(&iter, &probe) == 1) {
        if (skpcProbeInitializePacker((skpc_probe_t *)probe)) {
            CRITMSG("Failed to initialize packer for probe %s",
                    skpcProbeGetName(probe));
            exit(EXIT_FAILURE);
        }
    }

    if (OUTPUT_FLOWCAP == output_mode
        || OUTPUT_ONE_DESTINATION == output_mode)
    {
        /* Start the input thread(s). */
        if (input_mode_type.start_fn() != 0) {
            CRITMSG("Unable to start flow processor");
            exit(EXIT_FAILURE);
        }

        /* Nothing else to do */
        goto RUNNING;
    }

    /* Create the Deque that holds output file names */
    output_deque = skDequeCreate();
    if (NULL == output_deque) {
        CRITMSG("Unable to create output deque.");
        exit(EXIT_FAILURE);
    }

    /* Create a cache of streams (file handles) so we don't have to
     * incur the expense of reopening files */
    INFOMSG("Creating stream cache");

    stream_cache = skCacheCreate(stream_cache_size, &create_incremental_file);
    if (NULL == stream_cache) {
        CRITMSG("Unable to create stream cache.");
        exit(EXIT_FAILURE);
    }

    if (input_mode == INPUT_SINGLEFILE) {
        do_input_mode_single_file();
        appTeardown();
        return 0;
    }

    /* Start the thread to handle the files that this rwflowpack
     * process creates in the processing-directory */
    start_output_mode();

    /* Check for partial files in the processing-directory from a
     * previous run. */
    check_processing_dir();

    /* Start the input thread(s). */
    if (input_mode_type.start_fn() != 0) {
        CRITMSG("Unable to start flow processor");
        exit(EXIT_FAILURE);
    }

    /* Start the timer thread */
    if (start_timer()) {
        CRITMSG("Unable to start timer");
        exit(EXIT_FAILURE);
    }
    input_thread_started = 1;

  RUNNING:
    /* We now run forever, excepting signals, until the shutting_down
     * flag is set or until all threads exit. */
    while (!shutting_down) {
        size_t count_snapshot;
        count_snapshot = get_thread_count();
        TRACEMSG(1, (("Waiting for signal or for %" SK_PRIuZ
                      " threads to end"), count_snapshot));
        if (0 == count_snapshot) {
            break;
        }
        pause();
    }

    /* done */
    appTeardown();

    return 0;
}

/*
** Local Variables:
** mode:c
** indent-tabs-mode:nil
** c-basic-offset:4
** End:
*/
