/*
** Copyright (C) 2014-2017 by Carnegie Mellon University.
**
** @OPENSOURCE_LICENSE_START@
** See license information in ../../LICENSE.txt
** @OPENSOURCE_LICENSE_END@
*/
#ifndef _SKFIXFORMATTER_H
#define _SKFIXFORMATTER_H
#ifdef __cplusplus
extern "C" {
#endif

#include <silk/silk.h>

RCSIDENTVAR(rcsID_SKFIXFORMATTER_H, "$SiLK: skfixformatter.h efd886457770 2017-06-21 18:43:23Z mthomas $");

/*
 *  skfixformatter.h
 *
 *    IPFIX printing functions
 *
 *
 */

#include <silk/skfixbuf.h>
#include <silk/skschema.h>


/**
 *    The default floating point precision.
 */
#define SK_FIXFORMATTER_DEFAULT_FP_PRECISION 6


/**
 *    Object that takes the caller's records and formats them for
 *    textual output according to a schema that this object maintains.
 */
typedef struct sk_fixformatter_st sk_fixformatter_t;

/**
 *    Object that contains the knowledge to format one field/column.
 */
typedef struct sk_fixformatter_field_st sk_fixformatter_field_t;

/**
 *    Type to specify whether to justify fields to the left or right.
 */
enum sk_fixformatter_lr_en {
    SK_FMTR_LEFT, SK_FMTR_RIGHT
};
typedef enum sk_fixformatter_lr_en sk_fixformatter_lr_t;

/**
 *    A callback function used by fields that are not built-in.  This
 *    callback will be invoked by
 *    sk_fixformatter_record_to_string_extra().
 *
 *    The function should fill 'text_buf' with the value of the field
 *    for the 'rwrec' and 'extra' values passed to
 *    sk_fixformatter_record_to_string_extra().  'text_buf_size' is
 *    the size of the 'text_buf' buffer.  'cb_data' is the
 *    'callback_data' that was specified when the callback was added.
 *
 *    The return value of this function is ignored.
 */
typedef int
(*sk_fixformatter_get_extra_t)(
    const sk_fixrec_t  *rec,
    char               *text_buf,
    size_t              text_buf_size,
    void               *cb_data,
    void               *extra);


/**
 *    Create and return a new formatter object.  The information model
 *    may be NULL.  Exit the program on memory error.
 */
sk_fixformatter_t *
sk_fixformatter_create(
    fbInfoModel_t      *model);

/**
 *    Return the number of fields that the formatter 'fmtr' contains.
 */
size_t
sk_fixformatter_get_field_count(
    const sk_fixformatter_t    *fmtr);

/**
 *    Return the field in 'fmtr' at location 'position'.  Return NULL
 *    if 'position' is greater than the result of
 *    sk_fixformatter_get_field_count().
 */
sk_fixformatter_field_t *
sk_fixformatter_get_field(
    const sk_fixformatter_t    *fmtr,
    size_t                      position);

/**
 *    Release all resources held by the formatter 'fmtr'.  Do nothing
 *    when 'fmtr' is NULL.
 */
void
sk_fixformatter_destroy(
    sk_fixformatter_t  *fmtr);

/**
 *    Prepare the formatter 'fmtr' for producing output.  After
 *    calling this function, 'fmtr' may no longer be modified.
 *
 *    Do nothing if 'fmtr' is already finalized.
 */
int
sk_fixformatter_finalize(
    sk_fixformatter_t  *fmtr);

/**
 *    Use an information element to add a field to the formatter
 *    'fmtr' and return the field.  Return NULL if 'fmtr' has been
 *    finalized or if 'ie' is not in the information model.  Exit the
 *    program on memory allocation error.
 */
sk_fixformatter_field_t *
sk_fixformatter_add_ie(
    sk_fixformatter_t      *fmtr,
    const fbInfoElement_t  *ie);

/**
 *    Add a virtual field based on a callback function that accepts an
 *    additional argument, which is the 'extra' argument passed to the
 *    sk_fixformatter_record_to_string_extra() function.
 */
sk_fixformatter_field_t *
sk_fixformatter_add_extra_field(
    sk_fixformatter_t              *fmtr,
    sk_fixformatter_get_extra_t     get_value_extra_fn,
    void                           *callback_data,
    size_t                          min_width);

/**
 *    Add the fields from 'schema' to the formatter 'fmtr'.  Return 0
 *    on success.  Return -1 on error.
 */
int
sk_fixformatter_add_from_schema(
    sk_fixformatter_t  *fmtr,
    const sk_schema_t  *schema);

/**
 *    Fill 'output_buffer' with a formatted representation of the data
 *    in 'record' using the formatter 'fmtr'.  The caller must use the
 *    'buffer_size' parameter to pass in the available size of
 *    'output_buffer'.  Return the number of characters written to
 *    'output_buffer'.  If 'buffer_size' is too small, return the
 *    number of characters that would have been written if
 *    'output_buffer' was large enough.
 *
 *    Return 0 on error.  Errors include 'fmtr' not being finalzed and
 *    the inability to map 'record' into the schema used by 'fmtr'.
 */
size_t
sk_fixformatter_record_to_string(
    sk_fixformatter_t  *fmtr,
    const sk_fixrec_t  *record,
    char              **output_buffer);

/**
 *    Identical to sk_fixformatter_record_to_string() except for an
 *    'extra' argument, which is used by fields that were added by
 *    sk_fixformatter_add_extra_field().
 */
size_t
sk_fixformatter_record_to_string_extra(
    sk_fixformatter_t  *fmtr,
    const sk_fixrec_t  *record,
    void               *extra,
    char              **output_buffer);


/**
 *    Fill 'output_buffer' with title line for the fields known to the
 *    formatter 'fmtr'.  The caller must use the 'buffer_size'
 *    parameter to pass in the available size of 'output_buffer'.
 *    Return the number of characters written to 'output_buffer'.  If
 *    'buffer_size' is too small, return the number of characters that
 *    would have been written if 'output_buffer' was large enough.
 *
 *    Return 0 on error.  Errors include 'fmtr' not being finalzed.
 */
size_t
sk_fixformatter_fill_title_buffer(
    sk_fixformatter_t  *fmtr,
    char              **output_buffer);

/**
 *    Set the delimiter that 'fmtr' is to put between columns to
 *    'delimiter'.
 *
 *    Do nothing if 'fmtr' has been finalized.
 */
void
sk_fixformatter_set_delimeter(
    sk_fixformatter_t  *fmtr,
    char                delimeter);

/**
 *    Tell 'fmtr' not to produce columnar output.  In addition,
 *    enables complete titles.
 *
 *    Do nothing if 'fmtr' has been finalized.
 */
void
sk_fixformatter_set_no_columns(
    sk_fixformatter_t  *fmtr);

/**
 *    Tell 'fmtr' to produce complete title names.  Typically column
 *    names are shorted to the width necessary to hold all possible
 *    values for the field.
 *
 *    Do nothing if 'fmtr' has been finalized.
 */
void
sk_fixformatter_set_full_titles(
    sk_fixformatter_t  *fmtr);

/**
 *    Tell 'fmtr' not to include a delimiter after the final field.
 *
 *    Do nothing if 'fmtr' has been finalized.
 */
void
sk_fixformatter_set_no_final_delimeter(
    sk_fixformatter_t  *fmtr);

/**
 *    Tell 'fmtr' not to include a newline in the output buffer for
 *    sk_fixformatter_fill_title_buffer() and
 *    sk_fixformatter_record_to_string().
 *
 *    Do nothing if 'fmtr' has been finalized.
 */
void
sk_fixformatter_set_no_final_newline(
    sk_fixformatter_t   *fmtr);

/**
 *    Tell 'fmtr' to produce an emty column for 'field'.
 *
 *    Do nothing if 'fmtr' has been finalized or when 'field' is not
 *    in 'fmtr'.
 */
void
sk_fixformatter_field_set_empty(
    const sk_fixformatter_t    *fmtr,
    sk_fixformatter_field_t    *field);

/**
 *    Tell 'fmtr' to pass 'flags' to the IP address formatting
 *    function when wrting 'field'.
 *
 *    Do nothing if 'fmtr' has been finalized or when 'field' is not
 *    in 'fmtr'.
 */
void
sk_fixformatter_field_set_ipaddr_format(
    const sk_fixformatter_t    *fmtr,
    sk_fixformatter_field_t    *field,
    skipaddr_flags_t            flags);

/**
 *    Tell 'fmtr' whether 'field' should be left or right justified.
 *
 *    Do nothing if 'fmtr' has been finalized or when 'field' is not
 *    in 'fmtr'.
 */
void
sk_fixformatter_field_set_justification(
    const sk_fixformatter_t    *fmtr,
    sk_fixformatter_field_t    *field,
    sk_fixformatter_lr_t        left_or_right);


/**
 *    Tell 'fmtr' to use 'max_width' as the maximum column width for
 *    'field'.
 *
 *    Do nothing if 'fmtr' has been finalized or when 'field' is not
 *    in 'fmtr'.
 */
void
sk_fixformatter_field_set_max_width(
    const sk_fixformatter_t    *fmtr,
    sk_fixformatter_field_t    *field,
    size_t                      max_width);

/**
 *    Tell 'fmtr' to use 'min_width' as the minimum column width for
 *    'field'.
 *
 *    Do nothing if 'fmtr' has been finalized or when 'field' is not
 *    in 'fmtr'.
 */
void
sk_fixformatter_field_set_min_width(
    const sk_fixformatter_t    *fmtr,
    sk_fixformatter_field_t    *field,
    size_t                      min_width);

/**
 *    Tell 'fmtr' to use 'base' as the format for the number field
 *    'field'.  Currently 'base' is interrupted as base and only the
 *    values 10 and 16 are supported.
 *
 *    Do nothing if 'fmtr' has been finalized or when 'field' is not
 *    in 'fmtr'.
 */
void
sk_fixformatter_field_set_number_format(
    const sk_fixformatter_t    *fmtr,
    sk_fixformatter_field_t    *field,
    uint8_t                     base);

/**
 *    Tell 'fmtr' to use 'precision' as the precision for the floating
 *    point number in 'field'.
 *
 *    Do nothing if 'fmtr' has been finalized or when 'field' is not
 *    in 'fmtr'.
 */
void
sk_fixformatter_field_set_precision(
    const sk_fixformatter_t    *fmtr,
    sk_fixformatter_field_t    *field,
    uint8_t                     precision);

/**
 *    Tell 'fmtr' to use padding when printing the value in 'field'.
 *    This setting is used when printing TCP flags field so that the
 *    various flag characters always appear in the same column.
 *
 *    Do nothing if 'fmtr' has been finalized or when 'field' is not
 *    in 'fmtr'.
 */
void
sk_fixformatter_field_set_space_padded(
    const sk_fixformatter_t    *fmtr,
    sk_fixformatter_field_t    *field);

/**
 *    Tell 'fmtr' to pass 'flags' to the timestamp formatting function
 *    when wrting 'field'.
 *
 *    Do nothing if 'fmtr' has been finalized or when 'field' is not
 *    in 'fmtr'.
 */
void
sk_fixformatter_field_set_timestamp_format(
    const sk_fixformatter_t    *fmtr,
    sk_fixformatter_field_t    *field,
    uint32_t                    flags);

/**
 *    Tell 'fmtr' to use 'title' as the title for 'field'.
 *
 *    Do nothing if 'fmtr' has been finalized or when 'field' is not
 *    in 'fmtr'.
 */
void
sk_fixformatter_field_set_title(
    const sk_fixformatter_t    *fmtr,
    sk_fixformatter_field_t    *field,
    const char                 *title);

#if 0

/**
 *    UNUSED
 */
size_t
sk_fixformatter_default_print_len(
    const sk_field_t                   *ie,
    const sk_fixformatter_field_t      *fmtr,
    void                        UNUSED(*ctx));

/**
 *    UNUSED
 */
size_t
sk_fixformatter_default_print(
    char                               *buf,
    size_t                              len,
    const sk_field_t                   *ie,
    const sk_fixformatter_field_t      *fmtr,
    const sk_fixrec_t                  *rec,
    void                        UNUSED(*ctx));

/**
 *    UNUSED
 */
sk_fixformatter_t *
sk_fixformatter_clone(
    const sk_fixformatter_t    *src);

/**
 *    UNUSED
 */
void
sk_fixformatter_field_set_default(
    sk_fixformatter_field_t    *fmtr,
    uint8_t                     typ,
    uint8_t                     sem);

#endif  /* 0 */


#ifdef __cplusplus
}
#endif
#endif /* _SKFIXFORMATTER_H */

/*
** Local Variables:
** mode:c
** indent-tabs-mode:nil
** c-basic-offset:4
** End:
*/
