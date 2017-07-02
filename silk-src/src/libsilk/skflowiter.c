/*
** Copyright (C) 2014-2017 by Carnegie Mellon University.
**
** @OPENSOURCE_LICENSE_START@
** See license information in ../../LICENSE.txt
** @OPENSOURCE_LICENSE_END@
*/

#include <silk/silk.h>

RCSIDENT("$SiLK: skflowiter.c 373f990778e9 2017-06-22 21:57:36Z mthomas $");

#include <silk/rwrec.h>
#include <silk/skcircbuf.h>
#include <silk/skflowiter.h>
#include <silk/sksidecar.h>
#include <silk/skvector.h>
#include "skheader_priv.h"


/* LOCAL DEFINES AND TYPEDEFS */

struct flow_iter_stream_st {
    skstream_t         *stream;
    sk_file_header_t   *hdr;
    char               *pathname;
    unsigned            ignore  :1;
};
typedef struct flow_iter_stream_st flow_iter_stream_t;

/**
 *    flow_iter_cb_error_t holds a stream error callback function and
 *    its data.
 */
struct flow_iter_cb_error_st {
    sk_flow_iter_cb_error_fn_t  func;
    const void                 *data;
};
typedef struct flow_iter_cb_error_st flow_iter_cb_error_t;

/**
 *    flow_iter_cb_event_t holds a stream event callback function and
 *    its data.
 */
struct flow_iter_cb_event_st {
    sk_flow_iter_cb_event_fn_t  func;
    const void                 *data;
};
typedef struct flow_iter_cb_event_st flow_iter_cb_event_t;

/**
 *    Number of error callback types.  Keep in sync with the number of
 *    SK_FLOW_ITER_CB_ERROR_* values in sk_flow_iter_cb_type_t
 *    (defined in skflowiter.h).
 */
#define FLOW_ITER_CB_COUNT_ERROR  2

/**
 *    Number of event callback types.  Keep in sync with the number of
 *    SK_FLOW_ITER_CB_EVENT_* values in sk_flow_iter_cb_type_t
 *    (defined in skflowiter.h).
 */
#define FLOW_ITER_CB_COUNT_EVENT  5


/**
 *    sk_flow_iter_t is the user's handle to the flow iterator.
 */
struct sk_flow_iter_st {
    /* sk_circbuf_t       *circbuf; */

    skstream_t             *cur_stream;

    unsigned int            max_readers;

    sk_vector_t            *stream_vec;

    FILE                   *print_filenames;

    skstream_t             *copy_input;

    /* The position in 'stream_vec' that 'cur_stream' represents */
    size_t                  cur_idx;

    flow_iter_cb_error_t    error_cb[FLOW_ITER_CB_COUNT_ERROR];

    flow_iter_cb_event_t    event_cb[FLOW_ITER_CB_COUNT_EVENT];

    /* these are from skoptionsctx.c.  Currently it is unclear whether
     * these values get copied here or the sk_flow_iter_t has a
     * reference to the skoptionsctx that manages the files. */

    sk_options_ctx_t       *optctx;

    sk_ipv6policy_t         ipv6_policy;
};
/* typedef struct sk_flow_iter_st sk_flow_iter_t; // skflowiter.h */


struct sk_flow_iter_hdr_iter_st {
    sk_flow_iter_t     *f_iter;
    size_t              idx;
};
/* typedef struct sk_flow_iter_hdr_iter_st sk_flow_iter_hdr_iter_t; */



/* LOCAL VARIABLE DEFINITIONS */



/* LOCAL FUNCTION PROTOTYPES */

static flow_iter_stream_t *
flow_iter_stream_at(
    sk_flow_iter_t         *f_iter,
    size_t                  i);



/* FUNCTION DEFINITIONS */


/**
 *    Check that 'error_type' is a valid identifier for error
 *    callbacks.  If it is not, return NULL.
 *
 *    If it is, return a pointer to the callback structure on 'f_iter'.
 *
 *    Additionally, if 'error_idx' is non-NULL, set the value if
 *    references to the array index used to find the callback
 *    structure.
 */
static flow_iter_cb_error_t*
flow_iter_cb_type_to_idx_error(
    sk_flow_iter_t         *f_iter,
    sk_flow_iter_cb_type_t  error_type,
    unsigned int           *error_idx)
{
    unsigned int idx;

    switch (error_type) {
      case SK_FLOW_ITER_CB_ERROR_OPEN:
      case SK_FLOW_ITER_CB_ERROR_READ:
        break;
      default:
        return NULL;
    }

    idx = (0x7f & (unsigned int)error_type) - 1;
    assert(idx < FLOW_ITER_CB_COUNT_ERROR);
    if (error_idx) {
        *error_idx = idx;
    }
    return &f_iter->error_cb[idx];
}


/**
 *    Check that 'event_type' is a valid identifier for event
 *    callbacks.  If it is not, return NULL.
 *
 *    If it is, return a pointer to the callback structure on 'f_iter'.
 *
 *    Additionally, if 'event_idx' is non-NULL, set the value if
 *    references to the array index used to find the callback
 *    structure.
 */
static flow_iter_cb_event_t*
flow_iter_cb_type_to_idx_event(
    sk_flow_iter_t         *f_iter,
    sk_flow_iter_cb_type_t  event_type,
    unsigned int           *event_idx)
{
    unsigned int idx;

    switch (event_type) {
      case SK_FLOW_ITER_CB_EVENT_PRE_OPEN:
      case SK_FLOW_ITER_CB_EVENT_POST_OPEN:
      case SK_FLOW_ITER_CB_EVENT_PRE_READ:
      case SK_FLOW_ITER_CB_EVENT_PRE_CLOSE:
      case SK_FLOW_ITER_CB_EVENT_POST_CLOSE:
        break;
      default:
        return NULL;
    }

    idx = (0x7f & (unsigned int)event_type) - 1;
    assert(idx < FLOW_ITER_CB_COUNT_EVENT);
    if (event_idx) {
        *event_idx = idx;
    }
    return &f_iter->event_cb[idx];
}


/**
 *    Handle a stream error of type 'callback_type' that occurred on
 *    'stream' where the error_code is 'err_code'.  Return the result
 *    of the callback, or return SKSTREAM_OK if no callback is
 *    specified for the given error.
 */
static ssize_t
flow_iter_handle_stream_error(
    sk_flow_iter_t         *f_iter,
    sk_flow_iter_cb_type_t  callback_type,
    skstream_t             *stream,
    ssize_t                 err_code)
{
    flow_iter_cb_error_t *cb;

    cb = flow_iter_cb_type_to_idx_error(f_iter, callback_type, NULL);
    if (!cb) {
        skAbortBadCase(callback_type);
    }
    if (cb->func) {
        return cb->func(f_iter, stream, err_code, (void*)cb->data);
    }
    return SKSTREAM_OK;
}


/**
 *    Handle a stream event of type 'callback_type' that occurred on
 *    'stream'.  Do nother if no callback is specified for the given
 *    event.
 */
static void
flow_iter_handle_stream_event(
    sk_flow_iter_t         *f_iter,
    sk_flow_iter_cb_type_t  callback_type,
    skstream_t             *stream)
{
    flow_iter_cb_event_t *cb;

    cb = flow_iter_cb_type_to_idx_event(f_iter, callback_type, NULL);
    if (!cb) {
        skAbortBadCase(callback_type);
    }
    if (cb->func) {
        cb->func(f_iter, stream, (void*)cb->data);
    }
}


/**
 *    Close and destroy the stream in the stream_vec at index 'idx',
 *    doing nothing and returning SKSTREAM_OK if the stream at that
 *    index is NULL.  When closing the stream, also clear the 'hdr'
 *    member of the flow_iter_stream_t.
 *
 *    Call the event callbacks as needed.
 *
 *    Return the result of closing the stream.
 */
static ssize_t
flow_iter_close_stream(
    sk_flow_iter_t     *f_iter,
    size_t              idx)
{
    flow_iter_stream_t *iter_stream;
    ssize_t rv;

    iter_stream = flow_iter_stream_at(f_iter, idx);
    assert(iter_stream);

    if (NULL == iter_stream || NULL == iter_stream->stream) {
        return SKSTREAM_OK;
    }

    flow_iter_handle_stream_event(
        f_iter, SK_FLOW_ITER_CB_EVENT_PRE_CLOSE, iter_stream->stream);

    rv = skStreamClose(iter_stream->stream);

    flow_iter_handle_stream_event(
        f_iter, SK_FLOW_ITER_CB_EVENT_POST_CLOSE, iter_stream->stream);

    skStreamDestroy(&iter_stream->stream);
    iter_stream->hdr = NULL;

    return rv;
}


/**
 *    Open the stream named at position 'idx' in the stream vector and
 *    read its header, calling any event or error callbacks as
 *    required.  Return 0 on success.  Return -2 if 'idx' is out of
 *    range.  Return -1 on error.  Return 1 on an error that should be
 *    ignored.
 */
static int
flow_iter_open_stream(
    sk_flow_iter_t     *f_iter,
    size_t              idx,
    ssize_t            *rv)
{
    flow_iter_stream_t *iter_stream;

    assert(f_iter);
    assert(rv);
    *rv = SKSTREAM_OK;

    iter_stream = flow_iter_stream_at(f_iter, idx);
    if (NULL == iter_stream) {
        return -2;
    }

    /* Nothing to do when the stream is already open */
    if (iter_stream->stream) {
        return 0;
    }
    /* Destroy the header */
    skHeaderDestroy(&iter_stream->hdr);

    if (iter_stream->ignore) {
        return 1;
    }

    /* Allocate and initialize the stream */
    *rv = skStreamCreate(&iter_stream->stream, SK_IO_READ,SK_CONTENT_SILK_FLOW);
    if (*rv) { return -1; }
    *rv = skStreamBind(iter_stream->stream, iter_stream->pathname);
    if (*rv) { goto ERROR; }

    /* Open it */
    flow_iter_handle_stream_event(
        f_iter, SK_FLOW_ITER_CB_EVENT_PRE_OPEN, iter_stream->stream);

    *rv = skStreamOpen(iter_stream->stream);
    if (*rv) { goto ERROR; }

    flow_iter_handle_stream_event(
        f_iter, SK_FLOW_ITER_CB_EVENT_POST_OPEN, iter_stream->stream);

    /* Read the header */
    *rv = skStreamReadSilkHeader(iter_stream->stream, &iter_stream->hdr);
    if (*rv) { goto ERROR; }

    return 0;

  ERROR:
    if (*rv != SKSTREAM_ERR_CLOSED) {
        *rv = flow_iter_handle_stream_error(
            f_iter, SK_FLOW_ITER_CB_ERROR_OPEN, iter_stream->stream, *rv);
    }
    skStreamDestroy(&iter_stream->stream);
    iter_stream->hdr = NULL;
    if (SKSTREAM_ERR_CLOSED == *rv) {
        iter_stream->ignore = 1;
        return 1;
    }
    if (SKSTREAM_ERR_NOT_OPEN == *rv) {
        return 2;
    }
    return -1;
}


/*
 *    Open the stream at position 'idx'.  Call the pre-read callback
 *    function.  Set the stream's copy-input stream if appropriate.
 *    Set the stream's IPv6 policy.  Print its name if requested.
 *    Return 0 on success.  Return -2 if 'idx' is out of range.
 *    Return -1 on error.  Return 1 to ignore the stream.
 */
static int
flow_iter_prepare_read(
    sk_flow_iter_t     *f_iter,
    size_t              idx,
    ssize_t            *rv)
{
    flow_iter_stream_t *iter_stream;
    int err;

    assert(f_iter);
    assert(rv);

    err = flow_iter_open_stream(f_iter, idx, rv);
    if (err) { return err; }

    iter_stream = flow_iter_stream_at(f_iter, idx);
    assert(iter_stream);

    flow_iter_handle_stream_event(
        f_iter, SK_FLOW_ITER_CB_EVENT_PRE_READ, iter_stream->stream);

    if (f_iter->copy_input) {
        *rv = skStreamSetCopyInput(iter_stream->stream, f_iter->copy_input);
        if (*rv) { goto ERROR; }
    }

    *rv = skStreamSetIPv6Policy(iter_stream->stream, f_iter->ipv6_policy);
    if (*rv) { goto ERROR; }

    if (f_iter->print_filenames) {
        fprintf(f_iter->print_filenames, "%s\n", iter_stream->pathname);
    }

    return 0;

  ERROR:
    if (*rv != SKSTREAM_ERR_CLOSED) {
        *rv = flow_iter_handle_stream_error(
            f_iter, SK_FLOW_ITER_CB_ERROR_OPEN, iter_stream->stream, *rv);
    }
    skStreamDestroy(&iter_stream->stream);
    iter_stream->hdr = NULL;
    if (SKSTREAM_ERR_CLOSED == *rv) {
        return 1;
    }
    if (SKSTREAM_ERR_NOT_OPEN == *rv) {
        return 2;
    }
    return -1;
}


/*
 *    Prepare to read to the next stream.  If successful, return that
 *    stream.  If preparing to read fails and the error should be
 *    ignored, go the next stream.  Continue until a stream is
 *    successfully opened, a none-ignored error occurs, or there are
 *    no more streams.
 *
 *    The referent of 'rv' is set to SKSTREAM_ERR_EOF when there are
 *    no more streams, SKSTREAM_OK on success, or another error code.
 */
static flow_iter_stream_t *
flow_iter_prepare_read_next(
    sk_flow_iter_t     *f_iter,
    ssize_t            *rv)
{
    flow_iter_stream_t *iter_stream;
    int err;

    /* go to the next/first stream */
    while ((err = flow_iter_prepare_read(f_iter, f_iter->cur_idx, rv)) == 1) {
        ++f_iter->cur_idx;
    }
    if (-2 == err) {
        *rv = SKSTREAM_ERR_EOF;
        return NULL;
    }
    if (2 == err) {
        /* retry the stream */
        return NULL;
    }
    if (-1 == err) {
        iter_stream = NULL;
    } else {
        assert(0 == err);
        assert(SKSTREAM_OK == *rv);
        iter_stream = flow_iter_stream_at(f_iter, f_iter->cur_idx);
        f_iter->cur_stream = iter_stream->stream;
    }
    ++f_iter->cur_idx;
    return iter_stream;
}


static void
flow_iter_set_copy_stream(
    sk_flow_iter_t     *f_iter,
    skstream_t         *stream)
{
    assert(f_iter);
    f_iter->copy_input = stream;
}


static void
flow_iter_set_print_filenames(
    sk_flow_iter_t     *f_iter,
    FILE               *fileptr)
{
    assert(f_iter);
    assert(fileptr);
    f_iter->print_filenames = fileptr;
}


static flow_iter_stream_t *
flow_iter_stream_at(
    sk_flow_iter_t         *f_iter,
    size_t                  i)
{
    return ((flow_iter_stream_t *)
            sk_vector_get_value_pointer(f_iter->stream_vec, i));
}


/*
 *  Public functions
 *  ******************************************************************
 */

sk_flow_iter_t *
skOptionsCtxCreateFlowIterator(
    sk_options_ctx_t   *optctx)
{
    sk_flow_iter_t *f_iter;
    flow_iter_cb_error_t *cb;
    unsigned int i;
    flow_iter_stream_t iter_stream;
    char name[PATH_MAX];
    FILE *fp;
    skstream_t *copy_input;

    if (!optctx) {
        return NULL;
    }

    f_iter = sk_alloc(sk_flow_iter_t);
    f_iter->stream_vec = sk_vector_create(sizeof(flow_iter_stream_t));

    /*
     *f_iter->circbuf = sk_circbuf_create(
     *    SK_CIRCBUF_MEM_STD_CHUNK_SIZE, SK_CIRCBUF_MEM_STD_CHUNK_SIZE);
     *if (NULL == f_iter->circbuf) {
     *    free(f_iter);
     *    return -1;
     *}
     */

    f_iter->optctx = optctx;

    fp = skOptionsCtxGetPrintFilenames(optctx);
    if (fp) {
        flow_iter_set_print_filenames(f_iter, fp);
    }
    copy_input = skOptionsCtxGetCopyStream(optctx);
    if (copy_input) {
        flow_iter_set_copy_stream(f_iter, copy_input);
    }
    sk_flow_iter_set_ipv6_policy(f_iter, skOptionsCtxGetIPv6Policy(optctx));

    for (cb = f_iter->error_cb, i = 0; i < FLOW_ITER_CB_COUNT_ERROR; ++i, ++cb)
    {
        cb->func = sk_flow_iter_default_error_read_cb;
        cb->data = NULL;
    }
    cb = flow_iter_cb_type_to_idx_error(
        f_iter, SK_FLOW_ITER_CB_ERROR_OPEN, NULL);
    if (!cb) {
        skAbort();
    }
    cb->func = sk_flow_iter_default_error_open_cb;

    memset(&iter_stream, 0, sizeof(iter_stream));
    while (0 == skOptionsCtxNextArgument(optctx, name, sizeof(name))) {
        iter_stream.pathname = strdup(name);
        sk_vector_append_value(f_iter->stream_vec, &iter_stream);
    }
    return f_iter;
}


/* int */
/* sk_flow_iter_add_path( */
/*     sk_flow_iter_t     *f_iter, */
/*     const char         *name); */

/* int */
/* sk_flow_iter_add_paths( */
/*     sk_flow_iter_t     *f_iter, */
/*     size_t              num_entries, */
/*     const char        **path_entries); */

/* int */
/* sk_flow_iter_add_stream( */
/*     sk_flow_iter_t     *f_iter, */
/*     skstream_t         *stream); */


ssize_t
sk_flow_iter_close_all(
    sk_flow_iter_t     *f_iter)
{
    flow_iter_stream_t *iter_stream;
    ssize_t rv = SKSTREAM_OK;
    ssize_t rv_i;
    size_t i;

    if (f_iter) {
        f_iter->cur_stream = NULL;
        for (i = 0; (iter_stream = flow_iter_stream_at(f_iter, i)); ++i) {
            rv_i = flow_iter_close_stream(f_iter, i);
            if (rv_i && !rv) {
                rv = rv_i;
            }
        }
    }
    return rv;
}


ssize_t
sk_flow_iter_close_stream(
    sk_flow_iter_t     *f_iter,
    const skstream_t   *stream)
{
    flow_iter_stream_t *iter_stream;
    size_t i;

    assert(f_iter);
    assert(stream);

    for (i = 0; (iter_stream = flow_iter_stream_at(f_iter, i)); ++i) {
        if (stream == iter_stream->stream) {
            return flow_iter_close_stream(f_iter, i);
        }
    }
    return SKSTREAM_OK;
}



void
sk_flow_iter_destroy(
    sk_flow_iter_t    **f_iter_parm)
{
    sk_flow_iter_t *f_iter;
    flow_iter_stream_t *iter_stream;
    size_t i;

    if (f_iter_parm && *f_iter_parm) {
        f_iter = *f_iter_parm;
        *f_iter_parm = NULL;

        if (f_iter->copy_input) {
            skOptionsCtxCopyStreamClose(f_iter->optctx, NULL);
        }
        for (i = 0; (iter_stream = flow_iter_stream_at(f_iter, i)); ++i) {
            if (iter_stream->stream) {
                flow_iter_close_stream(f_iter, i);
            } else if (iter_stream->hdr) {
                skHeaderDestroy(&iter_stream->hdr);
            }
            free(iter_stream->pathname);
        }
        sk_vector_destroy(f_iter->stream_vec);

        /* sk_circbuf_destroy(f_iter->circbuf); */
        free(f_iter);
    }
}


ssize_t
sk_flow_iter_fill_sidecar(
    sk_flow_iter_t     *f_iter,
    sk_sidecar_t       *sidecar)
{
    sk_flow_iter_hdr_iter_t *hdr_iter;
    sk_file_header_t *hdr;
    sk_sidecar_iter_t sc_iter;
    const sk_sidecar_elem_t *sc_elem;
    sk_sidecar_elem_t *new_elem;
    sk_sidecar_t *hdr_sidecar;
    int rv;

    if (sk_flow_iter_read_silk_headers(f_iter, &hdr_iter)) {
        return -1;
    }
    while ((hdr = sk_flow_iter_hdr_iter_next(hdr_iter)) != NULL) {
        hdr_sidecar = sk_sidecar_create_from_header(hdr, &rv);
        if (NULL == hdr_sidecar) {
            if (rv) {
                return -1;
            }
        } else {
            sk_sidecar_iter_bind(hdr_sidecar, &sc_iter);
            while (sk_sidecar_iter_next(&sc_iter, &sc_elem) == SK_ITERATOR_OK){
                rv = sk_sidecar_add_elem(sidecar, sc_elem, &new_elem);
                if (SK_SIDECAR_E_DUPLICATE == rv) {
                    /* FIXME: ignore for now */
                } else if (rv) {
                    /* FIXME: ignore for now */
                    /* skAppPrintErr("Cannot add field '%s' from sidecar: %d",
                     *             buf, rv); */
                } else {
                }
            }
            sk_sidecar_destroy(&hdr_sidecar);
        }
    }
    sk_flow_iter_hdr_iter_destroy(&hdr_iter);

    return 0;
}


ssize_t
sk_flow_iter_get_next_rec(
    sk_flow_iter_t     *f_iter,
    rwRec              *rwrec)
{
    flow_iter_stream_t *iter_stream;
    ssize_t rv;
    ssize_t cb_rv;

    assert(f_iter);
    assert(rwrec);

    /*
     *if (!f_iter->started) {
     *    rv = flow_iter_start(f_iter);
     *    if (rv) {
     *        return rv;
     *    }
     *}
     */

    for (;;) {
        while (f_iter->cur_stream) {
            rv = skStreamReadRecord(f_iter->cur_stream, rwrec);
            if (SKSTREAM_OK == rv) {
                return rv;
            }
            if (SKSTREAM_ERR_EOF == rv || SKSTREAM_ERR_CLOSED == rv) {
                /* we could get an SKSTREAM_ERR_CLOSED if the error
                 * callback closed the stream */
                /* go to next stream */
                assert(f_iter->cur_idx > 0);
                f_iter->cur_stream = NULL;
                flow_iter_close_stream(f_iter, f_iter->cur_idx - 1);
                break;
            }
            cb_rv = flow_iter_handle_stream_error(
                f_iter, SK_FLOW_ITER_CB_ERROR_READ,
                f_iter->cur_stream, rv);
            if (cb_rv) {
                return cb_rv;
            }
        }

        /* go to the next/first stream */
        iter_stream = flow_iter_prepare_read_next(f_iter, &rv);
        if (NULL == iter_stream) {
            return rv;
        }
    }
}


ssize_t
sk_flow_iter_get_next_stream(
    sk_flow_iter_t     *f_iter,
    skstream_t        **stream)
{
    flow_iter_stream_t *iter_stream;
    ssize_t rv;

    assert(f_iter);
    assert(stream);

    /* go to the next/first stream */
    iter_stream = flow_iter_prepare_read_next(f_iter, &rv);
    if (NULL == iter_stream) {
        *stream = NULL;
        return rv;
    }
    *stream = iter_stream->stream;
    return SKSTREAM_OK;
}


void
sk_flow_iter_hdr_iter_destroy(
    sk_flow_iter_hdr_iter_t   **hdr_iter)
{
    if (hdr_iter && *hdr_iter) {
        free(*hdr_iter);
        *hdr_iter = NULL;
    }
}


sk_file_header_t *
sk_flow_iter_hdr_iter_next(
    sk_flow_iter_hdr_iter_t    *hdr_iter)
{
    flow_iter_stream_t *iter_stream;

    if (hdr_iter) {
        while ((iter_stream = flow_iter_stream_at(hdr_iter->f_iter,
                                                  hdr_iter->idx)))
        {
            ++hdr_iter->idx;
            if (iter_stream->hdr) {
                return iter_stream->hdr;
            }
        }
    }
    return NULL;
}


ssize_t
sk_flow_iter_read_silk_headers(
    sk_flow_iter_t             *f_iter,
    sk_flow_iter_hdr_iter_t   **hdr_iter)
{
    flow_iter_stream_t *iter_stream;
    sk_file_header_t *hdr;
    ssize_t rv = SKSTREAM_OK;
    size_t i;
    int err;

    if (NULL == f_iter || NULL == hdr_iter) {
        return rv;
    }
    *hdr_iter = NULL;

    /* FIXME: This code is inefficient now since it opens the stream,
     * reads/copies the header, and closes the stream again (if the
     * stream is seekable) meaning the stream must be re-opened when
     * it is time to read records.  A better approach is to attempt to
     * hold a handle to all streams (or to a maximum number of
     * streams), and only close streams when out of file handles or
     * memory or the maximum number of streams has been reached. */

    for (i = 0; (err = flow_iter_open_stream(f_iter, i, &rv)) >= 0; ++i) {
        if (1 == err) {
            continue;
        }
        assert(0 == err);

        iter_stream = flow_iter_stream_at(f_iter, i);
        if (!skStreamIsSeekable(iter_stream->stream)
            || skStreamGetDescriptor(iter_stream->stream) == STDIN_FILENO)
        {
            /* do nothing else with this stream */
        } else {
            hdr = NULL;
            if ((rv = skHeaderCreate(&hdr))
                || (rv = skHeaderCopy(hdr, iter_stream->hdr, SKHDR_CP_ALL)))
            {
                skHeaderDestroy(&hdr);
                return rv;
            }
            skStreamDestroy(&iter_stream->stream);
            iter_stream->hdr = hdr;
        }
    }
    if (-1 == err) {
        return rv;
    }
    assert(-2 == err);
    /* successfully processed all files */

    (*hdr_iter) = sk_alloc(sk_flow_iter_hdr_iter_t);
    (*hdr_iter)->f_iter = f_iter;
    (*hdr_iter)->idx = 0;

    return SKSTREAM_OK;
}


int
sk_flow_iter_set_ipv6_policy(
    sk_flow_iter_t     *f_iter,
    sk_ipv6policy_t     policy)
{
    if (NULL == f_iter) {
        return -1;
    }
    f_iter->ipv6_policy = policy;
    return 0;
}


int
sk_flow_iter_set_max_readers(
    sk_flow_iter_t     *f_iter,
    unsigned int        max_readers)
{
    if (NULL == f_iter || 0 == max_readers) {
        return -1;
    }
    f_iter->max_readers = max_readers;
    return 0;
}


int
sk_flow_iter_skip_remaining_records(
    sk_flow_iter_t     *f_iter)
{
    ssize_t rv;

    while ((rv = sk_flow_iter_skip_records(f_iter, UINT16_MAX, NULL))
           == SKSTREAM_OK)
        ;                       /* empty */

    return rv;
}


int
sk_flow_iter_skip_records(
    sk_flow_iter_t     *f_iter,
    size_t              skip_count,
    size_t             *number_skipped)
{
    flow_iter_stream_t *iter_stream;
    ssize_t rv;
    ssize_t cb_rv;
    size_t skipped;
    size_t local_number_skipped;

    assert(f_iter);

    if (number_skipped) {
        *number_skipped = 0;
    } else {
        local_number_skipped = 0;
        number_skipped = &local_number_skipped;
    }

    /*
     *if (!f_iter->started) {
     *    rv = flow_iter_start(f_iter);
     *    if (rv) {
     *        return rv;
     *    }
     *}
     */

    rv = SKSTREAM_OK;
    while (skip_count > 0) {
        while (f_iter->cur_stream) {
            skipped = 0;
            rv = skStreamSkipRecords(f_iter->cur_stream, skip_count, &skipped);
            skip_count -= skipped;
            *number_skipped += skipped;
            if (SKSTREAM_OK == rv) {
                return rv;
            }
            if (SKSTREAM_ERR_EOF == rv || SKSTREAM_ERR_CLOSED == rv) {
                /* we could get an SKSTREAM_ERR_CLOSED if the error
                 * callback closed the stream */
                /* go to next stream */
                assert(f_iter->cur_idx > 0);
                f_iter->cur_stream = NULL;
                flow_iter_close_stream(f_iter, f_iter->cur_idx - 1);
                break;
            }
            cb_rv = flow_iter_handle_stream_error(
                f_iter, SK_FLOW_ITER_CB_ERROR_READ,
                f_iter->cur_stream, rv);
            if (cb_rv) {
                return cb_rv;
            }
        }

        /* go to the next/first stream */
        iter_stream = flow_iter_prepare_read_next(f_iter, &rv);
        if (NULL == iter_stream) {
            return rv;
        }
    }

    return rv;
}


/*
 *  Callback-related functions
 *  ******************************************************************
 */

int
sk_flow_iter_set_stream_error_cb(
    sk_flow_iter_t             *f_iter,
    sk_flow_iter_cb_type_t      callback_type,
    sk_flow_iter_cb_error_fn_t  callback_func,
    const void                 *callback_data)
{
    flow_iter_cb_error_t *cb;

    if (!f_iter) {
        return -1;
    }
    cb = flow_iter_cb_type_to_idx_error(f_iter, callback_type, NULL);
    if (!cb) {
        return -1;
    }

    cb->func = callback_func;
    cb->data = callback_data;
    return 0;
}


int
sk_flow_iter_set_stream_event_cb(
    sk_flow_iter_t             *f_iter,
    sk_flow_iter_cb_type_t      callback_type,
    sk_flow_iter_cb_event_fn_t  callback_func,
    const void                 *callback_data)
{
    flow_iter_cb_event_t *cb;

    if (!f_iter) {
        return -1;
    }
    cb = flow_iter_cb_type_to_idx_event(f_iter, callback_type, NULL);
    if (!cb) {
        return -1;
    }

    cb->func = callback_func;
    cb->data = callback_data;
    return 0;
}


ssize_t
sk_flow_iter_default_error_open_cb(
    sk_flow_iter_t  UNUSED(*f_iter),
    skstream_t             *stream,
    ssize_t                 err_code,
    void            UNUSED(*cb_data))
{
    skStreamPrintLastErr(stream, err_code, &skAppPrintErr);
    skStreamClose(stream);
    return err_code;
}


ssize_t
sk_flow_iter_ignore_error_open_cb(
    sk_flow_iter_t  UNUSED(*f_iter),
    skstream_t             *stream,
    ssize_t                 err_code,
    void            UNUSED(*cb_data))
{
    skStreamPrintLastErr(stream, err_code, &skAppPrintErr);
    skStreamClose(stream);
    return SKSTREAM_ERR_CLOSED;
}


ssize_t
sk_flow_iter_default_error_read_cb(
    sk_flow_iter_t  UNUSED(*f_iter),
    skstream_t             *stream,
    ssize_t                 err_code,
    void            UNUSED(*cb_data))
{
    skStreamPrintLastErr(stream, err_code, &skAppPrintErr);
    if (SKSTREAM_ERROR_IS_FATAL(err_code)) {
        return err_code;
    }
    return 0;
}


/*
** Local Variables:
** mode:c
** indent-tabs-mode:nil
** c-basic-offset:4
** End:
*/
