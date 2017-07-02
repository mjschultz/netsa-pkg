/*
** Copyright (C) 2008-2017 by Carnegie Mellon University.
**
** @OPENSOURCE_LICENSE_START@
** See license information in ../../LICENSE.txt
** @OPENSOURCE_LICENSE_END@
*/

/*
**  rwflowpack-stream.c
**
**    Helper file for rwflowpack that implements the 'stream'
**    input-mode.
**
**    Specify the functions that are used to read NetFlow v5, IPFIX,
**    and NetFlow v9 records from a network socket and to poll a
**    directory for files containing NetFlow v5, IPFIX, NetFlow v9, or
**    SiLK flow records.
**
**    Any SiLK Flow records read by this input_mode_type will be
**    completely repacked.
**
*/


#include <silk/silk.h>

RCSIDENT("$SiLK: rwflowpack-stream.c efd886457770 2017-06-21 18:43:23Z mthomas $");

#include <silk/skpolldir.h>
#include "rwflowpack_priv.h"


/* LOCAL DEFINES AND TYPEDEFS */

/*
 *  Specify the maximum size (in terms of RECORDS) of the buffer used
 *  to hold records that have been read from the flow-source but not
 *  yet processed.  This value is the number of records as read from
 *  the wire (i.e., AMP-RECs) per PROBE.  The maximum memory per probe
 *  will be BUF_REC_COUNT * 1464.  If records are processed as quickly
 *  as they are read, the normal memory use per probe will be
 *  CIRCBUF_CHUNK_MAX_SIZE bytes.
 */
#define BUF_REC_COUNT 60000

/* A name for this input_mode_type. */
#define INPUT_MODE_TYPE_NAME  "Stream Input Mode"


/* LOCAL VARIABLES */

/* A vector containing the 'skpc_probe_t' structures used by this
 * input-mode-type. */
static struct input_probes_st {
    sk_vector_t        *vec;
    size_t              count;
} input_probes = {NULL, 0};

typedef size_t input_probe_vec_iter_t;

#define input_probe_vec_create()                                        \
    do {                                                                \
        input_probes.count = 0;                                         \
        input_probes.vec = sk_vector_create(sizeof(skpc_probe_t *));    \
        if (NULL == input_probes.vec) {                                 \
            skAppPrintOutOfMemory(NULL);                                \
            exit(EXIT_FAILURE);                                         \
        }                                                               \
    } while (0)

#define input_probe_vec_destroy()               \
    do {                                        \
        sk_vector_destroy(input_probes.vec);    \
        input_probes.vec = NULL;                \
        input_probes.count = 0;                 \
    } while (0)

#define input_probe_vec_add(m_probe)                                    \
    if (0 == sk_vector_append_value(input_probes.vec, &(m_probe))) {     \
        ++input_probes.count;                                           \
    } else {                                                            \
        skAppPrintOutOfMemory(NULL);                                    \
        exit(EXIT_FAILURE);                                             \
    }

#define input_probe_vec_get_count()             \
    (input_probes.count)

#define input_probe_vec_iter_init(m_iter)  (0)

#define input_probe_vec_iter_next(m_iter)                               \
    (((m_iter) >= input_probes.count)                                   \
     ? NULL :                                                           \
     (*((skpc_probe_t **)                                               \
        sk_vector_get_value_pointer(input_probes.vec, (m_iter)++))))


static int
sk_conv_silk_stream(
    skpc_probe_t       *probe,
    skstream_t         *stream);


/* FUNCTION DEFINITIONS */


/*
 *  ******************************************************************
 *
 *  Support for reading STREAMs from a directory
 *
 *  ******************************************************************
 */

/**
 *    THREAD ENTRY POINT
 *
 *    The sk_coll_directory_thread() function is the thread handling
 *    files as they appear in a directory that is being polled.
 *
 *    This thread is started from the sk_coll_start() function, and
 *    its location is stored in the 'thread' member of the
 *    sk_coll_directory_t structure.
 */
static void*
sk_coll_directory_thread(
    void               *v_probe)
{
    skpc_probe_t *probe = (skpc_probe_t *)v_probe;
    sk_coll_directory_t *coll;
    char path[PATH_MAX];
    char *filename;
    skstream_t *stream;
    skPollDirErr_t pderr;
    int running;
    int pack_err;
    ssize_t rv;

    assert(probe);
    assert(SKPROBE_COLL_DIRECTORY == probe->coll_type);

    coll = probe->coll.directory;

    pthread_mutex_lock(&coll->t.mutex);
    if (STARTING != coll->t.status) {
        goto END;
    }
    pderr = skPollDirStart(coll->polldir);
    if (pderr) {
        ERRMSG("'%s': Error! Could not initiate directory poller on '%s': %s",
               skpcProbeGetName(probe), skPollDirGetDir(coll->polldir),
               ((PDERR_SYSTEM == pderr)
                ? strerror(errno)
                : skPollDirStrError(pderr)));
        goto END;
    }
    coll->t.status = STARTED;
    pthread_cond_signal(&coll->t.cond);
    pthread_mutex_unlock(&coll->t.mutex);

    for (;;) {
        /* probably not necessary to lock the mutex while cheching
         * this variable, but since this only happens once per file,
         * it is not a huge amount of overhead */
        pthread_mutex_lock(&coll->t.mutex);
        if (STARTED != coll->t.status) {
            goto END;
        }
        pthread_mutex_unlock(&coll->t.mutex);

        /* Get next file from the directory poller */
        pderr = skPollDirGetNextFile(coll->polldir, path, &filename);
        if (PDERR_NONE == pderr) {
            /* no error; we were told to stop while waiting for a file
             * to appear? */
            pthread_mutex_lock(&coll->t.mutex);
            running = (STARTED == coll->t.status);
            pthread_mutex_unlock(&coll->t.mutex);
        } else {
            if (PDERR_STOPPED != pderr) {
                CRITMSG("'%s': Error polling directory '%s': %s",
                        skpcProbeGetName(probe),
                        skPollDirGetDir(coll->polldir),
                        ((PDERR_SYSTEM == pderr)
                         ? strerror(errno)
                         : skPollDirStrError(pderr)));
            }
            running = 0;
        }
        if (!running) {
            break;
        }

        /* Get a file handle. Check return status once we have the
         * handle in case we started shutting down while waiting for a
         * handle. */
        if (flowpackAcquireFileHandle()) {
            break;
        }
        pthread_mutex_lock(&coll->t.mutex);
        running = (STARTED == coll->t.status);
        pthread_mutex_unlock(&coll->t.mutex);
        if (!running) {
            flowpackReleaseFileHandle();
            break;
        }

        INFOMSG("'%s': Processing file '%s'",
                skpcProbeGetName(probe), filename);

        /* Open the source */
        if ((rv = skStreamCreate(&stream, SK_IO_READ, coll->content_type))
            || (rv = skStreamBind(stream, path))
            || (rv = skStreamOpen(stream)))
        {
            skStreamPrintLastErr(stream, rv, &ERRMSG);
            skStreamDestroy(&stream);
            flowpackReleaseFileHandle();
            skpcProbeDisposeIncomingFile(probe, path, 1);
            continue;
        }

        /* Read and pack the records */
        switch (skpcProbeGetType(probe)) {
          case PROBE_ENUM_IPFIX:
            pack_err = sk_conv_ipfix_stream(probe, stream);
            break;
          case PROBE_ENUM_NETFLOW_V5:
            pack_err = sk_conv_pdu_stream(probe, stream);
            break;
          case PROBE_ENUM_SILK:
            pack_err = sk_conv_silk_stream(probe, stream);
            break;
          default:
            skAbortBadCase(skpcProbeGetType(probe));
        }

        skStreamClose(stream);
        flowpackReleaseFileHandle();
        skpcProbeDisposeIncomingFile(probe, path, pack_err);
        skStreamDestroy(&stream);
    }

    pthread_mutex_lock(&coll->t.mutex);

  END:
    coll->t.status = STOPPED;
    pthread_cond_broadcast(&coll->t.cond);
    pthread_mutex_unlock(&coll->t.mutex);

    DEBUGMSG("'%s': Stopping reader thread...",
             skpcProbeGetName(probe));

    /* thread is ending, decrement the count and tell the main thread
     * to check the thread count */
    decrement_thread_count(1);

    return NULL;
}


static int
sk_conv_silk_stream(
    skpc_probe_t       *probe,
    skstream_t         *stream)
{
    rwRec rec;
    int pack_err;
    ssize_t rv;

    assert(probe);
    assert(stream);
    assert(PROBE_ENUM_SILK == skpcProbeGetType(probe));
    assert(skpcProbeGetFileSource(probe)
           || skpcProbeGetPollDirectory(probe));

    rwRecInitialize(&rec, NULL);

    /* Read and pack the records */
    pack_err = 0;
    while ((rv = skStreamReadRecord(stream, &rec)) == SKSTREAM_OK) {
        if (skpcProbePackRecord(probe, &rec, NULL) == -1) {
            pack_err = 1;
            break;
        }
    }
    if (!pack_err && SKSTREAM_ERR_EOF != rv) {
        skStreamPrintLastErr(stream, rv, NOTICEMSG);
    }
    rwRecReset(&rec);

    if (skStreamGetRecordCount(stream) == 0
        && SKSTREAM_ERR_EOF != rv)
    {
        /* put file into error directory */
        pack_err = 1;
    } else {
        INFOMSG("'%s': Processed file '%s': Recs %" PRIu64,
                skpcProbeGetName(probe), skStreamGetPathname(stream),
                skStreamGetRecordCount(stream));
    }

    return pack_err;
}


/*
 *  ******************************************************************
 *
 *  Support for reading a single file
 *
 *  ******************************************************************
 */

/**
 *    THREAD ENTRY POINT
 *
 *    The sk_coll_file_thread() function is the thread for reading data
 *    from a single STREAM file.
 *
 *    This thread is started from the sk_coll_start() function.
 */
static void*
sk_coll_file_thread(
    void               *v_probe)
{
    skpc_probe_t *probe = (skpc_probe_t *)v_probe;
    sk_coll_file_t *coll;
    int pack_err;

    assert(probe);
    assert(SKPROBE_COLL_FILE == probe->coll_type);

    coll = probe->coll.file;

    pthread_mutex_lock(&coll->t.mutex);
    if (STARTING != coll->t.status) {
        skStreamClose(coll->stream);
        goto END;
    }
    coll->t.status = STARTED;
    pthread_cond_signal(&coll->t.cond);
    pthread_mutex_unlock(&coll->t.mutex);

    switch (skpcProbeGetType(probe)) {
      case PROBE_ENUM_IPFIX:
        pack_err = sk_conv_ipfix_stream(probe, probe->coll.file->stream);
        break;
      case PROBE_ENUM_NETFLOW_V5:
        pack_err = sk_conv_pdu_stream(probe, probe->coll.file->stream);
        break;
      case PROBE_ENUM_SILK:
        pack_err = sk_conv_silk_stream(probe, probe->coll.file->stream);
        break;
      default:
        skAbortBadCase(skpcProbeGetType(probe));
    }

#if 0
    pack_err = 0;
    while ((rv = skStreamReadRecord(coll->stream, &rec))
           == SKSTREAM_OK)
    {
        if (skpcProbePackRecord(probe, rec) == -1) {
            pack_err = 1;
        }
    }
    if (!pack_err && SKSTREAM_ERR_EOF != rv) {
        skStreamPrintLastErr(coll->stream, rv, &WARNINGMSG);
    }
    skStreamClose(coll->stream);
    if (skStreamGetRecordCount(coll->stream) == 0
        && SKSTREAM_ERR_EOF != rv)
    {
        /* put file into error directory */
        pack_err = 1;
    } else {
        INFOMSG("'%s': Processed file '%s': Recs %" PRIu64,
                skpcProbeGetName(probe),
                skStreamGetPathname(coll->stream),
                skStreamGetRecordCount(coll->stream));
    }
#endif  /* 0 */

    skpcProbeDisposeIncomingFile(
        probe, skStreamGetPathname(coll->stream), pack_err);

  END:
    pthread_mutex_lock(&coll->t.mutex);
    coll->t.status = STOPPED;
    pthread_cond_broadcast(&coll->t.cond);
    pthread_mutex_unlock(&coll->t.mutex);
    decrement_thread_count(1);
    return NULL;
}


/*
 *  ******************************************************************
 *
 *  Support for reading STREAMs from a directory
 *
 *  ******************************************************************
 */


static void
sk_coll_stop_helper(
    skpc_probe_t       *probe)
{
    switch (probe->coll.any->t.status) {
      case UNKNONWN:
        skAbortBadCase(probe->coll.any->t.status);
      case CREATED:
        probe->coll.any->t.status = JOINED;
        return;
      case JOINED:
      case STOPPED:
        return;
      case STARTING:
      case STARTED:
        if (SKPROBE_COLL_DIRECTORY == probe->coll_type) {
            sk_coll_directory_t *coll = probe->coll.directory;
            skPollDirStop(coll->polldir);
        }
        probe->coll.any->t.status = STOPPING;
        /* FALLTHROUGH */
      case STOPPING:
        while (STOPPED != probe->coll.any->t.status) {
            pthread_cond_wait(
                &probe->coll.any->t.cond, &probe->coll.any->t.mutex);
        }
        break;
    }
}


void
sk_coll_stop(
    skpc_probe_t       *probe)
{
    assert(probe);
    assert(PROBE_ENUM_SILK == skpcProbeGetType(probe)
           || PROBE_ENUM_NETFLOW_V5 == skpcProbeGetType(probe)
           || PROBE_ENUM_IPFIX == skpcProbeGetType(probe));
    assert(skpcProbeGetFileSource(probe)
           || skpcProbeGetPollDirectory(probe));

    assert(probe->coll.any);

    pthread_mutex_lock(&probe->coll.any->t.mutex);
    sk_coll_stop_helper(probe);
    pthread_mutex_unlock(&probe->coll.any->t.mutex);
}


void
sk_coll_destroy(
    skpc_probe_t       *probe)
{
    assert(probe);
    assert(PROBE_ENUM_SILK == skpcProbeGetType(probe)
           || PROBE_ENUM_NETFLOW_V5 == skpcProbeGetType(probe)
           || PROBE_ENUM_IPFIX == skpcProbeGetType(probe));
    assert(skpcProbeGetFileSource(probe)
           || skpcProbeGetPollDirectory(probe));

    if (NULL == probe->coll.any) {
        return;
    }

    pthread_mutex_lock(&probe->coll.any->t.mutex);

    /* Make sure source is stopped */
    sk_coll_stop_helper(probe);

    if (probe->coll.any->t.status != JOINED) {
        pthread_join(probe->coll.any->t.thread, NULL);
    }
    if (skpcProbeGetPollDirectory(probe)) {
        sk_coll_directory_t *coll = probe->coll.directory;
        skPollDirDestroy(coll->polldir);
    } else {
        sk_coll_file_t *coll = probe->coll.file;
        ssize_t rv;

        rv = skStreamDestroy(&coll->stream);
        if (rv) {
            skStreamPrintLastErr(coll->stream, rv, ERRMSG);
        }
    }

    pthread_mutex_unlock(&probe->coll.any->t.mutex);
    pthread_mutex_destroy(&probe->coll.any->t.mutex);
    pthread_cond_destroy(&probe->coll.any->t.cond);
    free(probe->coll.any);
    probe->coll.any = NULL;
}


int
sk_coll_create(
    skpc_probe_t       *probe)
{
    skcontent_t content_type;
    const char *pathname;
    ssize_t rv;

    assert(probe);

    switch (skpcProbeGetType(probe)) {
      case PROBE_ENUM_SILK:
        content_type = SK_CONTENT_SILK_FLOW;
        break;
      case PROBE_ENUM_IPFIX:
        content_type = SK_CONTENT_OTHERBINARY;
        break;
      case PROBE_ENUM_NETFLOW_V5:
        content_type = SK_CONTENT_OTHERBINARY;
        break;
      default:
        skAbortBadCase(skpcProbeGetType(probe));
    }

    if (NULL != (pathname = skpcProbeGetPollDirectory(probe))) {
        /* Create the polldir object */
        sk_coll_directory_t *coll;
        coll = sk_alloc(sk_coll_directory_t);
        coll->content_type = content_type;
        coll->polldir = skPollDirCreate(pathname,
                                        skpcProbeGetPollInterval(probe));
        if (NULL == coll->polldir) {
            ERRMSG("'%s': Could not create directory poller on %s",
                   skpcProbeGetName(probe), pathname);
            free(coll);
            return -1;
        }
        probe->coll.directory = coll;

    } else if (NULL != (pathname = skpcProbeGetFileSource(probe))) {
        /* Create a stream to read the file */
        sk_coll_file_t *coll;
        coll = sk_alloc(sk_coll_file_t);
        coll->content_type = content_type;
        if ((rv = skStreamCreate(&coll->stream, SK_IO_READ, content_type))
            || (rv = skStreamBind(coll->stream, pathname)))
        {
            skStreamPrintLastErr(coll->stream, rv, &ERRMSG);
            skStreamDestroy(&coll->stream);
            free(coll);
            return -1;
        }
        probe->coll.file = coll;

    } else {
        skAppPrintErr("'%s': Expected a file source or a poll directory",
                      skpcProbeGetName(probe));
        skAbort();
    }

    pthread_cond_init(&probe->coll.any->t.cond, NULL);
    pthread_mutex_init(&probe->coll.any->t.mutex, NULL);

    probe->coll.any->t.status = CREATED;
    probe->coll.any->t.thread = pthread_self();

    return 0;
}


int
sk_coll_start(
    skpc_probe_t       *probe)
{
    char thread_name[2 * SK_MAX_STRLEN_SENSOR];
    void *(*thread_fn)(void *);
    ssize_t rv;

    assert(probe);

    pthread_mutex_lock(&probe->coll.any->t.mutex);

    if (skpcProbeGetPollDirectory(probe)) {
        thread_fn = sk_coll_directory_thread;
        snprintf(thread_name, sizeof(thread_name), "%s-%s",
                 skpcProbeGetName(probe), "sk_coll_directory_thread");

    } else if (skpcProbeGetFileSource(probe)) {
        sk_coll_file_t *coll = probe->coll.file;
        /* Create a stream to read the file */
        if (flowpackAcquireFileHandle()) {
            goto ERROR;
        }
        rv = skStreamOpen(coll->stream);
        if (rv) {
            skStreamPrintLastErr(coll->stream, rv, &ERRMSG);
            skStreamDestroy(&coll->stream);
            flowpackReleaseFileHandle();
            /* skpcProbeDisposeIncomingFile(probe, path_name, 1); */
            goto ERROR;
        }
        thread_fn = sk_coll_file_thread;
        snprintf(thread_name, sizeof(thread_name), "%s-%s",
                 skpcProbeGetName(probe), "sk_coll_file_thread");

    } else {
        skAbort();
    }

    probe->coll.any->t.status = STARTING;
    increment_thread_count();

    rv = skthread_create(thread_name, &probe->coll.any->t.thread,
                         thread_fn, probe);
    if (rv) {
        ERRMSG("Unable to create reader thread %s: %s",
               thread_name, strerror(rv));
        decrement_thread_count(0);
        probe->coll.any->t.thread = pthread_self();
        probe->coll.any->t.status = STOPPED;
        goto ERROR;
    }

    while (STARTING == probe->coll.any->t.status) {
        pthread_cond_wait(&probe->coll.any->t.cond, &probe->coll.any->t.mutex);
    }
    if (STARTED == probe->coll.any->t.status) {
        pthread_mutex_unlock(&probe->coll.any->t.mutex);
        return 0;
    }
    /* thread was spawned but there was a problem; call destroy to
     * clean up our state */
    pthread_mutex_unlock(&probe->coll.any->t.mutex);
    return -1;

  ERROR:
    probe->coll.any->t.status = JOINED;
    return -1;
}


static void
sk_coll_stop_network(
    skpc_probe_t       *probe)
{
    assert(probe);
    assert(NULL == skpcProbeGetFileSource(probe)
           && NULL == skpcProbeGetPollDirectory(probe));

    switch (skpcProbeGetType(probe)) {
      case PROBE_ENUM_NETFLOW_V5:
        sk_coll_pdu_stop(probe);
        break;
      case PROBE_ENUM_NETFLOW_V9:
      case PROBE_ENUM_SFLOW:
        sk_coll_ipfix_stop(probe);
        break;
      case PROBE_ENUM_IPFIX:
        sk_coll_ipfix_stop(probe);
        break;
      case PROBE_ENUM_SILK:
      default:
        skAbortBadCase(probe->probe_type);
    }
}

static void
sk_coll_destroy_network(
    skpc_probe_t       *probe)
{
    assert(probe);
    assert(NULL == skpcProbeGetFileSource(probe)
           && NULL == skpcProbeGetPollDirectory(probe));

    switch (skpcProbeGetType(probe)) {
      case PROBE_ENUM_NETFLOW_V5:
        sk_coll_pdu_destroy(probe);
        break;
      case PROBE_ENUM_NETFLOW_V9:
      case PROBE_ENUM_SFLOW:
        sk_coll_ipfix_destroy(probe);
        break;
      case PROBE_ENUM_IPFIX:
        sk_coll_ipfix_destroy(probe);
        break;
      case PROBE_ENUM_SILK:
      default:
        skAbortBadCase(probe->probe_type);
    }
}


static int
sk_coll_create_network(
    skpc_probe_t       *probe)
{
    int rv;

    assert(probe);
    assert(NULL == skpcProbeGetFileSource(probe)
           && NULL == skpcProbeGetPollDirectory(probe));

    switch (skpcProbeGetType(probe)) {
      case PROBE_ENUM_NETFLOW_V5:
        rv = sk_coll_pdu_create(probe);
        break;
      case PROBE_ENUM_NETFLOW_V9:
      case PROBE_ENUM_SFLOW:
        rv = sk_coll_ipfix_create(probe);
        break;
      case PROBE_ENUM_IPFIX:
        rv = sk_coll_ipfix_create(probe);
        break;
      case PROBE_ENUM_SILK:
      default:
        skAbortBadCase(probe->probe_type);
    }
    if (rv) {
        return -1;
    }
    return 0;
}


static int
sk_coll_start_network(
    skpc_probe_t       *probe)
{
    int rv;

    assert(probe);
    assert(NULL == skpcProbeGetFileSource(probe)
           && NULL == skpcProbeGetPollDirectory(probe));

    switch (skpcProbeGetType(probe)) {
      case PROBE_ENUM_NETFLOW_V5:
        rv = sk_coll_pdu_start(probe);
        break;
      case PROBE_ENUM_NETFLOW_V9:
      case PROBE_ENUM_SFLOW:
        rv = sk_coll_ipfix_start(probe);
        break;
      case PROBE_ENUM_IPFIX:
        rv = sk_coll_ipfix_start(probe);
        break;
      case PROBE_ENUM_SILK:
      default:
        skAbortBadCase(probe->probe_type);
    }
    if (rv) {
        return -1;
    }
    return 0;
}


int
sk_conv_silk_create(
    skpc_probe_t       *probe)
{
    assert(probe);
    assert(PROBE_ENUM_SILK == skpcProbeGetType(probe));
    assert(skpcProbeGetFileSource(probe)
           || skpcProbeGetPollDirectory(probe));

    if (probe->converter) {
        return 0;
    }

    /* there is no state we need to maintain, but we need to allocate
     * something to put into probe->converter */
    probe->converter = sk_alloc(int);

    return 0;
}

int
sk_conv_silk_destroy(
    skpc_probe_t       *probe)
{
    assert(probe);
    assert(PROBE_ENUM_SILK == skpcProbeGetType(probe));
    assert(skpcProbeGetFileSource(probe)
           || skpcProbeGetPollDirectory(probe));

    if (NULL == probe->converter) {
        return 0;
    }

    free(probe->converter);
    probe->converter = NULL;

    return 0;
}


/*
 *  input_print_stats();
 *
 *    Invoked by input_mode_type->print_stats_fn();
 */
static void
input_print_stats(
    void)
{
    skpc_probe_t *probe;
    input_probe_vec_iter_t i;

    if (0 == input_probe_vec_get_count()) {
        return;
    }

    i = input_probe_vec_iter_init();
    while ((probe = input_probe_vec_iter_next(i)) != NULL) {
        if (NULL == skpcProbeGetPollDirectory(probe)) {
            /* Network-based probe */
            skpcProbeLogSourceStats(probe);
        }
    }
}


/*
 *  status = input_start();
 *
 *    Invoked by input_mode_type->start_fn();
 */
static int
input_start(
    void)
{
    skpc_probe_t *probe;
    input_probe_vec_iter_t i;
    int rv;

    /* Start each collector. */
    INFOMSG("Starting " INPUT_MODE_TYPE_NAME "...");

    /* Create each converter */
    i = input_probe_vec_iter_init();
    while ((probe = input_probe_vec_iter_next(i)) != NULL) {
        switch (skpcProbeGetType(probe)) {
          case PROBE_ENUM_NETFLOW_V5:
            rv = sk_conv_pdu_create(probe);
            break;
          case PROBE_ENUM_NETFLOW_V9:
          case PROBE_ENUM_SFLOW:
            rv = sk_conv_ipfix_create(probe);
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
    }

    /* Create each collector */
    i = input_probe_vec_iter_init();
    while ((probe = input_probe_vec_iter_next(i)) != NULL) {
        if (skpcProbeGetFileSource(probe)
            || skpcProbeGetPollDirectory(probe))
        {
            rv = sk_coll_create(probe);
        } else {
            rv = sk_coll_create_network(probe);
        }
        if (rv) {
            return -1;
        }
    }

    /* Start each collector */
    i = input_probe_vec_iter_init();
    while ((probe = input_probe_vec_iter_next(i)) != NULL) {
        DEBUGMSG("'%s': Starting %s source",
                 skpcProbeGetName(probe), skpcProbeGetTypeAsString(probe));
        if (skpcProbeGetFileSource(probe)
            || skpcProbeGetPollDirectory(probe))
        {
            rv = sk_coll_start(probe);
        } else {
            rv = sk_coll_start_network(probe);
        }
        if (rv) {
            WARNINGMSG("Failed to completely start " INPUT_MODE_TYPE_NAME ".");
            /* input_start_cleanup(i, 0); */
            return -1;
        }
    }

    INFOMSG("Started " INPUT_MODE_TYPE_NAME ".");

    return 0;
}


/*
 *  input_stop(void);
 *
 *    Invoked by input_mode_type->stop_fn();
 */
static void
input_stop(
    void)
{
    skpc_probe_t *probe;
    input_probe_vec_iter_t i;

    if (0 == input_probe_vec_get_count()) {
        return;
    }

    INFOMSG("Stopping " INPUT_MODE_TYPE_NAME "...");

    /* stop each collector */
    i = input_probe_vec_iter_init();
    while ((probe = input_probe_vec_iter_next(i)) != NULL) {
        if (skpcProbeGetFileSource(probe)
            || skpcProbeGetPollDirectory(probe))
        {
            sk_coll_stop(probe);
        } else {
            sk_coll_stop_network(probe);
        }
    }

    INFOMSG("Stopped " INPUT_MODE_TYPE_NAME ".");

    if (OUTPUT_FLOWCAP == output_mode
        || OUTPUT_ONE_DESTINATION == output_mode)
    {
        /* tell the packer on each probe to stop */
        i = input_probe_vec_iter_init();
        while ((probe = input_probe_vec_iter_next(i)) != NULL) {
            if (probe->pack.stop_packer) {
                probe->pack.stop_packer(probe);
            }
        }
    }
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
    const char *dir;
    int rv = -1;

    /*
     *  Determine which of sources that exist will actually be used
     *  and fill the global 'sources' vector with that information.
     *
     *  In OUTPUT_FLOWCAP mode, use all verified probes that listen on
     *  the network, regardless of whether the probe is connected to a
     *  source.  In all other output modes, only use probes that are
     *  connected to a sensor.
     */
    /* FIXME: Now using all probes (except in FLOWCAP mode) */

    /* create the vector of sources */
    input_probe_vec_create();

    /* examine the probes defined in the config file */
    skpcProbeIteratorBind(&p_iter);
    while (skpcProbeIteratorNext(&p_iter, &p) == 1) {
        /* ignore any file-based probes */
        if (skpcProbeGetFileSource(p)) {
            continue;
        }
        if (OUTPUT_FLOWCAP == output_mode) {
            /* ignore directory-based probes */
            if (skpcProbeGetPollDirectory(p)) {
                continue;
            }
        }

        /* for a directory-based probe, verify directory exists */
        dir = skpcProbeGetPollDirectory(p);
        if (dir && !skDirExists(dir)) {
            skAppPrintErr("Probe %s polls a nonexistent directory '%s'",
                          skpcProbeGetName(p), dir);
            goto END;
        }

        input_probe_vec_add(p);
    }

    if (0 == input_probe_vec_get_count()) {
        skAppPrintErr("No appropriate probes were found");
        goto END;
    }

    rv = 0;

  END:
    if (rv != 0) {
        /* failure.  clean up the sources */
        input_probe_vec_destroy();
    }
    return rv;
}


/*
 *  inputTeardown(void);
 *
 *    Invoked by input_mode_type->teardown_fn();
 */
static void
input_teardown(
    void)
{
    skpc_probe_t *probe;
    input_probe_vec_iter_t i;

    INFOMSG("Destroying " INPUT_MODE_TYPE_NAME " state...");

    /* destroy each collector */
    i = input_probe_vec_iter_init();
    while ((probe = input_probe_vec_iter_next(i)) != NULL) {
        DEBUGMSG("'%s': Destroying %s source",
                 skpcProbeGetName(probe), skpcProbeGetTypeAsString(probe));
        if (skpcProbeGetFileSource(probe)
            || skpcProbeGetPollDirectory(probe))
        {
            sk_coll_destroy(probe);
        } else {
            sk_coll_destroy_network(probe);
        }
    }

    /* destroy each converter */
    i = input_probe_vec_iter_init();
    while ((probe = input_probe_vec_iter_next(i)) != NULL) {
        switch (skpcProbeGetType(probe)) {
          case PROBE_ENUM_NETFLOW_V5:
            sk_conv_pdu_destroy(probe);
            break;
          case PROBE_ENUM_NETFLOW_V9:
          case PROBE_ENUM_SFLOW:
            sk_conv_ipfix_destroy(probe);
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

    input_probe_vec_destroy();

    INFOMSG("Destroyed " INPUT_MODE_TYPE_NAME " state.");
}


/*
 *  status = stream_initialize(input_mode_fn_table);
 *
 *    Fill in the function pointers for the input_mode_type.
 */
int
stream_initialize(
    input_mode_type_t  *input_mode_fn_table)
{
    /* Set function pointers */
    input_mode_fn_table->setup_fn       = &input_setup;
    input_mode_fn_table->start_fn       = &input_start;
    input_mode_fn_table->print_stats_fn = &input_print_stats;
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
