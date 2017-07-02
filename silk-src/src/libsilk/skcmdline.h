/*
** Copyright (C) 2014-2017 by Carnegie Mellon University.
**
** @OPENSOURCE_LICENSE_START@
** See license information in ../../LICENSE.txt
** @OPENSOURCE_LICENSE_END@
*/
#ifndef _SKCMDLINE_H
#define _SKCMDLINE_H
#ifdef __cplusplus
extern "C" {
#endif
/*
 *  skcmdline.h
 *
 *    Structures and functions for handling the command line.
 *
 *    Mark Thomas
 *    November 2014
 *
 */

#include <silk/silk.h>

RCSIDENTVAR(rcsID_SKCMDLINE_H, "$SiLK: skcmdline.h efd886457770 2017-06-21 18:43:23Z mthomas $");

#include <silk/silk_types.h>
#include <silk/skfixbuf.h>
#include <silk/utils.h>


/**
 *    A structure to process each input stream to find each unique
 *    sk_field_t (Information Element) that exists in the input.
 *
 *    Create the object with skcli_input_fields_create().  Use
 *    skcli_input_fields_populate() to process the input fields.
 *
 *    Create and use skcli_input_fields_iter_t object as described
 *    next.
 *
 *    When finished with the iterator, call
 *    skcli_input_fields_iter_destroy() to destroy the iterator and
 *    skcli_input_fields_destroy() to free all memory associated with
 *    the skcli_input_fields_t.
 */
typedef struct skcli_input_fields_st skcli_input_fields_t;


/**
 *    An iterator to visit each of the unique fields seen by
 *    skcli_input_fields_populate().
 *
 *    Create the iterator with skcli_input_fields_iter_create(); use
 *    skcli_input_fields_iter_next() to return each unique sk_field_t
 *    object; and call skcli_input_fields_iter_destroy() to destroy
 *    the iterator.
 */
typedef struct skcli_input_fields_iter_st skcli_input_fields_iter_t;

/**
 *    Signature of a callback function that is invoked when a new
 *    schema is read by skcli_input_fields_populate().
 *
 *    The callback is set by calling
 *    skcli_input_fields_set_schema_callback().
 *
 *    'schema' is the schema that was read from the stream.  The
 *    schema is frozen before the callback is invoked.  'tid' is the
 *    (external) ID of the template.  'cb_data' is a parameter to hold
 *    caller-specific data.
 */
typedef void
(*skcli_input_fields_schema_cb_fn_t)(
    sk_schema_t        *schema,
    uint16_t            tid,
    void               *cb_data);


/**
 *    Return the number of unique input_fields seen in all the input
 *    streams processed by skcli_input_fields_populate().
 */
size_t
skcli_input_fields_count_fields(
    skcli_input_fields_t   *input_fields);

/**
 *    Return the number of input streams processed by
 *    skcli_input_fields_populate().
 */
size_t
skcli_input_fields_count_streams(
    skcli_input_fields_t   *input_fields);

/**
 *    Return the number of templates seen in the input streams
 *    processed by skcli_input_fields_populate().
 */
size_t
skcli_input_fields_count_templates(
    skcli_input_fields_t   *input_fields);

/**
 *    Allocate an skcli_input_fields_t structure and store it in the
 *    location referenced by 'input_fields'.  Exit the program if a
 *    structure cannot be allocated.
 */
int
skcli_input_fields_create(
    skcli_input_fields_t  **input_fields);

/**
 *    Destroy the skcli_input_fields_t structure 'input_fields'.  Do
 *    nothing if 'input_fields' is NULL.
 */
void
skcli_input_fields_destroy(
    skcli_input_fields_t   *input_fields);

/**
 *    Process all of the input streams referenced by 'options_ctx' and
 *    get a count of streams, templates, and unique fields seen in
 *    those input files.
 */
int
skcli_input_fields_populate(
    skcli_input_fields_t   *input_fields,
    sk_options_ctx_t       *options_ctx,
    fbInfoModel_t          *info_model);

/**
 *    Set a callback function 'cb_func' that 'input_fields' is to
 *    invoke on very new schema seen during the execution of
 *    skcli_input_fields_populate().  The 'cb_data' paramater is
 *    passed as the third argument to 'cb_func'.
 */
int
skcli_input_fields_set_schema_callback(
    skcli_input_fields_t               *input_fields,
    skcli_input_fields_schema_cb_fn_t   cb_func,
    void                               *cb_data);

/**
 *    Create an iterator to visit each of the unique fields seen by
 *    'input_fields' during the execution of
 *    skcli_input_fields_populate() and store the iterator in the
 *    location referenced by 'iter'.
 *
 *    Use 'skcli_input_fields_iter_next() to visit each of the
 *    sk_field_t objects.
 */
int
skcli_input_fields_iter_create(
    skcli_input_fields_iter_t **iter,
    const skcli_input_fields_t *input_fields);

/**
 *    Destroy the input fields iterator 'iter'.  Do nothing if 'iter'
 *    is NULL.
 */
void
skcli_input_fields_iter_destroy(
    skcli_input_fields_iter_t  *iter);

/**
 *    Return the next unique field.  Return NULL if there are no more
 *    fields.
 */
const sk_field_t *
skcli_input_fields_iter_next(
    skcli_input_fields_iter_t  *iter);



#ifdef __cplusplus
}
#endif
#endif /* _SKCMDLINE_H */

/*
** Local Variables:
** mode:c
** indent-tabs-mode:nil
** c-basic-offset:4
** End:
*/
