/*
** Copyright (C) 2008-2017 by Carnegie Mellon University.
**
** @OPENSOURCE_LICENSE_START@
** See license information in ../../LICENSE.txt
** @OPENSOURCE_LICENSE_END@
*/

/*
**  rwfilterthread.c
**
**    Variables/Functions to support having rwfilter spawn multiple
**    threads to process files.
*/

#include <silk/silk.h>

RCSIDENT("$SiLK: rwfilterthread.c efd886457770 2017-06-21 18:43:23Z mthomas $");

#include <silk/skthread.h>
#include "rwfilter.h"


/* TYPEDEFS AND DEFINES */


/* LOCAL VARIABLE DEFINITIONS */

/* the main thread */
static pthread_t main_thread;

static pthread_mutex_t next_file_mutex = PTHREAD_MUTEX_INITIALIZER;

static pthread_mutex_t dest_mutex[DESTINATION_TYPES];


/* FUNCTION DEFINITIONS */


/*
 *  appHandleSignal(signal_value)
 *
 *    Set the 'reading_records' global to 0 which will begin the
 *    shutdown process.  Print a message unless the signal is SIGPIPE.
 */
static void
appHandleSignal(
    int                 sig)
{
    reading_records = 0;

    if (sig == SIGPIPE) {
        /* we get SIGPIPE if something downstream, like rwcut, exits
         * early, so don't bother to print a warning, and exit
         * successfully */
        exit(EXIT_SUCCESS);
    } else {
        skAppPrintErr("Caught signal..cleaning up and exiting");
    }
}


/*
 *  status = writeBufferThreaded(dest_id, rec_buffer);
 *
 *    Write the records from 'rec_buffer' to the destinations in the
 *    global 'dest_type' array indexed by 'dest_id' (PASS, FAIL, ALL).
 *    Return SKSTREAM_OK on success, non-zero on error.
 *
 *    Reset the count of records in 'rec_buffer' to 0.
 *
 *    This function is called by filterFile() in rwfilter.c.
 */
int
writeBufferThreaded(
    filter_thread_t    *thread,
    int                 dest_id)
{
    recbuf_t *recbuf;
    destination_t *dest;
    destination_t *dest_next;
    const rwRec *recbuf_pos;
    const rwRec *end_rec;
    uint64_t total_rec_count;
    int close_after_add = 0;
    int recompute_reading = 0;
    int i;
    int rv = SKSTREAM_OK;

    recbuf = &thread->recbuf[dest_id];

    pthread_mutex_lock(&dest_mutex[dest_id]);

    /* list of destinations to get the records */
    dest = dest_type[dest_id].dest_list;
    if (dest == NULL) {
        assert(dest_type[dest_id].count == 0);
        goto END;
    }

    if (0 == dest_type[dest_id].max_records) {
        /* find location of our stopping condition */
        end_rec = recbuf->buf + recbuf->count;
    } else {
        /* an output limit was specified, so see if we will hit it
         * while adding these records.  If so, do not write all
         * records and set a flag to close the output after adding the
         * records. */
        uint32_t reccount = recbuf->count;
        total_rec_count = skStreamGetRecordCount(dest->stream);
        if (total_rec_count + reccount > dest_type[dest_id].max_records) {
            assert(dest_type[dest_id].max_records >= total_rec_count);
            reccount = dest_type[dest_id].max_records - total_rec_count;
            close_after_add = 1;
            recompute_reading = 1;
        }
        end_rec = recbuf->buf + reccount;
    }

    do {
        dest_next = dest->next;
        for (recbuf_pos = recbuf->buf; recbuf_pos < end_rec; ++recbuf_pos) {
            rv = skStreamWriteRecord(dest->stream, recbuf_pos);
            if (SKSTREAM_ERROR_IS_FATAL(rv)) {
                if (skStreamGetLastErrno(dest->stream) == EPIPE) {
                    /* close this stream */
                    closeOneOutput(dest_id, dest);
                    recompute_reading = 1;
                    break;
                } else {
                    /* print the error and return */
                    skStreamPrintLastErr(dest->stream, rv, &skAppPrintErr);
                    reading_records = 0;
                    goto END;
                }
            }
        }
    } while ((dest = dest_next) != NULL);

    if (close_after_add) {
        closeOutputDests(dest_id, 0);
    }

    rv = SKSTREAM_OK;

    if (recompute_reading) {
        /* deadlock avoidance: unlock the mutex for this 'dest_id',
         * then lock the mutexes for all destination types, sum the
         * number of outputs, then free all mutexes in reverse
         * order. */
        int num_outs = 0;
        pthread_mutex_unlock(&dest_mutex[dest_id]);
        for (i = 0; i < DESTINATION_TYPES; ++i) {
            pthread_mutex_lock(&dest_mutex[i]);
            num_outs += dest_type[i].count;
        }
        if (!num_outs) {
            reading_records = 0;
        }
        for (i = DESTINATION_TYPES; i > 0; ) {
            --i;
            pthread_mutex_unlock(&dest_mutex[i]);
        }
        return rv;
    }

  END:
    recbuf->count = 0;
    pthread_mutex_unlock(&dest_mutex[dest_id]);
    return rv;
}


/*
 *  workerThread(&filter_thread_data);
 *
 *    THREAD ENTRY POINT.
 *
 *    Get the name of the next file to process and calls filterFile()
 *    to process that file.  Stop processing when there are no more
 *    files to process or when an error occurs.
 *
 *    The filterFile() function calls writeBufferThreaded() to write
 *    buffers to disk.
 */
static void *
workerThread(
    void               *v_thread)
{
    filter_thread_t *self = (filter_thread_t *)v_thread;
    skstream_t *stream = NULL;
    int rv = 0;
    int i;

    /* ignore all signals unless this thread is the main thread */
    if (!pthread_equal(main_thread, self->thread)) {
        skthread_ignore_signals();
    }

    self->rv = 0;

    if (dryrun_fp) {
        char flowfile[PATH_MAX];

        for (;;) {
            pthread_mutex_lock(&next_file_mutex);
            rv = skOptionsCtxNextArgument(optctx, flowfile, sizeof(flowfile));
            pthread_mutex_unlock(&next_file_mutex);
            if (0 != rv) {
                break;
            }
            fprintf(dryrun_fp, "%s\n", flowfile);
        }
        return NULL;
    }

    for (;;) {
        pthread_mutex_lock(&next_file_mutex);
        /* close the previous stream */
        if (stream) {
            sk_flow_iter_close_stream(flowiter, stream);
        }
        rv = sk_flow_iter_get_next_stream(flowiter, &stream);
        pthread_mutex_unlock(&next_file_mutex);
        if (SKSTREAM_OK != rv) {
            if (SKSTREAM_ERR_EOF == rv) {
                break;
            }
            continue;
        }

        rv = filterFile(stream, NULL, self);
        if (rv < 0) {
            /* fatal error */
            self->rv = rv;
            pthread_mutex_lock(&next_file_mutex);
            sk_flow_iter_close_stream(flowiter, stream);
            pthread_mutex_unlock(&next_file_mutex);
            return NULL;
        }
        /* if (rv > 0) there was an error opening/reading input: ignore */
    }

    /* write any records still in the buffers */
    for (i = 0; i < DESTINATION_TYPES; ++i) {
        if (self->recbuf[i].count) {
            writeBufferThreaded(self, i);
        }
    }

    return NULL;
}


/*
 *  status = threadedFilter(&stats);
 *
 *    The "main" to use when rwfilter is used with threads.
 *
 *    Creates necessary data structures and then creates threads to
 *    process input files.  Once all input files have been processed,
 *    combines results from all threads to fill in the statistics
 *    structure 'stats'.  Returns 0 on success, non-zero on error.
 */
int
threadedFilter(
    filter_stats_t     *stats)
{
    filter_thread_t *thread;
    filter_thread_t *t;
    int i;
    uint32_t j;
    int rv = 0;

    /* get the main thread */
    main_thread = pthread_self();

    /* set a signal handler */
    if (skAppSetSignalHandler(&appHandleSignal)) {
        skAppPrintErr("Unable to set signal handler");
        exit(EXIT_FAILURE);
    }
    /* override that signal handler and ignore SIGPIPE */
    filterIgnoreSigPipe();

    /* initialize the mutexes for each destination type */
    for (i = 0; i < DESTINATION_TYPES; ++i) {
        pthread_mutex_init(&dest_mutex[i], NULL);
    }

    /* create the data structure to hold each thread's state */
    thread = (filter_thread_t*)calloc(thread_count, sizeof(filter_thread_t));
    if (thread == NULL) {
        goto END;
    }

    /* create a Lua state for each thread.  The thread's Lua state is
     * used to pass the sidecar data through the thread and it is not
     * used for partitioning the flow records.  Initialize the rwRec
     * on each thread using that Lua state. */
    for (j = 0, t = thread; j < thread_count; ++j, ++t) {
        /* thread 0 uses the existing lua state */
        if (j == 0) {
            t->lua_state = L;
        } else {
            t->lua_state = filterLuaCreateState();
            if (NULL == t->lua_state) {
                goto END;
            }
        }
        rwRecInitialize(&t->rwrec, t->lua_state);
    }

    /* for active each destination type, create buffers on each thread
     * that buffer the records prior to writing them to the stream. */
    for (i = 0; i < DESTINATION_TYPES; ++i) {
        if (dest_type[i].count) {
            for (j = 0, t = thread; j < thread_count; ++j, ++t) {
                t->recbuf[i].buf
                    = (rwRec *)malloc(RECBUF_MAX_RECS * sizeof(rwRec));
                if (t->recbuf[i].buf == NULL) {
                    goto END;
                }
                rwRecInitializeArray(
                    t->recbuf[i].buf, t->lua_state, RECBUF_MAX_RECS);
                t->recbuf[i].max_count = RECBUF_MAX_RECS;
            }
        }
    }

    /* thread[0] is the main_thread */
    thread[0].thread = main_thread;

    /* create the threads, skip 0 since that is the main thread */
    for (j = 1, t = &thread[j]; j < thread_count; ++j, ++t) {
        pthread_create(&t->thread, NULL, &workerThread, t);
    }

    /* allow the main thread to also process files */
    workerThread(&thread[0]);

    /* join with the threads as they die off */
    for (j = 0, t = thread; j < thread_count; ++j, ++t) {
        if (j > 0) {
            pthread_join(t->thread, NULL);
        }
        rv |= t->rv;
        stats->read.flows += t->stats.read.flows;
        stats->read.pkts  += t->stats.read.pkts;
        stats->read.bytes += t->stats.read.bytes;
        stats->pass.flows += t->stats.pass.flows;
        stats->pass.pkts  += t->stats.pass.pkts;
        stats->pass.bytes += t->stats.pass.bytes;
        stats->files      += t->stats.files;
#if 0
        fprintf(stderr,
                "thread %d processed %" PRIu32 " files and "
                "passed %" PRIu64 "/%" PRIu64 " flows\n",
                j, t->stats.files, t->stats.pass.flows,
                t->stats.read.flows);
#endif
    }

  END:
    if (thread) {
        for (j = 0, t = thread; j < thread_count; ++j, ++t) {
            for (i = 0; i < DESTINATION_TYPES; ++i) {
                free(t->recbuf[i].buf);
            }
            if (j > 0 && t->lua_state) {
                sk_lua_closestate(t->lua_state);
            }
        }
        free(thread);
    }

    return rv;
}


/*
** Local Variables:
** mode:c
** indent-tabs-mode:nil
** c-basic-offset:4
** End:
*/
