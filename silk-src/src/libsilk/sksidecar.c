/*
** Copyright (C) 2015-2017 by Carnegie Mellon University.
**
** @OPENSOURCE_LICENSE_START@
** See license information in ../../LICENSE.txt
** @OPENSOURCE_LICENSE_END@
*/

/*
 *  sksidecar.c
 *
 *    Implementation of sidecar data structure and functions to
 *    serialize and deserialize the description of the sidecar data
 *    and the data itself.
 *
 *  Mark Thomas
 *  October 2015
 */

#include <silk/silk.h>

RCSIDENT("$SiLK: sksidecar.c 373f990778e9 2017-06-22 21:57:36Z mthomas $");

#include <silk/skleb128.h>
#include <silk/skredblack.h>
#include <silk/sksidecar.h>
#include <silk/utils.h>
#include "skheader_priv.h"

#ifdef SKSIDECAR_TRACE_LEVEL
#define TRACEMSG_LEVEL SKSIDECAR_TRACE_LEVEL
#endif
#define TRACEMSG(lvl, msg)  TRACEMSG_TO_TRACEMSGLVL(lvl, msg)
#include <silk/sktracemsg.h>

#if TRACEMSG_LEVEL > 0
#define V(v_v) ((void *)(v_v))
#define TRC_FMT3  "%s:%d [%p] "
#define TRC_ARG3  __FILE__, __LINE__, V(sc)
#endif


/* LOCAL DEFINES AND TYPEDEFS */

/*
 *    Minimum and maximum versions of serialized sidecar structure
 *    that are currently supported.
 */
#define SIDECAR_VERSION_MINIMUM     1
#define SIDECAR_VERSION_MAXIMUM     1

/*
 *    Version to write when serializing the sidecar structure.
 */
#define SIDECAR_VERSION             1

/*
 *    At the initial allocation of the array of elements or when the
 *    array of elements runs out of room, the size of the array is
 *    increased by this number of elements.
 */
#define SIDECAR_ELEM_CAPACITY_STEP  64

/*
 *    Initialize size of a buffer to hold the name of sidecar
 *    elements.
 */
#define SIDECAR_DEFAULT_STRBUF_MAX  2048

/*
 *    sk_sidecar_t describes all the possible elements that may be
 *    used by the sidecar data elements of a SiLK flow record, rwRec.
 *
 *    The sk_sidecar_t holds a list of elements, each of which is
 *    represented by the sk_sidecar_elem_t object.
 */
struct sk_sidecar_st {
    sk_rbtree_t        *elem_by_name;
    sk_sidecar_elem_t **elem_by_id;
    size_t              elem_capacity;
    size_t              elem_count;
};
/* typedef struct sk_sidecar_st sk_sidecar_t; */

/*
 *    sk_sidecar_elem_t represents one element in an sk_sidecar_t.
 *
 *    A sk_sidecar_elem_t has a name that is unique across all
 *    elements in a sidecar.  It has an ID that is used when
 *    serializing data represented by this element.  It has a data
 *    type (e.g., IP, number, string) that is represented by an
 *    sk_sidecar_type_t.  It may have an optional reference to an
 *    IPFIX information element ID.
 */
struct sk_sidecar_elem_st {
    /* name of this element.  If a member of a structured data table,
     * their are embedded \0 to denote levels. */
    const char         *name;
    /* length of 'name'.  Includes the embedded and terminating
     * '\0' */
    size_t              name_len;
    /* the identifier used label data that has this name */
    size_t              id;
    /* an optional IPFIX information element ID */
    sk_field_ident_t    ipfix_ident;
    /* the type of this element. */
    sk_sidecar_type_t   data_type;
    /* the type  */
    sk_sidecar_type_t   list_elem_type;
};
/* typedef struct sk_sidecar_elem_st sk_sidecar_elem_t; */


/*
 *  Typedefs and Macros to Support Serialization
 */

/*
 *    There is no "sidecar_buffer_t" strucure; instead, there are two
 *    separate strucures, one used for writing(serializing) and one
 *    used for reading(deserializing).  The structures are identical
 *    with the exception of whether the buffer is "const".
 */

/*
 *    sidecar_output_buffer_t maintains information about the amount
 *    of space available in a buffer into which data is being
 *    serialized.
 *
 *    For reading(deserializing) of data, use a sidecar_input_buffer_t
 */
struct sidecar_output_buffer_st {
    uint8_t            *buffer;
    uint8_t            *bp;
    size_t              len;
    size_t              avail;
    size_t             *len_processed;
    int                 err_code;
};
typedef struct sidecar_output_buffer_st sidecar_output_buffer_t;

/*
 *    sidecar_input_buffer_t maintains information about the amount
 *    of data available in a buffer from which data is being
 *    deserialized.
 *
 *    For writing(serializing) of data, use a sidecar_output_buffer_t
 */
struct sidecar_input_buffer_st {
    const uint8_t      *buffer;
    const uint8_t      *bp;
    size_t              len;
    size_t              avail;
    size_t             *len_processed;
    int                 err_code;
};
typedef struct sidecar_input_buffer_st sidecar_input_buffer_t;

/*
 *    Initialize the sidecar_buffer_t 'm_sc_buf' to process the data
 *    buffer 'm_data_buf' whose length is 'm_data_buf_len' octets.
 *
 *    If an error occurs during processing, the function should set
 *    the value referenced by 'm_len_processed' to the number of
 *    octets processed and return the error code 'm_error'.
 */
#define sidecar_buffer_init(m_sc_buf, m_data_buf, m_data_buf_len,       \
                            m_len_processed, m_error)                   \
    do {                                                                \
        (m_sc_buf)->buffer = (m_data_buf);                              \
        (m_sc_buf)->bp = (m_data_buf);                                  \
        (m_sc_buf)->len = (m_data_buf_len);                             \
        (m_sc_buf)->avail = (m_data_buf_len);                           \
        (m_sc_buf)->len_processed = (m_len_processed);                  \
        (m_sc_buf)->err_code = (m_error);                               \
    } while(0)

/*
 *    sidecar_string_buf_t is used to hold the names of the members
 *    when serializing a table.
 */
struct sidecar_string_buf_st {
    char               *sb_buf;
    size_t              sb_max;
    size_t              sb_len;
    size_t              sb_baselen;
};
typedef struct sidecar_string_buf_st sidecar_string_buf_t;

/*
 *    Return an error from function if sidecar_buffer_t 'm_buf' has
 *    fewer than 'm_needed' bytes available.
 */
#define sidecar_buffer_check_avail(m_buf, m_needed)                     \
    if ((m_buf)->avail < (m_needed)) {                                  \
        TRACEMSG(3, (TRC_FMT3 "avail < needed (%zu < %zu)",             \
                     TRC_ARG3, (m_buf)->avail, (size_t)(m_needed)));    \
        *(m_buf)->len_processed = (m_buf)->bp - (m_buf)->buffer;        \
        return (m_buf)->err_code;                                       \
    }

/*
 *    Move the current buffer position forward 'm_skip' octets on the
 *    sidecar_buffer_t 'm_buf' and subtract 'm_skip' octets from the
 *    available octets.  Return an error from function if fewer than
 *    'm_skip' bytes are available.
 */
#define sidecar_buffer_skip(m_buf, m_skip)              \
    do {                                                \
        sidecar_buffer_check_avail(m_buf, m_skip);      \
        (m_buf)->bp += m_skip;                          \
        (m_buf)->avail -= m_skip;                       \
    } while(0)

/*
 *    Copy 'm_len' bytes of data from 'm_bytes' into the
 *    sidecar_buffer_t 'm_buf'.
 *
 *    Return from function if sidecar_buffer_t 'm_buf' has fewer than
 *    the required number of bytes available.
 */
#define sidecar_buffer_encode_bytes(m_buf, m_bytes, m_len)      \
    do {                                                        \
        sidecar_buffer_check_avail(m_buf, m_len);               \
        memcpy((m_buf)->bp, (m_bytes), (m_len));                \
        (m_buf)->bp += (m_len);                                 \
        (m_buf)->avail -= (m_len);                              \
    } while(0)

/*
 *    Encode the floating point value 'm_val' into the
 *    sidecar_buffer_r 'm_buf'.
 *
 *    Return from function if sidecar_buffer_t 'm_buf' has fewer than
 *    the required number of bytes available.
 */
#define sidecar_buffer_encode_double(m_buf, m_val)      \
    do {                                                \
        double m_d = (m_val);                           \
        sidecar_buffer_check_avail(m_buf, sizeof(m_d)); \
        memcpy((m_buf)->bp, &m_d, sizeof(m_d));         \
        (m_buf)->bp += sizeof(m_d);                     \
        (m_buf)->avail -= sizeof(m_d);                  \
    } while(0)

/*
 *    Encode the value 'val' into the sidecar_buffer_t 'buf' using
 *    LEB128 encoding.
 *
 *    Return from function if sidecar_buffer_t 'm_buf' has fewer than
 *    the required number of bytes available.
 */
#define sidecar_buffer_encode_leb(m_buf, m_val)                         \
    do {                                                                \
        size_t m_sz = (sk_leb128_encode_unsigned(                       \
                           (m_val), (m_buf)->bp, (m_buf)->avail));      \
        if (0 == m_sz) {                                                \
            *(m_buf)->len_processed = (m_buf)->bp - (m_buf)->buffer;    \
            return (m_buf)->err_code;                                   \
        }                                                               \
        (m_buf)->bp += m_sz;                                            \
        (m_buf)->avail -= m_sz;                                         \
    } while(0)

/*
 *    Encode the fixed-size unsigned inteeger value 'm_val' into the
 *    sidecar_buffer_t 'm_buf'.
 *
 *    Return from function if sidecar_buffer_t 'm_buf' has fewer than
 *    the required number of bytes available.
 */
#define sidecar_buffer_encode_u8(m_buf, m_val)                          \
    sidecar_buffer_encode_fixed(m_buf, m_val, uint8_t, (uint8_t))
#define sidecar_buffer_encode_u16(m_buf, m_val)                 \
    sidecar_buffer_encode_fixed(m_buf, m_val, uint16_t, htons)
#define sidecar_buffer_encode_u32(m_buf, m_val)                 \
    sidecar_buffer_encode_fixed(m_buf, m_val, uint32_t, htonl)
#define sidecar_buffer_encode_u64(m_buf, m_val)                 \
    sidecar_buffer_encode_fixed(m_buf, m_val, uint64_t, hton64)

/*
 *    Helper macro for sidecar_buffer_encode_uNN() macros above.
 *
 *    Encode the fixed-size value 'm_val' whose type is 'm_type' into
 *    the sidecar_buffer_t 'm_buf', using 'm_h_to_n' to transform the
 *    value into network byte order.
 *
 *    Return from function if sidecar_buffer_t 'm_buf' has fewer than
 *    the required number of bytes available.
 */
#ifdef SK_HAVE_ALIGNED_ACCESS_REQUIRED
#define sidecar_buffer_encode_fixed(m_buf, m_val, m_type, m_h_to_n)     \
    sidecar_buffer_encode_fixed_copy(m_buf, m_val, m_type, m_h_to_n)
#else
#define sidecar_buffer_encode_fixed(m_buf, m_val, m_type, m_h_to_n)     \
    sidecar_buffer_encode_fixed_cast(m_buf, m_val, m_type, m_h_to_n)
#endif

/*
 *    Helper for sidecar_buffer_encode_fixed() that copies the value
 *    to a temporary location then memcpy()s it into the buffer.
 */
#define sidecar_buffer_encode_fixed_copy(m_buf, m_val, m_type, m_h_to_n) \
    do {                                                                \
        m_type m_tmp;                                                   \
        sidecar_buffer_check_avail((m_buf), sizeof(m_type));            \
        m_tmp = m_h_to_n(m_val);                                        \
        memcpy((m_buf)->bp, &m_tmp, sizeof(m_type));                    \
        (m_buf)->bp += sizeof(m_type);                                  \
        (m_buf)->avail -= sizeof(m_type);                               \
    } while(0)

/*
 *    Helper for sidecar_buffer_encode_fixed() that casts the buffer
 *    pointer to the required type.
 */
#define sidecar_buffer_encode_fixed_cast(m_buf, m_val, m_type, m_h_to_n) \
    do {                                                                \
        sidecar_buffer_check_avail((m_buf), sizeof(m_type));            \
        *(m_type*)(m_buf)->bp = m_h_to_n(m_val);                        \
        (m_buf)->bp += sizeof(m_type);                                  \
        (m_buf)->avail -= sizeof(m_type);                               \
    } while(0)

/*
 *    Copy 'm_len' bytes of data from the sidecar_buffer_t 'm_buf'
 *    into 'm_bytes'.
 *
 *    Return from function if sidecar_buffer_t 'm_buf' has fewer than
 *    the required number of bytes available.
 */
#define sidecar_buffer_decode_bytes(m_buf, m_bytes, m_len)      \
    do {                                                        \
        sidecar_buffer_check_avail(m_buf, m_len);               \
        memcpy((m_bytes), (m_buf)->bp, (m_len));                \
        (m_buf)->bp += (m_len);                                 \
        (m_buf)->avail -= (m_len);                              \
    } while(0)

/*
 *    Decode the floating point value 'm_val' from the
 *    sidecar_buffer_t 'm_buf'.
 *
 *    Return from function if sidecar_buffer_t 'm_buf' has fewer than
 *    the required number of bytes available.
 */
#define sidecar_buffer_decode_double(m_buf, m_val)      \
    do {                                                \
        double m_d;                                     \
        sidecar_buffer_check_avail(m_buf, sizeof(m_d)); \
        memcpy(&m_d, (m_buf)->bp, sizeof(m_d));         \
        (m_val) = m_d;                                  \
        (m_buf)->bp += sizeof(m_d);                     \
        (m_buf)->avail -= sizeof(m_d);                  \
    } while(0)

/*
 *    Decode the value 'm_val' from the sidecar_buffer_t 'm_buf' using
 *    LEB128 encoding.
 *
 *    Return from function if sidecar_buffer_t 'm_buf' has fewer than
 *    the required number of bytes available.
 */
#define sidecar_buffer_decode_leb(m_buf, m_val)                         \
    do {                                                                \
        size_t m_sz;                                                    \
        (m_val) = (sk_leb128_decode_unsigned(                           \
                       (m_buf)->bp, (m_buf)->avail, &m_sz));            \
        if (m_sz > (m_buf)->avail) {                                    \
            *(m_buf)->len_processed = (m_buf)->bp - (m_buf)->buffer;    \
            return (m_buf)->err_code;                                   \
        }                                                               \
        (m_buf)->bp += m_sz;                                            \
        (m_buf)->avail -= m_sz;                                         \
    } while(0)

/*
 *    Helper macro for sidecar_buffer_decode_uNN() macros above.
 *
 *    Decode the fixed-size unsigned inteeger value 'm_val' from the
 *    sidecar_buffer_t 'm_buf'.
 *
 *    Return from function if sidecar_buffer_t 'm_buf' has fewer than
 *    the required number of bytes available.
 */
#define sidecar_buffer_decode_u8(m_buf, m_val)                          \
    sidecar_buffer_decode_fixed(m_buf, m_val, uint8_t, (uint8_t))
#define sidecar_buffer_decode_u16(m_buf, m_val)                 \
    sidecar_buffer_decode_fixed(m_buf, m_val, uint16_t, ntohs)
#define sidecar_buffer_decode_u32(m_buf, m_val)                 \
    sidecar_buffer_decode_fixed(m_buf, m_val, uint32_t, ntohl)
#define sidecar_buffer_decode_u64(m_buf, m_val)                 \
    sidecar_buffer_decode_fixed(m_buf, m_val, uint64_t, ntoh64)

/*
 *    Decode the fixed-size value 'm_val' whose type is 'm_type' from
 *    the sidecar_buffer_t 'm_buf', using 'm_n_to_h' to transform the
 *    value from network byte order.
 *
 *    Return from function if sidecar_buffer_t 'm_buf' has fewer than
 *    the required number of bytes available.
 */
#ifdef SK_HAVE_ALIGNED_ACCESS_REQUIRED
#define sidecar_buffer_decode_fixed(m_buf, m_val, m_type, m_n_to_h)     \
    sidecar_buffer_decode_fixed_copy(m_buf, m_val, m_type, m_n_to_h)
#else
#define sidecar_buffer_decode_fixed(m_buf, m_val, m_type, m_n_to_h)     \
    sidecar_buffer_decode_fixed_cast(m_buf, m_val, m_type, m_n_to_h)
#endif

/*
 *    Helper for sidecar_buffer_decode_fixed() that memcpy()s the
 *    value from the buffer into a temporary location then copies it
 *    to 'm_val'.
 */
#define sidecar_buffer_decode_fixed_copy(m_buf, m_val, m_type, m_n_to_h) \
    do {                                                                \
        m_type m_tmp;                                                   \
        sidecar_buffer_check_avail((m_buf), sizeof(m_type));            \
        memcpy(&m_tmp, (m_buf)->bp, sizeof(m_type));                    \
        (m_val) = m_n_to_h(m_tmp);                                      \
        (m_buf)->bp += sizeof(m_type);                                  \
        (m_buf)->avail -= sizeof(m_type);                               \
    } while(0)

/*
 *    Helper for sidecar_buffer_decode_fixed() that casts the buffer
 *    pointer to the required type.
 */
#define sidecar_buffer_decode_fixed_cast(m_buf, m_val, m_type, m_n_to_h) \
    do {                                                                \
        sidecar_buffer_check_avail((m_buf), sizeof(m_type));            \
        (m_val) = m_n_to_h(*(m_type*)(m_buf)->bp);                      \
        (m_buf)->bp += sizeof(m_type);                                  \
        (m_buf)->avail -= sizeof(m_type);                               \
    } while(0)




/* LOCAL FUNCTION PROTOTYPES */

static void sidecar_elem_free(sk_sidecar_elem_t  *elem);
static void sidecar_grow_array(sk_sidecar_t *sc);
static int
sidecar_deserialize_elem(
    const sk_sidecar_t         *sc,
    const sk_sidecar_elem_t    *elem,
    lua_State                  *L,
    sidecar_input_buffer_t     *buf);
static int
sidecar_serialize_elem(
    const sk_sidecar_t         *sc,
    const sk_sidecar_elem_t    *elem,
    lua_State                  *L,
    sidecar_output_buffer_t    *buf,
    size_t                     *count);


/* FUNCTION DEFINITIONS */

#if 0
/*
 *    Encode the value 'val' into the sidecar_buffer_t 'buf' using
 *    LEB128 encoding.
 *
 *    Return from function if sidecar_buffer_t 'm_buf' has fewer than
 *    the required number of bytes available.
 */
static int
sidecar_buffer_encode_leb(
    sidecar_output_buffer_t    *buf,
    uint64_t                    val)
{
    size_t m_sz = sk_leb128_encode_unsigned(val, buf->bp, buf->avail);
    if (0 == m_sz) {
        *buf->len_processed = buf->bp - buf->buffer;
        return buf->err_code;
    }
    buf->bp += m_sz;
    buf->avail -= m_sz;
    return SK_SIDECAR_OK;
}
#endif


/*
 *    Verify the number in 'raw_data_type' is a known data type and
 *    not SK_SIDECAR_UNKNOWN.  If it is, store the type in the
 *    location referenced by 'data_type' and return 1.  Otherwise
 *    return 0.
 */
static int
sidecar_data_type_check(
    uint64_t            raw_data_type,
    sk_sidecar_type_t  *data_type)
{
    switch (raw_data_type) {
      case SK_SIDECAR_UINT8:
      case SK_SIDECAR_UINT16:
      case SK_SIDECAR_UINT32:
      case SK_SIDECAR_UINT64:
      case SK_SIDECAR_DOUBLE:
      case SK_SIDECAR_STRING:
      case SK_SIDECAR_BINARY:
      case SK_SIDECAR_ADDR_IP4:
      case SK_SIDECAR_ADDR_IP6:
      case SK_SIDECAR_DATETIME:
      case SK_SIDECAR_BOOLEAN:
      case SK_SIDECAR_EMPTY:
      case SK_SIDECAR_LIST:
      case SK_SIDECAR_TABLE:
        *data_type = (sk_sidecar_type_t)raw_data_type;
        return 1;
    }
    return 0;
}


/*
 *    Return the element on sidecar at position 'id'.  This function
 *    does no error checking.
 */
static sk_sidecar_elem_t *
sidecar_elem_at(
    const sk_sidecar_t *sc,
    size_t              id)
{
    assert(sc);
    assert(id < sc->elem_count);
    return sc->elem_by_id[id];
}


/*
 *    Compare sk_sidecar_elem_t objects 'v_elem_a' and 'v_elem_b' by
 *    name.
 *
 *    This function is used by the redblack tree.
 */
static int
sidecar_elem_compare(
    const void         *v_elem_a,
    const void         *v_elem_b,
    const void         *v_context)
{
    const sk_sidecar_elem_t *a = (const sk_sidecar_elem_t *)v_elem_a;
    const sk_sidecar_elem_t *b = (const sk_sidecar_elem_t *)v_elem_b;

    /* Unused */
    (void)v_context;

    if (a->name_len == b->name_len) {
        return memcmp(a->name, b->name, a->name_len);
    }
    return ((a->name_len < b->name_len) ? -1 : 1);
}


/*
 *    Create and return a sidecar element having the specified 'name',
 *    'data_type' and 'ipfix_ident'.  Store the element indexed both
 *    by name and by position on the sk_sidecar_t 'sc'.
 *
 *    If 'name' is not unique on 'sc', return NULL.
 *
 *    Exit on memory allocation error.
 */
static sk_sidecar_elem_t *
sidecar_elem_create(
    sk_sidecar_t               *sc,
    const char                 *name,
    size_t                      namelen,
    sk_sidecar_type_t           data_type,
    sk_sidecar_type_t           list_elem_type,
    sk_field_ident_t            ipfix_ident)
{
    sk_sidecar_elem_t *e;
    int rv;

    if (sc->elem_count == sc->elem_capacity) {
        sidecar_grow_array(sc);
    }

    e = sk_alloc(sk_sidecar_elem_t);
    e->ipfix_ident = ipfix_ident;
    e->data_type = data_type;
    e->list_elem_type = list_elem_type;
    e->name_len = namelen;
    e->name = sk_alloc_memory(char, namelen, SK_ALLOC_FLAG_NO_CLEAR);
    memcpy((void*)e->name, name, namelen);

    e->id = sc->elem_count;
    sc->elem_by_id[sc->elem_count] = e;
    ++sc->elem_count;

    rv = sk_rbtree_insert(sc->elem_by_name, e, NULL);
    if (rv) {
        --sc->elem_count;
        sidecar_elem_free(e);
        if (SK_RBTREE_ERR_DUPLICATE == rv) {
            return NULL;
        }
        skAppPrintOutOfMemory("redblack node");
        exit(EXIT_FAILURE);
    }
    TRACEMSG(3, (TRC_FMT3 "Added element %p '%s'", TRC_ARG3, V(e), name));
    return e;
}


/*
 *    Free the sk_sidecar_elem_t 'elem'.
 *
 *    This function is used by the redblack tree.
 */
static void
sidecar_elem_free(
    sk_sidecar_elem_t  *elem)
{
    if (elem) {
        free((void*)elem->name);
        free(elem);
    }
}


/*
 *    Free the string buffer 'strbuf'.
 */
static void
sidecar_string_buf_free(
    sidecar_string_buf_t   *strbuf)
{
    if (strbuf) {
        free(strbuf->sb_buf);
    }
}


/*
 *    Initialize the string buffer 'strbuf' and copy the name of
 *    'elem' into the buffer.
 */
static void
sidecar_string_buf_init(
    sidecar_string_buf_t       *strbuf,
    const sk_sidecar_elem_t    *elem)
{
    if (NULL == elem || NULL == elem->name) {
        strbuf->sb_max = SIDECAR_DEFAULT_STRBUF_MAX;
        strbuf->sb_buf = sk_alloc_array(char, strbuf->sb_max);
        strbuf->sb_len = 0;
        strbuf->sb_baselen = 0;
        return;
    }

    if (elem->name_len > (SIDECAR_DEFAULT_STRBUF_MAX >> 1)) {
        strbuf->sb_max = elem->name_len + SIDECAR_DEFAULT_STRBUF_MAX;
    } else {
        strbuf->sb_max = SIDECAR_DEFAULT_STRBUF_MAX;
    }
    strbuf->sb_buf = sk_alloc_array(char, strbuf->sb_max);
    memcpy(strbuf->sb_buf, elem->name, elem->name_len);
    strbuf->sb_len = elem->name_len;
    strbuf->sb_baselen = elem->name_len;
}


/*
 *    Append the char array 'm_str' of length 'm_len' to the base name
 *    that was specified when the string buffer 'm_strbuf' was
 *    initialized.
 */
#define sidecar_string_buf_append_string(m_strbuf, m_str, m_len)        \
    do {                                                                \
        (m_strbuf)->sb_len = (m_strbuf)->sb_baselen + (m_len);          \
        if ((m_strbuf)->sb_len >= (m_strbuf)->sb_max) {                 \
            (m_strbuf)->sb_max += (m_len) + SIDECAR_DEFAULT_STRBUF_MAX; \
            (m_strbuf)->sb_buf                                          \
                = sk_alloc_realloc_noclear(                             \
                    (m_strbuf)->sb_buf, char, (m_strbuf)->sb_max);      \
        }                                                               \
        memcpy((m_strbuf)->sb_buf + (m_strbuf)->sb_baselen, (m_str),    \
               (m_len));                                                \
    } while(0)


/*
 *    Deserialize a list sidecar element.
 */
static int
sidecar_deserialize_list(
    const sk_sidecar_t         *sc,
    const sk_sidecar_elem_t    *elem,
    lua_State                  *L,
    sidecar_input_buffer_t     *buf)
{
    sidecar_input_buffer_t buf2;
    sk_sidecar_elem_t list_elem;
    size_t elem_count;
    uint16_t sc_len;
    size_t i;
    int t;
    int rv;

    rv = SK_SIDECAR_OK;

    if (SK_SIDECAR_LIST == elem->list_elem_type
        || SK_SIDECAR_TABLE == elem->list_elem_type
        || SK_SIDECAR_UNKNOWN == elem->list_elem_type)
    {
        skAbortBadCase(elem->list_elem_type);
    }

    /* cache the starting point */
    buf2 = *buf;

    /* length of this piece of sidecar data */
    sidecar_buffer_decode_u16(buf, sc_len);
    if (sc_len < sizeof(sc_len)) {
        /* length is shorter than what we've already read */
        return SK_SIDECAR_E_DECODE_ERROR;
    }
    sidecar_buffer_check_avail(buf, sc_len - sizeof(sc_len));

    /* adjust available to length of this entry */
    buf->avail = sc_len - sizeof(sc_len);

    /* number of elements */
    sidecar_buffer_decode_u16(buf, elem_count);

    /* create table to hold the data */
    lua_createtable(L, elem_count, 0);
    t = lua_gettop(L);

    /* create a fake element to use while processing the members
     * of the list */
    memset(&list_elem, 0, sizeof(list_elem));
    list_elem.data_type = elem->list_elem_type;
    list_elem.list_elem_type = SK_SIDECAR_UNKNOWN;

    for (i = 1; i <= elem_count; ++i) {
        rv = sidecar_deserialize_elem(sc, &list_elem, L, buf);
        if (rv) {
            goto END;
        }
        lua_seti(L, t, i);
    }

  END:
    /* adjust the available bytes by subtracting what was consumed by
     * this function */
    assert(buf->bp - buf2.bp > 0);
    assert(buf->bp - buf2.bp <= sc_len);
    buf->avail = buf2.avail - (buf->bp - buf2.bp);

    return rv;
}

/*
 *    Deserialize a table sidecar element.
 */
static int
sidecar_deserialize_table(
    const sk_sidecar_t         *sc,
    const sk_sidecar_elem_t    *elem,
    lua_State                  *L,
    sidecar_input_buffer_t     *buf)
{
    sidecar_input_buffer_t buf2;
    sk_sidecar_elem_t *e;
    uint16_t elem_count;
    uint16_t sc_len;
    uint32_t id;
    size_t i;
    int t;
    int rv;

    rv = SK_SIDECAR_OK;

    /* cache the starting point */
    buf2 = *buf;

    /* length of this piece of sidecar data */
    sidecar_buffer_decode_u16(buf, sc_len);
    if (sc_len < sizeof(sc_len)) {
        /* length is shorter than what we've already read */
        return SK_SIDECAR_E_DECODE_ERROR;
    }
    sidecar_buffer_check_avail(buf, sc_len - sizeof(sc_len));

    /* adjust available to length of this entry */
    buf->avail = sc_len - sizeof(sc_len);

    /* number of elements */
    sidecar_buffer_decode_u16(buf, elem_count);

    TRACEMSG(3, (TRC_FMT3 "lua %p Decoding table, len = %u, elem_count = %u",
                 TRC_ARG3, V(L), sc_len, elem_count));

    /* create table to hold the data */
    lua_createtable(L, 0, elem_count);
    t = lua_gettop(L);

    for (i = 0; i < elem_count; ++i) {
        sidecar_buffer_decode_leb(buf, id);
        if (id >= sc->elem_count) {
            /* do we treat this as an error or just ignore the element? */
            rv = SK_SIDECAR_E_DECODE_ERROR;
            goto END;
        }
        e = sidecar_elem_at(sc, id);
        if (elem->name) {
            /* the name of 'e' should start with a prefix that is
             * identical to the name of 'elem', and it should have
             * only one '\0' in its remaining length */
            if ((e->name_len != elem->name_len + strlen(e->name) + 1)
                || 0 != memcmp(e->name, elem->name, elem->name_len))
            {
                rv = SK_SIDECAR_E_DECODE_ERROR;
                goto END;
            }
        }
        rv = sidecar_deserialize_elem(sc, e, L, buf);
        if (rv) {
            goto END;
        }
        lua_setfield(L, t, e->name + elem->name_len);
    }

  END:
    /* adjust the available bytes by subtracting what was consumed by
     * this function */
    assert(buf->bp - buf2.bp > 0);
    assert(buf->bp - buf2.bp <= sc_len);
    buf->avail = buf2.avail - (buf->bp - buf2.bp);

    return rv;
}

/*
 *    Deserialize any type of sidecar element.
 */
static int
sidecar_deserialize_elem(
    const sk_sidecar_t         *sc,
    const sk_sidecar_elem_t    *elem,
    lua_State                  *L,
    sidecar_input_buffer_t     *buf)
{
    uint8_t u8;
    uint16_t u16;
    uint32_t u32;
    uint64_t u64;
    double d;
    sktime_t *dt;
    skipaddr_t *ip;
    size_t len;

    switch (elem->data_type) {
      case SK_SIDECAR_UINT8:
        sidecar_buffer_decode_u8(buf, u8);
        lua_pushinteger(L, u8);
        TRACEMSG(3, (TRC_FMT3 "decoded %s = %u", TRC_ARG3, elem->name, u8));
        break;
      case SK_SIDECAR_UINT16:
        sidecar_buffer_decode_u16(buf, u16);
        lua_pushinteger(L, u16);
        TRACEMSG(3, (TRC_FMT3 "decoded %s = %u", TRC_ARG3, elem->name, u16));
        break;
      case SK_SIDECAR_UINT32:
        sidecar_buffer_decode_u32(buf, u32);
        lua_pushinteger(L, u32);
        TRACEMSG(3, (TRC_FMT3 "decoded %s = %u", TRC_ARG3, elem->name, u32));
        break;
      case SK_SIDECAR_UINT64:
        /* FIXME: Handle unsigned values where MSB is set */
        sidecar_buffer_decode_u64(buf, u64);
        lua_pushinteger(L, u64);
        break;
      case SK_SIDECAR_DOUBLE:
        sidecar_buffer_decode_double(buf, d);
        lua_pushnumber(L, d);
        break;
      case SK_SIDECAR_STRING:
      case SK_SIDECAR_BINARY:
        /* get the length of the string; copy string directly */
        sidecar_buffer_decode_leb(buf, len);
        sidecar_buffer_check_avail(buf, len);
        lua_pushlstring(L, (const char *)buf->bp, len);
        sidecar_buffer_skip(buf, len);
        break;
      case SK_SIDECAR_DATETIME:
        dt = sk_lua_push_datetime(L);
        sidecar_buffer_decode_u64(buf, *dt);
        break;
      case SK_SIDECAR_ADDR_IP4:
        sidecar_buffer_decode_u32(buf, u32);
        ip = sk_lua_push_ipaddr(L);
        skipaddrSetV4(ip, &u32);
        break;
      case SK_SIDECAR_ADDR_IP6:
        sidecar_buffer_check_avail(buf, 16);
        sk_lua_push_ipv6_from_byte_ptr(L, buf->bp);
        sidecar_buffer_skip(buf, 16);
        break;
      case SK_SIDECAR_BOOLEAN:
        sidecar_buffer_decode_u8(buf, u8);
        lua_pushboolean(L, u8);
        break;
      case SK_SIDECAR_EMPTY:
        /* need to push some value onto lua stack since cannot add
         * NIL to a table */
        lua_pushboolean(L, 1);
        break;
      case SK_SIDECAR_LIST:
        return sidecar_deserialize_list(sc, elem, L, buf);
      case SK_SIDECAR_TABLE:
        return sidecar_deserialize_table(sc, elem, L, buf);
      case SK_SIDECAR_UNKNOWN:
        break;
      default:
        skAbortBadCase(elem->data_type);
    }

    return SK_SIDECAR_OK;
}


/*
 *    Serialize a list sidecar element.
 */
static int
sidecar_serialize_list(
    const sk_sidecar_t         *sc,
    const sk_sidecar_elem_t    *elem,
    lua_State                  *L,
    sidecar_output_buffer_t    *buf)
{
    sk_sidecar_elem_t list_elem;
    uint8_t *len_pos;
    size_t elem_count;
    lua_Integer table_len;
    lua_Integer i;
    size_t count;
    uint16_t u16;
    int rv;

    if (SK_SIDECAR_LIST == elem->list_elem_type
        || SK_SIDECAR_TABLE == elem->list_elem_type
        || SK_SIDECAR_UNKNOWN == elem->list_elem_type)
    {
        skAbortBadCase(elem->list_elem_type);
    }

    if (elem->name) {
        sidecar_buffer_encode_leb(buf, elem->id);
    }

    /* cache current position for length and number of entries */
    len_pos = buf->bp;
    elem_count = 0;

    /* allow space for the length and number of entries.  Spec
     * says that these are LEB128 encoded, but fixed size is so
     * much easier to handle */
    sidecar_buffer_skip(buf, 2 * sizeof(uint16_t));

    /* create a fake element to use while processing the members
     * of the list */
    memset(&list_elem, 0, sizeof(list_elem));
    list_elem.data_type = elem->list_elem_type;
    list_elem.list_elem_type = SK_SIDECAR_UNKNOWN;

    /* get length of the table (length of the list) */
    lua_len(L, -1);
    table_len = lua_tointegerx(L, -1, NULL);
    lua_pop(L, 1);

    /* visit each entry in list */
    for (i = 1; i <= table_len; ++i) {
        lua_geti(L, -1, i);
        rv = sidecar_serialize_elem(sc, &list_elem, L, buf, &count);
        lua_pop(L, 1);
        if (rv) {
            return rv;
        }
        elem_count += count;
    }

    if (buf->bp - len_pos > UINT16_MAX || elem_count > UINT16_MAX) {
        return SK_SIDECAR_E_NO_SPACE;
    }

    /* put length and number of entries into the buffer */
    u16 = htons(buf->bp - len_pos);
    memcpy(len_pos, &u16, sizeof(u16));
    u16 = htons(elem_count);
    memcpy(len_pos + 2, &u16, sizeof(u16));

    return SK_SIDECAR_OK;
}

/*
 *    Serialize a table sidecar element.
 */
static int
sidecar_serialize_table(
    const sk_sidecar_t         *sc,
    const sk_sidecar_elem_t    *elem,
    lua_State                  *L,
    sidecar_output_buffer_t    *buf,
    sidecar_string_buf_t       *strbuf)
{
    const sk_sidecar_elem_t *e;
    sk_sidecar_elem_t key;
    uint8_t *len_pos;
    size_t elem_count;
    const char *s;
    size_t count;
    int pop_key;
    uint16_t u16;
    int rv;

    /* write the ID of this element */
    if (elem->name) {
        sidecar_buffer_encode_leb(buf, elem->id);
        assert(strbuf->sb_len == elem->name_len);
        assert(strbuf->sb_baselen == elem->name_len);
        assert(0 == memcmp(strbuf->sb_buf, elem->name, elem->name_len));
    }

    /* cache current position for length and number of entries */
    len_pos = buf->bp;
    elem_count = 0;

    /*
     *    FIXME: We need to encode length of the encoding and number
     *    of elements here first.  current spec says they are in LEB
     *    encoding, but the variable size is difficult to plan for.
     *    we should either specify that the sizes are fixed or have
     *    the user provide two buffers: one for the length and element
     *    count and the other for the actual data.
     *
     *    For now, assume two 16-bit values for length and number of
     *    fields
     */
    sidecar_buffer_skip(buf, 2 * sizeof(uint16_t));

    /* visit elements in the table */
    for (lua_pushnil(L); lua_next(L, -2); lua_pop(L, 1)) {
        /* 'key' is at index -2 and 'value' is at index -1 */
        switch (lua_type(L, -2)) {
          case LUA_TSTRING:
            pop_key = 0;
            s = lua_tolstring(L, -2, &count);
            TRACEMSG(3, (TRC_FMT3 "Table key is '%s'", TRC_ARG3, s));
            break;
          case LUA_TNUMBER:
            /* push a copy of the key and convert it to a string */
            pop_key = 1;
            lua_pushvalue(L, -2);
            s = lua_tolstring(L, -1, &count);
            TRACEMSG(3, (TRC_FMT3 "Table key is '%s'", TRC_ARG3, s));
            break;
          default:
            TRACEMSG(3, (TRC_FMT3 "Table key has type %s",
                         TRC_ARG3, luaL_typename(L, -2)));
            continue;
        }
        /* add one for trailing '\0' */
        ++count;
        sidecar_string_buf_append_string(strbuf, s, count);

        /* pop the copy of the key */
        if (pop_key) { lua_pop(L, 1); }

        /* find this element */
        key.name = strbuf->sb_buf;
        key.name_len = strbuf->sb_len;
        e = (const sk_sidecar_elem_t *)sk_rbtree_find(sc->elem_by_name, &key);
        TRACEMSG(3, (TRC_FMT3 "Element with that name is %p", TRC_ARG3, V(e)));
        if (e) {
            rv = sidecar_serialize_elem(sc, e, L, buf, &count);
            if (rv) {
                /* if error encoding type, return the error */
                lua_pop(L, 2);
                TRACEMSG(3, (TRC_FMT3 "Returning error %d", TRC_ARG3, rv));
                return rv;
            }
            elem_count += count;
        }
    }

    if (buf->bp - len_pos > UINT16_MAX || elem_count > UINT16_MAX) {
        TRACEMSG(3, (TRC_FMT3 "Returning E_NO_SPACE", TRC_ARG3));
        return SK_SIDECAR_E_NO_SPACE;
    }

    /* Write length and element count into buffer */
    u16 = htons(buf->bp - len_pos);
    memcpy(len_pos, &u16, sizeof(u16));
    u16 = htons(elem_count);
    memcpy(len_pos + sizeof(uint16_t), &u16, sizeof(u16));

    TRACEMSG(3, (TRC_FMT3 "Encoded table, len = %zu, elem_count = %zu",
                 TRC_ARG3, buf->bp - len_pos, elem_count));

    return SK_SIDECAR_OK;
}

/*
 *    Serialize any type of sidecar element.
 */
static int
sidecar_serialize_elem(
    const sk_sidecar_t         *sc,
    const sk_sidecar_elem_t    *elem,
    lua_State                  *L,
    sidecar_output_buffer_t    *buf,
    size_t                     *count)
{
    sidecar_string_buf_t strbuf;
    lua_Integer n;
    lua_Number d;
    const char *s;
    const sktime_t *dt;
    const skipaddr_t *ip;
    uint32_t ip4;
    char ip6[16];
    size_t len;
    int is_num;
    int rv;

    *count = 0;

    TRACEMSG(3, (TRC_FMT3 "lua %p serializing %p, type %s, lua_type %s",
                 TRC_ARG3, V(L), V(elem),
                 sk_sidecar_type_get_name(elem->data_type),
                 luaL_typename(L, -1)));

    switch (elem->data_type) {
      case SK_SIDECAR_UNKNOWN:
        break;
      case SK_SIDECAR_UINT8:
        n = lua_tointegerx(L, -1, &is_num);
        if (is_num && n <= UINT8_MAX && n >= 0) {
            if (elem->name) {
                sidecar_buffer_encode_leb(buf, elem->id);
            }
            sidecar_buffer_encode_u8(buf, n);
            ++*count;
        }
        break;
      case SK_SIDECAR_UINT16:
        n = lua_tointegerx(L, -1, &is_num);
        if (is_num && n <= UINT16_MAX && n >= 0) {
            if (elem->name) {
                sidecar_buffer_encode_leb(buf, elem->id);
            }
            sidecar_buffer_encode_u16(buf, n);
            ++*count;
        }
        break;
      case SK_SIDECAR_UINT32:
        n = lua_tointegerx(L, -1, &is_num);
        if (is_num && n <= UINT32_MAX && n >= 0) {
            if (elem->name) {
                sidecar_buffer_encode_leb(buf, elem->id);
            }
            sidecar_buffer_encode_u32(buf, n);
            ++*count;
        }
        break;
      case SK_SIDECAR_UINT64:
        n = lua_tointegerx(L, -1, &is_num);
        if (is_num && n >= 0) {
            if (elem->name) {
                sidecar_buffer_encode_leb(buf, elem->id);
            }
            sidecar_buffer_encode_u64(buf, n);
            ++*count;
        }
        break;
      case SK_SIDECAR_DOUBLE:
        d = lua_tonumberx(L, -1, &is_num);
        if (is_num) {
            if (elem->name) {
                sidecar_buffer_encode_leb(buf, elem->id);
            }
            sidecar_buffer_encode_double(buf, d);
            ++*count;
        }
        break;
      case SK_SIDECAR_STRING:
      case SK_SIDECAR_BINARY:
        s = lua_tolstring(L, -1, &len);
        if (s) {
            if (elem->name) {
                TRACEMSG(3, ((TRC_FMT3 "Encoding #%zu, elem->id %zu,"
                              " type = %u, len = %zu"),
                             TRC_ARG3, *count, elem->id, elem->data_type,len));
                sidecar_buffer_encode_leb(buf, elem->id);
            }
            sidecar_buffer_encode_leb(buf, len);
            sidecar_buffer_encode_bytes(buf, s, len);
            ++*count;
        }
        break;
      case SK_SIDECAR_DATETIME:
        dt = sk_lua_todatetime(L, -1);
        if (dt) {
            if (elem->name) {
                sidecar_buffer_encode_leb(buf, elem->id);
            }
            sidecar_buffer_encode_u64(buf, *dt);
            ++*count;
        }
        break;
      case SK_SIDECAR_ADDR_IP4:
        ip = sk_lua_toipaddr(L, -1);
        if (ip && 0 == skipaddrGetAsV4(ip, &ip4)) {
            if (elem->name) {
                sidecar_buffer_encode_leb(buf, elem->id);
            }
            sidecar_buffer_encode_u32(buf, ip4);
            ++*count;
        }
        break;
      case SK_SIDECAR_ADDR_IP6:
        ip = sk_lua_toipaddr(L, -1);
        if (ip) {
            if (elem->name) {
                sidecar_buffer_encode_leb(buf, elem->id);
            }
            skipaddrGetAsV6(ip, ip6);
            sidecar_buffer_encode_bytes(buf, ip6, sizeof(ip6));
            ++*count;
        }
        break;
      case SK_SIDECAR_BOOLEAN:
        if (elem->name) {
            sidecar_buffer_encode_leb(buf, elem->id);
        }
        sidecar_buffer_encode_u8(buf, lua_toboolean(L, -1));
        ++*count;
        break;
      case SK_SIDECAR_EMPTY:
        if (elem->name) {
            sidecar_buffer_encode_leb(buf, elem->id);
        }
        ++*count;
        break;

      case SK_SIDECAR_LIST:
        if (LUA_TTABLE != lua_type(L, -1)) {
            break;
        }
        rv = sidecar_serialize_list(sc, elem, L, buf);
        if (rv) {
            return rv;
        }
        ++*count;
        break;

      case SK_SIDECAR_TABLE:
        if (LUA_TTABLE != lua_type(L, -1)) {
            break;
        }
        sidecar_string_buf_init(&strbuf, elem);
        rv = sidecar_serialize_table(sc, elem, L, buf, &strbuf);
        sidecar_string_buf_free(&strbuf);
        if (rv) {
            return rv;
        }
        ++*count;
        break;
        /* table acts a single item for its parent */
        ++*count;
        break;

      default:
        skAbortBadCase(elem->data_type);
    }

    return SK_SIDECAR_OK;
}


/*
 *    Increase the size of the array that holds the elements.
 */
static void
sidecar_grow_array(
    sk_sidecar_t       *sc)
{
    sc->elem_capacity += SIDECAR_ELEM_CAPACITY_STEP;

    sc->elem_by_id = sk_alloc_realloc_noclear(sc->elem_by_id,
                                              sk_sidecar_elem_t *,
                                              sc->elem_capacity);
}


/*
 *  ****************************************************************
 *  PUBLIC FUNCTIONS
 *  ****************************************************************
 */

int
sk_sidecar_create(
    sk_sidecar_t      **sc_parm)
{
    sk_sidecar_t *sc;

    if (NULL == sc_parm) {
        return SK_SIDECAR_E_NULL_PARAM;
    }
    sc = sk_alloc(sk_sidecar_t);
    sk_rbtree_create(&sc->elem_by_name, sidecar_elem_compare,
                     (sk_rbtree_free_fn_t)sidecar_elem_free, NULL);

    TRACEMSG(3, (TRC_FMT3 "Sidecar created", TRC_ARG3));

    *sc_parm = sc;
    return SK_SIDECAR_OK;
}


int
sk_sidecar_destroy(
    sk_sidecar_t      **sc_parm)
{
    sk_sidecar_t *sc;

    if (NULL == sc_parm || NULL == *sc_parm) {
        return SK_SIDECAR_OK;
    }
    sc = *sc_parm;
    *sc_parm = NULL;
    sk_rbtree_destroy(&sc->elem_by_name);
    free(sc->elem_by_id);
    free(sc);
    return SK_SIDECAR_OK;
}


void
sk_sidecar_free(
    sk_sidecar_t       *sc_parm)
{
    (void)sk_sidecar_destroy(&sc_parm);
}


int
sk_sidecar_copy(
    sk_sidecar_t      **sc_dest,
    const sk_sidecar_t *sc_src)
{
    sk_sidecar_t *sc;
    sk_sidecar_elem_t *elem;
    size_t i;

    if (NULL == sc_dest || NULL == sc_src) {
        return SK_SIDECAR_E_NULL_PARAM;
    }
    sc = sk_alloc(sk_sidecar_t);
    sk_rbtree_create(&sc->elem_by_name, sidecar_elem_compare,
                     (sk_rbtree_free_fn_t)sidecar_elem_free, NULL);
    do {
        sidecar_grow_array(sc);
    } while (sc->elem_capacity < sc_src->elem_count);

    for (i = 0; i < sc_src->elem_count; ++i) {
        elem = sidecar_elem_at(sc_src, i);
        sidecar_elem_create(sc, elem->name, elem->name_len, elem->data_type,
                            elem->list_elem_type, elem->ipfix_ident);
    }

    TRACEMSG(3,(TRC_FMT3 "Sidecar created [copy of %p]", TRC_ARG3, V(sc_src)));

    *sc_dest = sc;
    return SK_SIDECAR_OK;
}


int
sk_sidecar_add(
    sk_sidecar_t               *sc,
    const char                 *name,
    size_t                      namelen,
    sk_sidecar_type_t           data_type,
    sk_sidecar_type_t           list_elem_type,
    sk_field_ident_t            ipfix_ident,
    const sk_sidecar_elem_t    *before_elem,
    sk_sidecar_elem_t         **new_elem)
{
    sk_sidecar_elem_t *e = NULL;

    if (new_elem) {
        *new_elem = NULL;
    }
    if (NULL == sc || name == NULL) {
        return SK_SIDECAR_E_NULL_PARAM;
    }
    if (!sidecar_data_type_check(data_type, &data_type)) {
        return SK_SIDECAR_E_BAD_PARAM;
    }
    if (0 == namelen) {
        namelen = 1 + strlen(name);
    } else if ('\0' != name[namelen-1]) {
        return SK_SIDECAR_E_BAD_PARAM;
    }
    if (SK_SIDECAR_LIST == data_type) {
        switch (list_elem_type) {
          case SK_SIDECAR_LIST:
          case SK_SIDECAR_TABLE:
          case SK_SIDECAR_UNKNOWN:
            return SK_SIDECAR_E_BAD_PARAM;
          default:
            if (!sidecar_data_type_check(list_elem_type, &list_elem_type)) {
                return SK_SIDECAR_E_BAD_PARAM;
            }
        }
    }
#if 0
    if (NULL == before_elem) {
        pos = 0;
    } else if (before_elem->id >= sc->elem_count
               || before_elem != sidecar_elem_at(sc, before_elem->id))
    {
        return NULL;
    } else {
        pos = before_elem->id + 1;
    }
#endif  /* 0 */
    (void)before_elem;

    e = sidecar_elem_create(sc, name, namelen, data_type,
                            list_elem_type, ipfix_ident);
    if (NULL == e) {
        return SK_SIDECAR_E_DUPLICATE;
    }

    if (new_elem) {
        *new_elem = e;
    }
    return SK_SIDECAR_OK;
}


int
sk_sidecar_add_elem(
    sk_sidecar_t               *sc,
    const sk_sidecar_elem_t    *src_elem,
    sk_sidecar_elem_t         **new_elem)
{
    if (NULL == src_elem) {
        if (new_elem) {
            *new_elem = NULL;
        }
        return SK_SIDECAR_E_NULL_PARAM;
    }
    return sk_sidecar_add(sc, src_elem->name, src_elem->name_len,
                          src_elem->data_type, src_elem->list_elem_type,
                          src_elem->ipfix_ident, NULL, new_elem);
}


sk_sidecar_elem_t *
sk_sidecar_append(
    sk_sidecar_t       *sc,
    const char         *name,
    size_t              namelen,
    sk_sidecar_type_t   data_type,
    sk_field_ident_t    ident)
{
    sk_sidecar_elem_t *e = NULL;

    if (sk_sidecar_add(sc, name, namelen, data_type, SK_SIDECAR_UNKNOWN,
                       ident, NULL, &e)
        == SK_SIDECAR_OK)
    {
        return e;
    }
    return NULL;
}


sk_sidecar_elem_t *
sk_sidecar_append_list(
    sk_sidecar_t       *sc,
    const char         *name,
    size_t              namelen,
    sk_sidecar_type_t   list_elem_type,
    sk_field_ident_t    ident)
{
    sk_sidecar_elem_t *e = NULL;

    if (sk_sidecar_add(sc, name, namelen, SK_SIDECAR_LIST, list_elem_type,
                       ident, NULL, &e)
        == SK_SIDECAR_OK)
    {
        return e;
    }
    return NULL;
}


size_t
sk_sidecar_count_elements(
    const sk_sidecar_t *sc)
{
    if (NULL == sc) {
        return SIZE_MAX;
    }
    return sc->elem_count;
}


int
sk_sidecar_iter_bind(
    const sk_sidecar_t *sc,
    sk_sidecar_iter_t  *iter)
{
    if (NULL == sc || NULL == iter) {
        return SK_SIDECAR_E_NULL_PARAM;
    }
    iter->sc = sc;
    iter->pos = 0;
    return SK_SIDECAR_OK;
}


int
sk_sidecar_iter_next(
    sk_sidecar_iter_t          *iter,
    const sk_sidecar_elem_t   **elem)
{
    if (NULL == iter || NULL == elem) {
        return SK_ITERATOR_NO_MORE_ENTRIES;
    }
    if (iter->pos >= iter->sc->elem_count) {
        return SK_ITERATOR_NO_MORE_ENTRIES;
    }
    *elem = sidecar_elem_at(iter->sc, iter->pos);
    ++iter->pos;
    return SK_ITERATOR_OK;
}


const sk_sidecar_elem_t *
sk_sidecar_find_by_data_type(
    const sk_sidecar_t         *sc,
    sk_sidecar_type_t           data_type,
    const sk_sidecar_elem_t    *after)
{
    const sk_sidecar_elem_t *e = NULL;
    size_t pos;

    if (NULL == sc) {
        return NULL;
    }
    if (NULL == after) {
        pos = 0;
    } else if (after->id >= sc->elem_count
               || after != sidecar_elem_at(sc, after->id))
    {
        return NULL;
    } else {
        pos = after->id + 1;
    }
    for (e = sidecar_elem_at(sc, pos); pos < sc->elem_count; ++pos, ++e) {
        if (e->data_type == data_type) {
            return e;
        }
    }
    return NULL;
}


const sk_sidecar_elem_t *
sk_sidecar_find_by_id(
    const sk_sidecar_t         *sc,
    size_t                      id)
{
    if (NULL == sc || id >= sc->elem_count) {
        return NULL;
    }
    return sidecar_elem_at(sc, id);
}


const sk_sidecar_elem_t *
sk_sidecar_find_by_ipfix_ident(
    const sk_sidecar_t         *sc,
    sk_field_ident_t            ipfix_ident,
    const sk_sidecar_elem_t    *after)
{
    const sk_sidecar_elem_t *e = NULL;
    size_t pos;

    if (NULL == sc) {
        return NULL;
    }
    if (NULL == after) {
        pos = 0;
    } else if (after->id >= sc->elem_count
               || after != sidecar_elem_at(sc, after->id))
    {
        return NULL;
    } else {
        pos = after->id + 1;
    }
    for (e = sidecar_elem_at(sc, pos); pos < sc->elem_count; ++pos, ++e) {
        if (e->ipfix_ident == ipfix_ident) {
            return e;
        }
    }
    return NULL;
}


const sk_sidecar_elem_t *
sk_sidecar_find_by_name(
    const sk_sidecar_t *sc,
    const char         *name,
    size_t              namelen)
{
    const sk_sidecar_elem_t *e = NULL;

    if (NULL == sc || NULL == name) {
        return NULL;
    }
    if (0 == namelen) {
        namelen = 1 + strlen(name);
    } else if ('\0' != name[namelen]) {
        return NULL;
    }

    return e;
}


const char *
sk_sidecar_type_get_name(
    sk_sidecar_type_t   data_type)
{
    static char buf[256];

    switch (data_type) {
      case SK_SIDECAR_UNKNOWN:
        return "unknown";
      case SK_SIDECAR_UINT8:
        return "uint8";
      case SK_SIDECAR_UINT16:
        return "uint16";
      case SK_SIDECAR_UINT32:
        return "uint32";
      case SK_SIDECAR_UINT64:
        return "uint64";
      case SK_SIDECAR_DOUBLE:
        return "double";
      case SK_SIDECAR_STRING:
        return "string";
      case SK_SIDECAR_BINARY:
        return "binary";
      case SK_SIDECAR_ADDR_IP4:
        return "addr_ip4";
      case SK_SIDECAR_ADDR_IP6:
        return "addr_ip6";
      case SK_SIDECAR_DATETIME:
        return "datetime";
      case SK_SIDECAR_BOOLEAN:
        return "boolean";
      case SK_SIDECAR_EMPTY:
        return "empty";
      case SK_SIDECAR_LIST:
        return "list";
      case SK_SIDECAR_TABLE:
        return "table";
    }
    snprintf(buf, sizeof(buf), "unknown sidecar type id %d", data_type);
    return buf;
}


sk_sidecar_type_t
sk_sidecar_elem_get_data_type(
    const sk_sidecar_elem_t   *elem)
{
    if (NULL == elem) {
        return SK_SIDECAR_UNKNOWN;
    }
    return elem->data_type;
}


size_t
sk_sidecar_elem_get_id(
    const sk_sidecar_elem_t   *elem)
{
    if (NULL == elem) {
        return SIZE_MAX;
    }
    return elem->id;
}


sk_field_ident_t
sk_sidecar_elem_get_ipfix_ident(
    const sk_sidecar_elem_t   *elem)
{
    if (NULL == elem) {
        return 0;
    }
    return elem->ipfix_ident;
}


sk_sidecar_type_t
sk_sidecar_elem_get_list_elem_type(
    const sk_sidecar_elem_t   *elem)
{
    if (NULL == elem) {
        return SK_SIDECAR_UNKNOWN;
    }
    return ((SK_SIDECAR_LIST == elem->data_type)
            ? elem->list_elem_type
            : SK_SIDECAR_UNKNOWN);
}


const char *
sk_sidecar_elem_get_name(
    const sk_sidecar_elem_t   *elem,
    char                      *buf,
    size_t                    *buflen)
{
    if (NULL == elem || NULL == buf || NULL == buflen) {
        return NULL;
    }
    if (*buflen < elem->name_len) {
        buf = NULL;
    } else {
        memcpy(buf, elem->name, elem->name_len);
    }
    *buflen = elem->name_len;
    return buf;
}


size_t
sk_sidecar_elem_get_name_length(
    const sk_sidecar_elem_t   *elem)
{
    if (NULL == elem) {
        return SIZE_MAX;
    }
    return elem->name_len;
}


/*
 *  ****************************************************************
 *  Serialization
 *  ****************************************************************
 */

/**
 *    Given a data object 'data', serialize it into 'buffer' using the
 *    elements specified in the sidecar object 'sc'.
 *
 *    The 'buflen' parameter must be set to available space in
 *    'buffer'.  This function modifies that value to the number of
 *    bytes added to 'buffer'.
 *
 *    To deserialize the data, call sk_sidecar_deserialize_data().
 */
int
sk_sidecar_serialize_data(
    const sk_sidecar_t *sc,
    lua_State          *L,
    int                 lua_ref,
    uint8_t            *buffer,
    size_t             *buflen)
{
    sk_sidecar_elem_t root_elem;
    sidecar_output_buffer_t buf;
    sidecar_string_buf_t strbuf;
    int rv;

    if (NULL == sc || NULL == buffer || NULL == buflen || NULL == L) {
        TRACEMSG(3, (TRC_FMT3 "returns", TRC_ARG3));
        return SK_SIDECAR_E_NULL_PARAM;
    }
    sidecar_buffer_init(&buf, buffer, *buflen, buflen, SK_SIDECAR_E_NO_SPACE);

    if (LUA_NOREF == lua_ref || LUA_REFNIL == lua_ref) {
        *buflen = 2 * sizeof(uint16_t);
        sidecar_buffer_encode_u16(&buf, *buflen);
        sidecar_buffer_encode_u16(&buf, 0);
        TRACEMSG(3, (TRC_FMT3 "returns buflen = %zu", TRC_ARG3, *buflen));
        return SK_SIDECAR_OK;
    }
    if (lua_rawgeti(L, LUA_REGISTRYINDEX, lua_ref) != LUA_TTABLE) {
        lua_pop(L, 1);
        *buflen = 2 * sizeof(uint16_t);
        sidecar_buffer_encode_u16(&buf, *buflen);
        sidecar_buffer_encode_u16(&buf, 0);
        TRACEMSG(3, (TRC_FMT3 "returns buflen = %zu", TRC_ARG3, *buflen));
        return SK_SIDECAR_OK;
    }

    memset(&root_elem, 0, sizeof(root_elem));
    root_elem.name = NULL;
    root_elem.name_len = 0;
    root_elem.data_type = SK_SIDECAR_TABLE;

    /* create a buffer to hold element names */
    sidecar_string_buf_init(&strbuf, NULL);
    rv = sidecar_serialize_table(sc, &root_elem, L, &buf, &strbuf);
    sidecar_string_buf_free(&strbuf);
    lua_pop(L, 1);

    *buflen = buf.bp - buffer;

    TRACEMSG(3, (TRC_FMT3 "returns buflen = %zu", TRC_ARG3, *buflen));
    return rv;
}


/**
 *    Given the octet array 'buffer' that was created by a call to
 *    sk_sidecar_serialize_data() using the sidecar object 'sc',
 *    reconstitute the object represented by that buffer.
 *
 *    The 'buflen' parameter must be set to available bytes in
 *    'buffer'.  This function modifies that value to the number of
 *    bytes of 'buffer' that this function processed.
 */
int
sk_sidecar_deserialize_data(
    const sk_sidecar_t *sc,
    lua_State          *L,
    const uint8_t      *buffer,
    size_t             *buflen,
    int                *lua_ref)
{
    static uint8_t empty_data[4] = {0x0, 0x4, 0x0, 0x0};
    sk_sidecar_elem_t root_elem;
    sidecar_input_buffer_t buf;
    int rv;

    if (NULL == sc || NULL == buffer || NULL == buflen || NULL == L) {
        return SK_SIDECAR_E_NULL_PARAM;
    }
    if (*buflen >= 4 && 0 == memcmp(buffer, empty_data, sizeof(empty_data))) {
        *lua_ref = LUA_NOREF;
        *buflen = sizeof(empty_data);
        return SK_SIDECAR_OK;
    }

    sidecar_buffer_init(&buf, buffer, *buflen, buflen,SK_SIDECAR_E_SHORT_DATA);

    memset(&root_elem, 0, sizeof(root_elem));
    root_elem.name = NULL;
    root_elem.name_len = 0;
    root_elem.data_type = SK_SIDECAR_TABLE;

    TRACEMSG(3, (TRC_FMT3 "lua %p top %d", TRC_ARG3, V(L), lua_gettop(L)));
    rv = sidecar_deserialize_table(sc, &root_elem, L, &buf);
    TRACEMSG(3, (TRC_FMT3 "lua %p top %d, %s",
                 TRC_ARG3, V(L), lua_gettop(L), luaL_typename(L, -1)));

    if (SK_SIDECAR_OK != rv) {
        *lua_ref = LUA_NOREF;
    } else {
        *lua_ref = luaL_ref(L, LUA_REGISTRYINDEX);
    }

    *buflen = buf.bp - buffer;
    return rv;
}


int
sk_sidecar_skip_data(
    const sk_sidecar_t *sc,
    const uint8_t      *buffer,
    size_t             *buflen)
{
    sidecar_input_buffer_t buf;
    uint16_t u16;

    if (NULL == sc || NULL == buffer || NULL == buflen) {
        return SK_SIDECAR_E_NULL_PARAM;
    }
    sidecar_buffer_init(&buf, buffer, *buflen, buflen,SK_SIDECAR_E_SHORT_DATA);

    TRACEMSG(3, (TRC_FMT3 "skipping data", TRC_ARG3));

    /* length of the sidecar data */
    sidecar_buffer_decode_u16(&buf, u16);
    if (u16 > *buflen) {
        *buflen = buf.bp - buffer;
        return SK_SIDECAR_E_SHORT_DATA;
    }
    if (u16 < buf.bp - buffer) {
        /* length is shorter than what we've already read */
        *buflen = buf.bp - buffer;
        return SK_SIDECAR_E_DECODE_ERROR;
    }

    *buflen = u16;
    return SK_SIDECAR_OK;
}


int
sk_sidecar_serialize_self(
    const sk_sidecar_t *sc,
    uint8_t            *buffer,
    size_t             *buflen)
{
    const sk_sidecar_elem_t *e;
    sidecar_output_buffer_t buf;
    uint8_t *len_pos;
    uint16_t len;
    size_t i;

    TRACEMSG(3, (TRC_FMT3 "serializing sidecar", TRC_ARG3));

    if (NULL == sc || NULL == buffer || NULL == buflen) {
        return SK_SIDECAR_E_NULL_PARAM;
    }
    sidecar_buffer_init(&buf, buffer, *buflen, buflen, SK_SIDECAR_E_NO_SPACE);

    /* version of this format */
    sidecar_buffer_encode_u16(&buf, SIDECAR_VERSION);

    /* number of fields */
    sidecar_buffer_encode_u16(&buf, sc->elem_count);

    for (i = 0; i < sc->elem_count; ++i) {
        e = sidecar_elem_at(sc, i);

        /* remember where to insert the length */
        len_pos = buf.bp;
        sidecar_buffer_skip(&buf, sizeof(len));

        /* the length of the name */
        sidecar_buffer_encode_leb(&buf, e->name_len);
        /* the name */
        sidecar_buffer_encode_bytes(&buf, e->name, e->name_len);
        /* the data type */
        sidecar_buffer_encode_u8(&buf, e->data_type);
        /* the data type of elements in the list */
        if (SK_SIDECAR_LIST == e->data_type) {
            sidecar_buffer_encode_u8(&buf, e->list_elem_type);
        }
        if (0 != e->ipfix_ident) {
            /* IPFIX information element id */
            sidecar_buffer_encode_u16(
                &buf, SK_FIELD_IDENT_GET_ID(e->ipfix_ident));
            if (SK_FIELD_IDENT_GET_PEN(e->ipfix_ident)) {
                sidecar_buffer_encode_u32(
                    &buf, SK_FIELD_IDENT_GET_PEN(e->ipfix_ident));
            }
        }

        if (buf.bp - len_pos > UINT16_MAX) {
        }
        TRACEMSG(3, ((TRC_FMT3 "length is %ld name %lu \"%s\","
                      " type %u, ipfix_ident = %" PRIu64),
                     TRC_ARG3, (buf.bp - len_pos), e->name_len,
                     e->name, e->data_type, e->ipfix_ident));
        len = htons(buf.bp - len_pos);
        memcpy(len_pos, &len, sizeof(len));
    }

    *buflen = (buf.bp - buf.buffer);
    return SK_SIDECAR_OK;
}


int
sk_sidecar_deserialize_self(
    sk_sidecar_t       *sc,
    const uint8_t      *buffer,
    size_t             *buflen)
{
    /* const sk_sidecar_elem_t *e; */
    sidecar_input_buffer_t buf;
    uint16_t len;
    size_t i;
    uint16_t u16;
    uint16_t elem_count;
    const char *name;
    size_t namelen;
    uint8_t raw_data_type;
    uint8_t raw_list_elem_type;
    sk_sidecar_type_t data_type;
    sk_sidecar_type_t list_elem_type;
    uint16_t ipfix_id;
    uint32_t ipfix_pen;
    size_t old_avail;

    if (NULL == sc || NULL == buffer || NULL == buflen) {
        return SK_SIDECAR_E_NULL_PARAM;
    }
    sidecar_buffer_init(&buf, buffer, *buflen, buflen,SK_SIDECAR_E_SHORT_DATA);

    /* version of this format */
    sidecar_buffer_decode_u16(&buf, u16);
    if (u16 < SIDECAR_VERSION_MINIMUM || u16 > SIDECAR_VERSION_MAXIMUM) {
        *buflen = buf.bp - buffer;
        return SK_SIDECAR_E_DECODE_ERROR;
    }

    /* number of fields */
    sidecar_buffer_decode_u16(&buf, elem_count);

    while (sc->elem_capacity < elem_count) {
        sidecar_grow_array(sc);
    }

    raw_list_elem_type = list_elem_type = SK_SIDECAR_UNKNOWN;
    for (i = 0; i < elem_count; ++i) {
        ipfix_id = ipfix_pen = 0;
        TRACEMSG(3, (TRC_FMT3 "decoding field %zu", TRC_ARG3, i));
        /* note number of bytes currently available */
        old_avail = buf.avail;
        /* length of this entire entry */
        sidecar_buffer_decode_u16(&buf, len);
        /* adjust available to the length of this entry */
        TRACEMSG(3, (TRC_FMT3 "length %u", TRC_ARG3, len));
        if (len > old_avail) {
            *buflen = buf.bp - buffer;
            return SK_SIDECAR_E_SHORT_DATA;
        }
        if (len < (old_avail - buf.avail)) {
            *buflen = buf.bp - buffer;
            return SK_SIDECAR_E_DECODE_ERROR;
        }
        buf.avail = len - (old_avail - buf.avail);
        /* length of the name */
        sidecar_buffer_decode_leb(&buf, namelen);
        /* do not copy the name from the buffer; for now, just
         * remember where it is and move our pointer forward */
        name = (const char*)buf.bp;
        sidecar_buffer_skip(&buf, namelen);
        TRACEMSG(3, (TRC_FMT3 "name %zu \"%s\"", TRC_ARG3, namelen, name));
        /* the type */
        sidecar_buffer_decode_u8(&buf, raw_data_type);
        TRACEMSG(3, (TRC_FMT3 "type %u", TRC_ARG3, raw_data_type));
        if (SK_SIDECAR_LIST == (sk_sidecar_type_t)raw_data_type) {
            sidecar_buffer_decode_u8(&buf, raw_list_elem_type);
        }
        if (buf.avail) {
            /* the IPFIX IE element number */
            sidecar_buffer_decode_u16(&buf, ipfix_id);
            TRACEMSG(3, (TRC_FMT3 "ipfix id %u", TRC_ARG3, ipfix_id));
            if (buf.avail) {
                /* the IPFIX IE enterprise number */
                sidecar_buffer_decode_u32(&buf, ipfix_pen);
                TRACEMSG(3, (TRC_FMT3 "ipfix enterprise %u",
                             TRC_ARG3, ipfix_pen));
            }
        }
        /* restore available */
        buf.avail = old_avail - len;
        if (!sidecar_data_type_check(raw_data_type, &data_type)) {
            *buflen = buf.bp - buffer;
            return SK_SIDECAR_E_BAD_PARAM;
        }
        if (SK_SIDECAR_LIST == data_type
            && !sidecar_data_type_check(raw_list_elem_type, &list_elem_type))
        {
            *buflen = buf.bp - buffer;
            return SK_SIDECAR_E_BAD_PARAM;
        }

        sidecar_elem_create(sc, name, namelen, data_type, list_elem_type,
                            SK_FIELD_IDENT_CREATE(ipfix_pen, ipfix_id));
    }

    *buflen = buf.bp - buffer;
    return SK_SIDECAR_OK;
}



/*
 *  ****************************************************************
 *  File Header Entry Support
 *  ****************************************************************
 */

struct sk_hentry_sidecar_st {
    sk_header_entry_spec_t  he_spec;
    uint32_t                len;
    uint8_t                *data;
};
typedef struct sk_hentry_sidecar_st sk_hentry_sidecar_t;


static sk_header_entry_t *
sidecar_hentry_create(
    const uint8_t      *data,
    uint32_t            len)
{
    sk_hentry_sidecar_t *sc_hdr;

    sc_hdr = sk_alloc(sk_hentry_sidecar_t);
    sc_hdr->he_spec.hes_id  = SK_HENTRY_SIDECAR_ID;
    sc_hdr->he_spec.hes_len = sizeof(sk_header_entry_spec_t) + len;
    sc_hdr->len = len;

    sc_hdr->data = sk_alloc_memory(uint8_t, len, SK_ALLOC_FLAG_NO_CLEAR);
    memcpy(sc_hdr->data, data, len);

    return (sk_header_entry_t*)sc_hdr;
}


static sk_header_entry_t *
sidecar_hentry_copy(
    const sk_header_entry_t    *hentry)
{
    const sk_hentry_sidecar_t *sc_hdr = (sk_hentry_sidecar_t*)hentry;

    return sidecar_hentry_create(sc_hdr->data, sc_hdr->len);
}


static void
sidecar_hentry_free(
    sk_header_entry_t  *hentry)
{
    sk_hentry_sidecar_t *sc_hdr = (sk_hentry_sidecar_t *)hentry;

    if (sc_hdr) {
        assert(skHeaderEntryGetTypeId(sc_hdr) == SK_HENTRY_SIDECAR_ID);
        hentry->he_spec.hes_id = UINT32_MAX;

        free(sc_hdr->data);
        sc_hdr->data = NULL;
        free(sc_hdr);
    }
}


static ssize_t
sidecar_hentry_packer(
    const sk_header_entry_t    *in_hentry,
    uint8_t                    *out_packed,
    size_t                      bufsize)
{
    sk_hentry_sidecar_t *sc_hdr = (sk_hentry_sidecar_t*)in_hentry;
    uint32_t check_len;

    assert(in_hentry);
    assert(out_packed);
    assert(skHeaderEntryGetTypeId(sc_hdr) == SK_HENTRY_SIDECAR_ID);

    check_len = sizeof(sk_header_entry_spec_t) + sc_hdr->len;
    if (bufsize >= check_len) {
        /* ensure the length is correct */
        if (check_len != sc_hdr->he_spec.hes_len) {
            sc_hdr->he_spec.hes_len = check_len;
        }

        skHeaderEntrySpecPack(&(sc_hdr->he_spec), out_packed, bufsize);
        memcpy(&(out_packed[sizeof(sk_header_entry_spec_t)]),
               sc_hdr->data, sc_hdr->len);
    }

    return check_len;
}


static void
sidecar_hentry_print(
    const sk_header_entry_t    *hentry,
    FILE                       *fh)
{
    sk_hentry_sidecar_t *sc_hdr = (sk_hentry_sidecar_t*)hentry;
    sk_sidecar_t *sc = NULL;
    sk_sidecar_iter_t iter;
    const sk_sidecar_elem_t *elem;
    const char *listof;
    sk_sidecar_type_t t;
    sk_field_ident_t id;
    char buf[PATH_MAX];
    size_t buflen;
    size_t len;
    ssize_t rv;

    assert(skHeaderEntryGetTypeId(sc_hdr) == SK_HENTRY_SIDECAR_ID);

    len = sc_hdr->len;
    if ((rv = sk_sidecar_create(&sc))
        || (rv = sk_sidecar_deserialize_self(sc, sc_hdr->data, &len)))
    {
        sk_sidecar_destroy(&sc);
        fprintf(fh, "Sidecar, byte length %" PRIu32, sc_hdr->len);
        return;
    }

    fprintf(fh, "Sidecar, byte length %" PRIu32 ", element count = %" SK_PRIuZ,
            sc_hdr->len, sk_sidecar_count_elements(sc));
    sk_sidecar_iter_bind(sc, &iter);
    while (sk_sidecar_iter_next(&iter, &elem) == SK_ITERATOR_OK) {
        fprintf(fh, "\n  %20s  ", "");
        buflen = sizeof(buf);
        sk_sidecar_elem_get_name(elem, buf, &buflen);
        while ((len = 1 + strlen(buf)) < buflen) {
            buf[len] = ':';
        }
        t = sk_sidecar_elem_get_data_type(elem);
        id = sk_sidecar_elem_get_ipfix_ident(elem);
        if (SK_SIDECAR_LIST == t) {
            listof = "list of ";
            t = sk_sidecar_elem_get_list_elem_type(elem);
        } else {
            listof = "";
        }
        if (0 == id) {
            fprintf(fh, "%s, %s%s",
                    buf, listof, sk_sidecar_type_get_name(t));
        } else if (0 == SK_FIELD_IDENT_GET_PEN(id)) {
            fprintf(fh, "%s, %s%s, %d",
                    buf, listof, sk_sidecar_type_get_name(t),
                    SK_FIELD_IDENT_GET_ID(id));
        } else {
            fprintf(fh, "%s, %s%s, %d/%d",
                    buf, listof, sk_sidecar_type_get_name(t),
                    SK_FIELD_IDENT_GET_PEN(id), SK_FIELD_IDENT_GET_ID(id));
        }
    }
    sk_sidecar_destroy(&sc);
}


static sk_header_entry_t *
sidecar_hentry_unpacker(
    uint8_t            *in_packed)
{
    sk_hentry_sidecar_t *sc_hdr;

    assert(in_packed);

    /* create space for new header */
    sc_hdr = sk_alloc(sk_hentry_sidecar_t);

    /* copy the spec */
    skHeaderEntrySpecUnpack(&(sc_hdr->he_spec), in_packed);
    assert(skHeaderEntryGetTypeId(sc_hdr) == SK_HENTRY_SIDECAR_ID);

    /* copy the data */
    sc_hdr->len = sc_hdr->he_spec.hes_len;
    if (sc_hdr->len < sizeof(sk_header_entry_spec_t)) {
        free(sc_hdr);
        return NULL;
    }
    sc_hdr->len -= sizeof(sk_header_entry_spec_t);
    sc_hdr->data = sk_alloc_memory(uint8_t, sc_hdr->len,
                                   SK_ALLOC_FLAG_NO_CLEAR);
    memcpy(sc_hdr->data, &(in_packed[sizeof(sk_header_entry_spec_t)]),
           sc_hdr->len);

    return (sk_header_entry_t*)sc_hdr;
}


int
sk_sidecar_add_to_header(
    const sk_sidecar_t *sc,
    sk_file_header_t   *hdr)
{
    sk_header_entry_t *sc_hdr = NULL;
    uint8_t *buf;
    size_t buflen;
    size_t len;
    int rv;

    buf = NULL;
    buflen = 0;

    if (NULL == sc || NULL == hdr) {
        rv = SK_SIDECAR_E_NULL_PARAM;
        goto END;
    }

    /* serialize the sidecar */
    do {
        buflen += SIDECAR_DEFAULT_STRBUF_MAX;
        buf = sk_alloc_realloc_noclear(buf, uint8_t, buflen);
        len = buflen;
        rv = sk_sidecar_serialize_self(sc, buf, &len);
    } while (SK_SIDECAR_E_NO_SPACE == rv);

    if (SK_SIDECAR_OK != rv) {
        goto END;
    }

    sc_hdr = sidecar_hentry_create(buf, len);
    rv = skHeaderAddEntry(hdr, sc_hdr);
    if (rv) {
        rv = SK_SIDECAR_E_BAD_PARAM;
        sidecar_hentry_free(sc_hdr);
        goto END;
    }

    rv = SK_SIDECAR_OK;

  END:
    free(buf);
    return rv;
}


sk_sidecar_t *
sk_sidecar_create_from_header(
    const sk_file_header_t *hdr,
    int                    *status_parm)
{
    union h_un {
        sk_header_entry_t          *he;
        sk_hentry_sidecar_t        *sc;
    } h;
    sk_sidecar_t *sc = NULL;
    size_t len;
    int rv;

    if (NULL == hdr) {
        rv = SK_SIDECAR_E_NULL_PARAM;
        goto END;
    }

    h.he = skHeaderGetFirstMatch(hdr, SK_HENTRY_SIDECAR_ID);
    if (NULL == h.he) {
        rv = SK_SIDECAR_OK;
        goto END;
    }
    sk_sidecar_create(&sc);
    len = h.sc->len;
    rv = sk_sidecar_deserialize_self(sc, h.sc->data, &len);
    if (rv) {
        sk_sidecar_destroy(&sc);
    }

  END:
    if (status_parm) {
        *status_parm = rv;
    }
    return sc;
}


int
sk_sidecar_register_header_entry(
    sk_hentry_type_id_t     entry_id)
{
    assert(SK_HENTRY_SIDECAR_ID == entry_id);
    return (skHentryTypeRegister(
                entry_id, &sidecar_hentry_packer, &sidecar_hentry_unpacker,
                &sidecar_hentry_copy, &sidecar_hentry_free,
                &sidecar_hentry_print));
}


/*
** Local Variables:
** mode:c
** indent-tabs-mode:nil
** c-basic-offset:4
** End:
*/
