/*
** Copyright (C) 2014-2017 by Carnegie Mellon University.
**
** @OPENSOURCE_LICENSE_START@
** See license information in ../../LICENSE.txt
** @OPENSOURCE_LICENSE_END@
*/
#ifndef _SKFLOWITER_H
#define _SKFLOWITER_H
#ifdef __cplusplus
extern "C" {
#endif

#include <silk/silk.h>

RCSIDENTVAR(rcsID_SKFLOWITER_H, "$SiLK: skflowiter.h efd886457770 2017-06-21 18:43:23Z mthomas $");

#include <silk/skstream.h>
#include <silk/utils.h>


/*
 *  skflowiter.h
 *
 *    Implements an iterator over the flow records read from streams
 *    (files, named pipes, and standard input).
 *
 *    The purpose is to allow an application to process all the
 *    records in all the input streams without worrying about the
 *    details of the streams.  In addition, the flow iterator could
 *    use multiple threads to "pre-fetch" records and speed processing
 *    without the application itself needing to worry about managing
 *    threads.
 *
 *    Expected usage:
 *
 *        sk_flow_iter_t *fit = NULL;
 *        FILE *fp = stderr;
 *        rwRec rwrec;
 *        int rv;
 *
 *        sk_flow_iter_create(&fit);
 *        sk_flow_iter_set_ipv6_policy(fit, SK_IPV6POLICY_MIX);
 *        sk_flow_iter_set_print_filenames(fit, fp);
 *        sk_flow_iter_add_from_options_ctx(fit, optctx);
 *
 *        while ((rv = sk_flow_iter_get_next_rec(fit, &rwrec))
 *               == 0)
 *        {
 *            // process record
 *        }
 *
 *        sk_flow_iter_close_all(fit);
 *        sk_flow_iter_destroy(&fit);
 *
 *
 *    FIXME: Any need for user-defined data context-object on the
 *    sk_flow_iter_t itself?
 *
 *    FIXME: Unsure whether the event callback functions should return
 *    void (as they currently do) or an integer.  Question is how to
 *    interpret that integer?
 *
 *    TODO: Currently a wrapper over sk_options_ctx_t.  Do we need
 *    more ways to add streams to the sk_flow_iter_t?
 *
 *    TODO: Would like to use a circular buffer to decouple the
 *    reading of the records from the stream with returning the
 *    records to the caller.  This gives us multi-threading with
 *    little effort on application's part, though the callback
 *    functions could be problematic if not reentrant.  The pre-fetch
 *    also decouples the record from the stream, which could cause a
 *    close callback to invoked on a stream while the application is
 *    still processing records from the stream.  This may mean we need
 *    to add some way to disable pre-fetching.  Alternatively, create
 *    a new function that returns the record and its stream, though
 *    how will sk_flow_iter_t know it can close the stream?
 *
 *    FIXME: Would be nice to modify sk_flow_iter_t to support
 *    parallel multi-stream reading of the type that rwsort, rwuniq,
 *    and rwstats do when processing pre-sorted streams.
 */



typedef struct sk_flow_iter_st sk_flow_iter_t;

typedef struct sk_flow_iter_hdr_iter_st sk_flow_iter_hdr_iter_t;


/* /\** */
/*  *    Add the input streams known to the options context 'optctx' to */
/*  *    the flow iterator.  Copy the --copy-input stream from 'optctx'. */
/*  *    Copy the status of the --print-filenames setting from 'optctx'. */
/*  *    Copy the status of the --ipv6-policy setting from 'optctx'. */
/*  * */
/*  *    Return -1 on memory error or if 'optctx' is NULL. */
/*  *\/ */
/* int */
/* sk_flow_iter_add_from_options_ctx( */
/*     sk_flow_iter_t     *f_iter, */
/*     sk_options_ctx_t   *optctx); */

/* /\** */
/*  *    Add one stream (by name) to the flow iterator.  Return 0 on */
/*  *    success.  Accept "stdin" or "-" to mean the standard input. */
/*  *    Return -1 on memory error or if 'name' is NULL. */
/*  * */
/*  *    UNIMPLEMENTED. */
/*  *\/ */
/* int */
/* sk_flow_iter_add_path( */
/*     sk_flow_iter_t     *f_iter, */
/*     const char         *name); */

/* /\** */
/*  *    Add an array of streams (by name) to the flow iterator. */
/*  * */
/*  *    Call sk_flow_iter_add_path() for each name in 'path_entries' */
/*  *    until either a NULL entry is found in 'path_entries' or */
/*  *    'num_entries' is reached. */
/*  * */
/*  *    UNIMPLEMENTED. */
/*  *\/ */
/* int */
/* sk_flow_iter_add_paths( */
/*     sk_flow_iter_t     *f_iter, */
/*     size_t              num_entries, */
/*     const char        **path_entries); */

/* /\** */
/*  *    Add an existing stream to the flow iterator.  Return -1 on */
/*  *    memory error or if 'stream' is NULL. */
/*  * */
/*  *    UNIMPLEMENTED. */
/*  *\/ */
/* int */
/* sk_flow_iter_add_stream( */
/*     sk_flow_iter_t     *f_iter, */
/*     skstream_t         *stream); */

/**
 *    Create a flow iterator.
 *
 *    Add the input streams known to the options context 'optctx' to
 *    the flow iterator.  Copy the --copy-input stream from 'optctx'
 *    Copy the status of the --print-filenames setting from 'optctx'.
 *    Copy the status of the --ipv6-policy setting from 'optctx'.
 *
 *    Return the new flow iterator on success.  Return NULL if
 *    'optctx' is NULL.  Exit the application on memory allocation
 *    error.
 */
sk_flow_iter_t *
skOptionsCtxCreateFlowIterator(
    sk_options_ctx_t   *optctx);


/**
 *    Close all input streams and stop processing flow records.
 *    Return SKSTREAM_OK if there were no read errors or close errors
 *    on any stream (other than SKSTREAM_ERR_EOF).  If one or more
 *    streams had an error on read or on close, return the error code
 *    returned by one of those functions.
 */
ssize_t
sk_flow_iter_close_all(
    sk_flow_iter_t     *f_iter);


/**
 *    Close the input stream 'stream' that was returned by
 *    sk_flow_iter_get_next_stream().  Do nothing if 'stream' is not
 *    found on the flow iterator 'f_iter'.
 */
ssize_t
sk_flow_iter_close_stream(
    sk_flow_iter_t     *f_iter,
    const skstream_t   *stream);


/**
 *    Destroy a flow iterator.  Does nothing if 'f_iter' is NULL.
 */
void
sk_flow_iter_destroy(
    sk_flow_iter_t    **f_iter);


/**
 *    Fill the sidecar object 'sidecar' with the sidecar fields that
 *    exist on all the input streams.
 */
ssize_t
sk_flow_iter_fill_sidecar(
    sk_flow_iter_t     *f_iter,
    sk_sidecar_t       *sidecar);


/**
 *    Fill 'rwrec' with the next flow record read from any of the
 *    input streams and return SKSTREAM_OK.  Return SKSTREAM_ERR_EOF
 *    when all input streams are exhausted.
 *
 *    If a file-opening or file-read error occurs and a user-defined
 *    SK_FLOW_ITER_CB_ERROR_OPEN or SK_FLOW_ITER_CB_ERROR_READ
 *    callback has been set and that callback returns a non-zero
 *    value, that return value is used as the return value of this
 *    function.  If a user-defined callback has not been set,
 *    sk_flow_iter_default_error_cb() is used to report the error and
 *    processing continues.
 *
 *    Using this function in conjunction with
 *    sk_flow_iter_get_next_stream() confuses the flow iterator.
 */
ssize_t
sk_flow_iter_get_next_rec(
    sk_flow_iter_t     *f_iter,
    rwRec              *rwrec);


/**
 *    Fill the referent of 'stream' with a pointer to the next input
 *    stream and return SKSTREAM_OK.  Return SKSTREAM_ERR_EOF when all
 *    input streams are exhausted.
 *
 *    FIXME: Describe callbacks and error conditions.
 *
 *
 *    Using this function in conjunction with
 *    sk_flow_iter_get_next_rec() confuses the flow iterator.
 */
ssize_t
sk_flow_iter_get_next_stream(
    sk_flow_iter_t     *f_iter,
    skstream_t        **stream);


/**
 *    Destroy the iterator 'hdr_iter' that was filled by a call to
 *    sk_flow_iter_read_silk_headers().  Do nothing if 'hdr_iter' or
 *    its referent is NULL.
 */
void
sk_flow_iter_hdr_iter_destroy(
    sk_flow_iter_hdr_iter_t   **hdr_iter);


/**
 *    Move 'hdr_iter' to the first/next SiLK file header and return
 *    that header.  Return NULL when all headers have been visited.
 *    Return NULL if 'hdr_iter' is NULL.
 *
 *    A header iterator is returned by making a call to
 *    sk_flow_iter_read_silk_headers().
 */
sk_file_header_t *
sk_flow_iter_hdr_iter_next(
    sk_flow_iter_hdr_iter_t    *hdr_iter);


/**
 *    Set the referent of 'hdr_iter' to an iterator that may be used
 *    to visit the header of each of the SiLK files specified in
 *    'f_iter'.
 *
 *    Return SKSTREAM_OK on success or the error code returned when
 *    the attempt to open and read a file fails,
 *
 *    This function opens each stream named in 'f_iter'.  If the
 *    stream is seekable, the stream is closed and it will be
 *    re-opened when records are read from it.
 *
 *    Events registered as SK_FLOW_ITER_CB_EVENT_PRE_OPEN or
 *    SK_FLOW_ITER_CB_EVENT_POST_OPEN are called by this function.
 *    Those callbacks will be invoked again when the file is opened a
 *    second time to read its records.
 */
ssize_t
sk_flow_iter_read_silk_headers(
    sk_flow_iter_t             *f_iter,
    sk_flow_iter_hdr_iter_t   **hdr_iter);


/* /\** */
/*  *    Write a copy of every record read from an input stream to the */
/*  *    stream specified in 'stream'.  Return 0 on success; return -1 if */
/*  *    f_iter is NULL. */
/*  *\/ */
/* int */
/* sk_flow_iter_set_copy_stream( */
/*     sk_flow_iter_t     *f_iter, */
/*     skstream_t         *stream); */


/**
 *    Set the IPv6 record policy that should be specified on streams
 *    as they are opened.  Return 0 on success; return -1 if f_iter is
 *    NULL.
 */
int
sk_flow_iter_set_ipv6_policy(
    sk_flow_iter_t     *f_iter,
    sk_ipv6policy_t     policy);


/**
 *    Set the maximum number of streams from which
 *    sk_flow_iter_get_next_rec() is allowed to return a flow record.
 *    Return 0 on success; return -1 if f_iter is NULL.
 *
 *    The caller should set max_readers to 1 if strictly sequential
 *    file processing is required.
 *
 *    Currently pointless since record reading is always sequential.
 */
int
sk_flow_iter_set_max_readers(
    sk_flow_iter_t     *f_iter,
    unsigned int        max_readers);


/* /\** */
/*  *    Tell the flow iterator to print the name of each input stream to */
/*  *    the file pointer 'fileptr' as each stream is opened for reading. */
/*  *    If 'fileptr' is NULL, file names are not printed.  Return 0 on */
/*  *    success; return -1 if f_iter is NULL. */
/*  *\/ */
/* int */
/* sk_flow_iter_set_print_filenames( */
/*     sk_flow_iter_t     *f_iter, */
/*     FILE               *fileptr); */


/**
 *    Skip all remaining records in the input streams.
 *
 *    This is similar to closing all input streams except, in the case
 *    where the copy stream is being used, this function ensures that
 *    all input records are written to the copy stream.
 */
int
sk_flow_iter_skip_remaining_records(
    sk_flow_iter_t     *f_iter);


/**
 *    Skip 'skip_count' records, moving across streams as required.
 *
 *    If 'number_skipped' is not NULL, the number of records skipped
 *    is written to the location it references.
 */
int
sk_flow_iter_skip_records(
    sk_flow_iter_t     *f_iter,
    size_t              skip_count,
    size_t             *number_skipped);



/* ********  Registering Callbacks  ********** */

/**
 *    The caller may register callbacks to be invoked when the
 *    following stream events occur or stream errors are encountered.
 *
 *    To register a callback to handle an error, use the
 *    sk_flow_iter_set_stream_error_cb() function.
 *
 *    To register a callback to be invoked when an event occurs, use
 *    the sk_flow_iter_set_stream_event_cb() function.
 */
enum sk_flow_iter_cb_type_en {

    /*  The following identifiers use the function signature specified
     *  by sk_flow_iter_cb_error_fn_t and these identifiers should be
     *  passed to sk_flow_iter_set_stream_error_cb(). */

    /** Identifier for a callback to be invoked when there is an error
     *  opening a stream or reading its header.
     *
     *  If the callback return SKSTREAM_ERR_CLOSED, the file is
     *  ignored and processing continues with the next file.  If the
     *  callback returns SKSTREAM_ERR_NOT_OPEN, the error is ignored
     *  and the iterator is not advanced, meaning the flow iterator
     *  attempts to open the same file again. */
    SK_FLOW_ITER_CB_ERROR_OPEN = 0x01,

    /** Identifier for a callback to be invoked when there is an error
     *  reading from a stream (other than SKSTREAM_ERR_EOF). */
    SK_FLOW_ITER_CB_ERROR_READ,


    /*  The following identifiers use the function signature specified
     *  by sk_flow_iter_cb_event_fn_t and these identifiers should be
     *  passed to sk_flow_iter_set_stream_event_cb(). */

    /** Identifier for a callback to be invoked on a stream after
     *  binding the name of the file/pipe the stream and before
     *  opening it. */
    SK_FLOW_ITER_CB_EVENT_PRE_OPEN = 0x81,

    /** Identifier for a callback to be invoked on a stream after
     *  opening it and before reading the header. */
    SK_FLOW_ITER_CB_EVENT_POST_OPEN,

    /** Identifier for a callback to be invoked on a stream after
     *  reading the header and before processing records. */
    SK_FLOW_ITER_CB_EVENT_PRE_READ,

    /** Identifier for a callback to be invoked when a stream after
     *  reading the records and before closing it. */
    SK_FLOW_ITER_CB_EVENT_PRE_CLOSE,

    /** Identifier for a callback to be invoked when a stream after
     *  closing it and before destroying it. */
    SK_FLOW_ITER_CB_EVENT_POST_CLOSE
};
typedef enum sk_flow_iter_cb_type_en sk_flow_iter_cb_type_t;


/**
 *    Signature of a callback function that may be called when there
 *    is an unexpected error condition on a stream.  If the callback
 *    returns a non-zero value, that value is used as the return value
 *    of sk_flow_iter_get_next_rec().
 *
 *    The list of events is specified by the sk_flow_iter_cb_type_t
 *    enumeration.  To set a callback, use the
 *    sk_flow_iter_set_stream_error_cb() function.
 */
typedef ssize_t
(*sk_flow_iter_cb_error_fn_t)(
    sk_flow_iter_t     *f_iter,
    skstream_t         *stream,
    ssize_t             err_code,
    void               *cb_data);


/**
 *    Signature of a callback function that may be called when an
 *    event happens on a stream.
 *
 *    If the callback wishes to stop processing the stream, the
 *    callback should use skStreamClose() to close the stream.
 *
 *    The list of events is specified by the sk_flow_iter_cb_type_t
 *    enumeration.  To set a callback, use the
 *    sk_flow_iter_set_stream_event_cb() function.
 */
typedef void
(*sk_flow_iter_cb_event_fn_t)(
    sk_flow_iter_t     *f_iter,
    skstream_t         *stream,
    void               *cb_data);


/**
 *    Specify a callback that is to be invoked when an unexpected
 *    error condition occurs on a stream.
 *
 *    The callback function receives as parameters the 'f_iter'
 *    object, the stream that caused the error, the error code, and
 *    the 'callback_data' object passed to this function.
 *
 *    To clear a callback or to disable the default callback, specify
 *    a 'callback_func' of NULL.  To reset the callback to the
 *    default, specify sk_flow_iter_default_error_cb() as the
 *    'callback_func' and NULL for the 'callback_data'.
 *
 *    Return 0 on success.  Return -1 if 'f_iter' is NULL or if
 *    'callback_type' does not specify a valid error callback type.
 */
int
sk_flow_iter_set_stream_error_cb(
    sk_flow_iter_t             *f_iter,
    sk_flow_iter_cb_type_t      callback_type,
    sk_flow_iter_cb_error_fn_t  callback_func,
    const void                 *callback_data);


/**
 *    Specify a callback that is to be invoked when an event occurs on
 *    a stream.
 *
 *    The callback function receives as parameters the 'f_iter'
 *    object, the stream, and the 'callback_data' object passed to
 *    this function.
 *
 *    To clear a callback, specify a 'callback_func' of NULL.
 *
 *    Return 0 on success.  Return -1 if 'f_iter' is NULL or if
 *    'callback_type' does not specify a valid event callback type.
 */
int
sk_flow_iter_set_stream_event_cb(
    sk_flow_iter_t             *f_iter,
    sk_flow_iter_cb_type_t      callback_type,
    sk_flow_iter_cb_event_fn_t  callback_func,
    const void                 *callback_data);


/**
 *    Use skStreamPrintLastErr() to report an error on 'stream', close
 *    the steram, and return 'err_code'.
 *
 *    This is a potential callback function and the default callback
 *    function for a stream error encountered when opening an input
 *    file.
 */
ssize_t
sk_flow_iter_default_error_open_cb(
    sk_flow_iter_t     *f_iter,
    skstream_t         *stream,
    ssize_t             err_code,
    void               *cb_data);


/**
 *    Use skStreamPrintLastErr() to report an error on 'stream', close
 *    the steram, and return SKSTREAM_ERR_CLOSED.
 *
 *    This is a potential callback function and the default callback
 *    function for a stream error encountered when opening an input
 *    file.
 */
ssize_t
sk_flow_iter_ignore_error_open_cb(
    sk_flow_iter_t     *f_iter,
    skstream_t         *stream,
    ssize_t             err_code,
    void               *cb_data);


/**
 *    Use skStreamPrintLastErr() to report a error on 'stream' and
 *    return 0.
 *
 *    This is a potential callback function and the default callback
 *    function for a stream error encountered when reading records.
 */
ssize_t
sk_flow_iter_default_error_read_cb(
    sk_flow_iter_t     *f_iter,
    skstream_t         *stream,
    ssize_t             err_code,
    void               *cb_data);


#ifdef __cplusplus
}
#endif
#endif /* _SKFLOWITER_H */

/*
** Local Variables:
** mode:c
** indent-tabs-mode:nil
** c-basic-offset:4
** End:
*/
