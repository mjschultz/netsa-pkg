/*
** Copyright (C) 2015-2017 by Carnegie Mellon University.
**
** @OPENSOURCE_LICENSE_START@
** See license information in ../../LICENSE.txt
** @OPENSOURCE_LICENSE_END@
*/
#ifndef _SKLEB128_H
#define _SKLEB128_H
#ifdef __cplusplus
extern "C" {
#endif

#include <silk/silk.h>

RCSIDENTVAR(rcsID_SKLEB128_H, "$SiLK: skleb128.h efd886457770 2017-06-21 18:43:23Z mthomas $");

/*
 *  skleb128.h
 *
 *    Functions to serialize and deserialize integers using Little
 *    Endian Base-128 (LEB128) encoding.
 *
 *    Mark Thomas
 *    August 2015
 *
 */

/**
 *  @file
 *
 *    Functions to serialize and deserialize unsigned or signed
 *    integers using Little Endian Base-128 (LEB128) variable-length
 *    code compression.
 *
 *    The encoding for signed and unsigned values produces different
 *    results, so it is important to use the correct decoding function
 *    for the type of encoding that was used.
 *
 *    The encoding functions take a value, a buffer to fill, and the
 *    length of the buffer.  They return the number of octets written
 *    to the buffer, or 0 when the buffer is too short.
 *
 *    The decoding functions take a buffer, its length, and a result
 *    parameter and return the decoded value.  The length parameter is
 *    only used to ensure that attempting to decode invalid data does
 *    not read beyond the end of the buffer.  The result parameter is
 *    set to the number of octets that were read while decoding the
 *    value or to a value larger than the buffer size on error.
 *
 *    This file is part of libsilk.
 */


/**
 *    The maximum number of octets required to encode an 8 bit integer
 *    (signed or unsigned) using Little Endian Base-128 (LEB128)
 *    encoding.
 */
#define SK_LEB128_REQUIRE_INT_8     2

/**
 *    The maximum number of octets required to encode a 16 bit integer
 *    (signed or unsigned) using Little Endian Base-128 (LEB128)
 *    encoding.
 */
#define SK_LEB128_REQUIRE_INT_16    2

/**
 *    The maximum number of octets required to encode a 32 bit integer
 *    (signed or unsigned) using Little Endian Base-128 (LEB128)
 *    encoding.
 */
#define SK_LEB128_REQUIRE_INT_32    5

/**
 *    The maximum number of octets required to encode a 64 bit integer
 *    (signed or unsigned) using Little Endian Base-128 (LEB128)
 *    encoding.
 */
#define SK_LEB128_REQUIRE_INT_64    10


/**
 *    Encode the unsigned number 'value' into the buffer 'leb128',
 *    whose length is 'leb128_len', using Unsigned Little Endian
 *    Base-128 (LEB128) encoding.  Return the number of octets written
 *    to 'leb128' or return 0 if 'leb128_len' is smaller than the
 *    number of octets required.
 *
 *    To see the number of octets required for a particular unsigned
 *    value, use sk_leb128_required_unsigned().  The encoding always
 *    succeeds when 'leb128_len' is not less than
 *    SK_LEB128_REQUIRE_INT_64.
 *
 *    Use sk_leb128_decode_unsigned() to decode the value.
 *
 *    See also sk_leb128_encode_signed().
 */
size_t
sk_leb128_encode_unsigned(
    uint64_t            value,
    uint8_t            *leb128,
    size_t              leb128_len);

/**
 *    Decode the buffer 'leb128' containing an Unsigned Little Endian
 *    Base-128 (LEB128) encoding of an unsigned number and return the
 *    number.
 *
 *    Process octets in 'leb128' until either the value is decoded or
 *    'max_consume' octets of 'leb128' are consumed.  When the result
 *    parameter 'consumed' is provided, the function sets the value it
 *    references to the number of octets consumed.
 *
 *    When the end of the buffer is reached before decoding is
 *    complete, the function sets 'consumed' to a value larger than
 *    'max_consume' and returns UINT64_MAX.
 *
 *    This function decodes a value encoded by
 *    sk_leb128_encode_unsigned().
 */
uint64_t
sk_leb128_decode_unsigned(
    const uint8_t      *leb128,
    size_t              max_consume,
    size_t             *consumed);

/**
 *    Return the number of octets required to encode the unsigned
 *    number 'value' using Unsigned Little Endian Base-128 (LEB128)
 *    encoding.
 */
size_t
sk_leb128_required_unsigned(
    uint64_t            value);


/**
 *    Encode the signed number 'value' into the buffer 'leb128', whose
 *    length is 'leb128_len', using Little Endian Base-128 (LEB128)
 *    encoding.  Return the number of octets written to 'leb128' or
 *    return 0 if 'leb128_len' is smaller than the number of octets
 *    required.
 *
 *    To see the number of octets required for a particular signed
 *    value, use sk_leb128_required_signed().  The encoding always
 *    succeeds when 'leb128_len' is not less than
 *    SK_LEB128_REQUIRE_INT_64.
 *
 *    Use sk_leb128_decode_signed() to decode the value.
 *
 *    See also sk_leb128_encode_unsigned().
 */
size_t
sk_leb128_encode_signed(
    int64_t             value,
    uint8_t            *leb128,
    size_t              leb128_len);

/**
 *    Decode the buffer 'leb128' containing a Little Endian Base-128
 *    (LEB128) encoding of an signed number and return the number.
 *    Process octets in 'leb128' until either the value is decoded or
 *    'max_consume' octets of 'leb128' are consumed.  When the result
 *    parameter 'consumed' is provided, the function sets the value it
 *    references to the number of octets consumed.
 *
 *    When the end of the buffer is reached before decoding is
 *    complete, the function sets 'consumed' to a value larger than
 *    'max_consume' and returns INT64_MAX.
 *
 *    This function decodes a value encoded by
 *    sk_leb128_encode_signed().
 */
int64_t
sk_leb128_decode_signed(
    const uint8_t      *leb128,
    size_t              max_consume,
    size_t             *consumed);

/**
 *    Return the number of octets required to encode the signed number
 *    'value' using Little Endian Base-128 (LEB128) encoding.
 */
size_t
sk_leb128_required_signed(
    int64_t             value);

#ifdef __cplusplus
}
#endif
#endif /* _SKLEB128_H */

/*
** Local Variables:
** mode:c
** indent-tabs-mode:nil
** c-basic-offset:4
** End:
*/
