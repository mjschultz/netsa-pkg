/*
** Copyright (C) 2001-2017 by Carnegie Mellon University.
**
** @OPENSOURCE_LICENSE_START@
** See license information in ../../LICENSE.txt
** @OPENSOURCE_LICENSE_END@
*/

/*
**  rwfilter.c
**
**  6/3/2001
**
**  Suresh L. Konda
**
**  Allows for selective extraction of records and fields from a rw
**  packed file.  This version, unlike rwcut, creates a binary file with
**  the filtered records.  A new file type is used.  The header does not
**  contain valid recCount and rejectCount values.  The other fields are
**  taken from the original input file.
**
**  A second header is also created which records the filter rules used
**  for each pass.  Thus this is a variable length header
*/


#include <silk/silk.h>

RCSIDENT("$SiLK: rwfilter.c 373f990778e9 2017-06-22 21:57:36Z mthomas $");

#include <silk/sksidecar.h>
#include "rwfilter.h"


/* TYPEDEFS AND DEFINES */


/* EXPORTED VARIABLES */

/* information about the destination types (ALL, PASS, FAIL); includes
 * a linked list of destination streams */
dest_type_t dest_type[DESTINATION_TYPES];

/* support for --print-statistics and --print-volume-statistics */
skstream_t *print_stat = NULL;

/* where to print output during a --dry-run; NULL when the switch has
 * not been provided */
FILE *dryrun_fp = NULL;

/* handle command line switches for input files, xargs, fglob */
sk_options_ctx_t *optctx = NULL;

/* handle for looping over the input records or files */
sk_flow_iter_t *flowiter = NULL;

/* true as long as we are reading records */
int reading_records = 1;

/* whether to print volume statistics */
int print_volume_stats = 0;

/* total number of threads */
uint32_t thread_count = RWFILTER_THREADS_DEFAULT;

/* number of checks to preform */
int checker_count = 0;

/* function pointers to handle checking and or processing */
checktype_t (*checker[MAX_CHECKERS])(const rwRec*);

/* The Lua state */
lua_State *L = NULL;

/* The fglob state */
sk_fglob_t *fglob = NULL;



/* LOCAL VARIABLES */

/* read-only cache of argc and argv used for dumping headers */
static int pargc;
static char **pargv;


/* FUNCTION DEFINITIONS */

#if 0
/*
 *  status = writeHeaders(in_stream);
 *
 *    Create and print the header to each output file; include the
 *    current command line invocation in the header.  If 'in_stream'
 *    is non-null, the file history from that file is also included in
 *    the header.
 */
static int
writeHeaders(
    const skstream_t   *in_stream)
{
    static int did_headers = 0;
    sk_file_header_t *in_hdr = NULL;
    sk_file_header_t *out_hdr;
    destination_t *dest;
    int i;
    int rv = SKSTREAM_OK;

    /* only print headers one time */
    if (did_headers) {
        return rv;
    }
    did_headers = 1;

    /* don't print anything on a dry-run */
    if (dryrun_fp) {
        return rv;
    }

    if (in_stream) {
        in_hdr = skStreamGetSilkHeader(in_stream);
    }

    for (i = 0; i < DESTINATION_TYPES; ++i) {
        for (dest = dest_type[i].dest_list; dest != NULL; dest = dest->next) {
            out_hdr = skStreamGetSilkHeader(dest->stream);

            /* if 'in_stream' is provided, add its command invocation
             * history to each output file's headers */
            if (in_hdr) {
                rv = skHeaderCopyEntries(out_hdr, in_hdr,
                                         SK_HENTRY_INVOCATION_ID);
                if (rv == SKSTREAM_OK) {
                    rv = skHeaderCopyEntries(out_hdr, in_hdr,
                                             SK_HENTRY_ANNOTATION_ID);
                }
                if (rv == SKSTREAM_OK) {
                    rv = skHeaderCopyEntries(out_hdr, in_hdr,
                                             SK_HENTRY_SIDECAR_ID);
                }
            }
            if (rv == SKSTREAM_OK) {
                rv = skHeaderAddInvocation(out_hdr, 1, pargc, pargv);
            }
            if (rv == SKSTREAM_OK) {
                rv = skOptionsNotesAddToStream(dest->stream);
            }
            if (rv == SKSTREAM_OK) {
                rv = skStreamWriteSilkHeader(dest->stream);
            }
            if (rv != SKSTREAM_OK) {
                skStreamPrintLastErr(dest->stream, rv, &skAppPrintErr);
                return rv;
            }
        }
    }

    return rv;
}
#endif  /* 0 */


/* Write the stats to the program specified by the SILK_STATISTICS envar */
static void
logStats(
    const filter_stats_t   *stats,
    time_t                 *start_time,
    time_t                 *end_time)
{
#define SILK_LOGSTATS_RWFILTER_ENVAR "SILK_LOGSTATS_RWFILTER"
#define SILK_LOGSTATS_ENVAR "SILK_LOGSTATS"
#define SILK_LOGSTATS_VERSION "v0001"
#define SILK_LOGSTATS_DEBUG SILK_LOGSTATS_ENVAR "_DEBUG"
#define NUM_STATS 5
    struct stat st;
    char *cmd_name;
    char param[NUM_STATS][21];
    char **log_argv;
    int debug = 0;
    int p = 0;
    int log_argc = 0;
    pid_t pid;
    int i;

    /* see whether to enable debugging */
    cmd_name = getenv(SILK_LOGSTATS_DEBUG);
    if (cmd_name != NULL && cmd_name[0] != '\0') {
        debug = 1;
    }

    cmd_name = getenv(SILK_LOGSTATS_RWFILTER_ENVAR);
    if (cmd_name == NULL) {
        cmd_name = getenv(SILK_LOGSTATS_ENVAR);
    }
    if (cmd_name == NULL || cmd_name[0] == '\0') {
        if (debug) {
            skAppPrintErr("LOGSTATS value empty or not found in environment");
        }
        return;
    }

    /* Verify that cmd_name represents a path, that it exists, that it
     * is a regular file, and that it is executable */
    if (strchr(cmd_name, '/') == NULL) {
        if (debug) {
            skAppPrintErr("LOGSTATS value does not contain slash '%s'",
                          cmd_name);
        }
        return;
    }
    if (stat(cmd_name, &st) == -1) {
        if (debug) {
            skAppPrintSyserror("LOGSTATS value has no status '%s'", cmd_name);
        }
        return;
    }
    if (S_ISREG(st.st_mode) == 0) {
        if (debug) {
            skAppPrintErr("LOGSTATS value is not a file '%s'", cmd_name);
        }
        return;
    }
    if (access(cmd_name, X_OK) != 0) {
        if (debug) {
            skAppPrintSyserror("LOGSTATS value is not executable '%s'",
                               cmd_name);
        }
        return;
    }

    /* Parent (first rwfilter) program forks */
    pid = fork();
    if (pid == -1) {
        return;
    }
    if (pid != 0) {
        /* Parent reaps Child 1 */
        waitpid(pid, NULL, 0);
        return;
    }

    /* only Child 1 makes it here; Child 1 forks again and immediately
     * exits so that the waiting rwfilter Parent above can continue */
    pid = fork();
    if (pid == -1) {
        _exit(EXIT_FAILURE);
    }
    if (pid != 0) {
        _exit(EXIT_SUCCESS);
    }

    /* only Child 2 makes it here.  it now prepares the command line
     * for the log-command. */

    /* magic 4 is log-command-name, app-name, log-version, final NULL */
    log_argv = (char**)calloc((4 + NUM_STATS + pargc), sizeof(char*));
    if (log_argv == NULL) {
        return;
    }

    /* start building the command for the tool */
    log_argv[log_argc++] = cmd_name;
    log_argv[log_argc++] = (char*)"rwfilter";
    log_argv[log_argc++] = (char*)SILK_LOGSTATS_VERSION;

    /* start-time */
    snprintf(param[p], sizeof(param[p]), ("%" PRId64), (int64_t)*start_time);
    ++p;
    /* end-time */
    snprintf(param[p], sizeof(param[p]), ("%" PRId64), (int64_t)*end_time);
    ++p;
    /* files processed */
    snprintf(param[p], sizeof(param[p]), ("%" PRIu32), stats->files);
    ++p;
    /* records read */
    snprintf(param[p], sizeof(param[p]), ("%" PRIu64), stats->read.flows);
    ++p;
    /* records written */
    snprintf(param[p], sizeof(param[p]), ("%" PRIu64),
             ((dest_type[DEST_ALL].count * stats->read.flows)
              + (dest_type[DEST_PASS].count * stats->pass.flows)
              + (dest_type[DEST_FAIL].count
                 * (stats->read.flows - stats->pass.flows))));
    ++p;
    assert(NUM_STATS == p);

    for (i = 0; i < p; ++i) {
        log_argv[log_argc++] = param[i];
    }
    for (i = 0; i < pargc; ++i) {
        log_argv[log_argc++] = pargv[i];
    }
    log_argv[log_argc] = (char*)NULL;

    if (debug) {
        /* for debugging: print command to stderr */
        fprintf(stderr, "%s: LOGSTATS preparing to exec: \"%s\", \"%s",
                skAppName(), cmd_name, log_argv[0]);
        for (i = 1; log_argv[i]; ++i) {
            fprintf(stderr, " %s", log_argv[i]);
        }
        fprintf(stderr, "\"\n");
    }

    execv(cmd_name, log_argv);
    skAppPrintSyserror("Unable to exec '%s'", cmd_name);
    exit(EXIT_FAILURE);
}


/* Write the statistics in 'sum' to the 'buf' */
static void
printStats(
    skstream_t             *stream,
    const filter_stats_t   *stats)
{
    /* check input */
    if (stream == NULL || stats == NULL) {
        return;
    }

    /* detailed or simple statistics? */
    if (print_volume_stats) {
        /* detailed */
        skStreamPrint(stream,
                      ("%5s|%18s|%18s|%20s|%10s|\n"
                       "%5s|%18" PRIu64 "|%18" PRIu64 "|%20" PRIu64 "|%10u|\n"
                       "%5s|%18" PRIu64 "|%18" PRIu64 "|%20" PRIu64 "|%10s|\n"
                       "%5s|%18" PRIu64 "|%18" PRIu64 "|%20" PRIu64 "|%10s|\n"),
                      /* titles */ "", "Recs", "Packets", "Bytes", "Files",

                      "Total",
                      stats->read.flows,
                      stats->read.pkts,
                      stats->read.bytes,
                      (unsigned int)stats->files,

                      "Pass",
                      stats->pass.flows,
                      stats->pass.pkts,
                      stats->pass.bytes,
                      "",

                      "Fail",
                      (stats->read.flows - stats->pass.flows),
                      (stats->read.pkts  - stats->pass.pkts),
                      (stats->read.bytes - stats->pass.bytes),
                      "");
    } else {
        /* simple */
        skStreamPrint(stream,
                      ("Files %5" PRIu32 ".  Read %10" PRIu64 "."
                       "  Pass %10" PRIu64 ". Fail  %10" PRIu64 ".\n"),
                      stats->files, stats->read.flows, stats->pass.flows,
                      (stats->read.flows - stats->pass.flows));
    }
}


/*
 *  closeAllDests();
 *
 *    Close all the output destinations.  Return 0 if they all closed
 *    cleanly, or non-zero if there was an error closing any stream.
 */
int
closeAllDests(
    void)
{
    destination_t *dest;
    destination_t *next_dest;
    int i;
    int rv = 0;
    int io_rv;

    /* close all the output files (pass, fail, all-dest) */
    for (i = 0; i < DESTINATION_TYPES; ++i) {
        for (dest = dest_type[i].dest_list; dest != NULL; dest = next_dest) {
            io_rv = skStreamClose(dest->stream);
            switch (io_rv) {
              case SKSTREAM_OK:
              case SKSTREAM_ERR_NOT_OPEN:
              case SKSTREAM_ERR_CLOSED:
                break;
              default:
                rv |= io_rv;
                if (io_rv) {
                    skStreamPrintLastErr(dest->stream, io_rv, &skAppPrintErr);
                }
            }
            rv |= skStreamDestroy(&dest->stream);
            next_dest = dest->next;
            free(dest);
        }
        dest_type[i].dest_list = NULL;
    }

    return rv;
}


/*
 *  num_output_streams = closeOutputDests(dest_id, quietly);
 *
 *    Close all streams for the specified destination type 'dest_id'
 *    (DEST_PASS, DEST_FAIL, DEST_ALL).
 *
 *    If 'quietly' is non-zero, do not report errors in closing the
 *    file(s).
 *
 *    Return the number of output streams across all destination types
 *    that are still open.
 */
int
closeOutputDests(
    const int           dest_id,
    int                 quietly)
{
    destination_t *next_dest = NULL;
    destination_t *dest;
    int i;
    int rv;
    int num_outs;

    for (dest = dest_type[dest_id].dest_list; dest != NULL; dest = next_dest) {
        next_dest = dest->next;
        rv = skStreamClose(dest->stream);
        if (rv && !quietly) {
            skStreamPrintLastErr(dest->stream, rv, &skAppPrintErr);
        }
        skStreamDestroy(&dest->stream);
        free(dest);
    }
    dest_type[dest_id].dest_list = NULL;
    dest_type[dest_id].count = 0;

    /* compute and return the number of output streams that we now
     * have. */
    num_outs = 0;
    for (i = 0; i < DESTINATION_TYPES; ++i) {
        num_outs += dest_type[i].count;
    }
    return num_outs;
}


/*
 *  num_output_streams = closeOneOutput(dest_id, dest);
 *
 *    Quietly close the output stream at 'dest' and free its memory.
 *    'dest_id' specifies which dest_type[] linked list contains
 *    'dest', since 'dest' must be removed from that list.  Return the
 *    new number of output streams.
 */
int
closeOneOutput(
    const int           dest_id,
    destination_t      *dest)
{
    destination_t **dest_prev;
    int num_outs;
    int i;

    /* unwire 'dest' */
    dest_prev = &dest_type[dest_id].dest_list;
    while (*dest_prev != dest) {
        dest_prev = &((*dest_prev)->next);
    }
    *dest_prev = dest->next;

    /* destroy 'dest' */
    skStreamDestroy(&dest->stream);
    free(dest);
    --dest_type[dest_id].count;

    /* compute and return the number of output streams that we now
     * have. */
    num_outs = 0;
    for (i = 0; i < DESTINATION_TYPES; ++i) {
        num_outs += dest_type[i].count;
    }
    return num_outs;
}


/*
 *  status = writeBuffer(thread, dest_id);
 *
 *    Write record buffer on 'thread' that is indexed by 'dest_id'
 *    (PASS, FAIL, ALL) to the output stream(s) for the destination
 *    type.  Return SKSTREAM_OK on success, or non-zero on error.
 *
 *    If rwfilter is running with multiple threads, the caller must
 *    use writerBufferThreaded(), in rwfilterthread.c.
 */
static int
writeBuffer(
    filter_thread_t    *thread,
    int                 dest_id)
{
    recbuf_t *recbuf;
    destination_t *dest;
    destination_t *dest_next;
    const rwRec *recbuf_pos;
    const rwRec *end_rec;
    int rv = SKSTREAM_OK;

    assert(1 == thread_count);

    recbuf = &thread->recbuf[dest_id];

    /* the list of destinations to write the records to */
    dest = dest_type[dest_id].dest_list;
    if (dest == NULL) {
        assert(dest_type[dest_id].count == 0);
        return rv;
    }
    /* find location of our stopping condition */
    end_rec = recbuf->buf + recbuf->count;

    do {
        dest_next = dest->next;
        for (recbuf_pos = recbuf->buf; recbuf_pos < end_rec; ++recbuf_pos) {
            rv = skStreamWriteRecord(dest->stream, recbuf_pos);
            if (SKSTREAM_ERROR_IS_FATAL(rv)) {
                if (skStreamGetLastErrno(dest->stream) == EPIPE) {
                    /* close this stream */
                    reading_records = closeOneOutput(dest_id, dest);
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

    rv = SKSTREAM_OK;

    /* adjust the max_count member of 'recbuf' if filling 'recbuf'
     * would cause us to exceed --max-pass or --max-fail */
    if (dest_type[dest_id].max_records) {
        filter_stats_t *stats = &thread->stats;

        if (DEST_PASS == dest_id) {
            if (dest_type[DEST_PASS].max_records
                < (stats->pass.flows + recbuf->max_count))
            {
                assert(dest_type[DEST_PASS].max_records >= stats->pass.flows);
                recbuf->max_count =
                    dest_type[DEST_PASS].max_records - stats->pass.flows;
                if (0 == recbuf->max_count) {
                    /* close all streams for this destination type
                     * since we are at user's specified max. */
                    reading_records = closeOutputDests(DEST_PASS, 0);
                }
            }
        } else if (DEST_FAIL == dest_id) {
            const uint32_t fail_flows = stats->read.flows - stats->pass.flows;
            if (dest_type[DEST_FAIL].max_records
                < (fail_flows + recbuf->max_count))
            {
                assert(dest_type[DEST_FAIL].max_records >= fail_flows);
                recbuf->max_count
                    = dest_type[DEST_FAIL].max_records - fail_flows;
                if (0 == recbuf->max_count) {
                    reading_records = closeOutputDests(DEST_FAIL, 0);
                }
            }
        } else {
            assert(DEST_ALL == dest_id);
            /* this should never be set */
            skAbortBadCase(dest_id);
        }
    }

  END:
    recbuf->count = 0;
    return rv;
}


/*
 *    Copy the record on 'thread' to the destination buffer on
 *    'thread' whose index is 'dest_id'.  If the buffer if full after
 *    copying the record, call the appropriate function (writeBuffer()
 *    or writeBufferThreaded()) to write the buffer to the output
 *    stream.  Return 0 if buffer is not full or the result of calling
 *    the write function.
 *
 *    This is a helper for filterFile().
 */
static int
copyRecordToDest(
    filter_thread_t    *thread,
    int                 dest_id)
{
    recbuf_t *recbuf = &thread->recbuf[dest_id];

    assert(recbuf);
    assert(recbuf->count < recbuf->max_count);
    assert(recbuf->max_count <= RECBUF_MAX_RECS);

    rwRecCopy(&recbuf->buf[ recbuf->count ], &thread->rwrec,
              SK_RWREC_COPY_MOVE);
    ++recbuf->count;
    if (recbuf->count < recbuf->max_count) {
        return 0;
    }
#if SK_RWFILTER_THREADED
    if (thread_count > 1) {
        return writeBufferThreaded(thread, dest_id);
    }
#endif  /* SK_RWFILTER_THREADED */
    return writeBuffer(thread, dest_id);
}


/*
 *  ok = filterFile(in_stream, ipfile_basename, thread);
 *
 *    Read each record from the file 'in_stream' and copy it to the
 *    approprate destination buffers on 'thread'.
 *
 *    This function is used by both threaded and non-threaded code.
 *
 *    The 'ipfile_basename' parameter is passed to filterCheckFile();
 *    it should be NULL or contain the full-path (minus extension) of
 *    the file that contains Bloom filter or IPset information about
 *    the 'in_stream'.
 *
 *    The function returns 0 on success; or 1 if the input file could
 *    not be opened.
 */
int
filterFile(
    skstream_t         *in_stream,
    const char         *ipfile_basename,
    filter_thread_t    *thread)
{
    int i;
    int result;
    int rv = SKSTREAM_OK;
    int in_rv = SKSTREAM_OK;

    if (!reading_records) {
        return 0;
    }
    ++thread->stats.files;

    /* determine whether --all-dest is the only output and no
     * statistics are requested */
    if (NULL == print_stat
        && 0 == dest_type[DEST_PASS].count
        && 0 == dest_type[DEST_FAIL].count)
    {
        /* the only output is --all=stream */
        assert(dest_type[DEST_ALL].count);
        assert(0 == checker_count);
        while (reading_records
               && ((in_rv = skStreamReadRecord(in_stream, &thread->rwrec))
                   == SKSTREAM_OK))
        {
            INCR_REC_COUNT(thread->stats.read, &thread->rwrec);
            rv = copyRecordToDest(thread, DEST_ALL);
        }
        goto END;
    }

    /* determine whether all of the records in the input stream fail
     * the checks */
    if (filterCheckFile(in_stream, ipfile_basename) == 1) {
        /* all records in this file fail the test */
        if (dest_type[DEST_ALL].count || dest_type[DEST_FAIL].count) {
            /* at least one of all-dest or fail-dest is specified */
            while (reading_records
                   && ((in_rv = skStreamReadRecord(in_stream, &thread->rwrec))
                       == SKSTREAM_OK))
            {
                INCR_REC_COUNT(thread->stats.read, &thread->rwrec);
                if (dest_type[DEST_ALL].count) {
                    rv = copyRecordToDest(thread, DEST_ALL);
                }
                if (dest_type[DEST_FAIL].count) {
                    rv = copyRecordToDest(thread, DEST_FAIL);
                }
            }
        } else if (NULL == print_stat) {
            /* not writing the record or generating statistics */
        } else if (print_volume_stats) {
            /* computing volume stats, read each record to get its
             * byte and packet counts. */
            while (reading_records
                   && ((in_rv = skStreamReadRecord(in_stream, &thread->rwrec))
                       == SKSTREAM_OK))
            {
                INCR_REC_COUNT(thread->stats.read, &thread->rwrec);
            }
        } else {
            /* all we need to do is to count the records in the file,
             * which we can do by skipping them all */
            size_t skipped = 0;
            in_rv = skStreamSkipRecords(in_stream, SIZE_MAX, &skipped);
            thread->stats.read.flows += skipped;
        }
        goto END;
    }

    /* determine whether only statistics were requested or whether
     * --pass-dest is the only output */
    if (0 == dest_type[DEST_FAIL].count
        && 0 == dest_type[DEST_ALL].count)
    {
        if (0 == dest_type[DEST_PASS].count) {
            while (reading_records
               && ((in_rv = skStreamReadRecord(in_stream, &thread->rwrec))
                   == SKSTREAM_OK))
            {
                INCR_REC_COUNT(thread->stats.read, &thread->rwrec);
                /* run all checker() functions, stopping if one fails */
                for (i = 0, result = RWF_PASS;
                     i < checker_count && result == RWF_PASS;
                     ++i)
                {
                    result = (*(checker[i]))(&thread->rwrec);
                }
                if (RWF_PASS == result || RWF_PASS_NOW == result) {
                    INCR_REC_COUNT(thread->stats.pass, &thread->rwrec);
                }
            }
        } else {
            while (reading_records
               && ((in_rv = skStreamReadRecord(in_stream, &thread->rwrec))
                   == SKSTREAM_OK))
            {
                INCR_REC_COUNT(thread->stats.read, &thread->rwrec);
                for (i = 0, result = RWF_PASS;
                     i < checker_count && result == RWF_PASS;
                     ++i)
                {
                    result = (*(checker[i]))(&thread->rwrec);
                }
                if (RWF_PASS == result || RWF_PASS_NOW == result) {
                    INCR_REC_COUNT(thread->stats.pass, &thread->rwrec);
                    rv = copyRecordToDest(thread, DEST_PASS);
                }
            }
        }
        goto END;
    }

    /* read and process each record */
    while (reading_records
               && ((in_rv = skStreamReadRecord(in_stream, &thread->rwrec))
           == SKSTREAM_OK))
    {
        INCR_REC_COUNT(thread->stats.read, &thread->rwrec);
        /* run all checker() functions, stopping if one fails */
        for (i = 0, result = RWF_PASS;
             i < checker_count && result == RWF_PASS;
             ++i)
        {
            result = (*(checker[i]))(&thread->rwrec);
        }

        if (dest_type[DEST_ALL].count) {
            rv = copyRecordToDest(thread, DEST_ALL);
        }
        switch (result) {
          case RWF_PASS:
          case RWF_PASS_NOW:
            INCR_REC_COUNT(thread->stats.pass, &thread->rwrec);
            if (dest_type[DEST_PASS].count) {
                rv = copyRecordToDest(thread, DEST_PASS);
            }
            break;
          case RWF_FAIL:
            if (dest_type[DEST_FAIL].count) {
                rv = copyRecordToDest(thread, DEST_FAIL);
            }
            break;
          default:
            break;
        }
    }

  END:
    if (in_rv == SKSTREAM_OK || in_rv == SKSTREAM_ERR_EOF) {
        in_rv = 0;
    } else {
        skStreamPrintLastErr(in_stream, in_rv, &skAppPrintErr);
        in_rv = 1;
    }
    if (rv) {
        return -1;
    }
    return in_rv;
}


/*
 *  status = nonthreadedFilter(&stats);
 *
 *    The "main" to use when rwfilter is using a single thread.
 *
 *    Creates necessary data structures and then processes input
 *    files.  Returns 0 on success, non-zero on error.
 */
static int
nonthreadedFilter(
    filter_stats_t     *stats)
{
    filter_thread_t self;
    skstream_t *stream = NULL;
    int i;
    int rv_file;
    int rv = 0;

    filterIgnoreSigPipe();

    memset(&self, 0, sizeof(self));

    self.lua_state = L;
    rwRecInitialize(&self.rwrec, self.lua_state);

    /* create a buffer for each destination type */
    for (i = 0; i < DESTINATION_TYPES; ++i) {
        if (dest_type[i].count) {
            self.recbuf[i].buf
                = (rwRec*)malloc(RECBUF_MAX_RECS * sizeof(rwRec));
            if (self.recbuf[i].buf == NULL) {
                goto END;
            }
            rwRecInitializeArray(
                self.recbuf[i].buf, self.lua_state, RECBUF_MAX_RECS);
            if (dest_type[i].max_records
                && dest_type[i].max_records < RECBUF_MAX_RECS)
            {
                self.recbuf[i].max_count = dest_type[i].max_records;
            } else {
                self.recbuf[i].max_count = RECBUF_MAX_RECS;
            }
        }
    }

    while ((rv = sk_flow_iter_get_next_stream(flowiter, &stream))
           != SKSTREAM_ERR_EOF)
    {
        if (rv != SKSTREAM_OK) {
            continue;
        }
        rv_file = filterFile(stream, NULL, &self);
        sk_flow_iter_close_stream(flowiter, stream);
        if (rv_file < 0) {
            /* fatal */
            *stats = self.stats;
            return EXIT_FAILURE;
        }
        /* if (rv_file > 0) there was an error opening/reading
         * input: ignore */
    }
    assert(rv = SKSTREAM_ERR_EOF);
    rv = SKSTREAM_OK;

    /* write any records still in the buffers */
    for (i = 0; i < DESTINATION_TYPES; ++i) {
        if (self.recbuf[i].count) {
            writeBuffer(&self, i);
        }
    }

  END:
    *stats = self.stats;
    for (i = 0; i < DESTINATION_TYPES; ++i) {
        free(self.recbuf[i].buf);
    }
    return rv;
}


#if 0
char *
appNextInput(
    char               *buf,
    size_t              bufsize)
{
    if (!reading_records) {
        return NULL;
    }

    if (0 == skOptionsCtxNextArgument(optctx, buf, bufsize)) {
        FILE *filename_fp = skOptionsCtxGetPrintFilenames(optctx);
        if (filename_fp) {
            fprintf(filename_fp, "%s\n", buf);
        }
        return buf;
    }
    return NULL;

    /* Get the files.  Only one of these should be active */
    if (skFGlobValid(fglob)) {
        return skFGlobNext(fglob, buf, bufsize);
    } else if (xargs) {
        /* file names from stdin */
        if (first_call) {
            first_call = 0;
            /* open input */
            rv = skStreamOpen(xargs);
            if (rv) {
                skStreamPrintLastErr(xargs, rv, &skAppPrintErr);
                return NULL;
            }
        }
        /* read until end of file */
        while ((rv = skStreamGetLine(xargs, buf, bufsize, &lc))
               != SKSTREAM_ERR_EOF)
        {
            switch (rv) {
              case SKSTREAM_OK:
                /* good, we got our line */
                break;
              case SKSTREAM_ERR_LONG_LINE:
                /* bad: line was longer than sizeof(line) */
                skAppPrintErr("Input line %d too long---ignored", lc);
                continue;
              default:
                /* unexpected error */
                skStreamPrintLastErr(xargs, rv, &skAppPrintErr);
                return NULL;
            }
            return buf;
        }
    } else {
        /* file names from the command line */
        if (first_call) {
            first_call = 0;
            i = arg_index;
        } else {
            ++i;
        }
        if (i < pargc) {
            strncpy(buf, pargv[i], bufsize);
            buf[bufsize-1] = '\0';
            return buf;
        }
    }

    return NULL;
}
#endif  /* 0 */


static int
filterProcessFileHeaders(
    int                 argc,
    char              **argv)
{
    sk_flow_iter_hdr_iter_t *hdr_iter;
    sk_file_header_t *hdr;
    sk_sidecar_iter_t sc_iter;
    const sk_sidecar_elem_t *sc_elem;
    sk_sidecar_elem_t *new_elem;
    sk_sidecar_t *hdr_sidecar;
    sk_sidecar_t *sidecar;
    sk_file_header_t *out_hdr;
    destination_t *dest;
    int rv;
    int i;

    /* create the sidecar to use for writing */
    sk_sidecar_create(&sidecar);

    /* process the header of every input file */
    if (sk_flow_iter_read_silk_headers(flowiter, &hdr_iter)) {
        sk_sidecar_destroy(&sidecar);
        return -1;
    }
    while ((hdr = sk_flow_iter_hdr_iter_next(hdr_iter)) != NULL) {
        /* copy annotations and invocation to the each destination
         * file */
        for (i = 0; i < DESTINATION_TYPES; ++i) {
            for (dest = dest_type[i].dest_list; dest !=NULL; dest = dest->next)
            {
                out_hdr = skStreamGetSilkHeader(dest->stream);

                skHeaderCopyEntries(out_hdr, hdr, SK_HENTRY_INVOCATION_ID);
                skHeaderCopyEntries(out_hdr, hdr, SK_HENTRY_ANNOTATION_ID);
            }
        }

        /* add sidecar fields to the global sidecar */
        hdr_sidecar = sk_sidecar_create_from_header(hdr, &rv);
        if (NULL == hdr_sidecar) {
            if (rv) {
                sk_sidecar_destroy(&sidecar);
                return -1;
            }
        } else {
            sk_sidecar_iter_bind(hdr_sidecar, &sc_iter);
            while (sk_sidecar_iter_next(&sc_iter, &sc_elem)
                   == SK_ITERATOR_OK)
            {
                rv = sk_sidecar_add_elem(sidecar, sc_elem, &new_elem);
                if (SK_SIDECAR_E_DUPLICATE == rv) {
                    /* FIXME: ignore for now */
                } else if (rv) {
                    /* FIXME: ignore for now */
                    /* skAppPrintErr( */
                    /*     "Cannot add field '%s' from sidecar: %d", */
                    /*     buf, rv); */
                } else {
                }
            }
            sk_sidecar_destroy(&hdr_sidecar);
        }
    }
    sk_flow_iter_hdr_iter_destroy(&hdr_iter);

    /* destroy the sidecar if there are no elements */
    if (0 == sk_sidecar_count_elements(sidecar)) {
        sk_sidecar_destroy(&sidecar);
    }

    /* add invocation, any --note arguments, and the sidecar object to
     * all destinations */
    for (i = 0; i < DESTINATION_TYPES; ++i) {
        for (dest = dest_type[i].dest_list; dest != NULL; dest = dest->next) {
            out_hdr = skStreamGetSilkHeader(dest->stream);
            if ((rv = skHeaderAddInvocation(out_hdr, 1, argc, argv))
                || (rv = skOptionsNotesAddToStream(dest->stream)))
            {
                skStreamPrintLastErr(dest->stream, rv, &skAppPrintErr);
                sk_sidecar_destroy(&sidecar);
                return rv;
            }
            if (sidecar && (rv = skStreamSetSidecar(dest->stream, sidecar))) {
                skStreamPrintLastErr(dest->stream, rv, &skAppPrintErr);
                sk_sidecar_destroy(&sidecar);
                return rv;
            }
            rv = skStreamWriteSilkHeader(dest->stream);
            if (rv) {
                skStreamPrintLastErr(dest->stream, rv, &skAppPrintErr);
                sk_sidecar_destroy(&sidecar);
                return rv;
            }
        }
    }

    skOptionsNotesTeardown();
    sk_sidecar_destroy(&sidecar);

    return SKSTREAM_OK;
}



int main(int argc, char **argv)
{
    SILK_FEATURES_DEFINE_STRUCT(features);
    filter_stats_t stats;
    int rv = 0;
    time_t start_timer;
    time_t end_timer;

    /* register the application */
    skAppRegister(argv[0]);
    skAppVerifyFeatures(&features, NULL);

    /* initialize */
    memset(&stats, 0, sizeof(filter_stats_t));


    time(&start_timer);
    appSetup(argc, argv);

    pargc = argc;
    pargv = argv;

    /* nothing to do in dry-run mode but print the file names */
    if (dryrun_fp) {
#if SK_RWFILTER_THREADED
        if (thread_count > 1) {
            threadedFilter(&stats);
        } else
#endif  /* SK_RWFILTER_THREADED */
        {
            char flowfile[PATH_MAX];

            while (skOptionsCtxNextArgument(optctx, flowfile, sizeof(flowfile))
                   == 0)
            {
                fprintf(dryrun_fp, "%s\n", flowfile);
            }
        }
        goto PRINT_STATS;
    }

    /* read the headers from all the input files; this is going to
     * slow down the multi-threaded code */
    if (filterProcessFileHeaders(argc, argv)) {
        return EXIT_FAILURE;
    }

#if SK_RWFILTER_THREADED
    if (thread_count > 1) {
        rv = threadedFilter(&stats);
    } else
#endif  /* SK_RWFILTER_THREADED */
    {
        rv = nonthreadedFilter(&stats);
    }

  PRINT_STATS:
    /* Print the statistics */
    if (print_stat && !dryrun_fp) {
        printStats(print_stat, &stats);
    }

    time(&end_timer);
    logStats(&stats, &start_timer, &end_timer);

    appTeardown();
    return ((rv == 0) ? EXIT_SUCCESS : EXIT_FAILURE);
}


/*
** Local Variables:
** mode:c
** indent-tabs-mode:nil
** c-basic-offset:4
** End:
*/
