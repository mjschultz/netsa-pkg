/*
** Copyright (C) 2014-2017 by Carnegie Mellon University.
**
** @OPENSOURCE_LICENSE_START@
** See license information in ../../LICENSE.txt
** @OPENSOURCE_LICENSE_END@
*/
#ifndef _SKFORMAT_H
#define _SKFORMAT_H
#ifdef __cplusplus
extern "C" {
#endif

#include <silk/silk.h>

RCSIDENTVAR(rcsID_SKFORMAT_H, "$SiLK: skformat.h efd886457770 2017-06-21 18:43:23Z mthomas $");

/*
 *  skformat.h
 *
 *    IPFIX printing functions
 *
 *
 */

#include <silk/rwrec.h>
#include <silk/sksidecar.h>


/**
 *    The default floating point precision.
 */
#define SK_FORMATTER_DEFAULT_FP_PRECISION 6


/**
 *    Object that takes the caller's records and formats them for
 *    textual output according to configuration that this object
 *    maintains.
 */
typedef struct sk_formatter_st sk_formatter_t;

/**
 *    Object that contains the knowledge to format one field/column.
 */
typedef struct sk_formatter_field_st sk_formatter_field_t;

/**
 *    Type to specify whether to justify fields to the left or right.
 */
enum sk_formatter_lr_en {
    SK_FMTR_LEFT, SK_FMTR_RIGHT
};
typedef enum sk_formatter_lr_en sk_formatter_lr_t;

/**
 *    The signature of a callback function that may be used to format
 *    a column that the formatter does not support.
 *
 *    To register a column that the formatter does not support, call
 *    sk_formatter_add_extra_field() and pass a function with the
 *    following signature as the 'get_value_extra_fn' argument.  To
 *    include that column in the output, call
 *    sk_formatter_record_to_string_extra() which invokes the function
 *    registered with this signature.
 *
 *    When this callback function is invoked, the function should fill
 *    'text_buf' with the value of the field for the 'rwrec' and
 *    'extra' values passed to sk_formatter_record_to_string_extra()
 *    and return the number of characters that would be written to
 *    'text_buf' if the size of that buffer were infinite.  The return
 *    value should not include the terminating '\0'.  'text_buf_size'
 *    is the size of the 'text_buf' buffer.  'cb_data' is the
 *    'callback_data' that was specified when the callback was added
 *    by sk_formatter_add_extra_field().
 */
typedef int (*sk_formatter_field_extra_t)(
    const rwRec *rwrec,
    char        *text_buf,
    size_t       text_buf_size,
    void        *cb_data,
    void        *extra);


/**
 *    Create and return a new formatter object.  Exit the program on
 *    memory error.
 */
sk_formatter_t *
sk_formatter_create(
    void);

/**
 *    Return the number of fields that the formatter 'fmtr' contains.
 */
size_t
sk_formatter_get_field_count(
    const sk_formatter_t   *fmtr);

/**
 *    Return the field in 'fmtr' at location 'position'.  The first
 *    column is position 0.  The final column is one less than the
 *    value returned by sk_formatter_get_field_count().  Return NULL
 *    if 'position' is not less than the result of
 *    sk_formatter_get_field_count().
 */
sk_formatter_field_t *
sk_formatter_get_field(
    const sk_formatter_t   *fmtr,
    size_t                  position);

/**
 *    Release all resources held by the formatter 'fmtr'.  Do nothing
 *    when 'fmtr' is NULL.
 */
void
sk_formatter_destroy(
    sk_formatter_t     *fmtr);

/**
 *    Prepare the formatter 'fmtr' for producing output.  After
 *    calling this function, 'fmtr' may no longer be modified.
 *
 *    Do nothing if 'fmtr' is already finalized.
 */
int
sk_formatter_finalize(
    sk_formatter_t     *fmtr);

/**
 *    Add the standard SiLK rwRec field 'id' to the formatter 'fmtr'
 *    and return the field.  Return NULL if 'fmtr' has been finalized.
 *    Exit the program on memory allocation error.
 */
sk_formatter_field_t *
sk_formatter_add_silk_field(
    sk_formatter_t     *fmtr,
    rwrec_field_id_t    id);

/**
 *    Add a field having the specified 'name' (of length
 *    'namelen'---which includes the terminating '\0'), 'data_type',
 *    and IPFIX element ID 'ident' to the formatter 'fmtr' and return
 *    the field.  Return NULL if 'fmtr' has been finalized or if a
 *    value is invalid.  Exit the program on memory allocation error.
 */
sk_formatter_field_t *
sk_formatter_add_field(
    sk_formatter_t         *fmtr,
    const char             *name,
    size_t                  namelen,
    sk_sidecar_type_t       data_type,
    sk_field_ident_t        ident);

/**
 *    Add a virtual field based on the callback function specified in
 *    'get_value_extra_fn'.
 *
 *    The callback function is given the current record, a buffer to
 *    fill and the size of the buffer, the 'callback_data' parameter
 *    specified by this function, and another argument which is the
 *    'extra' argument passed to the
 *    sk_formatter_record_to_string_extra() function.
 */
sk_formatter_field_t *
sk_formatter_add_extra_field(
    sk_formatter_t             *fmtr,
    sk_formatter_field_extra_t  get_value_extra_fn,
    void                       *callback_data,
    size_t                      min_width);

/**
 *    Add the fields from 'sidecar' to the formatter 'fmtr'.  Return 0
 *    on success.  Return -1 on error.
 */
int
sk_formatter_add_from_sidecar(
    sk_formatter_t     *fmtr,
    const sk_sidecar_t *sidecar);

/**
 *    Set the location referenced by 'output_buffer' to a character
 *    array containing a formatted representation of the data in
 *    'record' using the formatter 'fmtr'.
 *
 *    Return one more than the strlen() of the string stored in the
 *    location referenced by 'output_buffer'.
 *
 *    Return 0 on if 'fmtr' has not been finalzed.
 *
 *    The character array is owned by the formatter, and that array is
 *    resized as needed to hold the output.  The character array
 *    becomes invalid on the next call to
 *    sk_formatter_record_to_string(),
 *    sk_formatter_record_to_string_extra(),
 *    sk_formatter_fill_title_buffer(), or sk_formatter_destroy().
 */
size_t
sk_formatter_record_to_string(
    sk_formatter_t     *fmtr,
    const rwRec        *record,
    char              **output_buffer);

/**
 *    Identical to sk_formatter_record_to_string() except for an
 *    'extra' argument, which is used by fields that were added by
 *    sk_formatter_add_extra_field().
 */
size_t
sk_formatter_record_to_string_extra(
    sk_formatter_t     *fmtr,
    const rwRec        *record,
    void               *extra,
    char              **output_buffer);


/**
 *    Set the location referenced by 'output_buffer' to a character
 *    array containing the title line for the fields known to the
 *    formatter 'fmtr'.
 *
 *    Return one more than the strlen() of the string stored in the
 *    location referenced by 'output_buffer'.
 *
 *    Return 0 on error.  Errors include 'fmtr' not being finalzed.
 *
 *    The character array is owned by the formatter, and that array is
 *    resized as needed to hold the output.  The character array
 *    becomes invalid on the next call to
 *    sk_formatter_record_to_string(),
 *    sk_formatter_record_to_string_extra(),
 *    sk_formatter_fill_title_buffer(), or sk_formatter_destroy().
 */
size_t
sk_formatter_fill_title_buffer(
    sk_formatter_t     *fmtr,
    char              **output_buffer);

/**
 *    Set the delimiter that 'fmtr' is to put between columns to
 *    'delimiter'.
 *
 *    Do nothing if 'fmtr' has been finalized.
 */
void
sk_formatter_set_delimeter(
    sk_formatter_t     *fmtr,
    char                delimeter);

/**
 *    Tell 'fmtr' not to produce columnar output.  In addition,
 *    enables complete titles.
 *
 *    Do nothing if 'fmtr' has been finalized.
 */
void
sk_formatter_set_no_columns(
    sk_formatter_t     *fmtr);

/**
 *    Tell 'fmtr' to produce complete title names.  Typically column
 *    names are shorted to the width necessary to hold all possible
 *    values for the field.
 *
 *    Do nothing if 'fmtr' has been finalized.
 */
void
sk_formatter_set_full_titles(
    sk_formatter_t     *fmtr);

/**
 *    Tell 'fmtr' not to include a delimiter after the final field.
 *
 *    Do nothing if 'fmtr' has been finalized.
 */
void
sk_formatter_set_no_final_delimeter(
    sk_formatter_t     *fmtr);

/**
 *    Tell 'fmtr' not to include a newline in the output buffer for
 *    sk_formatter_fill_title_buffer() and
 *    sk_formatter_record_to_string().
 *
 *    Do nothing if 'fmtr' has been finalized.
 */
void
sk_formatter_set_no_final_newline(
    sk_formatter_t     *fmtr);

/**
 *    Set the default format for the IP addresses printed by 'fmtr'.
 *    The format for an individual field may be changed from the
 *    default by calling sk_formatter_field_set_ipaddr_format().
 *
 *    Do nothing if 'fmtr' has been finalized.
 */
void
sk_formatter_set_default_ipaddr_format(
    sk_formatter_t     *fmtr,
    skipaddr_flags_t    flags);

/**
 *    Set the default width of any column containing IP addresses on
 *    the assumption that the IP addresses are IPv4.
 */
void
sk_formatter_set_assume_ipv4_ips(
    sk_formatter_t     *fmtr);

/**
 *    Set the default format for the IP addresses printed by 'fmtr'.
 *    The format for an individual field may be changed from the
 *    default by calling sk_formatter_field_set_timestamp_format().
 *
 *    Do nothing if 'fmtr' has been finalized.
 */
void
sk_formatter_set_default_timestamp_format(
    sk_formatter_t     *fmtr,
    uint32_t            flags);

/**
 *    Tell 'fmtr' to produce an emty column for 'field'.
 *
 *    Do nothing if 'fmtr' has been finalized or when 'field' is not
 *    in 'fmtr'.
 */
void
sk_formatter_field_set_empty(
    const sk_formatter_t   *fmtr,
    sk_formatter_field_t   *field);

/**
 *    Tell 'fmtr' to set the column width of 'field' such that the
 *    complete title of the column is visible.
 *
 *    Do nothing if 'fmtr' has been finalized or when 'field' is not
 *    in 'fmtr'.
 */
void
sk_formatter_field_set_full_title(
    const sk_formatter_t   *fmtr,
    sk_formatter_field_t   *field);

/**
 *    Tell 'fmtr' to pass 'flags' to the IP address formatting
 *    function when wrting 'field'.
 *
 *    Do nothing if 'fmtr' has been finalized or when 'field' is not
 *    in 'fmtr'.
 */
void
sk_formatter_field_set_ipaddr_format(
    const sk_formatter_t   *fmtr,
    sk_formatter_field_t   *field,
    skipaddr_flags_t        flags);

/**
 *    Tell 'fmtr' whether 'field' should be left or right justified.
 *
 *    Do nothing if 'fmtr' has been finalized or when 'field' is not
 *    in 'fmtr'.
 */
void
sk_formatter_field_set_justification(
    const sk_formatter_t   *fmtr,
    sk_formatter_field_t   *field,
    sk_formatter_lr_t       left_or_right);


/**
 *    Tell 'fmtr' to use 'max_width' as the maximum column width for
 *    'field'.
 *
 *    Do nothing if 'fmtr' has been finalized or when 'field' is not
 *    in 'fmtr'.
 */
void
sk_formatter_field_set_max_width(
    const sk_formatter_t   *fmtr,
    sk_formatter_field_t   *field,
    size_t                  max_width);

/**
 *    Tell 'fmtr' to use 'min_width' as the minimum column width for
 *    'field'.
 *
 *    Do nothing if 'fmtr' has been finalized or when 'field' is not
 *    in 'fmtr'.
 */
void
sk_formatter_field_set_min_width(
    const sk_formatter_t   *fmtr,
    sk_formatter_field_t   *field,
    size_t                  min_width);

/**
 *    Tell 'fmtr' to use 'base' as the format for the number field
 *    'field'.  Currently 'base' is interpreted as base and only the
 *    values 10 and 16 are supported.
 *
 *    Do nothing if 'fmtr' has been finalized or when 'field' is not
 *    in 'fmtr'.
 */
void
sk_formatter_field_set_number_format(
    const sk_formatter_t   *fmtr,
    sk_formatter_field_t   *field,
    uint8_t                 base);

/**
 *    Tell 'fmtr' to use 'precision' as the precision for the floating
 *    point number in 'field'.
 *
 *    Do nothing if 'fmtr' has been finalized or when 'field' is not
 *    in 'fmtr'.
 */
void
sk_formatter_field_set_precision(
    const sk_formatter_t   *fmtr,
    sk_formatter_field_t   *field,
    uint8_t                 precision);

/**
 *    Tell 'fmtr' to use padding when printing the value in 'field'.
 *    This setting is used when printing TCP flags field so that the
 *    various flag characters always appear in the same column.
 *
 *    Do nothing if 'fmtr' has been finalized or when 'field' is not
 *    in 'fmtr'.
 */
void
sk_formatter_field_set_space_padded(
    const sk_formatter_t   *fmtr,
    sk_formatter_field_t   *field);

/**
 *    Tell 'fmtr' to pass 'flags' to the timestamp formatting function
 *    when wrting 'field'.
 *
 *    Do nothing if 'fmtr' has been finalized or when 'field' is not
 *    in 'fmtr'.
 */
void
sk_formatter_field_set_timestamp_format(
    const sk_formatter_t   *fmtr,
    sk_formatter_field_t   *field,
    uint32_t                flags);

/**
 *    Tell 'fmtr' to use 'title' as the title for 'field'.
 *
 *    Do nothing if 'fmtr' has been finalized or when 'field' is not
 *    in 'fmtr'.
 */
void
sk_formatter_field_set_title(
    const sk_formatter_t   *fmtr,
    sk_formatter_field_t   *field,
    const char             *title);

#ifdef __cplusplus
}
#endif
#endif /* _SKFORMAT_H */

/*
** Local Variables:
** mode:c
** indent-tabs-mode:nil
** c-basic-offset:4
** End:
*/
