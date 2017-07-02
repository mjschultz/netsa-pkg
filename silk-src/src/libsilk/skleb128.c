/*
** Copyright (C) 2015-2017 by Carnegie Mellon University.
**
** @OPENSOURCE_LICENSE_START@
** See license information in ../../LICENSE.txt
** @OPENSOURCE_LICENSE_END@
*/

/*
 *  skleb128.c
 *
 *    Functions to serialize and deserialize integers using Little
 *    Endian Base-128 (LEB128) encoding.
 *
 *    Mark Thomas
 *    August 2015
 *
 */

#include <silk/silk.h>

RCSIDENT("$SiLK: skleb128.c efd886457770 2017-06-21 18:43:23Z mthomas $");

#include <silk/skleb128.h>


/* LOCAL DEFINES AND TYPEDEFS */

/**
 *    Location of the sign bit when using the signed version of the
 *    LEB128 encoding.
 */
#define SK_LEB128_SIGN_BIT          0x40


/* FUNCTION DEFINITIONS */

/* Encode unsigned 'value' into the buffer 'leb128'. Return 0 if
 * leb128 is short. */
size_t
sk_leb128_encode_unsigned(
    uint64_t            value,
    uint8_t            *leb128,
    size_t              leb128_len)
{
    uint8_t *b;

    assert(leb128);

    for (b = leb128; leb128_len; ++b, --leb128_len) {
        if (value < 0x80) {
            *b = (uint8_t)value;
            return 1u + (b - leb128);
        }
        *b = 0x80 | (value & 0x7f);
        value >>= 7;
    }
    return 0;
}

/* Decode and return unsigned value in 'leb128'. Put number of bytes
 * processed into 'consumed'. */
uint64_t
sk_leb128_decode_unsigned(
    const uint8_t      *leb128,
    size_t              max_consume,
    size_t             *consumed)
{
    unsigned int shift;
    uint64_t value;
    const uint8_t *b;

    assert(leb128);

    value = 0;
    for (b = leb128, shift = 0; max_consume; ++b, --max_consume, shift += 7) {
        value |= ((uint64_t)((*b) & 0x7f)) << shift;
        if (0 == ((*b) & 0x80)) {
            if (consumed) {
                *consumed = 1u + (b - leb128);
            }
            return value;
        }
    }
    if (consumed) {
        *consumed = 1u + (b - leb128);
    }
    return UINT64_MAX;
}

/* Return octet count required to encode unsigned 'value'. */
size_t
sk_leb128_required_unsigned(
    uint64_t            value)
{
    /*
     *  Every 7 bits starting from the most significant high bit
     *  requires a full byte for storage.  Find the required length
     *  using a binary search.  The first pivot is on 28.
     */
    if (value & (UINT64_MAX << 28)) {
        if (value & (UINT64_MAX << 42)) {
            if (value & (UINT64_MAX << 56)) {
                if (value & (UINT64_MAX << 63)) {
                    return 10;
                } else {
                    return 9;
                }
            } else {
                if (value & (UINT64_MAX << 49)) {
                    return 8;
                } else {
                    return 7;
                }
            }
        } else {
            if (value & (UINT64_MAX << 35)) {
                return 6;

            } else {
                return 5;
            }
        }
    } else {
        if (value & (UINT64_MAX << 14)) {
            if (value & (UINT64_MAX << 21)) {
                return 4;
            } else {
                return 3;
            }
        } else {
            if (value & (UINT64_MAX << 7)) {
                return 2;
            } else {
                return 1;
            }
        }
    }
}


/* Encode signed 'value' into the buffer 'leb128'. Return 0 if leb128
 * is short. */
size_t
sk_leb128_encode_signed(
    int64_t             value,
    uint8_t            *leb128,
    size_t              leb128_len)
{
    uint8_t *b;

    assert(leb128);

    /*
     *    This version puts the sign bit into position 0x40 of the
     *    final byte of the encoding.  The range of that byte is -64
     *    to 63.
     *
     *    The Protobuf version puts the sign bit into the LSB of the
     *    encoded value by computing the unsigned encoding of
     *
     *    ((value << 1) ^ (value >> 63))
     */
    if (value < 0) {
        for (b = leb128; leb128_len; ++b, --leb128_len) {
            if (value >= -SK_LEB128_SIGN_BIT) {
                *b = value & 0x7f;
                return 1u + (b - leb128);
            }
            *b = 0x80 | (value & 0x7f);
            /* Note: this must be an arthimetic shift */
            value >>= 7;
#if 0
            /* if >> on negative value fills with 0, we must set the
             * bits back to 1.  63 is number of bits in signed int */
            value |= -(INT64_C(1) << (63 - 7));

            /* Alternatively, replace the previous two statements
             * with the following */
            value = (UINT64_C(0xff) << 56) | (value >> 7);
#endif
        }
    } else {
        for (b = leb128; leb128_len; ++b, --leb128_len) {
            if (value < SK_LEB128_SIGN_BIT) {
                *b = (uint8_t)value;
                return 1u + (b - leb128);
            }
            *b = 0x80 | (value & 0x7f);
            value >>= 7;
        }
    }
    return 0;
}

/* Decode and return signed value in 'leb128'. Put number of bytes
 * processed into 'consumed'. */
int64_t
sk_leb128_decode_signed(
    const uint8_t      *leb128,
    size_t              max_consume,
    size_t             *consumed)
{
    unsigned int shift;
    int64_t value;
    const uint8_t *b;

    assert(leb128);

    value = 0;
    shift = 0;
    for (b = leb128; max_consume; ++b, --max_consume) {
        value |= ((int64_t)((*b) & 0x7f)) << shift;
        shift += 7;
        if (0 == ((*b) & 0x80)) {
            if (((*b) & SK_LEB128_SIGN_BIT) && (shift < 63)) {
                /* sign extend */
                value |= -(UINT64_C(1) << shift);
            }
            if (consumed) {
                *consumed = 1u + (b - leb128);
            }
            return value;
        }
    }
    if (consumed) {
        *consumed = 1u + (b - leb128);
    }
    return INT64_MAX;
}

/* Return octet count required to encode signed 'value'. */
size_t
sk_leb128_required_signed(
    int64_t             value)
{
    size_t count;
    int64_t mask;

    /* handle case where >> is not arithmetic; that is, where right
     * shift of negative value fills with 0s instead of 1s */
    if (value < 0) {
        /* 63 is number of bits in signed part of int64_t */
        mask = -(INT64_C(1) << (63 - 7));
    } else {
        mask = 0;
    }

    count = 1;
    while ((value < -SK_LEB128_SIGN_BIT) || (value >= SK_LEB128_SIGN_BIT)) {
        ++count;
        value = mask | (value >> 7);
    }
    return count;
}


#if 0
int
main(
    int                 argc,
    char              **argv)
{
    uint8_t buf1[20];
    uint8_t buf2[3];
    uint64_t ucheck;
    int64_t val, check;
    int i;
    size_t sz, sz2, j;

    for (i = 1; i < argc; ++i) {
        val = (int64_t)strtoull(argv[i], NULL, 0);
        memset(buf1, 0, sizeof(buf1));
        memset(buf2, 0, sizeof(buf2));

        sz = sk_leb128_encode_signed(val, buf1, sizeof(buf1));
        if (!sz) {
            printf("'%s' ==> SIGNED [0] FAILED!\n", argv[i]);
        } else {
            printf("'%s' ==> SIGNED [%zu]", argv[i], sz);
            for (j = 0; j < sz; ++j) {
                printf(" %02x", buf1[j]);
            }
            check = sk_leb128_decode_signed(buf1, sizeof(buf1), &sz2);
            if (check != val || sz != sz2) {
                printf(" ==> [%zu] %" PRId64 " FAILED!", sz2, check);
            } else {
                printf(" ==> [%zu] %" PRId64, sz2, check);
            }

            if ((sz2 = sk_leb128_required_signed(val)) != sz) {
                printf("    length FAILED [%zu]\n", sz2);
            }
            printf("\n");
        }

        if (sz > sizeof(buf2)) {
            sz = 0;
        }
        if (sk_leb128_encode_signed(val, buf2, sizeof(buf2)) != sz
            || memcmp(buf1, buf2, sizeof(buf2)))
        {
            printf("\tSHORT SIGNED ENCODE FAILED [%zu]", sz);
            for (j = 0; j < sz; ++j) {
                printf(" %02x", buf2[j]);
            }
            printf("\n");
        } else {
            check = sk_leb128_decode_signed(buf2, sizeof(buf2), &sz2);
            if (0 == sz) {
                if (check != INT64_MAX || sz2 <= sizeof(buf2)) {
                    printf("\tSHORT SIGNED DECODE FAIELD [%zu] %" PRId64 "\n",
                           sz2, check);
                }
            } else if (check != val || sz != sz2) {
                printf("\tSHORT SIGNED DECODE FAIELD [%zu] %" PRId64 "\n",
                       sz2, check);
            }
        }

        if (val < 0) {
            continue;
        }

        memset(buf1, 0, sizeof(buf1));
        memset(buf2, 0, sizeof(buf2));

        sz = sk_leb128_encode_unsigned(val, buf1, sizeof(buf1));
        if (!sz) {
            printf("'%s' ==> UNSIGNED [0] FAILED!\n", argv[i]);
        } else {
            printf("'%s' ==> UNSIGNED [%zu]", argv[i], sz);
            for (j = 0; j < sz; ++j) {
                printf(" %02x", buf1[j]);
            }
            ucheck = sk_leb128_decode_unsigned(buf1, sizeof(buf1), &sz2);
            if (ucheck != (uint64_t)val || sz != sz2) {
                printf(" ==> [%zu] %" PRIu64 " FAILED!",
                       sz2, ucheck);
            } else {
                printf(" ==> [%zu] %" PRIu64, sz2, ucheck);
            }

            if ((sz2 = sk_leb128_required_unsigned(val)) != sz) {
                printf("    length FAILED [%zu]\n", sz2);
            }
            printf("\n");
        }

        if (sz > sizeof(buf2)) {
            sz = 0;
        }
        if (sk_leb128_encode_unsigned(val, buf2, sizeof(buf2)) != sz
            || memcmp(buf1, buf2, sizeof(buf2)))
        {
            printf("\tSHORT UNSIGNED ENCODE FAILED [%zu]", sz);
            for (j = 0; j < sz; ++j) {
                printf(" %02x", buf2[j]);
            }
            printf("\n");
        } else {
            ucheck = sk_leb128_decode_unsigned(buf2, sizeof(buf2), &sz2);
            if (0 == sz) {
                if (ucheck != UINT64_MAX || sz2 <= sizeof(buf2)) {
                    printf("\tSHORT UNSIGNED DECODE FAIELD [%zu] %" PRIu64 "\n",
                           sz2, ucheck);
                }
            } else if (ucheck != (uint64_t)val || sz != sz2) {
                printf("\tSHORT UNSIGNED DECODE FAIELD [%zu] %" PRIu64 "\n",
                       sz2, ucheck);
            }
        }
    }

    return 0;
}
#endif


/*
** Local Variables:
** mode:c
** indent-tabs-mode:nil
** c-basic-offset:4
** End:
*/
