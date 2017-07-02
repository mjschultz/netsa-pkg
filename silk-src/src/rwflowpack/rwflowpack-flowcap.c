/*
** Copyright (C) 2003-2017 by Carnegie Mellon University.
**
** @OPENSOURCE_LICENSE_START@
** See license information in ../../LICENSE.txt
** @OPENSOURCE_LICENSE_END@
*/

#include <silk/silk.h>

RCSIDENT("$SiLK: rwflowpack-flowcap.c efd886457770 2017-06-21 18:43:23Z mthomas $");

#include <silk/skheader.h>
#include <silk/sktimer.h>
#ifdef SK_HAVE_SYS_STATVFS_H
#include <sys/statvfs.h>
#endif
#include "rwflowpack_priv.h"


/* TYPEDEFS AND DEFINES */

/* Max timestamp length (YYYYMMDDhhmmss) */
#define FC_TIMESTAMP_MAX 15
/* Maximum sensor size (including either trailing zero or preceeding hyphen) */
#define FC_SENSOR_MAX (SK_MAX_STRLEN_SENSOR + 1)
/* Maximum probe size (including either trailing zero or preceeding hyphen) */
#define FC_PROBE_MAX (SK_MAX_STRLEN_SENSOR + 1)
/* Size of uniquness extension */
#define FC_UNIQUE_MAX 7
/* Previous two, plus hyphen */
#define FC_NAME_MAX                                     \
    (FC_TIMESTAMP_MAX + FC_SENSOR_MAX +                 \
     FC_PROBE_MAX + FC_UNIQUE_MAX)


/* Minimum flowcap version */
/* We no longer support flowcap version 1 */
#define FC_VERSION_MIN 2

/* Maximum flowcap version */
#define FC_VERSION_MAX 5

/* Default version of flowcap to produce */
#define FC_VERSION_DEFAULT 5



/* the reason a file was closed; passed to closeFile() */
typedef enum close_reason_en {
    FC_TIMED_OUT,
    FC_OVERFULL,
    FC_SHUTDOWN
} close_reason_t;


typedef struct flowcap_reader_st flowcap_reader_t;
struct flowcap_reader_st {
    /* probe that this reader is capturing */
    skpc_probe_t       *probe;

    /* name of the probe */
    const char         *probename;

    /* the skStream that is used for writing */
    skstream_t         *stream;

    /* base name of the file; a pointer into 'path' */
    char               *filename;

    /* complete path to file */
    char                path[PATH_MAX];

    /* close timer */
    sk_timer_t         *timer;

    /* reader lock */
    pthread_mutex_t     mutex;

    /* time when the file was opened */
    time_t              start_time;

    /* number of records written to current file */
    uint32_t            records;

    /* whether it is time to shutdown */
    unsigned            shutdown    : 1;

    /* whether this file is due to be closed. */
    unsigned            close       : 1;

    /* whether this file is in the process of being closed---protect
     * against size limit and time limit firing simultaneously. */
    unsigned            closing     : 1;
};
/* flowcap_reader_t */


/* LOCAL VARIABLES */

/* number of readers; need to set this for computing amount of disk
 * space that will be used.  Probably easiest to get it from the probe
 * definitions. */
static size_t num_fc_readers;



/* LOCAL FUNCTION PROTOTYPES */

static skTimerRepeat_t
timer_main(
    void               *vreader);
static int
openFileBase(
    flowcap_reader_t   *reader);
static void
closeFile(
    flowcap_reader_t   *reader,
    close_reason_t      reason);
static int
closeFileBase(
    flowcap_reader_t   *reader,
    close_reason_t      reason);

#ifdef SK_HAVE_STATVFS
static int checkDiskSpace(void);
#else
#  define checkDiskSpace()  (0)
#endif


/* FUNCTION DEFINITIONS */

/*
 *  repeat = timer_main(reader);
 *
 *  THREAD ENTRY POINT
 *
 *    This function is the callback function that is invoked every
 *    'flush_timeout' seconds by the reader->timer thread.
 *
 *    The timer fired for 'reader'.  Close the current file, open a
 *    new file, and restart the timer.
 */
static skTimerRepeat_t
timer_main(
    void               *vreader)
{
    flowcap_reader_t *reader = (flowcap_reader_t*)vreader;

    if (reader->shutdown) {
        return SK_TIMER_END;
    }

    /* Set the close flag first. */
    reader->close = 1;

    /* Timer handler stuff */
    INFOMSG("Timer fired for '%s'", reader->filename);

    /* Close the file, and open a new one. */
    closeFile(reader, FC_TIMED_OUT);

    return SK_TIMER_REPEAT;
}


/*
 *  free_packer_state_flowcap(probe);
 *
 *    A callback function used by the packing logic to free the packer
 *    state.  Called by packlogic->free_packer_state_fn().
 *
 *    This function closes the file for this reader, destroys the
 *    reader's mutex, and destroys the reader.
 */
static void
free_packer_state_flowcap(
    skpc_probe_t       *probe)
{
    flowcap_reader_t *reader;

    reader = (flowcap_reader_t*)probe->pack.state;
    probe->pack.state = NULL;

    reader->shutdown = 1;
    reader->close = 1;
    closeFile(reader, FC_SHUTDOWN);

    sk_lua_closestate(probe->pack.lua_state);
    probe->pack.lua_state = NULL;

    pthread_mutex_destroy(&reader->mutex);
    free(reader);
}


static void
stop_packer_flowcap(
    skpc_probe_t       *probe)
{
    flowcap_reader_t *reader = (flowcap_reader_t*)probe->pack.state;

    reader->shutdown = 1;
    reader->close = 1;
    closeFile(reader, FC_SHUTDOWN);
}


/*
 *  status = openFileBase(reader);
 *
 *    Open a disk file to store the flows that are being read from the
 *    probe associated with 'reader'.
 *
 *    This function assumes it has the lock for 'reader'.
 *
 *    This function creates two files: a placeholder file and a
 *    temporary file that has the same name as the placeholder folder
 *    except it is prefixed with a dot.  The leading dot tells
 *    rwsender's directory poller to ignore the file.  We write the
 *    data into the temporary file.  In the closeFileBase() function,
 *    we move the temporary file over the placeholder file.
 *
 *    A timer is created for the 'reader' unless one already exists.
 *
 *    This function writes the SiLK header to the temporary file.
 *
 *    This function calls checkDiskSpace().
 *
 *    Return 0 on success, -1 on failure.
 */
static int
openFileBase(
    flowcap_reader_t   *reader)
{
    char dotpath[PATH_MAX];
    char ts[FC_TIMESTAMP_MAX + 1]; /* timestamp */
    const packer_fileinfo_t *file_info;
    sk_file_header_t *hdr;
    struct timeval tv;
    struct tm ut;
    int fd;
    int rv;

    DEBUGMSG("Opening new file...");

    file_info = skpcProbeGetFileInfo(reader->probe);

    /* make sure there is space available */
    if (checkDiskSpace()) {
        return -1;
    }

    /* Create a timestamp */
    gettimeofday(&tv, NULL);
    gmtime_r(&tv.tv_sec, &ut);
    strftime(ts, sizeof(ts), "%Y%m%d%H%M%S", &ut);

    /* Create a pathname from the directory, timestamp, and probe.  If
     * you change the number of X's here, be certain to update
     * FC_UNIQUE_MAX in flowcap.h. */
    if ((size_t)snprintf(reader->path, sizeof(reader->path), "%s/%s_%s.XXXXXX",
                         destination_directory, ts, reader->probename)
        >= sizeof(reader->path))
    {
        CRITMSG("Pathname exceeded maximum filename size.");
        return -1;
    }

    /* Open the file; making sure its name is unique */
    fd = mkstemp(reader->path);
    if (fd == -1) {
        CRITMSG("Unable to create file '%s': %s",
                reader->path, strerror(errno));
        return -1;
    }

    DEBUGMSG("Opened placeholder file '%s'", reader->path);

    /* Set the permissions */
    fchmod(fd, 0644);

    rv = close(fd);
    fd = -1;
    if (-1 == rv) {
        CRITMSG("Unable to close file '%s': %s",
                reader->path, strerror(errno));
        unlink(reader->path);
        return -1;
    }

    /* Get the basename of the file */
    reader->filename = strrchr(reader->path, '/');
    ++reader->filename;

    /* Create the name of the dotfile */
    if ((size_t)snprintf(dotpath, sizeof(dotpath), "%s/.%s",
                         destination_directory, reader->filename)
        >= sizeof(dotpath))
    {
        CRITMSG("Dot pathname exceeded buffer size.");
        unlink(reader->path);
        return -1;
    }

    /* Open the dot file.  The while() will repeat only if the dot
     * file already exists and can be removed successfully. */
    while ((fd = open(dotpath, O_WRONLY | O_CREAT | O_EXCL, 0644))
           == -1)
    {
        /* Remove the dotfile if it exists and try again; otherwise
         * give up on this file. */
        int saveerrno = errno;
        if (errno == EEXIST) {
            WARNINGMSG("Working file already exists. Removing '%s'",
                       dotpath);
            if (unlink(dotpath) == 0) {
                continue;
            }
            WARNINGMSG("Failed to unlink existing working file '%s': %s",
                       dotpath, strerror(errno));
        }
        CRITMSG("Could not create '%s': %s",
                dotpath, strerror(saveerrno));
        unlink(reader->path);
        return -1;
    }

    DEBUGMSG("Opened working file '%s'", dotpath);

    /* create a stream bound to the dotfile */
    if ((rv = skStreamCreate(&reader->stream,SK_IO_WRITE,SK_CONTENT_SILK_FLOW))
        || (rv = skStreamBind(reader->stream, dotpath))
        || (rv = skStreamFDOpen(reader->stream, fd)))
    {
        skStreamPrintLastErr(reader->stream, rv, &ERRMSG);
        /* NOTE: it is possible for skStreamFDOpen() to have stored
         * the value of 'fd' but return an error. */
        if (reader->stream && skStreamGetDescriptor(reader->stream) != fd) {
            close(fd);
        }
        skStreamDestroy(&reader->stream);
        unlink(dotpath);
        unlink(reader->path);
        return -1;
    }

    /* set and write the file's header */
    hdr = skStreamGetSilkHeader(reader->stream);
    if ((rv = skHeaderSetFileFormat(hdr, file_info->record_format))
        || (rv = skHeaderSetRecordVersion(hdr, file_info->record_version))
        || (rv = skHeaderSetByteOrder(hdr, file_info->byte_order))
        || (rv = skHeaderSetCompressionMethod(hdr, file_info->comp_method))
        || (rv = skHeaderAddProbename(hdr, reader->probename))
        || (file_info->sidecar != NULL
            && (rv = skStreamSetSidecar(reader->stream, file_info->sidecar)))
        || (rv = skStreamWriteSilkHeader(reader->stream)))
    {
        skStreamPrintLastErr(reader->stream, rv, &ERRMSG);
        skStreamDestroy(&reader->stream);
        unlink(dotpath);
        unlink(reader->path);
        return -1;
    }

    /* Set up default values */
    reader->start_time = tv.tv_sec;
    reader->records    = 0;
    reader->closing    = 0;
    reader->close      = 0;

    /* set the timer to flush_timeout */
    if (NULL == reader->timer) {
        skTimerCreate(&reader->timer, flush_timeout,
                      &timer_main, (void*)reader, clock_time);
    }

    INFOMSG("Opened new file '%s'", reader->filename);
    return 0;
}


/*
 *  closeFile(reader, reason);
 *
 *    Close the current disk file associated with 'reader'.
 *
 *    Unless 'resaon' is FC_SHUTDOWN, close the file and then call
 *    openFileBase() to open a new file.
 *
 *    This function must protect against attempts by the size limit
 *    and the time limit to close the file simultaneously.  Unless
 *    reason is FC_SHUTDOWN, simply return if 'reader' is already in
 *    the state of being closed.
 *
 *    Otherwise, get the lock for 'reader' and call closeFileBase() to
 *    close the disk file associated with 'reader'.
 */
static void
closeFile(
    flowcap_reader_t   *reader,
    close_reason_t      reason)
{
    static pthread_mutex_t close_lock = PTHREAD_MUTEX_INITIALIZER;
    uint8_t quit = 0;

    /* Ah, the perils of threads.  reader->closing keeps us from
     * double-closing a reader.  reader->close makes sure we don't honor
     * a request to close a reader that has been closed and reopened
     * since the request. */
    MUTEX_LOCK(&close_lock);

    if (reader->closing || !reader->close) {
        quit = 1;
    } else {
        reader->closing = 1;
    }
    MUTEX_UNLOCK(&close_lock);

    if (quit && (reason != FC_SHUTDOWN)) {
        DEBUGMSG("Avoiding duplicate call to closeFile.");
        return;
    }

    MUTEX_LOCK(&reader->mutex);

    if (closeFileBase(reader, reason)) {
        reader->filename = NULL;
        MUTEX_UNLOCK(&reader->mutex);
        exit(EXIT_FAILURE);
    }
    if (reason != FC_SHUTDOWN) {
        if (openFileBase(reader)) {
            reader->filename = NULL;
            MUTEX_UNLOCK(&reader->mutex);
            exit(EXIT_FAILURE);
        }
    }

    MUTEX_UNLOCK(&reader->mutex);
}


/*
 *  status = closeFileBase(reader, reason);
 *
 *    Close the disk file associated with the 'reader'.
 *
 *    This function assumes it has the lock for 'reader'.
 *
 *    The function closes the temporary dot file.  If the dot file
 *    contains no records, the dot file and placeholder file are
 *    removed.  If the dot file contains records, the dot file is
 *    moved on top of the placeholder file.
 *
 *    If 'reader' has a timer associated with it, the timer is
 *    destroyed unless this function has been called because the timer
 *    fired---that is, if 'reason' is FC_TIMED_OUT.
 *
 *    Return 0 on success; -1 on failure.
 */
static int
closeFileBase(
    flowcap_reader_t   *reader,
    close_reason_t      reason)
{
    const sk_file_header_t *hdr;
    char dotpath[PATH_MAX];
    int64_t uncompress_size;
    int64_t size;
    double change;
    time_t end_time;
    ssize_t rv;

    if (NULL == reader->filename) {
        /* Do not close an unopened file.  An unopened file can occur
         * during start up when there are multiple sources and a
         * source (other than the final source) fails to start. */
        if (reader->timer && (reason != FC_TIMED_OUT)) {
            DEBUGMSG("'%s': Destroying timer", reader->probename);
            skTimerDestroy(reader->timer);
            reader->timer = NULL;
        }
        return 0;
    }

    DEBUGMSG("Closing file '%s'...", reader->filename);

    /* Make certain the timer for this file doesn't fire.  If the file
     * timed out, however, keep the timer, which will just restart.
     * The assumption is that the time to create a new file after this
     * point is less than the timer fire time. */
    if (reader->timer && (reason != FC_TIMED_OUT)) {
        DEBUGMSG("'%s': Destroying timer", reader->probename);
        skTimerDestroy(reader->timer);
        reader->timer = NULL;
    }

    /* get path to the dot file. */
    sprintf(dotpath, "%s/.%s",
            destination_directory, reader->filename);

    /* if no records were written, close and remove the file */
    if (reader->records == 0) {
        end_time = time(NULL);
        rv = skStreamClose(reader->stream);
        if (rv) {
            skStreamPrintLastErr(reader->stream, rv, &ERRMSG);
            CRITMSG("Fatal error closing '%s'", dotpath);
            return -1;
        }
        skStreamDestroy(&reader->stream);
        unlink(dotpath);
        unlink(reader->path);

        INFOMSG(("Removed empty file '%s': %" PRId64 " seconds"),
                reader->filename, (int64_t)(end_time - reader->start_time));

        reader->filename = NULL;
        return 0;
    }

    /* flush the file so we can get its final size */
    rv = skStreamFlush(reader->stream);
    if (rv) {
        skStreamPrintLastErr(reader->stream, rv, &ERRMSG);
        CRITMSG("Fatal error flushing file '%s'", reader->path);
        return -1;
    }
    end_time = time(NULL);

    /* how many uncompressed bytes were processed? */
    hdr = skStreamGetSilkHeader(reader->stream);
    uncompress_size = (skHeaderGetLength(hdr)
                       + reader->records * skHeaderGetRecordLength(hdr));

    /* how many bytes were written to disk? */
    size = (int64_t)skStreamTell(reader->stream);

    /* what's the compression ratio? */
    if (uncompress_size == 0) {
        change = 0.0;
    } else {
        change = (100.0 * (double)(uncompress_size - size)
                  / (double)uncompress_size);
    }

    INFOMSG(("'%s': Closing file '%s': %" PRId64 " seconds,"
             " %" PRIu32 " records, %" PRId64 " bytes, %4.1f%% compression"),
            reader->probename,
            reader->filename,
            (int64_t)(end_time - reader->start_time),
            reader->records,
            size,
            change);

    skpcProbeLogSourceStats(reader->probe);

    /* close the file and destroy the handle */
    rv = skStreamClose(reader->stream);
    if (rv) {
        skStreamPrintLastErr(reader->stream, rv, &ERRMSG);
        CRITMSG("Fatal error closing '%s'", dotpath);
        return -1;
    }
    skStreamDestroy(&(reader->stream));

    /* move the dot-file over the placeholder file. */
    rv = rename(dotpath, reader->path);
    if (rv != 0) {
        CRITMSG("Failed to replace '%s' with '%s': %s",
                reader->path, dotpath, strerror(errno));
        return -1;
    }

    INFOMSG("Finished closing '%s'", reader->filename);
    reader->filename = NULL;
    return 0;
}


#ifdef SK_HAVE_STATVFS
/*
 *  checkDiskSpace();
 *
 *    Verify that we haven't reached the limits of the file system
 *    usage specified by the command line parameters.
 *
 *    If we're out of space, return -1.  Else, return 0.
 */
static int
checkDiskSpace(
    void)
{
    struct statvfs vfs;
    int64_t free_space, total, newfree;
    int rv;
    double percent_used;

    rv = statvfs(destination_directory, &vfs);
    if (rv != 0) {
        CRITMSG("Could not statvfs '%s'", destination_directory);
        return -1;
    }

    /* free bytes is fundamental block size multiplied by the
     * available (non-privileged) blocks. */
    free_space = ((int64_t)vfs.f_frsize * (int64_t)vfs.f_bavail);
    /* to compute the total (non-privileged) blocks, subtract the
     * available blocks from the free (privileged) blocks to get
     * the count of privileged-only blocks, subtract that from the
     * total blocks, and multiply the result by the block size. */
    total = ((int64_t)vfs.f_frsize
             * ((int64_t)vfs.f_blocks
                - ((int64_t)vfs.f_bfree - (int64_t)vfs.f_bavail)));

    newfree = free_space - alloc_file_size * num_fc_readers;
    percent_used = ((double)(total - newfree) /
                    ((double)total / 100.0));

    if (newfree < freespace_minimum_bytes) {
        CRITMSG(("Free disk space limit overrun: "
                 "free=%" PRId64 " < min=%" PRId64 " (used %.4f%%)"),
                newfree, freespace_minimum_bytes, percent_used);
        /* TODO: Create a wait routine instead of exiting? */
        return -1;
    }
    if (percent_used > usedspace_maximum_percent) {
        CRITMSG(("Free disk space limit overrun: "
                 "used=%.4f%% > max=%.4f%% (free %" PRId64 " bytes)"),
                percent_used, usedspace_maximum_percent, newfree);
        /* TODO: Create a wait routine instead of exiting? */
        return -1;
    }

    DEBUGMSG(("Free space available is %" PRId64 " bytes (%.4f%%)"),
             newfree, percent_used);
    return 0;
}
#endif /* SK_HAVE_STATVFS */


/*
 *  status = pack_record_flowcap(probe, fwd_rec, rev_rec);
 *
 *    A callback function used by the packing logic to write the
 *    record.  A pointer to this function is set on probe by the call
 *    to packlogic->set_packing_function_fn().
 *
 *    Write the records 'fwd_rec' and 'rev_rec' to the disk file
 *    associated with 'reader' that is stored on 'probe'.
 *
 *    If the file reaches the maximum size, the file is closed and a
 *    new file is opened.
 */
static int
flowcap_pack_record(
    skpc_probe_t        *probe,
    const rwRec         *fwd_rec,
    const rwRec         *rev_rec)
{
    flowcap_reader_t *reader = (flowcap_reader_t*)probe->pack.state;
    int rv;

    assert(reader);
    MUTEX_LOCK(&reader->mutex);

    /* loop over fwd_rec and rev_rec */
    for (;;) {
        /* Write the record to the file */
        rv = skStreamWriteRecord(reader->stream, fwd_rec);
        if (rv) {
            skStreamPrintLastErr(reader->stream, rv, &ERRMSG);
            CRITMSG("Fatal error writing record.");
            MUTEX_UNLOCK(&reader->mutex);
            return -1;
        }
        ++reader->records;

        /* Check to see if we have reached the size limit */
        if (skStreamGetUpperBound(reader->stream) >= (off_t)max_file_size) {
            reader->close = 1;
            MUTEX_UNLOCK(&reader->mutex);
            /* Close the file and open a new one in its place */
            closeFile(reader, FC_OVERFULL);
            if (NULL == rev_rec) {
                return 0;
            }
            MUTEX_LOCK(&reader->mutex);
        } else if (NULL == rev_rec) {
            MUTEX_UNLOCK(&reader->mutex);
            return 0;
        }
        fwd_rec = rev_rec;
        rev_rec = NULL;
    }
}


/*
 *    Implementation of the Lua function
 *
 *    write_rwrec(rec)
 *
 *    that is used when the record is being written to the current
 *    output file (OUTPUT_FLOWCAP output-mode).
 *
 *    The caller only needs to specify the record to write.  The
 *    file's location is determined by a timer, and the file's format
 *    and sidecar data for the current probe was specified in the
 *    configuraiton file.
 *
 *    This function use one upvalue: the current probe.
 */
int
flowcap_write_rwrec_lua(
    lua_State          *L)
{
    skpc_probe_t *probe;
    flowcap_reader_t *reader;
    const rwRec *rec;
    ssize_t rv;

    /* record */
    rec = sk_lua_checkrwrec(L, 1);

    /* probe */
    probe = (skpc_probe_t *)lua_touserdata(L, lua_upvalueindex(1));
    assert(probe);

    reader = (flowcap_reader_t *)probe->pack.state;
    assert(reader);
    MUTEX_LOCK(&reader->mutex);

    /* Write the record to the file */
    rv = skStreamWriteRecord(reader->stream, rec);
    if (rv) {
        skStreamPrintLastErr(reader->stream, rv, &ERRMSG);
        CRITMSG("Fatal error writing record.");
        MUTEX_UNLOCK(&reader->mutex);
        return luaL_error(L, "write_rwrec error");
    }

    ++reader->records;
    /* Check to see if we have reached the size limit */
    if (skStreamGetUpperBound(reader->stream) < (off_t)max_file_size) {
        MUTEX_UNLOCK(&reader->mutex);
        return 0;
    }

    reader->close = 1;
    MUTEX_UNLOCK(&reader->mutex);
    /* Close the file and open a new one in its place */
    closeFile(reader, FC_OVERFULL);
    return 0;
}


/*
 *    A helper function that is invoked by the callback functions
 *    which are invoked by skpcProbeInitializePacker().
 *
 *    This function creates a new state object (the reader) for the
 *    specified probe, creates the first file for the reader, and
 *    starts the timer.
 *
 *    The lua_State object is NULL if the user did not provide a Lua
 *    function to write the records.  The value of the lua_State
 *    determines which packing callback function is used.
 */
int
flowcap_initialize_packer(
    skpc_probe_t       *probe,
    lua_State          *L)
{
    flowcap_reader_t *reader;

    assert(OUTPUT_FLOWCAP == output_mode);

    if (skpcProbeGetFileInfo(probe) == NULL) {
        NOTICEMSG("'%s': No fileinfo defined", skpcProbeGetName(probe));
        return -1;
    }

    /* initialize the reader for this probe */
    reader = sk_alloc(flowcap_reader_t);
    reader->probe = probe;
    reader->probename = skpcProbeGetName(probe);

    pthread_mutex_init(&reader->mutex, NULL);

    if (NULL == L) {
        probe->pack.pack_record = flowcap_pack_record;
    }

    /* Create the first file and its timer */
    MUTEX_LOCK(&reader->mutex);
    if (openFileBase(reader)) {
        reader->filename = NULL;
        MUTEX_UNLOCK(&reader->mutex);
        pthread_mutex_destroy(&reader->mutex);
        free(reader);
        return -1;
    }
    MUTEX_UNLOCK(&reader->mutex);

    probe->pack.state = (packlogic_state_t)reader;
    probe->pack.free_state = free_packer_state_flowcap;
    probe->pack.stop_packer = stop_packer_flowcap;

    return 0;
}


/*
** Local Variables:
** mode:c
** indent-tabs-mode:nil
** c-basic-offset:4
** End:
*/
