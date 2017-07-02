/*
** Copyright (C) 2014-2017 by Carnegie Mellon University.
**
** @OPENSOURCE_LICENSE_START@
** See license information in ../../LICENSE.txt
** @OPENSOURCE_LICENSE_END@
*/

/*
 *  skfixstream.h
 *
 *    A wrapper over skstream_t that supports reading and writing
 *    streams of IPFIX records.
 *
 */
#ifndef _SKFIXSTREAM_H
#define _SKFIXSTREAM_H
#ifdef __cplusplus
extern "C" {
#endif

#include <silk/silk.h>

RCSIDENTVAR(rcsID_SKFIXSTREAM_H, "$SiLK: skfixstream.h efd886457770 2017-06-21 18:43:23Z mthomas $");

#include <silk/silk_types.h>
#include <silk/skfixbuf.h>


/**
 *  @file
 *
 *    A wrapper over skstream_t that supports reading and writing
 *    streams of IPFIX records.
 *
 *    This file is part of libsilk.
 */


/**
 *    Signature of a callback function that is invoked when a new
 *    schema is read from an IPFIX input stream.
 *
 *    The callback is set by a call to sk_fixstream_set_schema_cb().
 *
 *    'schema' is the schema that was read from the stream.  'tid' is
 *    the (external) ID of the template.  'cb_data' is a parameter to
 *    hold caller-specific data.
 */
typedef void
(*sk_fixstream_schema_cb_fn_t)(
    sk_schema_t        *schema,
    uint16_t            tid,
    void               *cb_data);


#if 0
/**
 *    Add 'schema' to the internal session for the output stream
 *    'fixstream'.
 *
 *    This function is useful when writing to tell the stream about a
 *    schema that exists in a subTemplateList or subTemplateMultiList
 *    that a record contains.
 *
 *    Return SKSTREAM_OK on success.
 */
int
sk_fixstream_add_schema(
    sk_fixstream_t     *fixstream,
    const sk_schema_t  *schema);
#endif  /* 0 */


/**
 *    Create a new skstream_t whose mode is given by
 *    'read_write_append' (see skStreamCreate()), bind that skstream_t
 *    to 'pathname' (see skStreamBind()), and set 'fixstream' to use
 *    that skstream_t as if sk_fixstream_set_stream() had been called.
 *
 *    To get a handle to this skstream_t object, call
 *    sk_fixstream_get_stream().
 *
 *    To remove a skstream_t from a fixstream, call
 *    sk_fixstream_remove_stream().
 */
int
sk_fixstream_bind(
    sk_fixstream_t     *fixstream,
    const char         *pathname,
    skstream_mode_t     read_write_append);


/**
 *    Close the skstream_t that 'fixstream' wraps.
 */
int
sk_fixstream_close(
    sk_fixstream_t     *fixstream);


/**
 *    Create a new sk_fixstream_t and store it at the location
 *    referenced by 'fixstream'.
 */
int
sk_fixstream_create(
    sk_fixstream_t    **fixstream);


/**
 *    Destroy the sk_fixstream_t whose address in held by 'fixstream'
 *    and set 'fixstream' to NULL.  Do nothing if 'fixstream' or the
 *    address it holds is NULL.
 *
 *    If the skstream_t that 'fixstream' wraps is open, the stream is
 *    closed and destroyed.
 */
void
sk_fixstream_destroy(
    sk_fixstream_t    **fixstream);


/**
 *    Ensure that any records that have been written to 'fixstream'
 *    have been written to wrapped skstream_t and call skStreamFlush()
 *    on that stream.
 */
int
sk_fixstream_flush(
    sk_fixstream_t     *fixstream);


/**
 *    Return the information model being used by 'fixstream'.
 *
 *    Return NULL if no information model has been set and 'fixstream'
 *    is not open.
 */
fbInfoModel_t *
sk_fixstream_get_info_model(
    const sk_fixstream_t   *fixstream);


/**
 *    Return the export time of the most recent IPFIX record read from
 *    'fixstream'.  Return -1 if 'fixstream' is NULL, is not open, or
 *    is closed.
 */
sktime_t
sk_fixstream_get_last_export_time(
    const sk_fixstream_t   *fixstream);


#if 0
/**
 *    Return the current observation domain of 'fixstream'.
 */
uint32_t
sk_fixstream_get_observation_domain(
    const sk_fixstream_t   *fixstream)
#endif  /* 0 */


/**
 *    Return the number of records that have been processed by
 *    'fixstream'.
 */
uint64_t
sk_fixstream_get_record_count(
    const sk_fixstream_t   *fixstream);


#if 0
/**
 *    Return the schema that is used to decode records whose template
 *    is 'tmpl'.
 */
const sk_schema_t *
sk_fixstream_get_schema_from_template(
    const fbTemplate_t *tmpl);
#endif  /* 0 */


/**
 *    Return the skstream_t that 'fixstream' wraps.
 *
 *    To have 'fixstream' wrap a stream, call sk_fixstream_bind() or
 *    sk_fixstream_set_stream().
 *
 *    Return NULL if there is no skstream_t set on 'fixstream'.
 */
skstream_t *
sk_fixstream_get_stream(
    const sk_fixstream_t   *fixstream);


/**
 *    Open the IPFIX stream 'fixstream'.
 *
 *    Prior to this call, an skstream_t must have been set on
 *    'fixstream' by a call to sk_fixstream_set_stream() or
 *    sk_fixstream_bind().
 *
 *    The wrapped 'skstream_t' is opened (see skStreamOpen()) but an
 *    error code of SKSTREAM_ERR_PREV_OPEN is ignored by this
 *    function.
 *
 *    The content type of 'stream' is checked and
 *    SKSTREAM_ERR_UNSUPPORT_CONTENT is returned if the content type
 *    is not SK_CONTENT_OTHERBINARY.
 *
 *    Unless an information model was set on 'fixstream' by a call to
 *    sk_fixstream_set_info_model(), a handle to a shared information
 *    model is set on 'fixstream'.
 */
int
sk_fixstream_open(
    sk_fixstream_t     *fixstream);


/**
 *    Read the next IPFIX record from 'fixstream' and store the
 *    address of that record in the location referenced by 'rec'.
 *
 *    If reading a record results in new templates being read and a
 *    new schema callback was set (see sk_fixstream_set_schema_cb()),
 *    that function is invoked for each template/schema.
 */
int
sk_fixstream_read_record(
    sk_fixstream_t     *fixstream,
    const sk_fixrec_t **rec);


/**
 *    Remove the skstream_t that was set on by 'fixstream' by the call
 *    to sk_fixstream_bind() or sk_fixstream_set_stream().
 *
 *    If IPFIX records have been written to 'fixstream', any pending
 *    records are flushed and 'skstream_t' is flushed.
 *
 *    If the 'stream' parameter is not NULL, its referent is set to
 *    the skstream_t.  Otherwise the wrapped skstream_t is closed and
 *    destroyed.
 *
 *    The return code is SKSTREAM_OK if all calls succeed.  Otherwise
 *    it is the first error code encountered.
 */
int
sk_fixstream_remove_stream(
    sk_fixstream_t     *fixstream,
    skstream_t        **stream);


/**
 *    Set the information model to use on 'fixstream' to 'info_model'.
 *
 *    To use a specific information model, the model must be set by a
 *    call to this function prior to calling sk_fixstream_open().
 */
int
sk_fixstream_set_info_model(
    sk_fixstream_t     *fixstream,
    fbInfoModel_t      *info_model);


/**
 *    Set the observation domain to use on 'fixstream' when writing
 *    IPFIX records to 'domain'.
 */
int
sk_fixstream_set_observation_domain(
    sk_fixstream_t     *fixstream,
    uint32_t            domain);


/**
 *    Set a callback function to invoke when a new schema is read from
 *    'fixstream'.
 *
 *    To remove the callback function, pass NULL as the
 *    'new_schema_cb' parameter.
 */
int
sk_fixstream_set_schema_cb(
    sk_fixstream_t                 *fixstream,
    sk_fixstream_schema_cb_fn_t     new_schema_cb,
    const void                     *callback_data);


/**
 *    Have 'fixstream' use the skstream_t 'stream' for reading or
 *    writing data.  The content type of 'stream' must be
 *    SK_CONTENT_OTHERBINARY, but the content type of 'stream' is not
 *    checked until sk_fixstream_open() is called.
 *
 *    If 'fixstream' already has an skstream_t, return
 *    SKSTREAM_ERR_PREV_DATA.
 *
 *    To get a handle to this skstream_t object, call
 *    sk_fixstream_get_stream().
 *
 *    To remove a skstream_t from a fixstream, call
 *    sk_fixstream_remove_stream().
 */
int
sk_fixstream_set_stream(
    sk_fixstream_t     *fixstream,
    skstream_t         *stream);


/**
 *    Return a string containing the most recent error encountered by
 *    'fixstream'.  The errror buffer is owned by 'fixstream' and must
 *    be considered constant and cannot be freed.
 */
const char *
sk_fixstream_strerror(
    const sk_fixstream_t   *fixstream);


/**
 *    Write the IPFIX record in 'rec' to the stream 'fixstream' using
 *    the template associated with 'schema'.  If 'schema' is NULL, use
 *    the template associated with 'rec'.
 */
int
sk_fixstream_write_record(
    sk_fixstream_t     *fixstream,
    const sk_fixrec_t  *rec,
    const sk_schema_t  *schema);


#ifdef __cplusplus
}
#endif
#endif /* _SKFIXSTREAM_H */

/*
** Local Variables:
** mode:c
** indent-tabs-mode:nil
** c-basic-offset:4
** End:
*/
