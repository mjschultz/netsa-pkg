/*
** Copyright (C) 2016-2017 by Carnegie Mellon University.
**
** @OPENSOURCE_LICENSE_START@
** See license information in ../../LICENSE.txt
** @OPENSOURCE_LICENSE_END@
*/

#include <silk/silk.h>

RCSIDENT("$SiLK: rwflowpack-onedest.c efd886457770 2017-06-21 18:43:23Z mthomas $");

#include <silk/skheader.h>
#include <silk/sktimer.h>
#ifdef SK_HAVE_SYS_STATVFS_H
#include <sys/statvfs.h>
#endif
#include "rwflowpack_priv.h"

#ifdef  RWFP_ONEDEST_TRACE_LEVEL
#define TRACEMSG_LEVEL RWFP_ONEDEST_TRACE_LEVEL
#endif
#define TRACEMSG(msg)                           \
    TRACEMSG_TO_TRACEMSGLVL(2, msg)
#include <silk/sktracemsg.h>

/*
 *  Logging messages for function entry/return.
 *
 *    Define the macro RWFP_ONEDEST_ENTRY_RETURN to trace entry to
 *    and return from the functions in this file.
 *
 *    Developers must use "TRACE_ENTRY" at the beginning of every
 *    function.  Use "TRACE_RETURN(x);" for functions that return the
 *    value "x", and use "TRACE_RETURN;" for functions that have no
 *    return value.
 */
/* #define RWFP_ONEDEST_ENTRY_RETURN 1 */

#ifndef RWFP_ONEDEST_ENTRY_RETURN
#  define TRACE_ENTRY
#  define TRACE_RETURN       return
#else
/*
 *  this macro is used when the extra-level debugging statements write
 *  to the log, since we do not want the result of the log's printf()
 *  to trash the value of 'errno'
 */
#define WRAP_ERRNO(x)                                           \
    do { int _saveerrno = errno; x; errno = _saveerrno; }       \
    while (0)

#if defined(SK_HAVE_C99___FUNC__)

#  define TRACE_ENTRY                                   \
    WRAP_ERRNO(DEBUGMSG("%s:%d: Entering %s()",         \
                        __FILE__, __LINE__,__func__))
#  define TRACE_RETURN                                  \
    WRAP_ERRNO(DEBUGMSG("%s:%d: Exiting %s()",          \
                        __FILE__, __LINE__, __func__)); \
    return

#else

#  define TRACE_ENTRY                                   \
    WRAP_ERRNO(DEBUGMSG("%s:%d: Entering function",     \
                        __FILE__, __LINE__))
#  define TRACE_RETURN                                  \
    WRAP_ERRNO(DEBUGMSG("%s:%d: Exiting function",      \
                        __FILE__, __LINE__));           \
    return

#endif
#endif  /* RWFP_ONEDEST_ENTRY_RETURN */


/* TYPEDEFS AND DEFINES */

typedef struct onedest_state_st onedest_state_t;
struct onedest_state_st {
    /* the skStream that is used for writing */
    skstream_t         *stream;

    /* flush timer */
    sk_timer_t         *timer;

    /* state lock */
    pthread_mutex_t     mutex;

    /* record count */
    uint64_t            records;

    /* thread count */
    uint32_t            threads;

    unsigned            shutdown :1;
};
/* onedest_state_t */


/* LOCAL VARIABLES */

/* there is a single state */
static onedest_state_t onedest_state_static = {
    NULL, NULL, PTHREAD_MUTEX_INITIALIZER, 0, 0, 0
};


/* LOCAL FUNCTION PROTOTYPES */

static int openDestination(onedest_state_t *state);
static size_t flushDestination(onedest_state_t *state);
static int closeDestination(onedest_state_t *state);



/* FUNCTION DEFINITIONS */

/*
 *  repeat = timer_main(state);
 *
 *  THREAD ENTRY POINT
 *
 *    This function is the callback function that is invoked every
 *    'flush_timeout' seconds by the state->timer thread.
 */
static skTimerRepeat_t
timer_main(
    void               *vstate)
{
    onedest_state_t *state = (onedest_state_t*)vstate;
    size_t count;

    TRACE_ENTRY;

    MUTEX_LOCK(&state->mutex);

    if (state->shutdown || NULL == state->stream) {
        MUTEX_UNLOCK(&state->mutex);
        TRACE_RETURN(SK_TIMER_END);
    }

    count = flushDestination(state);
    MUTEX_UNLOCK(&state->mutex);
    if (count) {
        INFOMSG("%s: %" SK_PRIuZ " recs",
                skStreamGetPathname(state->stream), count);
    }

    TRACE_RETURN(SK_TIMER_REPEAT);
}


static void
stop_packer_onedest(
    skpc_probe_t       *probe)
{
    onedest_state_t *state = &onedest_state_static;
    sk_timer_t *timer = NULL;

    TRACE_ENTRY;

    (void)probe;

    MUTEX_LOCK(&state->mutex);
    TRACEMSG(("%s:%d: changing shutdown from %d to 1",
              __FILE__, __LINE__, state->shutdown));
    state->shutdown = 1;
    if (state->timer) {
        timer = state->timer;
        state->timer = NULL;
    }
    MUTEX_UNLOCK(&state->mutex);
    if (timer) {
        TRACEMSG(("%s:%d: destroying timer", __FILE__, __LINE__));
        skTimerDestroy(timer);
    }
    TRACE_RETURN;
}


/*
 *  free_packer_onedest(probe);
 *
 *    A callback function used by the packing logic to free the packer
 *    state.  Called by packlogic->free_packer_state_fn().
 *
 *    This function closes the file for this state, destroys the
 *    state's mutex, and destroys the state.
 */
static void
free_packer_onedest(
    skpc_probe_t       *probe)
{
    onedest_state_t *state = &onedest_state_static;

    TRACE_ENTRY;

    stop_packer_onedest(probe);

    sk_lua_closestate(probe->pack.lua_state);

    MUTEX_LOCK(&state->mutex);
    TRACEMSG(("%s:%d: threads is %u", __FILE__, __LINE__, state->threads));
    if (state->threads > 0) {
        --state->threads;
        TRACEMSG(("%s:%d: threads is %u", __FILE__, __LINE__, state->threads));
        if (0 == state->threads) {
            closeDestination(state);
        }
    }
    MUTEX_UNLOCK(&state->mutex);
    TRACE_RETURN;
}


/*
 *  status = openDestination(state);
 *
 *    Open this one destination file.
 *
 *    This function assumes it has the lock for 'state'.
 *
 *    A timer is created for the 'state' unless one already exists.
 *
 *    Return 0 on success, -1 on failure.
 */
static int
openDestination(
    onedest_state_t   *state)
{
    const packer_fileinfo_t *fileinfo = one_destination_fileinfo;
    sk_file_header_t *hdr;
    ssize_t rv;

    TRACE_ENTRY;
    DEBUGMSG("Opening destination file...");

    /* create a stream */
    if ((rv = skStreamCreate(&state->stream,SK_IO_WRITE,SK_CONTENT_SILK_FLOW))
        || (rv = skStreamBind(state->stream, one_destination_path))
        || (rv = skStreamOpen(state->stream)))
    {
        skStreamPrintLastErr(state->stream, rv, &ERRMSG);
        skStreamDestroy(&state->stream);
        unlink(one_destination_path);
        TRACE_RETURN(-1);
    }

    /* set and write the file's header */
    hdr = skStreamGetSilkHeader(state->stream);
    if ((rv = skHeaderSetFileFormat(hdr, fileinfo->record_format))
        || (rv = skHeaderSetRecordVersion(hdr, fileinfo->record_version))
        || (rv = skHeaderSetByteOrder(hdr, fileinfo->byte_order))
        || (rv = skHeaderSetCompressionMethod(hdr, fileinfo->comp_method))
        || (fileinfo->sidecar != NULL
            && (rv = skStreamSetSidecar(state->stream, fileinfo->sidecar)))
        || (rv = skStreamWriteSilkHeader(state->stream)))
    {
        skStreamPrintLastErr(state->stream, rv, &ERRMSG);
        skStreamDestroy(&state->stream);
        unlink(one_destination_path);
        TRACE_RETURN(-1);
    }

    /* Set up default values */
    state->records    = 0;

    INFOMSG("Opened destination file '%s'",
            skStreamGetPathname(state->stream));
    TRACE_RETURN(0);
}


static size_t
flushDestination(
    onedest_state_t    *state)
{
    ssize_t rv;
    size_t count;
    size_t diff;

    TRACE_ENTRY;

    rv = skStreamFlush(state->stream);
    if (rv) {
        skStreamPrintLastErr(state->stream, rv, &WARNINGMSG);
    }

    count = skStreamGetRecordCount(state->stream);
    if (state->records == count) {
        diff = 0;
    } else {
        diff = count - state->records;
        state->records = count;
    }
    TRACE_RETURN(diff);
}


/*
 *  status = closeDestination(state);
 *
 *    Close the disk file associated with the 'state'.
 *
 *    This function assumes it has the lock for 'state'.
 *
 *    The function closes the temporary dot file.  If the dot file
 *    contains no records, the dot file and placeholder file are
 *    removed.  If the dot file contains records, the dot file is
 *    moved on top of the placeholder file.
 *
 *    If 'state' has a timer associated with it, the timer is
 *    destroyed unless this function has been called because the timer
 *    fired---that is, if 'reason' is FC_TIMED_OUT.
 *
 *    Return 0 on success; -1 on failure.
 */
static int
closeDestination(
    onedest_state_t    *state)
{
    size_t count;
    ssize_t rv;

    TRACE_ENTRY;
    assert(state);
    assert(NULL == state->timer);

    if (NULL == state->stream) {
        TRACE_RETURN(0);
    }

    count = flushDestination(state);
    if (count) {
        INFOMSG("%s: %" SK_PRIuZ " recs",
                skStreamGetPathname(state->stream), count);
    }

    DEBUGMSG("Closing file '%s'...", skStreamGetPathname(state->stream));
    rv = skStreamClose(state->stream);
    if (rv) {
        skStreamPrintLastErr(state->stream, rv, &ERRMSG);
        CRITMSG("Fatal error closing '%s'",skStreamGetPathname(state->stream));
    }
    skStreamDestroy(&state->stream);

    TRACE_RETURN(0);
}


/*
 *    Implementation of the Lua function
 *
 *    write_rwrec(rec)
 *
 *    that is used when the record is being written to a single output
 *    file (OUTPUT_ONE_DESTINATION output-mode).
 *
 *    The caller only needs to specify the record to write.  The
 *    file's location, the file's format, and the sidecar data was
 *    specified in the configuraiton file.
 */
int
onedest_write_rwrec_lua(
    lua_State          *L)
{
    onedest_state_t *state = &onedest_state_static;
    const rwRec *rec;
    ssize_t rv;

    TRACE_ENTRY;
    /* record */
    rec = sk_lua_checkrwrec(L, 1);

    MUTEX_LOCK(&state->mutex);

    /* Write the record to the file */
    rv = skStreamWriteRecord(state->stream, rec);
    if (rv) {
        skStreamPrintLastErr(state->stream, rv, &ERRMSG);
        MUTEX_UNLOCK(&state->mutex);
        CRITMSG("Fatal error writing record.");
        TRACE_RETURN(luaL_error(L, "write_rwrec error"));
    }
    MUTEX_UNLOCK(&state->mutex);

    TRACE_RETURN(0);
}


/*
 *  status = pack_record_onedest(probe, fwd_rec, rev_rec);
 *
 *    A callback function used by the packing logic to write the
 *    record.  A pointer to this function is set on probe by the call
 *    to packlogic->set_packing_function_fn().
 *
 *    Write the records 'fwd_rec' and 'rev_rec' to the disk file
 *    associated with 'state' that is stored on 'probe'.
 *
 *    If the file reaches the maximum size, the file is closed and a
 *    new file is opened.
 */
static int
pack_record_onedest(
    skpc_probe_t        *probe,
    const rwRec         *fwd_rec,
    const rwRec         *rev_rec)
{
    onedest_state_t *state = &onedest_state_static;
    ssize_t rv;

    TRACE_ENTRY;
    (void)probe;                /* UNUSED */

    MUTEX_LOCK(&state->mutex);

    for (;;) {
        /* Write the record to the file */
        rv = skStreamWriteRecord(state->stream, fwd_rec);
        if (rv) {
            skStreamPrintLastErr(state->stream, rv, &ERRMSG);
            CRITMSG("Fatal error writing record.");
            MUTEX_UNLOCK(&state->mutex);
            TRACE_RETURN(-1);
        }

        if (NULL == rev_rec) {
            MUTEX_UNLOCK(&state->mutex);
            TRACE_RETURN(0);
        }
        fwd_rec = rev_rec;
        rev_rec = NULL;
    }
}


/*
 *    A helper function that is invoked by the callback functions
 *    which are invoked by skpcProbeInitializePacker().
 *
 *    This function creates a new state object (the state) for the
 *    specified probe, creates the first file for the state, and
 *    starts the timer.
 *
 *    The lua_State object is NULL if the user did not provide a Lua
 *    function to write the records.  The value of the lua_State
 *    determines which packing callback function is used.
 */
int
onedest_initialize_packer(
    skpc_probe_t       *probe,
    lua_State          *L)
{
    onedest_state_t *state = &onedest_state_static;
    int rv;

    TRACE_ENTRY;

    assert(OUTPUT_ONE_DESTINATION == output_mode);

    if (one_destination_fileinfo == NULL) {
        NOTICEMSG("'%s': No fileinfo defined", skpcProbeGetName(probe));
        return -1;
    }

    if (NULL == L) {
        probe->pack.pack_record = pack_record_onedest;
    }
    probe->pack.stop_packer = stop_packer_onedest;
    probe->pack.free_state = free_packer_onedest;

    MUTEX_LOCK(&state->mutex);
    if (state->shutdown) {
        MUTEX_UNLOCK(&state->mutex);
        TRACE_RETURN(-1);
    }
    if (state->stream) {
        ++state->threads;
        TRACEMSG(("%s:%d: threads is %u", __FILE__, __LINE__, state->threads));
        MUTEX_UNLOCK(&state->mutex);
        TRACE_RETURN(0);
    }

    /* Create the first file */
    rv = openDestination(state);
    if (rv) {
        TRACEMSG(("%s:%d: changing shutdown from %d to 1",
                  __FILE__, __LINE__, state->shutdown));
        state->shutdown = 1;
        TRACEMSG(("%s:%d: threads is %u", __FILE__, __LINE__, state->threads));
    } else {
        /* Create the timer */
        if (NULL == state->timer) {
            skTimerCreate(&state->timer, flush_timeout,
                          &timer_main, (void*)state, clock_time);
        }
        ++state->threads;
        TRACEMSG(("%s:%d: threads is %u", __FILE__, __LINE__, state->threads));
    }

    MUTEX_UNLOCK(&state->mutex);
    TRACE_RETURN(rv);
}


/*
** Local Variables:
** mode:c
** indent-tabs-mode:nil
** c-basic-offset:4
** End:
*/
