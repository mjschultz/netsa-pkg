/*
** Copyright (C) 2003-2017 by Carnegie Mellon University.
**
** @OPENSOURCE_LICENSE_START@
** See license information in ../../LICENSE.txt
** @OPENSOURCE_LICENSE_END@
*/

/*
**  rwflowpack-local.c
**
**    Helper file for rwflowpack that implements the 'local-storage'
**    output-mode.
**
**    Specify the functions that take the names of incremental files
**    from the output_deque and append the contents of those files to
**    hourly files in the data repository, creating the hourly file if
**    it does not exist.  Any newly created hourly file will have the
**    same flowtype, sensor, and timestamp as the incremental file.
**
*/

#include <silk/silk.h>

RCSIDENT("$SiLK: rwflowpack-local.c 21315f05de74 2017-06-26 20:03:25Z mthomas $");

#include <silk/redblack.h>
#include "rwflowpack_priv.h"
#include "stream-cache.h"

/* use TRACEMSG_LEVEL as our tracing variable */
#define TRACEMSG(lvl, msg) TRACEMSG_TO_TRACEMSGLVL(lvl, msg)
#include <silk/sktracemsg.h>


/* MACROS AND DATA TYPES */

/*
 *    When rwflowpack opens a file for writing, it first reads this
 *    number of bytes to determine whether the file is an existing
 *    SiLK file or an empty file.
 */
#define RWFLOWPACK_OPEN_EXIST_READLEN   8

/*
 *  The appender_status_t indicates an appender thread's status.
 */
enum appender_status_en {
    APPENDER_STOPPED = 0,
    APPENDER_STARTING,
    APPENDER_STARTED
};
typedef enum appender_status_en appender_status_t;

/*
 *  The appender_state_t contains thread information for each appender
 *  thread.
 */
struct appender_state_st {
    /* the thread itself */
    pthread_t           thread;
    /* a Lua state for handling sidecar data */
    lua_State          *L;
    /* input stream it is currently reading */
    skstream_t         *in_stream;
    /* output stream it is currently writing */
    skstream_t         *out_stream;
    /* position in the 'out_stream' when the file was opened */
    int64_t             pos;
    /* flowtypeID, sensorID, and starttime of this file */
    sksite_repo_key_t   key;
    /* the full path to the output file */
    char                out_path[PATH_MAX];
    /* the location in 'out_path' where the basename begins */
    char               *out_basename;
    /* a pointer to the path of the input file */
    const char         *in_path;
    /* the location in 'in_path' where the basename begins */
    const char         *in_basename;
    /* the name of this thread, for log messages */
    char                name[16];
    /* current status of this thread */
    appender_status_t   status;
};
typedef struct appender_state_st appender_state_t;


/* LOCAL VARIABLES */

/* are we running? */
static volatile int running = 0;

/* the states for each of the threads that handle appending
 * incremental files to the hourly files */
static appender_state_t *appender_state = NULL;

/* mutex to guard access to the 'status' field of appender_state */
static pthread_mutex_t appender_state_mutex = PTHREAD_MUTEX_INITIALIZER;

/* red-black tree to ensure multiple threads do not attempt to modify
 * an hourly file simultaneously.  the tree stores appender_state_t
 * pointers, and the key is the basename of the hourly file. */
static struct rbtree *appender_tree = NULL;

/* mutex to guard access to appender_tree */
static pthread_mutex_t appender_tree_mutex = PTHREAD_MUTEX_INITIALIZER;

/* condition variable to awake blocked appender threads */
static pthread_cond_t appender_tree_cond = PTHREAD_COND_INITIALIZER;


/* LOCAL FUNCTION PROTOTYPES */

static int
appender_handle_file(
    appender_state_t   *state);
static int
appender_open_output(
    appender_state_t   *state,
    skstream_mode_t    *out_mode);
static int
appender_write_output_header(
    appender_state_t       *state,
    const sk_file_header_t *in_hdr);
static void
appender_finished_output(
    appender_state_t   *state);
static int
appender_truncate_output(
    appender_state_t   *state);
static void
appender_error_output(
    appender_state_t   *state,
    int64_t             close_pos,
    int                 err_code);
static int
appender_error_input(
    const appender_state_t *state);
static int
appender_tree_cmp(
    const void         *state1,
    const void         *state2,
    const void         *ctx);


/* FUNCTION DEFINITIONS */

/*
 *  THREAD ENTRY POINT
 *
 *    This is the entry point for each of the appender_state[].thread.
 *
 *    This function waits for an incremental file to appear in the
 *    incoming_directory being monitored by polldir.  When a file
 *    appears, its corresponding hourly file is determined and the
 *    incremental file is appended to the hourly file.
 */
static void*
appender_thread_main(
    void               *vstate)
{
    appender_state_t *state = (appender_state_t*)vstate;
    cache_closed_file_t *incr_path;
    skDQErr_t dqerr;
    int appender_rv = 0;

    assert(OUTPUT_LOCAL_STORAGE == output_mode);

    /* set this thread's state as started */
    pthread_mutex_lock(&appender_state_mutex);
    state->status = APPENDER_STARTED;
    if (!running) {
        pthread_mutex_unlock(&appender_state_mutex);
        decrement_thread_count(1);
        return NULL;
    }
    pthread_mutex_unlock(&appender_state_mutex);

    INFOMSG("Started appender thread %s.", state->name);

    state->L = sk_lua_newstate();

    while (running && 0 == appender_rv) {
        incr_path = NULL;

        /* Get the name of the next file to handle */
        dqerr = skDequePopFrontTimed(output_deque, (void**)&incr_path, 1);
        if (dqerr != SKDQ_SUCCESS) {
            /* deque probably timedout */
            if (SKDQ_TIMEDOUT == dqerr) {
                if (input_thread_started
                    && get_thread_count() <= appender_count)
                {
                    /* stop running once the inputs have been started
                     * and only appender threads remain */
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

        state->in_path = incr_path->filename;
        state->in_basename = strrchr(state->in_path, '/');
        if (!state->in_basename) {
            state->in_basename = state->in_path;
        } else {
            ++state->in_basename;
        }
        state->key = incr_path->key;

        /* for now, ignore errors with incremental files */
        appender_rv = appender_handle_file(state);
        cache_closed_file_destroy(incr_path);
    }

    INFOMSG("Finishing appender thread %s...", state->name);

    if (-1 == appender_rv) {
        /* fatal error.  must call exit() since we want the process to
         * end; if we only stopped all the appending threads, the
         * input threads may continue to run */
        exit(EXIT_FAILURE);
    }

    decrement_thread_count(1);

    sk_lua_closestate(state->L);
    state->L = NULL;

    return NULL;
}


/*
 *  status = appender_handle_file(state);
 *
 *    Append the file named by 'state->in_path' to the appropriate
 *    hourly file in the repository, creating the file if necessary.
 *
 *    Return 0 on success, 1 if the application is told to shutdown,
 *    and -1 on error.
 */
static int
appender_handle_file(
    appender_state_t   *state)
{
    char errbuf[2 * PATH_MAX];
    union h_un {
        sk_header_entry_t          *he;
        sk_hentry_packedfile_t     *pf;
    } h;
    sksite_repo_key_t repo_key;
    sk_file_header_t *in_hdr;
    skstream_mode_t mode;
    int64_t close_pos;
    const void *found;
    rwRec rwrec;
    int out_rv;
    int rv;

    /* location in output file where records for this input file
     * begin */
    state->pos = 0;
    /* file handles */
    state->in_stream = NULL;
    state->out_stream = NULL;
    /* output file locations */
    state->out_path[0] = '\0';
    state->out_basename = state->out_path;

    /* open the incremental file as the input */
    if ((rv = skStreamCreate(
             &state->in_stream, SK_IO_READ, SK_CONTENT_SILK_FLOW))
        || (rv = skStreamBind(state->in_stream, state->in_path))
        || (rv = skStreamOpen(state->in_stream))
        || (rv = skStreamReadSilkHeader(state->in_stream, &in_hdr)))
    {
        /* Problem with input file.  Move to error directory. */
        skStreamLastErrMessage(state->in_stream, rv, errbuf, sizeof(errbuf));
        WARNINGMSG(("Error initializing incremental file: %s."
                    " Repository unchanged"), errbuf);
        skStreamDestroy(&state->in_stream);
        return appender_error_input(state);
    }

#if 1
    /* Determine the pathname of the hourly file to which the
     * incremental file will be appended; attempt to use the
     * packed-file header in the file, but fall back to the file
     * naming convention if we must. */
    h.he = skHeaderGetFirstMatch(in_hdr, SK_HENTRY_PACKEDFILE_ID);
    if (!(h.he
          && skHentryPackedfileGetRepositoryKey(h.pf, &repo_key)
          && sksiteGeneratePathname(state->out_path, sizeof(state->out_path),
                                    &repo_key, "", NULL,&state->out_basename)))
    {
        if (h.he) {
            DEBUGMSG(("Falling back to file naming convention for '%s':"
                      " Unable to generate path from packed-file header"),
                     state->in_basename);
        } else {
            DEBUGMSG(("Falling back to file naming convention for '%s':"
                      " File does not have a packed-file header"),
                     state->in_basename);
        }
        if ( !sksiteParseGeneratePath(state->out_path, sizeof(state->out_path),
                                      state->in_basename, "", NULL,
                                      &state->out_basename))
        {
            WARNINGMSG(("Error initializing incremental file:"
                        " File does not have the necessary header and"
                        " does not match SiLK naming convention: '%s'."
                        " Repository unchanged"), state->in_path);
            skStreamDestroy(&state->in_stream);
            return appender_error_input(state);
        }
    }
#else  /* #if 1 */
    if (!sksiteGeneratePathname(state->out_path, sizeof(state->out_path),
                                &state->key, "", NULL,&state->out_basename))
    {
        WARNINGMSG(("Unable to generate repository pathname for"
                    " incremental file '%s' %u %u %" PRId64
                    ". Repository unchanged"),
                   state->in_path, state->key.flowtype_id,
                   state->key.sensor_id, state->key.timestamp);
        skStreamDestroy(&state->in_stream);
        return appender_error_input(state);
    }
#endif  /* #else of #if 1 */

    rwRecInitialize(&rwrec, state->L);

    /* Read the first record from the incremental file */
    rv = skStreamReadRecord(state->in_stream, &rwrec);
    if (SKSTREAM_OK != rv) {
        if (SKSTREAM_ERR_EOF == rv) {
            INFOMSG(("No records found in incremental file '%s'."
                     " Repository unchanged"), state->in_basename);
            /* the next message is here for consistency, but it is
             * misleading since the output file was never opened
             * and may not even exist */
            INFOMSG(("APPEND OK '%s' to '%s' @ %" PRId64),
                    state->in_basename, state->out_path, state->pos);
            skStreamDestroy(&state->in_stream);
            if (INPUT_APPEND == input_mode) {
                dispose_incoming_file(state->in_path, incoming_directory, 0);
            } else {
                /* we could move this file to the archive_directory,
                 * but what would be the point given that the file is
                 * empty? */
                rv = unlink(state->in_path);
                if (-1 == rv) {
                    WARNINGMSG("Error removing incremental file '%s': %s",
                               state->in_path, strerror(errno));
                }
            }
            return 0;
        }
        skStreamLastErrMessage(state->in_stream, rv, errbuf, sizeof(errbuf));
        WARNINGMSG(("Error reading first record from incremental file: %s."
                    " Repository unchanged"), errbuf);
        skStreamDestroy(&state->in_stream);
        return appender_error_input(state);
    }

    /* Check for incremental files outside of the time window.
     *
     * NOTE: This is not a loop; the while() is used to allow us to
     * break out of the block. */
    while (check_time_window) {
        int64_t diff;
        time_t t = time(NULL);
        sktime_t rec_time;

        rec_time = rwRecGetStartTime(&rwrec);
        diff = ((int64_t)t / 3600) - (sktimeGetSeconds(rec_time) / 3600);
        if (diff > reject_hours_past) {
            NOTICEMSG(("Skipping incremental file: First record's"
                       " timestamp occurs %" PRId64 " hours in the"
                       " past: '%s'. Repository unchanged"),
                      diff, state->in_path);
            skStreamDestroy(&state->in_stream);
            /* rwRecReset(rwrec); */
            return appender_error_input(state);
        }
        if (-diff > reject_hours_future) {
            NOTICEMSG(("Skipping incremental file: First record's"
                       " timestamp occurs %" PRId64 " hours in the"
                       " future: '%s'. Repository unchanged"),
                      -diff, state->in_path);
            skStreamDestroy(&state->in_stream);
            /* rwRecReset(rwrec); */
            return appender_error_input(state);
        }
        break;
    }

    /* if necessary, wait for another thread to finish modifying this
     * hourly file */
    found = NULL;
    pthread_mutex_lock(&appender_tree_mutex);
    while (running
           && ((found = rbsearch(state, appender_tree)) != state))
    {
        if (NULL == found) {
            skAppPrintOutOfMemory("red-black insert");
            pthread_mutex_unlock(&appender_tree_mutex);
            /*rwRecReset(rwrec);*/
            return -1;
        }
        TRACEMSG(1, ("Thread %s waiting for thread %s to finish writing '%s'",
                     state->name, ((const appender_state_t*)found)->name,
                     state->out_basename));
        pthread_cond_wait(&appender_tree_cond, &appender_tree_mutex);
    }
    if (!running) {
        if (found == state) {
            rbdelete(found, appender_tree);
        }
        pthread_mutex_unlock(&appender_tree_mutex);
        /*rwRecReset(rwrec);*/
        return 1;
    }
    pthread_mutex_unlock(&appender_tree_mutex);

    TRACEMSG(1, ("Thread %s is writing '%s'",
                 state->name, state->out_basename));

    /* open the file */
    rv = appender_open_output(state, &mode);
    if (0 == rv) {
        /* FIXME: This code is broken in SiLK 4.  The read
         * cursor is always at byte offset 0 at this point. */
        if (SK_IO_APPEND == mode) {
            /* FIXME: Set this to an arbitrary, non-zero value, so the
             * is-this-a-new-hourly-file code will work.
             *
             * So much fail! */
            state->pos = 8;
        } else {
            /* Create and write a new file header */
            state->pos = 0;
            if (skStreamGetContentType(state->in_stream)
                == SK_CONTENT_SILK_FLOW)
            {
                in_hdr = skStreamGetSilkHeader(state->in_stream);
                rv = appender_write_output_header(state, in_hdr);
            }
        }
    }
    if (rv) {
        if (-1 == rv) {
            ERRMSG("APPEND FAILED '%s' to '%s' -- nothing written",
                   state->in_basename, state->out_path);
        }
        skStreamDestroy(&state->in_stream);
        appender_finished_output(state);
        /*rwRecReset(rwrec);*/
        return rv;
    }

    /* initialize close_pos */
    close_pos = 0;

    /* Write record to output and read next record from input */
    do {
        out_rv = skStreamWriteRecord(state->out_stream, &rwrec);
        if (out_rv != SKSTREAM_OK) {
            if (SKSTREAM_ERROR_IS_FATAL(out_rv)) {
                appender_error_output(state, close_pos, out_rv);
                /*rwRecReset(rwrec);*/
                return -1;
            }
            skStreamPrintLastErr(state->out_stream, out_rv, &WARNINGMSG);
        }
    } while ((rv = skStreamReadRecord(state->in_stream, &rwrec))
             == SKSTREAM_OK);

    /* Flush and close the output file.  If flush fails, truncate
     * the file before closing. */
    out_rv = skStreamFlush(state->out_stream);
    if (out_rv) {
        appender_error_output(state, close_pos, out_rv);
        return -1;
    }
    close_pos = (int64_t)skStreamTell(state->out_stream);
    out_rv = skStreamClose(state->out_stream);
    if (out_rv) {
        /* Assuming the flush above was successful (and assuming
         * the stream is still open), the close() call should not
         * fail except for EINTR (interrupt).  However, go ahead
         * and exit anyway. */
        appender_error_output(state, close_pos, out_rv);
        return -1;
    }

    DEBUGMSG(("Read %" PRIu64 " recs from '%s';"
              " wrote %" PRIu64 " recs to '%s';"
              " old size %" PRId64 "; new size %" PRId64),
             skStreamGetRecordCount(state->in_stream), state->in_basename,
             skStreamGetRecordCount(state->out_stream),state->out_basename,
             state->pos, close_pos);

    appender_finished_output(state);

    if (SKSTREAM_ERR_EOF != rv) {
        /* Success; though unexpected error on read.  Currently treat
         * this as successful, but should we move the incremental file
         * to the error_directory instead? */
        skStreamLastErrMessage(state->in_stream,rv,errbuf,sizeof(errbuf));
        NOTICEMSG(("Unexpected error reading incremental file but"
                   " treating file as successful: %s"), errbuf);
    }

    /* close input */
    rv = skStreamClose(state->in_stream);
    if (rv) {
        skStreamPrintLastErr(state->in_stream, rv, &NOTICEMSG);
    }
    skStreamDestroy(&state->in_stream);

    INFOMSG(("APPEND OK %s to %s @ %" PRId64),
            state->in_basename, state->out_path, state->pos);

    /* run the hour_file_command when specified */
    if (hour_file_command && 0 == state->pos) {
        char *expanded_cmd;

        expanded_cmd = (skSubcommandStringFill(
                            hour_file_command, "s", state->out_path));
        if (NULL == expanded_cmd) {
            WARNINGMSG("Unable to allocate memory to create command string");
        } else {
            DEBUGMSG("Running hour_file_command: %s", expanded_cmd);
            rv = skSubcommandExecuteShell(expanded_cmd);
            switch (rv) {
              case -1:
                ERRMSG("Unable to fork to run hour_file_command: %s",
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
    }

    if (INPUT_APPEND == input_mode) {
        dispose_incoming_file(state->in_path, incoming_directory, 0);
    } else {
        /* FIXME: should allow these files to be moved to another
         * directory for processing by something else, e.g., pipeline */
        rv = unlink(state->in_path);
        if (-1 == rv) {
            WARNINGMSG("Error removing incremental file '%s': %s",
                       state->in_path, strerror(errno));
        }
    }

    return 0;
}


/*
 *  status = appender_open_output(state, in_hdr)
 *
 *    Given the SiLK Flow stream connected to an incremental file
 *    whose SiLK header is in 'in_hdr', either open an existing hourly
 *    file or create a new hourly file at the location specified by
 *    'state->out_path' of the same type and version (RWSPLIT, etc) to
 *    hold the data in the incremental file.  The handle to the opened
 *    stream is put into the value pointed at by 'state->out_stream'.
 *    'state->pos' is set to 0 if the file is newly created, or to the
 *    current size of the file.  This function obtains a write-lock on
 *    the opened file.
 *
 *    Return 0 on success.  On error, print a message to the log and
 *    return -1.  When an error occurs, the value of 'out_pos' is
 *    indeterminate.  Return 1 if the 'shutting_down' variable is set
 *    while waiting on another thread's write-lock.
 */
static int
appender_open_output(
    appender_state_t   *state,
    skstream_mode_t    *out_mode)
{
    char buf[PATH_MAX];
    int rv = SKSTREAM_OK;
    int filemod = S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH;
    skcontent_t content;
    int flags;
    int fd = -1;

    /* Open an existing hourly file or create a new hourly file as
     * necessary. */
    if (skFileExists(state->out_path)) {
        DEBUGMSG("Opening existing repository file '%s'", state->out_path);

        /* Open existing file for read and write. */
        flags = O_RDWR | O_APPEND;
        fd = open(state->out_path, flags, filemod);
        if (-1 == fd) {
            if (ENOENT != errno) {
                WARNINGMSG("Unable to open existing output file '%s': %s",
                           state->out_path, strerror(errno));
                return -1;
            }
            DEBUGMSG(("Existing file removed before opening;"
                      " attempting to open new file '%s'"), state->out_path);
            flags = O_RDWR | O_CREAT | O_EXCL;
            fd = open(state->out_path, flags, filemod);
            if (-1 == fd) {
                WARNINGMSG("Unable to open new output file '%s': %s",
                           state->out_path, strerror(errno));
                return -1;
            }
        }

    } else {
        DEBUGMSG("Opening new repository file '%s'", state->out_path);

        /* Create directory for new file */
        if (!skDirname_r(buf, state->out_path, sizeof(buf))) {
            WARNINGMSG("Unable to determine directory of '%s'",
                       state->out_path);
            return -1;
        }
        if (!skDirExists(buf)) {
            TRACEMSG(3, ("Creating directory '%s'...", buf));
            if (skMakeDir(buf)) {
                WARNINGMSG("Unable to create directory '%s': %s",
                           buf, strerror(errno));
                return -1;
            }
        }

        /* Open new file. */
        flags = O_RDWR | O_CREAT | O_EXCL;
        fd = open(state->out_path, flags, filemod);
        if (-1 == fd) {
            if (EEXIST != errno) {
                WARNINGMSG("Unable to open new output file '%s': %s",
                           state->out_path, strerror(errno));
                return -1;
            }
            DEBUGMSG(("Nonexistent file appeared before opening;"
                      " attempting to open existing file '%s'"),
                     state->out_path);
            flags = O_RDWR | O_APPEND;
            fd = open(state->out_path, flags, filemod);
            if (-1 == fd) {
                WARNINGMSG("Unable to open new output file '%s': %s",
                           state->out_path, strerror(errno));
                return -1;
            }
        }
    }

    TRACEMSG(2, ("Flags are 0x%x for opened file '%s'",
                 flags, state->out_path));

    /* Lock the file */
    if (!no_file_locking) {
        TRACEMSG(1, ("Locking file '%s'", state->out_path));
        while (skFileSetLock(fd, F_WRLCK, F_SETLKW) != 0) {
            if (!running) {
                TRACEMSG(1, ("Shutdown while locking '%s'", state->out_path));
                close(fd);
                return 1;
            }
            switch (errno) {
              case EINTR:
                TRACEMSG(1, ("Interrupt while locking '%s'", state->out_path));
                continue;
              case ENOLCK:
              case EINVAL:
                TRACEMSG(1, ("Errno %d while locking '%s'",
                             errno, state->out_path));
                NOTICEMSG("Unable to get write lock;"
                          " consider using the --no-file-locking switch");
                break;
              default:
                TRACEMSG(1, ("Errno %d while locking '%s'",
                             errno, state->out_path));
                break;
            }
            goto ERROR;
        }
    }

    /*
     * At this point we have the write lock.  Regardless of whether we
     * think the file is new or existing, we need to check for the
     * file header for a couple of reasons: (1)We may be opening a
     * 0-length file from a previously failed attempt. (2)We may open
     * a new file but another process can find the file, lock it, and
     * write the header to it prior to us locking the file.
     */

    /* Can we read the number of bytes in a SiLK file header?  The
     * header will be reread and verified when the descriptor is bound
     * to an skstream. */
    rv = read(fd, buf, RWFLOWPACK_OPEN_EXIST_READLEN);
    if (rv == RWFLOWPACK_OPEN_EXIST_READLEN) {
        TRACEMSG(1, ("Read all header bytes from file '%s'", state->out_path));
        /* file has enough bytes to contain a silk header; will treat
         * it as SK_IO_APPEND */
        if (!(flags & O_APPEND)) {
            /* add O_APPEND to the flags */
            DEBUGMSG("Found data in file; will append to '%s'",
                     state->out_path);
            flags = fcntl(fd, F_GETFL, 0);
            if (-1 == flags) {
                WARNINGMSG("Failed to get flags for file '%s': %s",
                           state->out_path, strerror(errno));
                goto ERROR;
            }
            flags |= O_APPEND;
            TRACEMSG(2, ("Setting flags to 0x%x for '%s'",
                         flags, state->out_path));
            rv = fcntl(fd, F_SETFL, flags);
            if (-1 == rv) {
                WARNINGMSG("Failed to set flags for file '%s': %s",
                           state->out_path, strerror(errno));
                goto ERROR;
            }
        }
        /* else, flags include O_APPEND, we are good. */

    } else if (0 == rv) {
        TRACEMSG(1, ("Read no header bytes from file '%s'", state->out_path));
        /* file is empty; will treat it as SK_IO_WRITE */
        if (flags & O_APPEND) {
            /* must remove the O_APPEND flag */
            DEBUGMSG("Opened empty file; adding header to '%s'",
                     state->out_path);
            flags = fcntl(fd, F_GETFL, 0);
            if (-1 == flags) {
                WARNINGMSG("Failed to get flags for file '%s': %s",
                           state->out_path, strerror(errno));
                goto ERROR;
            }
            flags &= ~O_APPEND;
            TRACEMSG(2, ("Setting flags to 0x%x for '%s'",
                         flags, state->out_path));
            rv = fcntl(fd, F_SETFL, flags);
            if (-1 == rv) {
                WARNINGMSG("Failed to set flags for file '%s': %s",
                           state->out_path, strerror(errno));
                goto ERROR;
            }
        }
        /* else, flags do not include O_APPEND, we are good */

    } else if (-1 == rv) {
        WARNINGMSG("Error attempting to read file header from '%s': %s",
                   state->out_path, strerror(errno));
        goto ERROR;
    } else {
        /* short read */
        WARNINGMSG("Read %d/%d bytes from '%s'",
                   rv, RWFLOWPACK_OPEN_EXIST_READLEN, state->out_path);
        goto ERROR;
    }

    TRACEMSG(2, ("Flags are 0x%x for opened file '%s'",
                 fcntl(fd, F_GETFL, 0), state->out_path));

    *out_mode = ((flags & O_APPEND) ? SK_IO_APPEND : SK_IO_WRITE);

    /* File looks good; create an skstream */
    TRACEMSG(1, ("Creating %s skstream for '%s'",
                 ((SK_IO_APPEND == *out_mode) ? "APPEND" : "WRITE"),
                 state->out_path));

    content = skStreamGetContentType(state->in_stream);
    if ((rv = skStreamCreate(&state->out_stream, *out_mode, content))
        || (rv = skStreamBind(state->out_stream, state->out_path))
        || (rv = skStreamFDOpen(state->out_stream, fd)))
    {
        /* NOTE: it is possible for skStreamFDOpen() to have stored
         * the value of 'fd' but return an error. */
        if (state->out_stream
            && skStreamGetDescriptor(state->out_stream) == fd)
        {
            fd = -1;
        }
        goto ERROR;
    }
    /* The stream controls this now */
    fd = -1;

    if (SK_IO_APPEND == *out_mode && content == SK_CONTENT_SILK_FLOW) {
        /* read the header---which also seeks to the end of the
         * file */
        rv = skStreamReadSilkHeader(state->out_stream, NULL);
        if (rv) {
            goto ERROR;
        }
    }

    return 0;

  ERROR:
    if (state->out_stream) {
        if (rv) {
            skStreamPrintLastErr(state->out_stream, rv, &WARNINGMSG);
        }
        skStreamDestroy(&state->out_stream);
    }
    if (-1 != fd) {
        close(fd);
    }
    return -1;
}


/*
 *  status = appender_write_output_header(state, in_hdr);
 *
 *    Write the SiLK file header to the empty SiLK Flow file contained
 *    in 'state'.
 *
 *    The new file's header is a complete copy of the data in the
 *    existing SiLK header 'in_hdr'.
 *
 *    Return 0 on success.  On error, print a message to the log,
 *    truncate the file to 0 bytes, and return -1.
 */
int
appender_write_output_header(
    appender_state_t       *state,
    const sk_file_header_t *in_hdr)
{
    char errbuf[2 * PATH_MAX];
    sk_file_header_t *out_hdr;
    sk_sidecar_t *sc;
    int rv = SKSTREAM_OK;

    out_hdr = skStreamGetSilkHeader(state->out_stream);
    rv = skHeaderCopy(out_hdr, in_hdr, SKHDR_CP_ALL);
    if (rv) {
        skStreamPrintLastErr(state->out_stream, rv, &WARNINGMSG);
        return -1;
    }

    skHeaderRemoveAllMatching(out_hdr, SK_HENTRY_SIDECAR_ID);
    sc = sk_sidecar_create_from_header(in_hdr, NULL);
    if (sc) {
        skStreamSetSidecar(state->out_stream, sc);
        /* stream copies the sidecar */
        sk_sidecar_destroy(&sc);
    }

    rv = skStreamWriteSilkHeader(state->out_stream);
    if (rv) {
        skStreamLastErrMessage(state->out_stream, rv, errbuf, sizeof(errbuf));
        ERRMSG("Error writing header to newly opened file: %s", errbuf);
        appender_truncate_output(state);
        return -1;
    }

    /* Success! */
    return 0;
}

/*
 *  appender_finished_output(state);
 *
 *    Destroy the output stream that 'state' is writing, and remove
 *    'state' from the red-black tree.
 *
 *    To handle or log errors when the output stream is closed, the
 *    caller must call skStreamClose() to close the stream before
 *    calling this function.
 */
static void
appender_finished_output(
    appender_state_t   *state)
{
    skStreamDestroy(&state->out_stream);
    pthread_mutex_lock(&appender_tree_mutex);
    TRACEMSG(1, ("Thread %s has finished processing file '%s'",
                 state->name, state->out_basename));
    rbdelete(state, appender_tree);
    pthread_cond_broadcast(&appender_tree_cond);
    pthread_mutex_unlock(&appender_tree_mutex);
}


/*
 *  status = appender_truncate_output(state);
 *
 *    Handle an error after writing some data to the repository file
 *    in 'state->out_stream'.  This function assumes the stream is still
 *    open.
 *
 *    Truncate the repository file to its original size as specified
 *    by 'state->pos' and then close the file and destroy the stream.
 *
 *    If either of these actions result in an error, return -1.
 *    Otherwise, return 0.
 */
static int
appender_truncate_output(
    appender_state_t   *state)
{
    char errbuf[2 * PATH_MAX];
    int retval;
    int rv;

    retval = 0;

    NOTICEMSG("Truncating repository file size to %" PRId64 ": '%s'",
              state->pos, state->out_path);
    rv = skStreamTruncate(state->out_stream, (off_t)state->pos);
    if (rv) {
        skStreamLastErrMessage(state->out_stream, rv, errbuf, sizeof(errbuf));
        ERRMSG(("State of repository file is unknown due to error"
                " while truncating file: %s"), errbuf);
        retval = -1;
        rv = skStreamClose(state->out_stream);
        if (rv) {
            skStreamPrintLastErr(state->out_stream, rv, ERRMSG);
        }
    } else {
        rv = skStreamClose(state->out_stream);
        if (rv) {
            skStreamLastErrMessage(state->out_stream,rv,errbuf,sizeof(errbuf));
            NOTICEMSG(("State of repository file is unknown due to error"
                       " while closing the truncated file: %s"), errbuf);
            retval = -1;
        }
    }
    appender_finished_output(state);

    return retval;
}


/*
 *  status = appender_error_output(state, close_pos, err_code);
 *
 *    Helper function called by appender_handle_file() when there is a
 *    problem writing to the hourly file.
 *
 *    'close_pos' holds the file offset after calling skStreamFlush()
 *    and before calling skStreamClose().  'err_code' is the value
 *    returned by the skstream call that failed.
 */
static void
appender_error_output(
    appender_state_t   *state,
    int64_t             close_pos,
    int                 err_code)
{
    char errbuf[2 * PATH_MAX];

    /* Error writing. If repository file is still open, truncate it to
     * its original size.  Move incremental file to the error
     * directory if repository file cannot be truncated. */
    skStreamLastErrMessage(state->out_stream, err_code, errbuf,sizeof(errbuf));
    ERRMSG("Fatal error writing to hourly file: %s", errbuf);
    ERRMSG(("APPEND FAILED '%s' to '%s' @ %" PRId64),
           state->in_basename, state->out_path, state->pos);
    if (close_pos) {
        /* flush was okay but close failed. */
        ERRMSG(("Repository file '%s' in unknown state since flush"
                " succeeded but close failed"), state->out_path);
        appender_finished_output(state);
    } else if (appender_truncate_output(state)) {
        /* error truncating file */
        close_pos = -1;
    }
    if (close_pos) {
        appender_error_input(state);
    }
    skStreamDestroy(&state->in_stream);
    CRITMSG("Aborting due to append error");
}


/*
 *  status = appender_error_input(state);
 *
 *    Helper function called by appender_handle_file() when there is a
 *    problem with the incremental file.
 *
 *    This function is to be used before the output file has been
 *    opened.
 */
static int
appender_error_input(
    const appender_state_t *state)
{
    int rv;

    if (INPUT_APPEND == input_mode) {
        INFOMSG("Moving incremental file '%s' to the error directory",
                state->in_basename);
        if (dispose_incoming_file(state->in_path, incoming_directory, 1)) {
            return -1;
        }
    } else {
        /* FIXME: Should not delete incremental files that have
         * problems when those files have been creaed by this instance
         * of rwflowpack. */
        rv = unlink(state->in_path);
        if (-1 == rv) {
            WARNINGMSG("Error removing incremental file '%s': %s",
                       state->in_path, strerror(errno));
        }
    }
    return 0;
}


/*
 *  cmp = appender_tree_cmp(a, b, ctx);
 *
 *    Comparison function for the red-black tree 'appender_tree'.  The
 *    first two parameters are pointers to appender_state_t.
 */
static int
appender_tree_cmp(
    const void         *state1,
    const void         *state2,
    const void  UNUSED(*ctx))
{
    return strcmp(((const appender_state_t*)state1)->out_basename,
                  ((const appender_state_t*)state2)->out_basename);
}


int
appender_start(
    void)
{
    appender_state_t *state;
    uint32_t i;
    int rv;

    running = 1;

    /* Start the appender threads. */
    NOTICEMSG("Starting %" PRIu32 " appender thread%s...",
              appender_count, ((appender_count > 1) ? "s" : ""));
    pthread_mutex_lock(&appender_state_mutex);
    for (i = 0, state = appender_state; i < appender_count; ++i, ++state) {
        DEBUGMSG("Starting appender thread %s...", state->name);
        state->status = APPENDER_STARTING;
        increment_thread_count();
        rv = skthread_create(
            state->name, &state->thread, appender_thread_main, state);
        if (rv) {
            running = 0;
            decrement_thread_count(0);
            CRITMSG("Failed to start appender thread %s: %s",
                    state->name, strerror(rv));
            state->status = APPENDER_STOPPED;
            pthread_mutex_unlock(&appender_state_mutex);
            return -1;
        }
    }
    pthread_mutex_unlock(&appender_state_mutex);
    NOTICEMSG("Started all appender threads.");

    return 0;
}


void
appender_stop(
    void)
{
    appender_state_t *state;
    appender_status_t status;
    uint32_t i;

    if (NULL == appender_state) {
        return;
    }

    running = 0;

    if (appender_tree) {
        /* awake anyone blocked on red-black tree access */
        pthread_cond_broadcast(&appender_tree_cond);
    }

    /* wait for threads to finish and join each thread */
    for (i = 0, state = appender_state; i < appender_count; ++i, ++state) {
        pthread_mutex_lock(&appender_state_mutex);
        status = state->status;
        pthread_mutex_unlock(&appender_state_mutex);
        if (APPENDER_STARTED == status) {
            DEBUGMSG("Waiting for appender thread %s to finish...",
                     state->name);
            pthread_join(state->thread, NULL);
            DEBUGMSG("Appender thread %s has finished.", state->name);
        }
        pthread_mutex_lock(&appender_state_mutex);
        state->status = APPENDER_STOPPED;
        pthread_mutex_unlock(&appender_state_mutex);
    }
}


int
appender_setup(
    void)
{
    appender_state_t *state;
    uint32_t i;

    /* need to initialize the appender_state for each thread */
    appender_state = ((appender_state_t*)
                      calloc(appender_count, sizeof(appender_state_t)));
    if (NULL == appender_state) {
        skAppPrintOutOfMemory("appender_state");
        appender_count = 0;
        return -1;
    }
    for (i = 0, state = appender_state; i < appender_count; ++i, ++state) {
        state->status = APPENDER_STOPPED;
        snprintf(state->name, sizeof(state->name), "#%" PRIu32, 1 + i);
    }
    /* create the red-black tree */
    appender_tree = rbinit(&appender_tree_cmp, NULL);
    if (NULL == appender_tree) {
        skAppPrintOutOfMemory("red-black tree");
        return -1;
    }
    return 0;
}


void
appender_teardown(
    void)
{
    if (appender_tree) {
        rbdestroy(appender_tree);
    }
    free(appender_state);
}


/*
** Local Variables:
** mode:c
** indent-tabs-mode:nil
** c-basic-offset:4
** End:
*/
