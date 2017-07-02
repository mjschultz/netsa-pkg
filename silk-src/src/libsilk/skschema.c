/*
** Copyright (C) 2014-2017 by Carnegie Mellon University.
**
** @OPENSOURCE_LICENSE_START@
** See license information in ../../LICENSE.txt
** @OPENSOURCE_LICENSE_END@
*/

/*
**  IPFIX-style handling of records in SiLK.
**
*/

#include <silk/silk.h>

RCSIDENT("$SiLK: skschema.c efd886457770 2017-06-21 18:43:23Z mthomas $");

#include <silk/skipaddr.h>
#include <silk/skipfixcert.h>
#include <silk/skschema.h>
#include <silk/skvector.h>
#include <silk/utils.h>

#ifdef  SKSCHEMA_TRACE_LEVEL
#define TRACEMSG_LEVEL SKSCHEMA_TRACE_LEVEL
#endif
#define TRACEMSG(lvl, msg) TRACEMSG_TO_TRACEMSGLVL(lvl, msg)
#include <silk/sktracemsg.h>



/* LOCAL DEFINES AND TYPEDEFS */

/* If non-zero, a pthread_mutex_t is enabled around the refcount */
/* #define SKSCHEMA_USE_REFCOUNT_MUTEX 0 */
#ifndef SKSCHEMA_USE_REFCOUNT_MUTEX
#define SKSCHEMA_USE_REFCOUNT_MUTEX 1
#endif

/*
 *    PEN used for temporary, transitory, or generated elements.
 *    (Currently 32473, which is reserved as Example Enterprise Number
 *    for Documentation Use)
 */
#define TEMPORARY_PEN 32473


/*
 *    The number of seconds between Jan 1, 1900 (the NTP epoch) and
 *    Jan 1, 1970 (the UNIX epoch)
 */
#define NTP_EPOCH_TO_UNIX_EPOCH     UINT64_C(0x83AA7E80)

#define NTPFRAC UINT64_C(0x100000000)


/*
 *    Macros that specify whether an IE has a particular type.  In
 *    each, the value 'x' is an fbInfoElementDataType_t.
 *
 *     TYPE_IS_IP(x)         IP Address
 *     TYPE_IS_DT(x)         Datetime
 *     TYPE_IS_INT(x)        Signed or Unsigned integer
 *     TYPE_IS_FLOAT(x)      Floating Point value
 *     TYPE_IS_STRINGLIKE(x) String or Octet Array
 *     TYPE_IS_LIST(x)       List (Structured Data)
 */
#define TYPE_IS_IP(x) ((x) == FB_IP4_ADDR || (x) == FB_IP6_ADDR)
#define TYPE_IS_DT(x) ((x) >= FB_DT_SEC  && (x) <= FB_DT_NANOSEC)
#define TYPE_IS_INT(x) ((x) >= FB_UINT_8 && (x) <= FB_INT_64)
#define TYPE_IS_FLOAT(x) ((x) == FB_FLOAT_32 || (x) == FB_FLOAT_64)
#define TYPE_IS_STRINGLIKE(x) ((x) == FB_OCTET_ARRAY || (x) == FB_STRING)
#define TYPE_IS_LIST(x) ((x) >= FB_BASIC_LIST && (x) <= FB_SUB_TMPL_MULTI_LIST)

/*
 *    Return the number of octets the fbInfoElement_t* 'igd_ie'
 *    occupies in the data array.  That is, use the size of the C
 *    structure as the length for elements that use a struct.
 */
#define IE_GET_DATALEN(igd_ie)                          \
    ((FB_IE_VARLEN != (igd_ie)->len)                    \
     ? (igd_ie)->len                                    \
     : ((FB_SUB_TMPL_MULTI_LIST == (igd_ie)->type)      \
        ? sizeof(fbSubTemplateMultiList_t)              \
        : ((FB_SUB_TMPL_LIST == (igd_ie)->type)         \
           ? sizeof(fbSubTemplateList_t)                \
           : ((FB_BASIC_LIST == (igd_ie)->type)         \
              ? sizeof(fbBasicList_t)                   \
              : sizeof(fbVarfield_t)))))


/*    Helper macro for MEM_TO_NUM() and NUM_TO_MEM() */
#define _DETERMINE_COPY_LENGTH(src, dest)               \
    size_t _slen = src;                                 \
    size_t _dlen = dest;                                \
    size_t _len = (_slen < _dlen) ? _slen : _dlen       \

#if SK_BIG_ENDIAN

/* Copies a number of length 'src_len' bytes into 'dest', which is of
 * length 'dest_size' bytes. */
#define MEM_TO_NUM(dest, dest_size, src, src_len)                       \
    {                                                                   \
        _DETERMINE_COPY_LENGTH(src_len, dest_size);                     \
        memcpy(((uint8_t *)(dest)) + _dlen - _len, (src), _len);        \
    }

/* Copies number in 'src' to 'dest'. */
#define NUM_TO_MEM(dest, dest_len, src, src_size)                       \
    {                                                                   \
        _DETERMINE_COPY_LENGTH(src_size, dest_len);                     \
        memcpy((dest), ((uint8_t *)(src)) + _slen - _len, _len);        \
    }

#else  /* !SK_BIG_ENDIAN */

#define MEM_TO_NUM(dest, dest_size, src, src_len)                       \
    {                                                                   \
        _DETERMINE_COPY_LENGTH(src_len, dest_size);                     \
        memcpy((dest), (src), _len);                                    \
    }

#define NUM_TO_MEM(dest, dest_len, src, src_size)                       \
    {                                                                   \
        _DETERMINE_COPY_LENGTH(src_size, dest_len);                     \
        memcpy((dest), (src), _len);                                    \
    }

#endif  /* SK_BIG_ENDIAN*/


#if !SKSCHEMA_USE_REFCOUNT_MUTEX
#define  REFCOUNT_MUTEX_DECLARE
#define  REFCOUNT_LOCK(rc_s)
#define  REFCOUNT_UNLOCK(rc_s)
#define  REFCOUNT_MUTEX_CREATE(rc_s)
#define  REFCOUNT_MUTEX_DESTROY(rc_s)
#else

#define  REFCOUNT_MUTEX_DECLARE  pthread_mutex_t  refcount_mutex;
#define  REFCOUNT_LOCK(rc_s)                    \
    pthread_mutex_lock(&(rc_s)->refcount_mutex)
#define  REFCOUNT_UNLOCK(rc_s)                          \
    pthread_mutex_unlock(&(rc_s)->refcount_mutex)
#define  REFCOUNT_MUTEX_CREATE(rc_s)                    \
    pthread_mutex_init(&(rc_s)->refcount_mutex, NULL)
#define  REFCOUNT_MUTEX_DESTROY(rc_s)                   \
    pthread_mutex_destroy(&(rc_s)->refcount_mutex)

#endif  /* SKSCHEMA_USE_REFCOUNT_MUTEX */


/*
 *    Assert that sk_field_t 'm_field' is on sk_fixrec_t 'm_rec' by
 *    checking that they have the same schemas.
 */
#ifdef NDEBUG
#define ASSERT_FIELD_IN_REC(m_field, m_rec)
#else
#define ASSERT_FIELD_IN_REC(m_field, m_rec)                             \
    do {                                                                \
        assert(m_field); assert(m_rec);                                 \
        if ((m_field)->schema != (m_rec)->schema) {                     \
            skAppPrintErr(                                              \
                "field %p has schema %p but record %p has schema %p",   \
                (void*)(m_field), (void*)((m_field)->schema),           \
                (void*)(m_rec), (void*)((m_rec)->schema));              \
            assert((m_field)->schema == (m_rec)->schema);               \
        }                                                               \
    } while(0)
#endif  /* NDEBUG */

/*
 *    Check that the type of the sk_field_t 'm_field' is the
 *    fbInfoElementDataType_en given in 'm_type'.  If it is not,
 *    return SK_SCHEMA_ERR_BAD_TYPE.
 */
#define FIELD_CHECK_TYPE(m_field, m_type)                       \
    if (m_type == (m_field)->ie->type) { /* no-op */ } else {   \
        return SK_SCHEMA_ERR_BAD_TYPE;                          \
    }

/*
 *    Copy the value in the sk_field_t 'm_field' from the data portion
 *    of the sk_fixrec_t 'm_rec' into the variable 'm_var'.
 *
 *    Note: 'm_var' is where to write the bytes, not the address of
 *    that location.  To use this macro, the expression
 *    "sizeof(m_var)" must return the size of the variable.
 *
 *    If the length of the field is different that the size of 'm_var'
 *    return SK_SCHEMA_ERR_BAD_SIZE.
 */
#define REC_CHECK_SIZE_SET_VAR_FROM_FIELD(m_rec, m_field, m_var)        \
    if (sizeof(m_var) != (m_field)->len) {                              \
        return SK_SCHEMA_ERR_BAD_SIZE;                                  \
    } else {                                                            \
        memcpy(&(m_var), (m_rec)->data + (m_field)->offset, sizeof(m_var)); \
    }

/*
 *    Copy the value from the variable 'v_var' into the data portion
 *    of the sk_fixrec_t 'm_rec' referenced by the sk_field_t
 *    'm_field'.
 *
 *    Note: 'm_var' is where to read the bytes, not the address of
 *    that location.  To use this macro, the expression
 *    "sizeof(m_var)" must return the size of the variable.
 *
 *    If the length of the field is different that the size of 'm_var'
 *    return SK_SCHEMA_ERR_BAD_SIZE.
 */
#define REC_CHECK_SIZE_SET_FIELD_FROM_VAR(m_rec, m_field, m_var)        \
    if (sizeof(m_var) != (m_field)->len) {                              \
        return SK_SCHEMA_ERR_BAD_SIZE;                                  \
    } else {                                                            \
        memcpy((m_rec)->data + (m_field)->offset, &(m_var), sizeof(m_var)); \
    }

/*
 *    When creating a fake schema for a basic list, the template id to
 *    use.
 */
#define BASICLIST_FAKE_SCHEMA_TID   0xFF

#if TRACEMSG_LEVEL >= 4
/*
 *    Return the size of the elements in an fbBasicList_t.
 */
#define SK_FB_ELEM_SIZE_BL(m_bl)                                \
    (((m_bl).numElements)                                       \
     ? (unsigned int)((m_bl).dataLength / (m_bl).numElements)   \
     : 0u)
/*
 *    Return the size of the elements in an fbSubTemplateList_t.
 */
#define SK_FB_ELEM_SIZE_STL(m_stl)                                      \
    (((m_stl).numElements)                                              \
     ? (unsigned int)((m_stl).dataLength.length / (m_stl).numElements)  \
     : 0u)
/*
 *    Return the size of the elements in an fbSubTemplateMultiListEntry_t.
 */
#define SK_FB_ELEM_SIZE_STMLE(m_stmle) SK_FB_ELEM_SIZE_BL(m_stmle)

#endif  /* TRACEMSG_LEVEL >= 4 */

/*
 *  ********************
 *  Field List
 *  ********************
 */

/**
 *    sk_field_list_t is a structure to hold the list of sk_field_t's
 *    inside a schema.
 *
 *    The implementation uses either a sk_vector_t or standard C
 *    array.  It is meant to be used as a vector of fields that is
 *    later converted to an array (frozen) for efficiency reasons.
 *    Use the FIELD_LIST_* macros to operate on field_lists.
 */
typedef struct field_list_st {
    /* number of elements in the array; when this is SIZE_MAX, then
     * the vector is in use */
    size_t count;
    /* the list of sk_field_t's. */
    union {
        sk_vector_t  *vec;
        sk_field_t  **array;
    } data;
} field_list_t;

/*
 *    Return true if the field_list value 'va' has been set (initialized)
 */
#define FIELD_LIST_IS_SET(va) ((va).data.array != NULL)

/*
 *    Return true if the field_list value 'va' is a vector
 */
#define FIELD_LIST_IS_VEC(va) (SIZE_MAX == (va).count)

/*
 *    Initialize a field_list
 */
#define FIELD_LIST_INIT(va)                                     \
    {                                                           \
        (va).data.vec = sk_vector_create(sizeof(sk_field_t *)); \
        (va).count = SIZE_MAX;                                  \
    }

/*
 *    Freeze a field_list.  This turns a field_list in vector form
 *    into a field_list in array form.  After this change is made, the
 *    list can no longer be modified.
 */
#define FIELD_LIST_FREEZE(va)                                   \
    {                                                           \
        void *_a;                                               \
        assert(FIELD_LIST_IS_VEC(va));                          \
        _a = sk_vector_to_array_alloc((va).data.vec);           \
        (va).count = sk_vector_get_count((va).data.vec);        \
        sk_vector_destroy((va).data.vec);                       \
        (va).data.array = (sk_field_t **)_a;                    \
    }

/*
 *    Free the content of a field_list, re-setting the field list to
 *    its un-initialized value.
 */
#define FIELD_LIST_CLEAR(va)                    \
    if (FIELD_LIST_IS_SET(va)) {                \
        if (FIELD_LIST_IS_VEC(va)) {            \
            sk_vector_destroy((va).data.vec);   \
        } else {                                \
            free((va).data.array);              \
        }                                       \
        memset(&(va), 0, sizeof(va));           \
    }

/*
 *    Append a field pointer to a field_list.
 */
#define FIELD_LIST_APPEND(va, field_ptr)                        \
    {                                                           \
        assert(FIELD_LIST_IS_VEC(va));                          \
        sk_vector_append_value((va).data.vec, &field_ptr);      \
    }

/*
 *    Return the number of entries in a vector-based field_list.
 */
#define FIELD_LIST_VEC_COUNT(va)                \
    (assert(FIELD_LIST_IS_VEC(va)),             \
     sk_vector_get_count((va).data.vec))

/*
 *    Return the i'th field pointer in a vector-based field_list.
 *    Return NULL if i is out of range.
 */
#define FIELD_LIST_VEC_GET(va, i)                                       \
    (assert(FIELD_LIST_IS_VEC(va)),                                     \
     *(sk_field_t **)sk_vector_get_value_pointer((va).data.vec, i))

/*
 *    Return the number of entries in an array-based field_list.
 */
#define FIELD_LIST_ARRAY_COUNT(va)                      \
    (assert(!FIELD_LIST_IS_VEC(va)), (va).count)

/*
 *    Return the i'th field pointer in an array-based field_list.
 *    Return arbitrary memory if i is out of range.
 */
#define FIELD_LIST_ARRAY_GET(va, i)                             \
    (assert(!FIELD_LIST_IS_VEC(va)), (va).data.array[i])

/*
 *    Return the number of entries in a field_list.
 */
#define FIELD_LIST_COUNT(va)                    \
    ((FIELD_LIST_IS_VEC(va))                    \
     ? FIELD_LIST_VEC_COUNT(va)                 \
     : FIELD_LIST_ARRAY_COUNT(va))

/*
 *    Return the i'th field pointer in a field_list.  Possibly return
 *    arbitrary memory if i is out of range.
 */
#define FIELD_LIST_GET(va, i)                   \
    ((FIELD_LIST_IS_VEC(va))                    \
     ? FIELD_LIST_VEC_GET(va, i)                \
     : FIELD_LIST_ARRAY_GET(va, i))

/*
 *    Return the i'th field pointer in a field_list.  Return NULL if i
 *    is out of range.
 */
#define FIELD_LIST_GET_SAFE(va, i)              \
    ((FIELD_LIST_IS_VEC(va))                    \
     ? (((i) < FIELD_LIST_VEC_COUNT(va))        \
        ? FIELD_LIST_VEC_GET(va, i)             \
        : NULL)                                 \
     : (((i) < FIELD_LIST_ARRAY_COUNT(va))      \
        ? FIELD_LIST_ARRAY_GET(va, i)           \
        : NULL))


/* sk_field_t */


/*
//  FIXME: need to add info here for lists to maintain the templates
//  they use to avoid having them be garbage collected.

//  Current the sk_fixlist_subtemplate_t structure maintains a vector
//  of schemas for the entries in the list.  This is a partial
//  solution to the problem of a schema that owns the fbTemplate_t
//  being used by a subTemplateMultiListEntry being destroyed while
//  the list is still active.  This is not a complete solution,
//  however, since once the data portion of the fixbuf list structures
//  are copied into the containing record, the containing record no
//  longer has the fixlist structure that contains the schemas.  The
//  proper solution is to probably to add the fbTemplate_t's to the
//  session on top-level record.
*/

struct sk_field_st {
    const fbInfoElement_t  *ie;
    const sk_schema_t      *schema;
    sk_field_ops_t          ops;
    uint16_t                len;
    uint16_t                offset;
};
/* typdef struct sk_field_st sk_field_t; */

/*
 *    sk_schema_ctx_t contains a context pointer and a pointer to the
 *    user's callback function to free that pointer.
 *
 *    sk_schema_t contains an array of these, which the user sets by
 *    calling sk_schema_set_context().
 */
struct sk_schema_ctx_st {
    void          *ptr;
    void         (*free_fn)(void *);
};
typedef struct sk_schema_ctx_st sk_schema_ctx_t;


/* sk_schema_t */
struct sk_schema_st {
    /* all IEs used directly by this schema */
    field_list_t        fields;
    /* IEs used directly by this schema that are fbVarfield_t */
    field_list_t        varfields;
    /* IEs used directly by this schema that are lists */
    field_list_t        listfields;
    /* IEs used directly by this schema that are computed/plug-in */
    field_list_t        computed_fields;
    /* template used by this schema */
    fbTemplate_t       *tmpl;
    /* information model used by this schema */
    fbInfoModel_t      *model;
    /* session used by this schema */
    fbSession_t        *session;
    /* array of structures that contain a pointer and that pointer's
     * free() function; set by sk_schema_set_context() */
    sk_schema_ctx_t    *ctx;
    /* size of the 'ctx' array */
    size_t              ctx_count;
    /* number of references to this schema */
    uint32_t            refcount;
    /* template ID used by this schema */
    uint16_t            tid;
    /* length of 'data' array in the sk_fixrec_t that uses this
     * schema; uses sizeof fixbuf structures for varfields, lists */
    uint16_t            len;
    /* whether 'model' pointer is owned by this schema */
    unsigned            owns_model   : 1;
    /* whether 'session' pointer is owned by this schema */
    unsigned            owns_session : 1;
    /* mutex for protecting the 'refcount' */
    REFCOUNT_MUTEX_DECLARE
};


/*
 *    sk_fixlist_basic_t is used as the bl member of the union in
 *    sk_fixlist_t when the sk_fixlist_t represents a basicList
 */
struct sk_fixlist_basic_st {
    /* The fixbuf list object */
    fbBasicList_t           fb_list;
    /* Length of the field or struct for list/varlen fields */
    uint16_t                item_len;
    /* The "fake" schema for this list */
    const sk_schema_t      *schema;
    /* A field representation of the single IE; this points into the
     * 'schema' member */
    const sk_field_t       *field;
    /* The single information element in this list */
    const fbInfoElement_t  *ie;
    /* The element returned by sk_fixlist_get_element(); the schema of
     * this record is 'schema'. */
    sk_fixrec_t             element;
};
typedef struct sk_fixlist_basic_st sk_fixlist_basic_t;

/*
 *    sk_fixlist_subtemplate_t is used as the stl member of the union
 *    in sk_fixlist_t when the sk_fixlist_t represents a
 *    subTemplateList
 */
struct sk_fixlist_subtemplate_st {
    /* The fixbuf list object */
    fbSubTemplateList_t     fb_list;
    /* The schema for this list */
    const sk_schema_t      *schema;
    /* The element returned by sk_fixlist_get_element(); the schema of
     * this record is 'schema'. */
    sk_fixrec_t             element;
};
typedef struct sk_fixlist_subtemplate_st sk_fixlist_subtemplate_t;

/*
 *    sk_fixlist_subtemplatemulti_t is used as the stml member of the
 *    union in sk_fixlist_t when the sk_fixlist_t represents a
 *    subTemplateMultiList
 */
struct sk_fixlist_subtemplatemulti_st {
    /* The fixbuf list object */
    fbSubTemplateMultiList_t        fb_list;
    /* Vector of schemas that exist on this fixlist */
    sk_vector_t                    *schema_vec;
    /* Info model pointer when the list owns the model */
    fbInfoModel_t                  *model;
    /* When iterating, the most recent record returned; its schema is
     * in 'schema_vec' at position 'iter_pos' */
    sk_fixrec_t                     iter_element;
    /* For random access, the most recent record returned; its schema
     * is in 'schema_vec' at position 'rand_pos' */
    sk_fixrec_t                     rand_element;
    /* Index in 'schema_vec' of most recent {stmlEntry,schema} pair
     * returned by sk_fixlist_next_element() (i.e. interation) */
    uint16_t                        iter_pos;
    /* Index in 'schema_vec' of most recent {stmlEntry,schema} pair
     * returned by sk_fixlist_get_element() (i.e. random access) */
    uint16_t                        rand_pos;
};
typedef struct sk_fixlist_subtemplatemulti_st sk_fixlist_subtemplatemulti_t;

/*
 *    sk_fixlist_t
 */
struct sk_fixlist_st {
    /* The type of list */
    fbInfoElementDataType_t     type;
    /* When iterating, the position within the list. */
    uint16_t                    iter_idx;
    /* Whether the fbVarfield_t data in the list is owned by fixbuf
     * (0==no, 1==yes) */
    unsigned                    fixbuf_owns_vardata  :1;
    /* Whether the iterator has processed all entries (0==no, 1==yes) */
    unsigned                    iter_no_more_entries :1;
    /* The record containing the list; when this is set, the fixlist
     * is read-only.  Either this member or the session member must be
     * non-NULL and the other must be NULL. */
    const sk_fixrec_t          *containing_rec;
    /* A session to hold templates used by elements in this list and
     * any sublists when building a list.  Either this member or the
     * containing_rec member must be non-NULL and the other must be
     * NULL. */
    fbSession_t                *session;
    /* Values specific to the type of list */
    union t_un {
        sk_fixlist_basic_t              bl;
        sk_fixlist_subtemplate_t        stl;
        sk_fixlist_subtemplatemulti_t   stml;
    }                           t;
};
/* typdef struct sk_fixlist_st sk_fixlist_t; */


/*
 *    sk_fixrec_template_map_t is a data structure to keep track of
 *    the templates that exist in a record and the template IDs that
 *    are in use.
 *
 *    This data structure is a vector instead of red-black tree
 *    because we expect the number of templates used by any
 *    schema/record to be relatively small and we need to be able to
 *    search by either template pointer or template id.
 */
typedef sk_vector_t sk_fixrec_template_map_t;

/*
 *    sk_fixrec_template_map_entry_t is the data structure for an
 *    individual entry (a template pointer/template id pair) in the
 *    sk_fixrec_template_map_t.
 */
struct sk_fixrec_template_map_entry_st {
    fbTemplate_t       *tmpl;
    uint16_t            tid;
};
typedef struct sk_fixrec_template_map_entry_st sk_fixrec_template_map_entry_t;


/*
 *    A schemamap is used to map fields between different schemas---to
 *    "transcode" them in the libfixbuf parlance.
 *
 *    A schemamap is comprised of one or more sk_schemamap_t
 *    structures allocated as a single array.  Each sk_schemamap_t has
 *    an "op_type" (from the sk_schemamap_op_t enum) and values needed
 *    by the op_type.  The final op_type in the schemamap is always
 *    SK_SCHEMAMAP_DONE.
 *
 *    At present, a list (structured data) element may only be mapped
 *    into a list of the same type.
 */

typedef enum sk_schemamap_op_en {
    /* End of schemamap array marker */
    SK_SCHEMAMAP_DONE,
    /* Copy a range of octets */
    SK_SCHEMAMAP_COPY,
    /* Copy a range of bytes into an fbVarfield_t */
    SK_SCHEMAMAP_COPY_TO_VARLEN,
    /* Copy an fbVarfield_t into a range of bytes */
    SK_SCHEMAMAP_COPY_FROM_VARLEN,
    /* Copy between fbVarfield_t's */
    SK_SCHEMAMAP_COPY_VARLEN_TO_VARLEN,
    /* Copy floating point values */
    SK_SCHEMAMAP_COPY_F32_TO_F64,
    /* Copy floating point values */
    SK_SCHEMAMAP_COPY_F64_TO_F32,
    /* Copy datetime; uses sk_schemamap_dt_t */
    SK_SCHEMAMAP_COPY_DATETIME,
    /* Copy an fbBasicList_t */
    SK_SCHEMAMAP_COPY_BASIC_LIST,
    /* Copy an fbSubTemplateList_t */
    SK_SCHEMAMAP_COPY_SUB_TMPL_LIST,
    /* Copy an fbSubTemplateMultiList_t */
    SK_SCHEMAMAP_COPY_SUB_TMPL_MULTI_LIST,
    /* Range of octets to clear; uses sk_schemamap_range_t */
    SK_SCHEMAMAP_ZERO,
    /* Schemas are identical, use sk_fixrec_copy_into() */
    SK_SCHEMAMAP_RECORD_COPY_INTO
} sk_schemamap_op_t;

typedef struct sk_schemamap_copy_st {
    uint16_t    from;
    uint16_t    to;
    uint16_t    length;
} sk_schemamap_copy_t;

typedef struct sk_schemamap_range_st {
    uint16_t    offset;
    uint16_t    length;
} sk_schemamap_range_t;

typedef struct sk_schemamap_dt_st {
    uint16_t    from;
    uint16_t    to;
    uint8_t     from_type;
    uint8_t     to_type;
} sk_schemamap_dt_t;

struct sk_schemamap_st {
    sk_schemamap_op_t   op_type;
    union schemamap_op_un {
        sk_schemamap_copy_t     copy;
        sk_schemamap_range_t    zero;
        sk_schemamap_dt_t       dt;
    }                   op;
};
/* typedef struct sk_schemamap_st sk_schemamap_t; */


/*
 *    sk_schema_timemap_t is a structure used to map between time
 *    fields on a schema.  Created by sk_schema_timemap_create();
 *    destroyed by sk_schema_timemap_destroy(); applied to a record
 *    using sk_schema_timemap_apply().
 */
struct sk_schema_timemap_st {
    /* The schema for which the timemap was created */
    const sk_schema_t  *schema;
    /* The start millisecond field */
    const sk_field_t   *start_msec;
    /* The end millisecond field */
    const sk_field_t   *end_msec;
    /* Whatever start-time field the schema holds, or NULL if the
     * schema already has an start_msec field. */
    const sk_field_t   *rec_start;
    /* Whatever end-time field the schema holds, or NULL if the schema
     * already has an end_msec field. */
    const sk_field_t   *rec_end;
    /* The initialization time (router boot time) field */
    const sk_field_t   *rec_init;
};

/*
 *    Field idents to use in the sk_schema_timemap_create() and
 *    sk_schema_timemap_apply() functions
 */
#define SK_SCHEMA_TIMEMAP_EN_DEFINE                             \
    enum sk_schema_timemap_en {                                 \
        start_sec         = SK_FIELD_IDENT_CREATE(0, 150),      \
        end_sec           = SK_FIELD_IDENT_CREATE(0, 151),      \
        start_milli       = SK_FIELD_IDENT_CREATE(0, 152),      \
        end_milli         = SK_FIELD_IDENT_CREATE(0, 153),      \
        start_micro       = SK_FIELD_IDENT_CREATE(0, 154),      \
        end_micro         = SK_FIELD_IDENT_CREATE(0, 155),      \
        start_nano        = SK_FIELD_IDENT_CREATE(0, 156),      \
        end_nano          = SK_FIELD_IDENT_CREATE(0, 157),      \
        start_delta_micro = SK_FIELD_IDENT_CREATE(0, 158),      \
        end_delta_micro   = SK_FIELD_IDENT_CREATE(0, 159),      \
        start_uptime      = SK_FIELD_IDENT_CREATE(0,  22),      \
        end_uptime        = SK_FIELD_IDENT_CREATE(0,  21),      \
        dur_milli         = SK_FIELD_IDENT_CREATE(0, 161),      \
        dur_micro         = SK_FIELD_IDENT_CREATE(0, 162),      \
        sys_init_time     = SK_FIELD_IDENT_CREATE(0, 160)       \
    }


/* LOCAL FUNCTION DECLARATIONS */

static void
sk_fixrec_update_session_basic(
    fbSession_t        *session,
    const void         *src_pos,
    unsigned int        ext_int);
static void
sk_fixrec_update_session_subtemplate(
    fbSession_t        *session,
    const void         *src_pos,
    unsigned int        ext_int);
static void
sk_fixrec_update_session_subtemplatemulti(
    fbSession_t        *session,
    const void         *src_pos,
    unsigned int        ext_int);

static void sk_fixrec_copy_list_basic(
    void                       *dest,
    const void                 *src,
    sk_fixrec_template_map_t   *tmpl_map);
static void sk_fixrec_copy_list_subtemplate(
    void                       *dest,
    const void                 *src,
    sk_fixrec_template_map_t   *tmpl_map);
static void sk_fixrec_copy_list_subtemplatemulti(
    void                       *dest,
    const void                 *src,
    sk_fixrec_template_map_t   *tmpl_map);

static void sk_fixrec_free_list_basic(void *bl);
static void sk_fixrec_free_list_subtemplate(void *stl);
static void sk_fixrec_free_list_subtemplatemulti(void *stml);

static void
sk_fixrec_template_map_add_record(
    sk_fixrec_template_map_t   *tmpl_map,
    const sk_fixrec_t          *rec);
static void
sk_fixrec_template_map_add_basic(
    sk_fixrec_template_map_t   *tmpl_map,
    const void                 *src_pos);
static void
sk_fixrec_template_map_add_subtemplate(
    sk_fixrec_template_map_t   *tmpl_map,
    const void                 *src_pos);
static void
sk_fixrec_template_map_add_subtemplatemulti(
    sk_fixrec_template_map_t   *tmpl_map,
    const void                 *src_pos);
static sk_fixrec_template_map_t *
sk_fixrec_template_map_create(
    const sk_fixrec_t  *rec);
static void
sk_fixrec_template_map_destroy(
    sk_fixrec_template_map_t   *tmpl_map);
static void
sk_fixrec_template_map_insert(
    sk_fixrec_template_map_t   *tmpl_map,
    fbTemplate_t               *tmpl,
    uint16_t                    tid);
static sk_schema_err_t
sk_fixrec_template_map_update_session(
    sk_fixrec_template_map_t   *tmpl_map,
    fbSession_t                *session);



/* FUNCTION DEFINITIONS */

/*
 *    Similar to fbInfoModelAddElement(), with the following two
 *    differences: (1)If the PEN and Id are zero, a unique Id is
 *    assigned in the PEN specified by the TEMPORARY_PEN macro.
 *    (2)The fbInfoElement_t in the model is returned (or NULL if
 *    there was a problem).
 *
 *    BUGS: (1) This code is not thread safe.  (2) There is one global
 *    counter for the temporary ID across all info-models---this
 *    should not be too much of an issue, since the number of info
 *    models is probably small. (3) Although this code ensures the ID
 *    it assigns is unique in the model in its current state, it is
 *    possible for the ID to duplicate an ID that exists in a file we
 *    read later.  Since the IPFIX model contains nearly all data
 *    about the elemnents, this could potentially cause confusion.
 */
static const fbInfoElement_t *
sk_infomodel_add_element(
    fbInfoModel_t          *model,
    const fbInfoElement_t  *ie)
{
    /* Next temporary IE ID to use */
    static uint16_t next_temporary_id = 1;
    uint16_t search_start;

    fbInfoElement_t copy = *ie;
    if (copy.ent == 0 && copy.num == 0) {
        /* create a new id and avoid using a value that is already in
         * the info model */
        copy.ent = TEMPORARY_PEN;
        search_start = next_temporary_id;
        do {
            copy.num = next_temporary_id++;
            if (next_temporary_id > INT16_MAX) {
                next_temporary_id = 1;
            }
        } while (fbInfoModelGetElementByID(model, copy.num, copy.ent)
                 && next_temporary_id != search_start);
    }
    fbInfoModelAddElement(model, &copy);
    return fbInfoModelGetElementByID(model, copy.num, copy.ent);
}

/*
 *    Initialize an sk_field_ops_t to default values.
 */
static void
sk_field_ops_init(
    sk_field_ops_t     *ops)
{
    /* Currently default values are all zeroes */
    memset(ops, 0, sizeof(*ops));
}

sk_schema_err_t
sk_field_set_length(
    sk_field_t         *field,
    uint16_t            size)
{
    assert(field);
    if (field->schema->tmpl) {
        return SK_SCHEMA_ERR_FROZEN;
    }
    field->len = size;
    return 0;
}

const fbInfoElement_t *
sk_field_get_ie(
    const sk_field_t   *field)
{
    assert(field);
    if (field->schema->tmpl) {
        return field->ie->ref.canon;
    }
    return field->ie;
}

const char *
sk_field_get_name(
    const sk_field_t   *field)
{
    assert(field);
    if (field->schema->tmpl) {
        return field->ie->ref.canon->ref.name;
    }
    return field->ie->ref.name;
}

const char *
sk_field_get_description(
    const sk_field_t   *field)
{
    return field->ie->description;
}

sk_field_ops_t *
sk_field_get_ops(
    sk_field_t         *field)
{
    assert(field);
    return &field->ops;
}

uint16_t
sk_field_get_length(
    const sk_field_t   *field)
{
    assert(field);
    return field->len;
}

sk_field_ident_t
sk_field_get_ident(
    const sk_field_t   *field)
{
    assert(field);
    return SK_FIELD_IDENT_CREATE(field->ie->ent, field->ie->num);
}

uint32_t
sk_field_get_pen(
    const sk_field_t   *field)
{
    assert(field);
    return field->ie->ent;
}

uint16_t
sk_field_get_id(
    const sk_field_t   *field)
{
    assert(field);
    return field->ie->num;
}

uint8_t
sk_field_get_type(
    const sk_field_t   *field)
{
    assert(field);
    return field->ie->type;
}

const char *
sk_field_get_type_string(
    const sk_field_t   *field)
{
    static char invalid_type[128];

    assert(field);
    switch ((fbInfoElementDataType_t)field->ie->type) {
      case FB_BOOL:
        return "boolean";
      case FB_UINT_8:
        return "unsigned8";
      case FB_UINT_16:
        return "unsigned16";
      case FB_UINT_32:
        return "unsigned32";
      case FB_UINT_64:
        return "unsigned64";
      case FB_INT_8:
        return "signed8";
      case FB_INT_16:
        return "signed16";
      case FB_INT_32:
        return "signed32";
      case FB_INT_64:
        return "signed64";
      case FB_FLOAT_32:
        return "float32";
      case FB_FLOAT_64:
        return "float64";
      case FB_MAC_ADDR:
        return "macAddress";
      case FB_IP4_ADDR:
        return "ipv4Address";
      case FB_IP6_ADDR:
        return "ipv6Address";
      case FB_STRING:
        return "string";
      case FB_OCTET_ARRAY:
        return "octetArray";
      case FB_DT_SEC:
        return "dateTimeSeconds";
      case FB_DT_MILSEC:
        return "dateTimeMilliseconds";
      case FB_DT_MICROSEC:
        return "dateTimeMicroseconds";
      case FB_DT_NANOSEC:
        return "dateTimeNanoseconds";
      case FB_BASIC_LIST:
        return "basicList";
      case FB_SUB_TMPL_LIST:
        return "subTemplateList";
      case FB_SUB_TMPL_MULTI_LIST:
        return "subTemplateMultiList";
    }
    snprintf(invalid_type, sizeof(invalid_type), "invalidType#%u",
             field->ie->type);
    return invalid_type;
}

uint8_t
sk_field_get_semantics(
    const sk_field_t   *field)
{
    assert(field);
    return FB_IE_SEMANTIC(field->ie->flags);
}

uint16_t
sk_field_get_units(
    const sk_field_t   *field)
{
    assert(field);
    return FB_IE_UNITS(field->ie->flags);
}

uint64_t
sk_field_get_max(
    const sk_field_t   *field)
{
    return field->ie->max;
}

uint64_t
sk_field_get_min(
    const sk_field_t   *field)
{
    return field->ie->min;
}

/*
 *    Destroy the field at 'field'.  If the object has a teardown()
 *    method, call that function and return that function's return
 *    code.
 */
static sk_schema_err_t
sk_field_destroy(
    sk_field_t         *field)
{
    sk_schema_err_t err = 0;
    if (field) {
        if (field->ops.teardown) {
            err = field->ops.teardown(field);
        }
        free(field);
    }
    return err;
}

void
sk_field_print_debug(
    const sk_field_t   *f)
{
    const char *data_type_list[] = {
        "FB_OCTET_ARRAY",
        "FB_UINT_8", "FB_UINT_16", "FB_UINT_32", "FB_UINT_64",
        "FB_INT_8", "FB_INT_16", "FB_INT_32", "FB_INT_64",
        "FB_FLOAT_32", "FB_FLOAT_64", "FB_BOOL",
        "FB_MAC_ADDR", "FB_STRING",
        "FB_DT_SEC", "FB_DT_MILSEC", "FB_DT_MICROSEC", "FB_DT_NANOSEC",
        "FB_IP4_ADDR", "FB_IP6_ADDR",
        "FB_BASIC_LIST", "FB_SUB_TMPL_LIST", "FB_SUB_TMPL_MULTI_LIST"
    };
    const char *units_list[] = {
        "NONE", "FB_UNITS_BITS",
        "FB_UNITS_OCTETS", "FB_UNITS_PACKETS", "FB_UNITS_FLOWS",
        "FB_UNITS_SECONDS", "FB_UNITS_MILLISECONDS", "FB_UNITS_MICROSECONDS",
        "FB_UNITS_NANOSECONDS",
        "FB_UNITS_WORDS", "FB_UNITS_MESSAGES", "FB_UNITS_HOPS",
        "FB_UNITS_ENTRIES", "FB_UNITS_FRAMES"
    };
    const char *semantics_list[] = {
        "FB_IE_DEFAULT", "FB_IE_QUANTITY",
        "FB_IE_TOTALCOUNTER", "FB_IE_DELTACOUNTER",
        "FB_IE_IDENTIFIER", "FB_IE_FLAGS", "FB_IE_LIST"
    };
    const char *data_type;
    const char *units;
    const char *semantics;

    if (NULL == f) {
        fprintf(stderr, "field(%p) => null\n", (void*)f);
        return;
    }

    if (sk_field_get_type(f) < sizeof(data_type_list)/sizeof(char*)) {
        data_type = data_type_list[sk_field_get_type(f)];
    } else {
        data_type = "out_of_range";
    }
    if (sk_field_get_units(f) < sizeof(units_list)/sizeof(char*)) {
        units = units_list[sk_field_get_units(f)];
    } else {
        units = "out_of_range";
    }
    if (sk_field_get_semantics(f) < sizeof(semantics_list)/sizeof(char*)) {
        semantics = semantics_list[sk_field_get_semantics(f)];
    } else {
        semantics = "out_of_range";
    }

    fprintf(stderr,
            ("field(%p) => \"%s\" %" PRIu32 "/%" PRIu16
             ", len=%" PRIu16 ", offset=%" PRIu16
             ", type=%s, units=%s, semantics=%s"
             ", range=%" PRIu64 "-%" PRIu64
             ", ops=%p\n"),
            (void*)f,
            sk_field_get_name(f), sk_field_get_pen(f), sk_field_get_id(f),
            sk_field_get_length(f), f->offset,
            data_type, units, semantics,
            sk_field_get_min(f), sk_field_get_max(f),
            (void*)sk_field_get_ops((sk_field_t*)f));
}


/*
 *    INTERNAL.
 *
 *    Allocate, initialize, and return a new schema that uses the info
 *    model 'model'.  If 'model' is NULL, use the global model.
 *
 *    The reference count is set to 1; the template ID is set to
 *    FB_TID_AUTO.
 */
static sk_schema_t *
sk_schema_alloc(
    fbInfoModel_t      *model)
{
    sk_schema_t *schema;

    schema = sk_alloc(sk_schema_t);
    schema->tid = FB_TID_AUTO;
    schema->refcount = 1;
    REFCOUNT_MUTEX_CREATE(schema);

    if (NULL == model) {
        schema->model = skipfix_information_model_create(0);
        schema->owns_model = 1;
    } else {
        schema->model = model;
    }
    return schema;
}

#ifndef SKSCHEMA_CREATE_TRACE
/*    If not tracing, each of the FOO_impl() functions below is the
 *    implementation of the function FOO().  */
#define sk_schema_create_impl sk_schema_create
#define sk_schema_clone_impl  sk_schema_clone
#else
/*    When tracing, print the log message then call the FOO_impl()
 *    function explicitly. */
static sk_schema_err_t
sk_schema_create_impl(
    sk_schema_t               **schema,
    fbInfoModel_t              *model,
    const fbInfoElementSpec_t  *spec,
    uint32_t                    flags);

static const sk_schema_t *
sk_schema_clone_impl(
    const sk_schema_t          *schema);

sk_schema_err_t
sk_schema_create_trace(
    sk_schema_t               **schema,
    fbInfoModel_t              *model,
    const fbInfoElementSpec_t  *spec,
    uint32_t                    flags,
    const char                 *filename,
    int                         linenum)
{
    TRACEMSG(3,("sk_schema_create() called from %s:%d", filename, linenum));
    return sk_schema_create_impl(schema, model, spec, flags);
}

const sk_schema_t *
sk_schema_clone_trace(
    const sk_schema_t          *schema,
    const char                 *filename,
    int                         linenum)
{
    TRACEMSG(3,("sk_schema_clone() called from %s:%d", filename, linenum));
    return sk_schema_clone_impl(schema);
}
#endif  /* SKSCHEMA_CREATE_TRACE */

/* sk_schema_create() */
sk_schema_err_t
sk_schema_create_impl(
    sk_schema_t               **schema,
    fbInfoModel_t              *model,
    const fbInfoElementSpec_t  *spec,
    uint32_t                    flags)
{
    sk_schema_t *s;
    sk_schema_err_t err = SK_SCHEMA_ERR_MEMORY;

    assert(schema);

    s = sk_schema_alloc(model);
    FIELD_LIST_INIT(s->fields);

    if (spec) {
        sk_field_t *field;

        /* Add the spec to the template */
        for ( ; spec->name; ++spec) {
            if (flags && spec->flags
                && ((flags & spec->flags) != spec->flags)) {
                continue;
            }
            field = sk_alloc(sk_field_t);
            sk_field_ops_init(&field->ops);
            field->schema = s;
            field->ie = ((fbInfoElement_t *)
                         fbInfoModelGetElementByName(s->model, spec->name));
            if (field->ie == NULL) {
                sk_field_destroy(field);
                err = SK_SCHEMA_ERR_UNKNOWN_IE;
                goto ERR;
            }
            field->len = (spec->len_override
                          ? spec->len_override
                          : field->ie->len);
            FIELD_LIST_APPEND(s->fields, field);
        }
    }
    *schema = s;
    TRACEMSG(3,("sk_schema_create() %p->refcount = %" PRIu32, s, s->refcount));

    return 0;

  ERR:
    sk_schema_destroy(s);
    return err;
}

/* sk_schema_clone() */
const sk_schema_t *
sk_schema_clone_impl(
    const sk_schema_t  *schema)
{
    sk_schema_t *s = (sk_schema_t *)schema;

    assert(schema);
    REFCOUNT_LOCK(s);
    ++s->refcount;
    REFCOUNT_UNLOCK(s);
    TRACEMSG(3,("sk_schema_clone() %p->refcount = %" PRIu32, s, s->refcount));

    return schema;
}


sk_schema_err_t
sk_schema_create_from_template(
    sk_schema_t       **schema,
    fbInfoModel_t      *model,
    fbTemplate_t       *tmpl)
{
    sk_schema_t *s;
    sk_field_t *f;
    uint32_t i;
    fbInfoElement_t *ie;
    sk_schema_err_t err;

    assert(schema);
    assert(tmpl);

    if ((err = sk_schema_create(&s, model, NULL, 0))) {
        return err;
    }
    /* the IEs from the template are specific to that template; we must
     * get the canonical IE from the info model */
    for (i = 0; ((ie = fbTemplateGetIndexedIE(tmpl, i)) != NULL); ++i) {
        if ((err = sk_schema_insert_field_by_ident(
                 &f, s, SK_FIELD_IDENT_CREATE(ie->ent, ie->num), NULL, NULL)))
        {
            goto ERR;
        }
        sk_field_set_length(f, ie->len);
    }
    *schema = s;
    return 0;

  ERR:
    sk_schema_destroy(s);
    return err;
}

sk_schema_err_t
sk_schema_wrap_template(
    sk_schema_t       **schema,
    fbInfoModel_t      *model,
    fbTemplate_t       *tmpl,
    uint16_t            tid)
{
    GError *gerr = NULL;
    fbInfoElement_t *ie;
    sk_schema_err_t err;
    sk_schema_t *s;
    sk_field_t *f;
    uint32_t i;

    assert(schema);
    assert(tmpl);

    s = sk_schema_alloc(model);

    /* Since we know the number of elements we could fill the array
     * directly instead of first creating a vector, but use a vector
     * for simplicity. */
    FIELD_LIST_INIT(s->fields);
    FIELD_LIST_INIT(s->varfields);
    FIELD_LIST_INIT(s->listfields);
    sk_vector_set_capacity(s->fields.data.vec, fbTemplateCountElements(tmpl));

    s->len = 0;
    for (i = 0; ((ie = fbTemplateGetIndexedIE(tmpl, i)) != NULL); ++i) {
        f = sk_alloc(sk_field_t);
        sk_field_ops_init(&f->ops);
        f->schema = s;
        f->ie = ie;
        f->len = f->ie->len;
        f->offset = s->len;
        FIELD_LIST_APPEND(s->fields, f);

        switch (f->ie->type) {
          case FB_BASIC_LIST:
            if (f->len != FB_IE_VARLEN) {
                s->len += f->len;
            } else {
                s->len += sizeof(fbBasicList_t);
            }
            FIELD_LIST_APPEND(s->listfields, f);
            break;
          case FB_SUB_TMPL_LIST:
            if (f->len != FB_IE_VARLEN) {
                s->len += f->len;
            } else {
                s->len += sizeof(fbSubTemplateList_t);
            }
            FIELD_LIST_APPEND(s->listfields, f);
            break;
          case FB_SUB_TMPL_MULTI_LIST:
            if (f->len != FB_IE_VARLEN) {
                s->len += f->len;
            } else {
                s->len += sizeof(fbSubTemplateMultiList_t);
            }
            FIELD_LIST_APPEND(s->listfields, f);
            break;
          default:
            if (f->len != FB_IE_VARLEN) {
                s->len += f->len;
            } else {
                FIELD_LIST_APPEND(s->varfields, f);
                s->len += sizeof(fbVarfield_t);
            }
            break;
        }
    }
    FIELD_LIST_FREEZE(s->fields);
    FIELD_LIST_FREEZE(s->listfields);
    FIELD_LIST_FREEZE(s->varfields);

    /* no computed fields */
    FIELD_LIST_INIT(s->computed_fields);
    FIELD_LIST_FREEZE(s->computed_fields);

    /* create a session and add the template to it */
    s->session = fbSessionAlloc(s->model);
    s->owns_session = 1;

    s->tmpl = tmpl;
    s->tid = fbSessionAddTemplate(s->session, 1, tid, s->tmpl, &gerr);
    if (s->tid == 0) {
        TRACEMSG(2, ("Unable to add template %p 0x%04x to session %p: %s",
                     (void*)s->tmpl, tid, (void*)s->session, gerr->message));
        g_clear_error(&gerr);
        err = SK_SCHEMA_ERR_FIXBUF;
        goto ERROR;
    }

    TRACEMSG(3,("sk_schema_wrap_template() %p->refcount = %" PRIu32,
                (void*)s, s->refcount));
    TRACEMSG(3,("sk_schema_wrap_template() %p, tmpl %p 0x%04x, session %p",
                (void*)s, (void*)s->tmpl, s->tid, (void*)s->session));

    *schema = s;
    return 0;

  ERROR:
    sk_schema_destroy(s);
    return err;
}

int
sk_schema_destroy(
    const sk_schema_t  *schema)
{
    size_t i;
    sk_schema_t *s = (sk_schema_t *)schema;

    if (s == NULL) {
        return -1;
    }
    REFCOUNT_LOCK(s);
#if TRACEMSG_LEVEL >= 3
    if (0 == s->refcount) {
        REFCOUNT_UNLOCK(s);
        TRACEMSG(3,("sk_schema_destroy() %p->refcount = 0 on entry!", s));
        return 0;
    }
#endif
    if (s->refcount && --s->refcount) {
        REFCOUNT_UNLOCK(s);
        TRACEMSG(3,("sk_schema_destroy() %p->refcount = %" PRIu32,
                    s, s->refcount));
        return 0;
    }
    REFCOUNT_UNLOCK(s);
    TRACEMSG(3,("sk_schema_destroy() %p->refcount = %" PRIu32, s, s->refcount));

    for (i = 0; i < s->ctx_count; ++i) {
        if (s->ctx[i].free_fn) {
            s->ctx[i].free_fn(s->ctx[i].ptr);
        }
    }
    free(s->ctx);

    if (FIELD_LIST_IS_SET(s->fields)) {
        /* Call field teardown functions */
        for (i = 0; i < FIELD_LIST_COUNT(s->fields); ++i) {
            sk_field_t *field = FIELD_LIST_GET(s->fields, i);
            sk_field_destroy(field);
            /* FIXME: Ignoring return value */
        }
        FIELD_LIST_CLEAR(s->fields);
    }
    FIELD_LIST_CLEAR(s->varfields);
    FIELD_LIST_CLEAR(s->listfields);
    FIELD_LIST_CLEAR(s->computed_fields);

    if (s->owns_session && s->session) {
        fbSessionFree(s->session);
    } else if (s->tmpl && !s->session) {
        fbTemplateFreeUnused(s->tmpl);
    }

    if (s->owns_model && s->model) {
        skipfix_information_model_destroy(s->model);
    }
    REFCOUNT_MUTEX_DESTROY(s);
    free(s);
    return 1;
}

sk_schema_err_t
sk_schema_copy(
    sk_schema_t       **schema_copy,
    const sk_schema_t  *schema)
{
    sk_schema_t *s;
    sk_schema_err_t err = SK_SCHEMA_ERR_MEMORY;
    size_t len;
    size_t i;

    assert(schema);

    s = sk_schema_alloc(schema->model);
    FIELD_LIST_INIT(s->fields);
    len = FIELD_LIST_COUNT(schema->fields);
    sk_vector_set_capacity(s->fields.data.vec, len);

    for (i = 0; i < len; ++i) {
        sk_field_t *field;
        sk_field_t *new_field;

        field = FIELD_LIST_GET(schema->fields, i);
        new_field = sk_alloc_flags(sk_field_t, SK_ALLOC_FLAG_NO_CLEAR);
        *new_field = *field;
        new_field->schema = s;
        if (field->ops.copy_cbdata) {
            sk_schema_err_t err2;
            err2 = field->ops.copy_cbdata(field, &new_field->ops.cbdata);
            if (err2) {
                free(new_field);
                err = err2;
                goto ERR;
            }
        }
        new_field->ie = (fbInfoElement_t *)fbInfoModelGetElementByID(
            s->model, field->ie->num, field->ie->ent);
        new_field->offset = 0;
        FIELD_LIST_APPEND(s->fields, new_field);
    }

    *schema_copy = s;
    TRACEMSG(3,("sk_schema_copy() %p->refcount = %" PRIu32, s, s->refcount));

    return 0;

  ERR:
    sk_schema_destroy(s);
    return err;
}

int
sk_schema_is_frozen(
    sk_schema_t        *schema)
{
    return (schema->tmpl != NULL);
}

sk_schema_err_t
sk_schema_freeze(
    sk_schema_t        *schema)
{
    size_t i;
    uint16_t tid;
    sk_field_t *f;
    GError *gerr = NULL;

    assert(schema);

    if (schema->tmpl) {
        return 0;
    }

    FIELD_LIST_INIT(schema->varfields);
    FIELD_LIST_INIT(schema->listfields);
    FIELD_LIST_INIT(schema->computed_fields);
    schema->tmpl = fbTemplateAlloc(schema->model);

    FIELD_LIST_FREEZE(schema->fields);

    /* Create template and offsets */
    schema->len = 0;
    for (i = 0; i < FIELD_LIST_ARRAY_COUNT(schema->fields); ++i) {
        fbInfoElement_t ie;
        f = FIELD_LIST_ARRAY_GET(schema->fields, i);
        ie = *f->ie;
        ie.len = f->len;
        if (!fbTemplateAppend(schema->tmpl, &ie, &gerr)) {
            TRACEMSG(2, ("Unable to append IE%u/%u to template: %s",
                         ie.ent, ie.num, gerr->message));
            g_clear_error(&gerr);
            fbTemplateFreeUnused(schema->tmpl);
            schema->tmpl = NULL;
            return SK_SCHEMA_ERR_FIXBUF;
        }
        f->ie = fbTemplateGetIndexedIE(schema->tmpl, i);
        if (NULL == f->ie) {
            skAbort();
        }
        f->offset = schema->len;
        switch (f->ie->type) {
          case FB_BASIC_LIST:
            if (f->len != FB_IE_VARLEN) {
                schema->len += f->len;
            } else {
                schema->len += sizeof(fbBasicList_t);
            }
            FIELD_LIST_APPEND(schema->listfields, f);
            break;
          case FB_SUB_TMPL_LIST:
            if (f->len != FB_IE_VARLEN) {
                schema->len += f->len;
            } else {
                schema->len += sizeof(fbSubTemplateList_t);
            }
            FIELD_LIST_APPEND(schema->listfields, f);
            break;
          case FB_SUB_TMPL_MULTI_LIST:
            if (f->len != FB_IE_VARLEN) {
                schema->len += f->len;
            } else {
                schema->len += sizeof(fbSubTemplateMultiList_t);
            }
            FIELD_LIST_APPEND(schema->listfields, f);
            break;
          default:
            if (f->len != FB_IE_VARLEN) {
                schema->len += f->len;
            } else {
                FIELD_LIST_APPEND(schema->varfields, f);
                schema->len += sizeof(fbVarfield_t);
            }
            break;
        }
        if (f->ops.compute) {
            FIELD_LIST_APPEND(schema->computed_fields, f);
        }
    }

    FIELD_LIST_FREEZE(schema->listfields);
    FIELD_LIST_FREEZE(schema->varfields);
    FIELD_LIST_FREEZE(schema->computed_fields);

    if (schema->session == NULL) {
        schema->session = fbSessionAlloc(schema->model);
        schema->owns_session = 1;
    }

    tid = fbSessionAddTemplate(schema->session, 1, schema->tid,
                               schema->tmpl, &gerr);
    if (tid == 0) {
        TRACEMSG(2, ("Unable to add template %p 0x%04x to session %p: %s",
                     (void*)schema->tmpl, schema->tid, (void*)schema->session,
                     gerr->message));
        g_clear_error(&gerr);
        return SK_SCHEMA_ERR_FIXBUF;
    }
    schema->tid = tid;

    for (i = 0; i < FIELD_LIST_ARRAY_COUNT(schema->fields); ++i) {
        f = FIELD_LIST_ARRAY_GET(schema->fields, i);
        if (f->ops.init) {
            f->ops.init(f, schema);
        }
    }

    TRACEMSG(3,("sk_schema_freeze() %p, tmpl %p 0x%04x, session %p",
                (void*)schema, (void*)schema->tmpl, schema->tid,
                (void*)schema->session));
    return 0;
}

sk_schema_err_t
sk_schema_insert_field_by_ident(
    sk_field_t            **field,
    sk_schema_t            *schema,
    sk_field_ident_t        ident,
    const sk_field_ops_t   *ops,
    const sk_field_t       *before)
{
    const fbInfoElement_t *ie;
    sk_field_t *f;
    size_t len;
    size_t i = 0;

    assert(schema);

    if (schema->tmpl) {
        return SK_SCHEMA_ERR_FROZEN;
    }
    ie = fbInfoModelGetElementByID(schema->model,
                                   SK_FIELD_IDENT_GET_ID(ident),
                                   SK_FIELD_IDENT_GET_PEN(ident));
    if (ie == NULL) {
        return SK_SCHEMA_ERR_UNKNOWN_IE;
    }

    if (before) {
        len = FIELD_LIST_VEC_COUNT(schema->fields);
        for (i = 0; i < len; ++i) {
            f = FIELD_LIST_VEC_GET(schema->fields, i);
            if (f == before) {
                break;
            }
        }
        if (i == len) {
            return SK_SCHEMA_ERR_FIELD_NOT_FOUND;
        }
    }

    f = sk_alloc(sk_field_t);
    if (ops) {
        f->ops = *ops;
    } else {
        sk_field_ops_init(&f->ops);
    }
    f->schema = schema;
    f->ie = (fbInfoElement_t *)ie;
    f->len = f->ie->len;
    if (!before) {
        FIELD_LIST_APPEND(schema->fields, f);
    } else {
        sk_vector_insert_value(schema->fields.data.vec, i, (void *)&f);
    }

    if (field) {
        *field = f;
    }
    return 0;
}

sk_schema_err_t
sk_schema_insert_field_by_name(
    sk_field_t            **field,
    sk_schema_t            *schema,
    const char             *name,
    const sk_field_ops_t   *ops,
    const sk_field_t       *before)
{
    const fbInfoElement_t *ie;

    assert(schema);

    if (schema->tmpl) {
        return SK_SCHEMA_ERR_FROZEN;
    }
    ie = fbInfoModelGetElementByName(schema->model, name);
    if (ie == NULL) {
        return SK_SCHEMA_ERR_UNKNOWN_IE;
    }
    return sk_schema_insert_field_by_ident(
        field, schema, SK_FIELD_IDENT_CREATE(ie->ent, ie->num), ops, before);
}


sk_schema_err_t
sk_schema_remove_field(
    sk_schema_t        *schema,
    const sk_field_t   *field)
{
    sk_field_t *f;
    size_t i;

    assert(schema);

    if (schema->tmpl) {
        return SK_SCHEMA_ERR_FROZEN;
    }

    for (i = 0; i < FIELD_LIST_VEC_COUNT(schema->fields); ++i) {
        f = FIELD_LIST_VEC_GET(schema->fields, i);
        if (f == field) {
            sk_field_destroy(f);
            sk_vector_remove_value(schema->fields.data.vec, i, NULL);
            return 0;
        }
    }

    return SK_SCHEMA_ERR_FIELD_NOT_FOUND;
}

sk_schema_err_t
sk_schema_set_tid(
    sk_schema_t        *schema,
    uint16_t            tid)
{
    assert(schema);

    if (schema->tmpl) {
        return SK_SCHEMA_ERR_FROZEN;
    }

    schema->tid = tid;
    return 0;
}

fbInfoModel_t *
sk_schema_get_infomodel(
    const sk_schema_t  *schema)
{
    return schema->model;
}

sk_schema_err_t
sk_schema_get_template(
    const sk_schema_t  *schema,
    fbTemplate_t      **tmpl,
    uint16_t           *tid)
{
    assert(schema);

    if (tmpl) {
        *tmpl = schema->tmpl;
    }
    if (tid) {
        *tid = schema->tid;
    }
    if (!schema->tmpl) {
        return SK_SCHEMA_ERR_NOT_FROZEN;
    }
    return 0;
}

size_t
sk_schema_get_record_length(
    const sk_schema_t  *schema)
{
    if (schema->tmpl) {
        return schema->len;
    }
    return 0;
}

uint16_t
sk_schema_get_count(
    const sk_schema_t  *schema)
{
    assert(schema);
    return FIELD_LIST_COUNT(schema->fields);
}

const sk_field_t *
sk_schema_get_field(
    const sk_schema_t  *schema,
    uint16_t            index)
{
    assert(schema);
    return FIELD_LIST_GET_SAFE(schema->fields, index);
}

const sk_field_t *
sk_schema_get_field_by_ident(
    const sk_schema_t  *schema,
    sk_field_ident_t    ident,
    const sk_field_t   *from)
{
    size_t i;
    const sk_field_t *f;

    assert(schema);

    i = 0;
    if (from) {
        for (; i < FIELD_LIST_COUNT(schema->fields); ++i) {
            f = FIELD_LIST_GET(schema->fields, i);
            if (f == from) {
                ++i;
                break;
            }
        }
    }
    for (; i < FIELD_LIST_COUNT(schema->fields); ++i) {
        f = FIELD_LIST_GET(schema->fields, i);
        if (SK_FIELD_IDENT_CREATE(f->ie->ent, f->ie->num) == ident) {
            return f;
        }
    }

    return NULL;
}

const sk_field_t *
sk_schema_get_field_by_name(
    const sk_schema_t  *schema,
    const char         *name,
    const sk_field_t   *from)
{
    const fbInfoElement_t *ie;

    assert(schema);
    assert(name);

    ie = fbInfoModelGetElementByName(schema->model, name);
    if (ie == NULL) {
        return NULL;
    }
    return sk_schema_get_field_by_ident(
        schema, SK_FIELD_IDENT_CREATE(ie->ent, ie->num), from);
}

/*
 *    Return 1 if the elements in 'tmpl_1' match those in 'tmpl_2'.
 *    Return 0 otherwise.
 */
static int
sk_template_matches_template(
    const fbTemplate_t *tmpl_1,
    const fbTemplate_t *tmpl_2)
{
    fbTemplate_t *t1 = (fbTemplate_t*)tmpl_1;
    fbTemplate_t *t2 = (fbTemplate_t*)tmpl_2;
    uint32_t count;
    uint32_t i;
    const fbInfoElement_t *ie1;
    const fbInfoElement_t *ie2;

    if (t1 == t2) {
        return (NULL != t1);
    }
    if (NULL == t1 || NULL == t2) {
        return 0;
    }
    count = fbTemplateCountElements(t1);
    if (fbTemplateCountElements(t2) != count) {
        return 0;
    }
    for (i = 0; i < count; ++i) {
        ie1 = fbTemplateGetIndexedIE(t1, i);
        ie2 = fbTemplateGetIndexedIE(t2, i);
        if (ie1->ref.canon != ie2->ref.canon
            || ie1->len != ie2->len)
        {
            return 0;
        }
    }
    return 1;
}

int
sk_schema_matches_schema(
    const sk_schema_t  *schema_a,
    const sk_schema_t  *schema_b,
    uint16_t           *mismatch)
{
    size_t count_a;
    size_t count_b;
    const sk_field_t *field_a;
    const sk_field_t *field_b;
    uint16_t i;

    if (schema_a == schema_b) {
        return NULL != schema_a;
    }
    if (NULL == schema_a
        || NULL == schema_b
        || (sk_schema_get_infomodel(schema_a)
            != sk_schema_get_infomodel(schema_b)))
    {
        return 0;
    }

    count_a = sk_schema_get_count(schema_a);
    count_b = sk_schema_get_count(schema_b);

    if (NULL == mismatch) {
        if (count_a != count_b) {
            return 0;
        }
        mismatch = &i;
    }
    for (i = 0; count_a && count_b; ++i, --count_a, --count_b) {
        field_a = sk_schema_get_field(schema_a, i);
        field_b = sk_schema_get_field(schema_b, i);
        if (field_a->ie->num != field_b->ie->num
            || field_a->ie->ent != field_b->ie->ent
            || field_a->ie->len != field_b->ie->len
            || sk_field_get_name(field_a) != sk_field_get_name(field_b))
        {
            *mismatch = i;
            return 0;
        }
    }
    if (count_a != count_b) {
        *mismatch = i;
        return 0;
    }
    return 1;
}

void
sk_schema_context_ident_create(
    sk_schema_ctx_ident_t  *ident)
{
    static sk_schema_ctx_ident_t next = 0;
    static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

    pthread_mutex_lock(&mutex);
    if (*ident == SK_SCHEMA_CTX_IDENT_INVALID) {
        *ident = next++;
    }
    pthread_mutex_unlock(&mutex);
}

void
sk_schema_set_context(
    const sk_schema_t      *schema,
    sk_schema_ctx_ident_t   ident,
    void                   *ctx,
    void                  (*ctx_free)(void *))
{
    sk_schema_t *s = (sk_schema_t *)schema;
    void *tmp;

    assert(schema);

    assert(ident != SK_SCHEMA_CTX_IDENT_INVALID);
    if (ident == SK_SCHEMA_CTX_IDENT_INVALID) {
        return;
    }

    REFCOUNT_LOCK(s);
    if (ident < s->ctx_count) {
        if (s->ctx[ident].free_fn) {
            /* There is already something here, free it */
            s->ctx[ident].free_fn(s->ctx[ident].ptr);
        }
    } else {
        /* The ident does not fit; resize the array */
        tmp = realloc(s->ctx, sizeof(*s->ctx) * (ident + 1));
        if (tmp == NULL) {
            REFCOUNT_UNLOCK(s);
            skAppPrintOutOfMemory("schema context array");
            exit(EXIT_FAILURE);
        }
        s->ctx = (sk_schema_ctx_t *)tmp;

        /* Clear the new members of the array (except for [ident]) */
        memset(&s->ctx[s->ctx_count], 0,
               sizeof(*s->ctx) * (ident - s->ctx_count));

        /* Update the count */
        s->ctx_count = ident + 1;
    }

    /* Set the context */
    s->ctx[ident].ptr = ctx;
    s->ctx[ident].free_fn = ctx_free;
    REFCOUNT_UNLOCK(s);
}

void *
sk_schema_get_context(
    const sk_schema_t      *schema,
    sk_schema_ctx_ident_t   ident)
{
    void *retval;

    assert(schema);
    if (ident == SK_SCHEMA_CTX_IDENT_INVALID) {
        return NULL;
    }
    REFCOUNT_LOCK((sk_schema_t *)schema);
    if (ident >= schema->ctx_count) {
        REFCOUNT_UNLOCK((sk_schema_t *)schema);
        return NULL;
    }
    retval = schema->ctx[ident].ptr;
    REFCOUNT_UNLOCK((sk_schema_t *)schema);
    return retval;
}

const char *
sk_schema_strerror(
    sk_schema_err_t     errcode)
{
    static char buf[128];

    switch (errcode) {
      case SK_SCHEMA_ERR_SUCCESS:
        return "Success";
      case SK_SCHEMA_ERR_MEMORY:
        return "Memory failure";
      case SK_SCHEMA_ERR_FIXBUF:
        return "Fixbuf error";
      case SK_SCHEMA_ERR_FROZEN:
        return "Attempt to modify a frozen schema";
      case SK_SCHEMA_ERR_NOT_FROZEN:
        return "Illegal operation on an unfrozen schema";
      case SK_SCHEMA_ERR_UNKNOWN_IE:
        return "IE cannot be found in the information model";
      case SK_SCHEMA_ERR_FIELD_NOT_FOUND:
        return "Field could not be found in the schema";
      case SK_SCHEMA_ERR_INCOMPATIBLE:
        return "Illegal operation as field types are incompatible";
      case SK_SCHEMA_ERR_BAD_TYPE:
        return "Function was called on the wrong type of field";
      case SK_SCHEMA_ERR_BAD_SIZE:
        return "Field has an unsupported size";
      case SK_SCHEMA_ERR_NOT_IPV4:
        return "IPv6 could not be converted to IPv4";
      case SK_SCHEMA_ERR_TRUNCATED:
        return "Field was truncated on copy";
      case SK_SCHEMA_ERR_UNKNOWN_BOOL:
        return "IPFIX boolean value is not true (1) or false (2)";
      case SK_SCHEMA_ERR_NO_SCHEMA:
        return "Record does not have a schema";
      case SK_SCHEMA_ERR_UNSPECIFIED:
        return "Unspecified error in schema/record/field";
    }

    snprintf(buf, sizeof(buf), "Unknown sk_schema_err_t value %ld",
             (long)errcode);
    return buf;
}

/*
 *    Set the schema of the record 'rec' to 'schema', allocate the
 *    buffer to hold the record's data, and set the appropriate flags
 *    on the record.
 *
 *    This function does not clear any previous schema or data; the
 *    caller should do that before invoking this function.
 */
static void
sk_fixrec_set_schema(
    sk_fixrec_t        *rec,
    const sk_schema_t  *schema)
{
    assert(rec);
    assert(schema);
    assert(schema->tmpl);
    rec->schema = sk_schema_clone(schema);
    rec->data = sk_alloc_array(uint8_t, schema->len);
}

sk_schema_err_t
sk_fixrec_init(
    sk_fixrec_t        *rec,
    const sk_schema_t  *schema)
{
    assert(rec);

    memset(rec, 0, sizeof(*rec));
    if (schema) {
        if (!schema->tmpl) {
            return SK_SCHEMA_ERR_NOT_FROZEN;
        }
        sk_fixrec_set_schema(rec, schema);
    }
    return 0;
}

sk_schema_err_t
sk_fixrec_create(
    sk_fixrec_t       **rec,
    const sk_schema_t  *schema)
{
    assert(rec);
    assert(schema);

    if (!schema->tmpl) {
        return SK_SCHEMA_ERR_NOT_FROZEN;
    }
    *rec = sk_alloc(sk_fixrec_t);
    (*rec)->flags = SK_FIXREC_ALLOCATED;
    sk_fixrec_set_schema(*rec, schema);
    return 0;
}


/*
 *    Free the 'data' member of record 'rec' unless it is "foreign"
 *    data or the record is a SiLK3 record.
 */
static void
sk_fixrec_free_data(
    sk_fixrec_t        *rec)
{
    assert(rec);
    sk_fixrec_clear(rec);
    if (!(rec->flags & SK_FIXREC_FOREIGN_DATA)) {
        free(rec->data);
    }
}

/*
 *    Copy the fbVarfield_t structure at 'src_pos' to the fbVarfield_t
 *    structure at 'dest_pos'.
 *
 *    Allocate a new buffer for the contents of the varfield.  If
 *    either the source's length is 0 or its data buffer is NULL, the
 *    destination's length is set to 0 and its buffer is set to NULL.
 *
 *    When 'src_pos' and 'dest_pos' point to the same location, assume
 *    data is being copied from fixbuf and allocate a new data buffer
 *    for the destination.  If instead the source had a data buffer
 *    outside of fixbuf, that buffer is lost.
 *
 *    Make no assumptions about the alignment of 'src_pos' and
 *    'dest_pos'.
 */
static inline void
sk_fixrec_copy_varfield(
    void               *dest_pos,
    const void         *src_pos)
{
    fbVarfield_t dest;
    fbVarfield_t src;
    const uint8_t *buf;

    assert(dest_pos);
    assert(src_pos);

    memcpy(&src, src_pos, sizeof(fbVarfield_t));
    if (0 == src.len || NULL == src.buf) {
        dest.len = 0;
        dest.buf = NULL;
    } else {
        /* using a separate 'buf' variable allows the function to work
         * when src_pos and dest_pos are the same location. */
        buf = src.buf;
        dest.len = src.len;
        dest.buf = sk_alloc_memory(uint8_t, dest.len, SK_ALLOC_FLAG_NO_CLEAR);
        memcpy(dest.buf, buf, dest.len);
    }
    memcpy(dest_pos, &dest, sizeof(fbVarfield_t));
    TRACEMSG(4, ("%s:%d:Allocated varfield %zu-bytes %p",
                 __FILE__, __LINE__, dest.len, (void*)dest.buf));
}

/*
 *    Free the data for the fbVarfield_t structure at 'src_pos'.
 */
static inline void
sk_fixrec_free_varfield(
    void               *src_pos)
{
    fbVarfield_t src;

    assert(src_pos);

    memcpy(&src, src_pos, sizeof(fbVarfield_t));
#if TRACEMSG_LEVEL > 0
    if (src.buf) {
        TRACEMSG(4, ("%s:%d:Freeing varfield %zu-bytes %p",
                     __FILE__, __LINE__, src.len, (void*)src.buf));
    }
#endif
    free(src.buf);
}


void
sk_fixrec_destroy(
    sk_fixrec_t        *rec)
{
    if (rec) {
        sk_fixrec_free_data(rec);
        sk_schema_destroy(rec->schema);
        if (rec->flags & SK_FIXREC_ALLOCATED) {
            memset(rec, 0, sizeof(*rec));
            free(rec);
        } else {
            memset(rec, 0, sizeof(*rec));
        }
    }
}

void
sk_fixrec_set_data(
    sk_fixrec_t        *rec,
    void               *data)
{
    assert(rec);
    assert(rec->schema);
    assert(data);
    sk_fixrec_free_data(rec);
    rec->data = (uint8_t *)data;
    rec->flags |= SK_FIXREC_FOREIGN_DATA;
}

void
sk_fixrec_clear(
    sk_fixrec_t        *rec)
{
    sk_field_t *field;
    size_t i;

    if (NULL == rec->schema
        || NULL == rec->data)
    {
        uint8_t flags;
        flags = rec->flags & (SK_FIXREC_ALLOCATED);
        memset(rec, 0, sizeof(*rec));
        rec->flags = flags;
        return;
    }

    /* free the data used by the computed (plug-in) fields */
    for (i=0; i < FIELD_LIST_ARRAY_COUNT(rec->schema->computed_fields); ++i) {
        field = FIELD_LIST_ARRAY_GET(rec->schema->computed_fields, i);
        if (field->len == FB_IE_VARLEN) {
            sk_fixrec_free_varfield(rec->data + field->offset);
        }
    }

    if (rec->flags & SK_FIXREC_FOREIGN_DATA) {
        /* do nothing to foreign data */
        return;
    }
    if (rec->flags & SK_FIXREC_FIXBUF_VARDATA) {
        /* all other data is owned by fixbuf */
        fBufListFree(rec->schema->tmpl, rec->data);
        memset(rec->data, 0, rec->schema->len);
        return;
    }

    for (i = 0; i < FIELD_LIST_ARRAY_COUNT(rec->schema->varfields); ++i) {
        field = FIELD_LIST_ARRAY_GET(rec->schema->varfields, i);
        assert(FB_IE_VARLEN == field->len);
        sk_fixrec_free_varfield(rec->data + field->offset);
    }
    for (i = 0; i < FIELD_LIST_ARRAY_COUNT(rec->schema->listfields); ++i) {
        field = FIELD_LIST_ARRAY_GET(rec->schema->listfields, i);
        switch (field->ie->type) {
          case FB_BASIC_LIST:
            sk_fixrec_free_list_basic(rec->data + field->offset);
            break;
          case FB_SUB_TMPL_LIST:
            sk_fixrec_free_list_subtemplate(rec->data + field->offset);
            break;
          case FB_SUB_TMPL_MULTI_LIST:
            sk_fixrec_free_list_subtemplatemulti(rec->data + field->offset);
            break;
          default:
            skAbortBadCase(field->ie->type);
        }
    }
    memset(rec->data, 0, rec->schema->len);
}

const sk_schema_t *
sk_fixrec_get_schema(
    const sk_fixrec_t  *rec)
{
    assert(rec);
    return rec->schema;
}

sk_schema_err_t
sk_fixrec_update_computed(
    sk_fixrec_t        *rec)
{
    size_t i;
    const sk_field_t *field;
    sk_schema_err_t err;

    for (i = 0;
         i < FIELD_LIST_ARRAY_COUNT(rec->schema->computed_fields);
         ++i)
    {
        field = FIELD_LIST_ARRAY_GET(rec->schema->computed_fields, i);
        err = field->ops.compute(rec, field);
        if (err) {
            return err;
        }
    }
    return 0;
}


/**
 *    Copy the data from the schema-based record at 'src' into the
 *    memory pointed to by 'dest_ptr'.  Assumes the length of the data
 *    at 'dest_ptr' is no smaller than the data size of 'src'.
 *
 *    Deep copy the data contained in the lists in 'src'.
 *
 *    The template map 'tmpl_map' is expected to contain all the
 *    templates used by the lists in 'src' since those lists use
 *    'tmpl_map' to get the template IDs.
 */
static void
sk_fixrec_copy_data(
    void                       *dest_ptr,
    const sk_fixrec_t          *src,
    sk_fixrec_template_map_t   *tmpl_map)
{
    uint8_t *dest = (uint8_t*)dest_ptr;
    sk_field_t *field;
    size_t i;

    assert(src);
    assert(dest_ptr);
    assert(src->data != dest_ptr);
    assert(tmpl_map);

    /* shallow copy the data */
    memcpy(dest, src->data, src->schema->len);

    /* deep copy the varfields */
    for (i = 0; i < FIELD_LIST_ARRAY_COUNT(src->schema->varfields); ++i) {
        field = FIELD_LIST_ARRAY_GET(src->schema->varfields, i);
        assert(field->len == FB_IE_VARLEN);
        sk_fixrec_copy_varfield(dest + field->offset,
                                src->data + field->offset);
    }

    /* deep copy the lists */
    for (i = 0; i < FIELD_LIST_ARRAY_COUNT(src->schema->listfields); ++i) {
        field = FIELD_LIST_ARRAY_GET(src->schema->listfields, i);
        switch (field->ie->type) {
          case FB_BASIC_LIST:
            sk_fixrec_copy_list_basic(
                dest + field->offset, src->data + field->offset, tmpl_map);
            break;
          case FB_SUB_TMPL_LIST:
            sk_fixrec_copy_list_subtemplate(
                dest + field->offset, src->data + field->offset, tmpl_map);
            break;
          case FB_SUB_TMPL_MULTI_LIST:
            sk_fixrec_copy_list_subtemplatemulti(
                dest + field->offset, src->data + field->offset, tmpl_map);
            break;
          default:
            skAbortBadCase(field->ie->type);
        }
    }
}

sk_schema_err_t
sk_fixrec_copy_into(
    sk_fixrec_t        *dest,
    const sk_fixrec_t  *src)
{
    sk_fixrec_template_map_t *tmpl_map;
    uint8_t allocated_bit;

    assert(dest);
    assert(src);
    assert(src->schema);
    if (src == dest) {
        return 0;
    }

    /* Remove old data from dest  */
    allocated_bit = dest->flags & SK_FIXREC_ALLOCATED;
    dest->flags &= ~SK_FIXREC_ALLOCATED;
    sk_fixrec_destroy(dest);

    if (!src->data) {
        /* If all the data is in the record, just copy the record */
        memcpy(dest, src, sizeof(sk_fixrec_t));
        if (src->schema) {
            dest->schema = sk_schema_clone(src->schema);
        }
    } else {
        /* clone the schema */
        dest->schema = sk_schema_clone(src->schema);
        /* create a buffer for the data and copy it */
        dest->data = sk_alloc_memory(uint8_t, dest->schema->len,
                                     SK_ALLOC_FLAG_NO_CLEAR);
        tmpl_map = sk_fixrec_template_map_create(src);
        sk_fixrec_copy_data(dest->data, src, tmpl_map);
        sk_fixrec_template_map_destroy(tmpl_map);
    }

    /* Revert the allocated bit */
    dest->flags |= allocated_bit;

    return 0;
}

sk_schema_err_t
sk_fixrec_copy(
    sk_fixrec_t       **dest,
    const sk_fixrec_t  *src)
{
    *dest = sk_alloc(sk_fixrec_t);
    (*dest)->flags = SK_FIXREC_ALLOCATED;
    return sk_fixrec_copy_into(*dest, src);
}

/*
 *    Fill 'val' with a varfield representation of the 'field' field
 *    of the record 'rec'.
 */
static void
sk_fixrec_get_varfield(
    const sk_fixrec_t  *rec,
    const sk_field_t   *field,
    fbVarfield_t       *val)
{
    ASSERT_FIELD_IN_REC(field, rec);
    assert(TYPE_IS_STRINGLIKE(field->ie->type));
    if (field->len == FB_IE_VARLEN) {
        memcpy(val, rec->data + field->offset, sizeof(fbVarfield_t));
    } else {
        val->buf = rec->data + field->offset;
        val->len = field->len;
    }
}

sk_schema_err_t
sk_fixrec_data_to_text(
    const sk_fixrec_t  *rec,
    const sk_field_t   *field,
    char               *dest,
    size_t              size)
{
    sk_schema_err_t err;

    if (field->ops.to_text) {
        ASSERT_FIELD_IN_REC(field, rec);
        return field->ops.to_text(rec, field, dest, size);
    }
    switch (field->ie->type) {
      case FB_BOOL:
        {
            int b;
            err = sk_fixrec_get_boolean(rec, field, &b);
            if (err && (err != SK_SCHEMA_ERR_UNKNOWN_BOOL)) {
                return err;
            }
            switch (b) {
              case 1:
                snprintf(dest, size, "True");
                break;
              case 2:
                snprintf(dest, size, "False");
                break;
              default:
                snprintf(dest, size, "%d", b);
                break;
            }
        }
        break;
      case FB_UINT_8:
        {
            uint8_t u8;
            if ((err = sk_fixrec_get_unsigned8(rec, field, &u8))) {
                return err;
            }
            snprintf(dest, size, "%" PRIu8, u8);
        }
        break;
      case FB_UINT_16:
        {
            uint16_t u16;
            if ((err = sk_fixrec_get_unsigned16(rec, field, &u16))) {
                return err;
            }
            snprintf(dest, size, "%" PRIu16, u16);
        }
        break;
      case FB_UINT_32:
        {
            uint32_t u32;
            if ((err = sk_fixrec_get_unsigned32(rec, field, &u32))) {
                return err;
            }
            snprintf(dest, size, "%" PRIu32, u32);
        }
        break;
      case FB_UINT_64:
        {
            uint64_t u64;
            if ((err = sk_fixrec_get_unsigned64(rec, field, &u64))) {
                return err;
            }
            snprintf(dest, size, "%" PRIu64, u64);
        }
        break;
      case FB_INT_8:
        {
            int8_t i8;
            if ((err = sk_fixrec_get_signed8(rec, field, &i8))) {
                return err;
            }
            snprintf(dest, size, "%" PRId8, i8);
        }
        break;
      case FB_INT_16:
        {
            int16_t i16;
            if ((err = sk_fixrec_get_signed16(rec, field, &i16))) {
                return err;
            }
            snprintf(dest, size, "%" PRId16, i16);
        }
        break;
      case FB_INT_32:
        {
            int32_t i32;
            if ((err = sk_fixrec_get_signed32(rec, field, &i32))) {
                return err;
            }
            snprintf(dest, size, "%" PRId32, i32);
        }
        break;
      case FB_INT_64:
        {
            int64_t i64;
            if ((err = sk_fixrec_get_signed64(rec, field, &i64))) {
                return err;
            }
            snprintf(dest, size, "%" PRId64, i64);
        }
        break;
      case FB_IP4_ADDR:
        {
            char buf[SK_NUM2DOT_STRLEN];
            skipaddr_t addr;
            uint32_t u32;
            if ((err = sk_fixrec_get_ipv4_addr(rec, field, &u32))) {
                return err;
            }
            skipaddrSetV4(&addr, &u32);
            snprintf(dest, size, "%s", skipaddrString(buf, &addr, 0));
        }
        break;
      case FB_IP6_ADDR:
        {
            char buf[SK_NUM2DOT_STRLEN];
            skipaddr_t addr;
            uint8_t v6[16];
            if ((err = sk_fixrec_get_ipv6_addr(rec, field, v6))) {
                return err;
            }
            skipaddrSetV6(&addr, v6);
            snprintf(dest, size, "%s", skipaddrString(buf, &addr, 0));
        }
        break;
      case FB_MAC_ADDR:
        {
            uint8_t mac[6];
            if ((err = sk_fixrec_get_mac_address(rec, field, mac))) {
                return err;
            }
            snprintf(dest, size, "%02x:%02x:%02x:%02x:%02x:%02x",
                     mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        }
        break;
      case FB_FLOAT_32:
        {
            float f;
            if ((err = sk_fixrec_get_float32(rec, field, &f))) {
                return err;
            }
            snprintf(dest, size, "%f", f);
        }
        break;
      case FB_FLOAT_64:
        {
            double d;
            if ((err = sk_fixrec_get_float64(rec, field, &d))) {
                return err;
            }
            snprintf(dest, size, "%f", d);
        }
        break;
      case FB_DT_SEC:
      case FB_DT_MILSEC:
      case FB_DT_MICROSEC:
      case FB_DT_NANOSEC:
        {
            char buf[SKTIMESTAMP_STRLEN];
            sktime_t t;
            if ((err = sk_fixrec_get_datetime(rec, field, &t))) {
                return err;
            }
            if (dest && size >= SKTIMESTAMP_STRLEN) {
                sktimestamp_r(dest, t, 0);
            } else {
                snprintf(dest, size, "%s", sktimestamp_r(buf, t, 0));
            }
        }
        break;
      case FB_STRING:
        {
            char buf[UINT16_MAX];
            uint16_t s = UINT16_MAX;
            if ((err = sk_fixrec_get_string(rec, field, buf, &s))) {
                return err;
            }
            snprintf(dest, size, "%s", buf);
        }
        break;
      case FB_OCTET_ARRAY:
        {
            uint8_t buf[UINT16_MAX];
            uint16_t s = UINT16_MAX;
            uint16_t i;
            uint16_t left = size;
            char *d = dest;
            if ((err = sk_fixrec_get_octet_array(rec, field, buf, &s))) {
                return err;
            }
            for (i = 0; left && i < s; ++i) {
                snprintf(d, left, "%02x", buf[i]);
                d += 2;
                if (--left) {
                    --left;
                }
            }
        }
        break;
    }
    dest[size - 1] = '\0';
    return 0;
}

sk_schema_err_t
sk_fixrec_data_compare(
    const sk_fixrec_t  *rec_a,
    const sk_field_t   *field_a,
    const sk_fixrec_t  *rec_b,
    const sk_field_t   *field_b,
    int                *cmp)
{
    int ta, tb;
    sk_schema_err_t err;

    ASSERT_FIELD_IN_REC(field_a, rec_a);
    ASSERT_FIELD_IN_REC(field_b, rec_b);
    if (field_a->ops.compare) {
        return field_a->ops.compare(rec_a, field_a, rec_b, field_b, cmp);
    }

    ta = field_a->ie->type;
    tb = field_b->ie->type;
    if (ta != tb
        && !(TYPE_IS_IP(ta) && TYPE_IS_IP(tb))
        && !(TYPE_IS_DT(ta) && TYPE_IS_DT(tb)))
    {
        return SK_SCHEMA_ERR_INCOMPATIBLE;
    }
    switch (ta) {
      case FB_BOOL:
        *cmp = (*(rec_a->data + field_a->offset)
                - *(rec_b->data + field_b->offset));
        break;
      case FB_UINT_8:
      case FB_UINT_16:
      case FB_UINT_32:
      case FB_UINT_64:
        {
            uint64_t u64a, u64b;
            if ((err = sk_fixrec_get_unsigned(rec_a, field_a, &u64a))) {
                return err;
            }
            if ((err = sk_fixrec_get_unsigned(rec_b, field_b, &u64b))) {
                return err;
            }
            *cmp = (u64a < u64b) ? -1 : (u64a > u64b);
        }
        break;
      case FB_INT_8:
      case FB_INT_16:
      case FB_INT_32:
      case FB_INT_64:
        {
            int64_t i64a, i64b;
            if ((err = sk_fixrec_get_signed(rec_a, field_a, &i64a))) {
                return err;
            }
            if ((err = sk_fixrec_get_signed(rec_b, field_b, &i64b))) {
                return err;
            }
            *cmp = (i64a < i64b) ? -1 : (i64a > i64b);
        }
        break;
      case FB_IP4_ADDR:
      case FB_IP6_ADDR:
        {
            skipaddr_t addra, addrb;
            if ((err = sk_fixrec_get_ip_address(rec_a, field_a, &addra))) {
                return err;
            }
            if ((err = sk_fixrec_get_ip_address(rec_b, field_b, &addrb))) {
                return err;
            }
            *cmp = skipaddrCompare(&addra, &addrb);
        }
        break;
      case FB_FLOAT_32:
      case FB_FLOAT_64:
        {
            double da, db;
            if ((err = sk_fixrec_get_float(rec_a, field_a, &da))) {
                return err;
            }
            if ((err = sk_fixrec_get_float(rec_b, field_b, &db))) {
                return err;
            }
            *cmp = (da < db) ? -1 : (da > db);
        }
        break;
      case FB_MAC_ADDR:
        {
            uint8_t *a = rec_a->data + field_a->offset;
            uint8_t *b = rec_b->data + field_b->offset;
            *cmp = memcmp(a, b, 6);
        }
        break;
      case FB_STRING:
        {
            fbVarfield_t va, vb;

            sk_fixrec_get_varfield(rec_a, field_a, &va);
            sk_fixrec_get_varfield(rec_b, field_b, &vb);
            if (va.len > vb.len) {
                *cmp = strncmp((char *)va.buf, (char *)vb.buf, vb.len);
                if (*cmp == 0) {
                    *cmp = va.buf[vb.len] ? 1 : 0;
                }
            } else {
                *cmp = strncmp((char *)va.buf, (char *)vb.buf, va.len);
                if (*cmp == 0 && va.len != vb.len) {
                    *cmp = vb.buf[va.len] ? -1 : 0;
                }
            }
        }
        break;
      case FB_OCTET_ARRAY:
        {
            fbVarfield_t va, vb;

            sk_fixrec_get_varfield(rec_a, field_a, &va);
            sk_fixrec_get_varfield(rec_b, field_b, &vb);
            if (va.len > vb.len) {
                *cmp = memcmp(va.buf, vb.buf, vb.len);
                if (*cmp == 0) {
                    *cmp = 1;
                }
            } else {
                *cmp = memcmp(va.buf, vb.buf, va.len);
                if (*cmp == 0 && va.len != vb.len) {
                    *cmp = -1;
                }
            }
        }
        break;
      case FB_DT_SEC:
      case FB_DT_MILSEC:
        {
            sktime_t a, b;
            sk_fixrec_get_datetime(rec_a, field_a, &a);
            sk_fixrec_get_datetime(rec_b, field_b, &b);
            *cmp = (a < b) ? -1 : (a > b);
        }
        break;
      case FB_DT_MICROSEC:
      case FB_DT_NANOSEC:
        {
            sk_ntp_time_t a, b;
            sk_fixrec_get_datetime_ntp(rec_a, field_a, &a);
            sk_fixrec_get_datetime_ntp(rec_b, field_b, &b);
            *cmp = (a < b) ? -1 : (a > b);
        }
        break;
    }
    return 0;
}

sk_schema_err_t
sk_fixrec_data_merge(
    sk_fixrec_t        *dest_rec,
    const sk_field_t   *dest_field,
    const sk_fixrec_t  *src_rec,
    const sk_field_t   *src_field)
{
    sk_schema_err_t err;
    int ta, tb;

    ASSERT_FIELD_IN_REC(dest_field, dest_rec);
    ASSERT_FIELD_IN_REC(src_field, src_rec);
    if (dest_field->ops.merge) {
        return dest_field->ops.merge(
            dest_rec, dest_field, src_rec, src_field);
    }
    ta = dest_field->ie->type;
    tb = src_field->ie->type;
    if (ta != tb) {
        return SK_SCHEMA_ERR_INCOMPATIBLE;
    }
    switch (ta) {
      case FB_UINT_8:
      case FB_UINT_16:
      case FB_UINT_32:
      case FB_UINT_64:
        {
            uint64_t u64a, u64b;
            if ((err = sk_fixrec_get_unsigned(dest_rec, dest_field, &u64a))
                || (err = sk_fixrec_get_unsigned(src_rec, src_field, &u64b))
                || (err = sk_fixrec_set_unsigned(
                        dest_rec, dest_field, u64a + u64b)))
            {
                return err;
            }
        }
        break;
      case FB_INT_8:
      case FB_INT_16:
      case FB_INT_32:
      case FB_INT_64:
        {
            int64_t i64a, i64b;
            if ((err = sk_fixrec_get_signed(dest_rec, dest_field, &i64a))
                || (err = sk_fixrec_get_signed(src_rec, src_field, &i64b))
                || (err = sk_fixrec_set_signed(
                        dest_rec, dest_field, i64a + i64b)))
            {
                return err;
            }
        }
        break;
      default:
        return SK_SCHEMA_ERR_INCOMPATIBLE;
    }
    return 0;
}

/*
 *    INTERNAL.
 *
 *    For each list element in 'rec', add the template(s) used by the
 *    list and any sub-lists it contains to 'session'.
 *
 *    NOTE: The function assumes the caller has already added the
 *    template used by 'rec' to 'session'.
 *
 *    Helper function for sk_fixrec_export_templates() and
 *    sk_fixrec_copy_list_templates().
 */
static void
sk_fixrec_update_session(
    fbSession_t        *session,
    const sk_fixrec_t  *rec,
    unsigned int        ext_int)
{
    const sk_field_t *field;
    size_t i;

    for (i = 0; i < FIELD_LIST_ARRAY_COUNT(rec->schema->listfields); ++i) {
        field = FIELD_LIST_ARRAY_GET(rec->schema->listfields, i);
        switch (field->ie->type) {
          case FB_BASIC_LIST:
            sk_fixrec_update_session_basic(
                session, rec->data + field->offset, ext_int);
            break;
          case FB_SUB_TMPL_LIST:
            sk_fixrec_update_session_subtemplate(
                session, rec->data + field->offset, ext_int);
            break;
          case FB_SUB_TMPL_MULTI_LIST:
            sk_fixrec_update_session_subtemplatemulti(
                session, rec->data + field->offset, ext_int);
            break;
          default:
            skAbortBadCase(field->ie->type);
        }
    }
}

sk_schema_err_t
sk_fixrec_export_templates(
    const sk_fixrec_t  *rec,
    fbSession_t        *session,
    unsigned int        ext_int)
{
    const sk_schema_t *schema;
    fbTemplate_t *cur_tmpl;
    GError *gerr = NULL;

    schema = rec->schema;
    if (NULL == schema) {
        return SK_SCHEMA_ERR_NO_SCHEMA;
    }
    /* add the record's template to the session */
    cur_tmpl = fbSessionGetTemplate(session, ext_int, schema->tid, NULL);
    if (schema->tmpl != cur_tmpl) {
        TRACEMSG(4, (("%s:%d:Adding %s template %p 0x%04x to session %p"
                      " (replacing %p)"),
                     __FILE__, __LINE__,
                     ((0 == ext_int) ? "external" : "internal"),
                     schema->tmpl, schema->tid, session, cur_tmpl));
        if (!fbSessionAddTemplate(
                session, ext_int, schema->tid, schema->tmpl, &gerr)) {
            TRACEMSG(2, ("Unable to add template %p 0x%04x"
                         " to session %p: %s",
                         (void*)schema->tmpl, schema->tid,
                         (void*)schema->session, gerr->message));
            g_clear_error(&gerr);
            return SK_SCHEMA_ERR_FIXBUF;
        }
    }
    /* visit the lists in the record */
    sk_fixrec_update_session(session, rec, ext_int);
    return SK_SCHEMA_ERR_SUCCESS;
}

sk_schema_err_t
sk_fixrec_copy_list_templates(
    const sk_fixrec_t  *rec)
{
    if (NULL == rec->schema) {
        return SK_SCHEMA_ERR_NO_SCHEMA;
    }
    sk_fixrec_update_session(rec->schema->session, rec, 1);
    return SK_SCHEMA_ERR_SUCCESS;
}


sk_schema_err_t
sk_fixrec_copy_fixbuf_data(
    sk_fixrec_t        *src)
{
    sk_field_t *field;
    size_t i;

    assert(src);
    assert(src->schema);

    if (!(src->flags & SK_FIXREC_FIXBUF_VARDATA)) {
        /* does not contain vardata owned by fixbuf */
        return 0;
    }
    /* process the varfields */
    for (i = 0; i < FIELD_LIST_ARRAY_COUNT(src->schema->varfields); ++i) {
        field = FIELD_LIST_ARRAY_GET(src->schema->varfields, i);
        assert(field->len == FB_IE_VARLEN);
        sk_fixrec_copy_varfield(src->data + field->offset,
                                src->data + field->offset);
    }
#if 0
    /* process the lists and any varfields they contain */
    for (i = 0; i < FIELD_LIST_ARRAY_COUNT(src->schema->listfields); ++i) {
        field = FIELD_LIST_ARRAY_GET(src->schema->listfields, i);
        switch (field->ie->type) {
          case FB_BASIC_LIST:
                sk_fixrec_copy_list_basic(dest->data + field->offset,
                                          src->data + field->offset);
                break;
              case FB_SUB_TMPL_LIST:
                sk_fixrec_copy_list_subtemplate(dest->data + field->offset,
                                                src->data + field->offset);
                break;
              case FB_SUB_TMPL_MULTI_LIST:
                sk_fixrec_copy_list_subtemplatemulti(dest->data + field->offset,
                                                     src->data + field->offset);
                break;
              default:
                skAbortBadCase(field->ie->type);
            }
        }
        /* FIXME */
        skAbort();
    }
#endif  /* 0 */
    src->flags &= ~SK_FIXREC_FIXBUF_VARDATA;
    return 0;
}

void
sk_schemamap_destroy(
    sk_schemamap_t     *map)
{
    free(map);
}

sk_schema_err_t
sk_schemamap_apply(
    const sk_schemamap_t   *map,
    sk_fixrec_t            *dest,
    const sk_fixrec_t      *src)
{
    sk_fixrec_template_map_t *tmpl_map = NULL;
    fbVarfield_t vf;
    fbVarfield_t vf2;
    uint16_t len;
    double d;
    float f;

    assert(map);
    assert(dest);
    assert(src);

    if (dest->flags & SK_FIXREC_FIXBUF_VARDATA) {
        sk_fixrec_copy_fixbuf_data(dest);
    }

    /* process any lists first to get their templates */
    if (SK_SCHEMAMAP_RECORD_COPY_INTO != map->op_type) {
        const sk_schemamap_t *m = map;
        unsigned int has_lists = 0;

        tmpl_map = sk_fixrec_template_map_create(dest);

        while (m->op_type != SK_SCHEMAMAP_DONE) {
            switch (m->op_type) {
              case SK_SCHEMAMAP_COPY_BASIC_LIST:
                has_lists = 1;
                sk_fixrec_template_map_add_basic(
                    tmpl_map, src->data + m->op.copy.from);
                break;
              case SK_SCHEMAMAP_COPY_SUB_TMPL_LIST:
                has_lists = 1;
                sk_fixrec_template_map_add_subtemplate(
                    tmpl_map, src->data + m->op.copy.from);
                break;
              case SK_SCHEMAMAP_COPY_SUB_TMPL_MULTI_LIST:
                has_lists = 1;
                sk_fixrec_template_map_add_subtemplatemulti(
                    tmpl_map, src->data + m->op.copy.from);
                break;
              default:
                break;
            }
            ++m;
        }

        if (!has_lists) {
            sk_fixrec_template_map_destroy(tmpl_map);
            tmpl_map = NULL;
        } else {
            sk_fixrec_template_map_update_session(
                tmpl_map, dest->schema->session);
        }
    }

    /* copy the data */
    while (map->op_type != SK_SCHEMAMAP_DONE) {
        switch (map->op_type) {
          case SK_SCHEMAMAP_DONE:
            break;
          case SK_SCHEMAMAP_RECORD_COPY_INTO:
            assert(SK_SCHEMAMAP_DONE == (map + 1)->op_type);
            return sk_fixrec_copy_into(dest, src);
          case SK_SCHEMAMAP_COPY:
            memcpy(dest->data + map->op.copy.to,
                   src->data + map->op.copy.from,
                   map->op.copy.length);
            break;
          case SK_SCHEMAMAP_ZERO:
            memset(dest->data + map->op.zero.offset, 0, map->op.zero.length);
            break;
          case SK_SCHEMAMAP_COPY_VARLEN_TO_VARLEN:
            memcpy(&vf2, src->data + map->op.copy.from, sizeof(fbVarfield_t));
            memcpy(&vf, dest->data + map->op.copy.to, sizeof(fbVarfield_t));
            if (vf.len >= vf2.len) {
                memcpy(vf.buf, vf2.buf, vf2.len);
                memset(vf.buf + vf2.len, 0, vf.len - vf2.len);
                vf.len = vf2.len;
            } else {
                sk_fixrec_free_varfield(&vf);
                sk_fixrec_copy_varfield(&vf, &vf2);
            }
            memcpy(dest->data + map->op.copy.to, &vf, sizeof(fbVarfield_t));
            break;
          case SK_SCHEMAMAP_COPY_TO_VARLEN:
            memcpy(&vf, dest->data + map->op.copy.to, sizeof(fbVarfield_t));
            if (vf.len >= map->op.copy.length) {
                memcpy(vf.buf, src->data + map->op.copy.from,
                       map->op.copy.length);
                memset(vf.buf + map->op.copy.length, 0,
                       vf.len - map->op.copy.length);
                vf.len = map->op.copy.length;
            } else {
                vf2.len = map->op.copy.length;
                vf2.buf = src->data + map->op.copy.from;
                sk_fixrec_free_varfield(&vf);
                sk_fixrec_copy_varfield(&vf, &vf2);
            }
            memcpy(dest->data + map->op.copy.to, &vf, sizeof(fbVarfield_t));
            break;
          case SK_SCHEMAMAP_COPY_FROM_VARLEN:
            memcpy(&vf, src->data + map->op.copy.from, sizeof(fbVarfield_t));
            len = vf.len > map->op.copy.length ? map->op.copy.length : vf.len;
            memcpy(dest->data + map->op.copy.to, vf.buf, len);
            if (map->op.copy.length > len) {
                memset(dest->data + map->op.copy.to + len, 0,
                       map->op.copy.length - len);
            }
            break;
          case SK_SCHEMAMAP_COPY_F32_TO_F64:
            memcpy(&f, src->data + map->op.copy.from, 4);
            d = f;
            memcpy(dest->data + map->op.copy.to, &d, 8);
            break;
          case SK_SCHEMAMAP_COPY_F64_TO_F32:
            memcpy(&d, src->data + map->op.copy.from, 8);
            f = d;
            memcpy(dest->data + map->op.copy.to, &f, 4);
            break;
          case SK_SCHEMAMAP_COPY_DATETIME:
            {
                fbInfoElement_t fie, tie;
                sk_field_t from, to;
                struct timespec dt;
                fie.type = map->op.dt.from_type;
                tie.type = map->op.dt.to_type;
                from.len = (fie.type == FB_DT_SEC) ? 4 : 8;
                to.len = (tie.type == FB_DT_SEC) ? 4 : 8;
                from.ie = &fie;
                to.ie = &tie;
                from.offset = map->op.dt.from;
                to.offset = map->op.dt.to;
                from.schema = src->schema;
                to.schema = dest->schema;
                sk_field_ops_init(&from.ops);
                sk_field_ops_init(&to.ops);
                sk_fixrec_get_datetime_timespec(src, &from, &dt);
                sk_fixrec_set_datetime_timespec(dest, &to, &dt);
            }
            break;
          case SK_SCHEMAMAP_COPY_BASIC_LIST:
            sk_fixrec_free_list_basic(dest->data + map->op.copy.to);
            sk_fixrec_copy_list_basic(
                dest->data + map->op.copy.to, src->data + map->op.copy.from,
                tmpl_map);
            break;
          case SK_SCHEMAMAP_COPY_SUB_TMPL_LIST:
            sk_fixrec_free_list_subtemplate(dest->data + map->op.copy.to);
            sk_fixrec_copy_list_subtemplate(
                dest->data + map->op.copy.to, src->data + map->op.copy.from,
                tmpl_map);
            break;
          case SK_SCHEMAMAP_COPY_SUB_TMPL_MULTI_LIST:
            sk_fixrec_free_list_subtemplatemulti(dest->data + map->op.copy.to);
            sk_fixrec_copy_list_subtemplatemulti(
                dest->data + map->op.copy.to, src->data + map->op.copy.from,
                tmpl_map);
            break;
        }
        ++map;
    }

    sk_fixrec_template_map_destroy(tmpl_map);
    return 0;
}

/*
 *    Callback function for skQSort().  Called on an array of
 *    sk_field_t where a pair of fields is considered a single unit (a
 *    source dest field pair).  Sorts the field-pair in the order they
 *    appear in the destination.
 */
static int
sk_schemamap_field_compare(
    const void         *va,
    const void         *vb)
{
    const sk_field_t **a = (const sk_field_t **)va;
    const sk_field_t **b = (const sk_field_t **)vb;
    ++a;
    ++b;
    assert(*a);
    assert(*b);
    return (*a)->offset - (*b)->offset;
}


/*
 *    Create a schemamap for mapping values between fields on
 *    different schemas.
 */
sk_schema_err_t
sk_schemamap_create_across_fields(
    sk_schemamap_t    **map,
    const sk_vector_t  *src_dest_pairs)
{
    size_t count;
    size_t i;
    const sk_field_t **fields;
    sk_vector_t *vec;
    sk_schemamap_t op;
    sk_schemamap_t zero;
    sk_schemamap_t last;
    size_t lindex = 0;
    sk_schema_err_t err = 0;

    assert(map);
    assert(src_dest_pairs);

    /* Must be a vector of pointers containing an even number of
     * entries.  (src, dest) */
    count = sk_vector_get_count(src_dest_pairs);
    if (sk_vector_get_element_size(src_dest_pairs) != sizeof(sk_field_t *)
        || count & 1)
    {
        assert(sk_vector_get_element_size(src_dest_pairs)
               == sizeof(sk_field_t *));
        assert((count & 1) == 0);
        return SK_SCHEMA_ERR_UNSPECIFIED;
    }
    if (0 == count) {
        *map = sk_alloc(sk_schemamap_t);
        (*map)->op_type = SK_SCHEMAMAP_DONE;
        return SK_SCHEMA_ERR_SUCCESS;
    }

    fields = (const sk_field_t **)sk_vector_to_array_alloc(src_dest_pairs);

    skQSort(fields, count >> 1, sizeof(const sk_field_t *) * 2,
            sk_schemamap_field_compare);
    vec = sk_vector_create(sizeof(sk_schemamap_t));

    last.op_type = SK_SCHEMAMAP_DONE;
    zero.op_type = SK_SCHEMAMAP_ZERO;
    for (i = 0; i < count; i += 2) {
        const sk_field_t *src = fields[i];
        const sk_field_t *dest = fields[i + 1];
        int stype = sk_field_get_type(src);
        int dtype = sk_field_get_type(dest);
        uint16_t slen = sk_field_get_length(src);
        uint16_t dlen = sk_field_get_length(dest);

        /* Reduced float64 can be treated just like float32 */
        if (stype == FB_FLOAT_64 && slen == 4) {
            stype = FB_FLOAT_32;
        }
        if (dtype == FB_FLOAT_64 && dlen == 4) {
            dtype = FB_FLOAT_32;
        }

        op.op.copy.from = src->offset;
        op.op.copy.to = dest->offset;
        if (stype == dtype) {
            /* Copying between equivalent types */
            if (TYPE_IS_LIST(stype)) {
                if (slen != dlen || slen != FB_IE_VARLEN) {
                    TRACEMSG(2, ("Copying fixed size lists is not supported"));
                    err = SK_SCHEMA_ERR_BAD_TYPE;
                    goto END;
                }
                if (FB_BASIC_LIST == stype) {
                    op.op_type = SK_SCHEMAMAP_COPY_BASIC_LIST;
                    op.op.copy.length = sizeof(fbBasicList_t);
                } else if (FB_SUB_TMPL_LIST == stype) {
                    op.op_type = SK_SCHEMAMAP_COPY_SUB_TMPL_LIST;
                    op.op.copy.length = sizeof(fbSubTemplateList_t);
                } else if (FB_SUB_TMPL_MULTI_LIST == stype) {
                    op.op_type = SK_SCHEMAMAP_COPY_SUB_TMPL_MULTI_LIST;
                    op.op.copy.length = sizeof(fbSubTemplateMultiList_t);
                } else {
                    skAppPrintErr("Unexpected type %u\n", stype);
                    skAbort();
                }
            } else if (slen == dlen) {
                /* Same size */
                if (slen != FB_IE_VARLEN) {
                    /* Same type, same size */
                    op.op_type = SK_SCHEMAMAP_COPY;
                    op.op.copy.length = dlen;
                } else {
                    /* Sane type, both varlen */
                    op.op_type = SK_SCHEMAMAP_COPY_VARLEN_TO_VARLEN;
                    op.op.copy.length = sizeof(fbVarfield_t);
                }
            } else if (slen == FB_IE_VARLEN) {
                if (!TYPE_IS_STRINGLIKE(stype)) {
                    err = SK_SCHEMA_ERR_BAD_TYPE;
                    goto END;
                }
                op.op_type = SK_SCHEMAMAP_COPY_FROM_VARLEN;
                op.op.copy.length = dlen;
            } else if (dlen == FB_IE_VARLEN) {
                if (!TYPE_IS_STRINGLIKE(stype)) {
                    err = SK_SCHEMA_ERR_BAD_TYPE;
                    goto END;
                }
                op.op_type = SK_SCHEMAMAP_COPY_TO_VARLEN;
                op.op.copy.length = slen;
            } else if (dlen < slen) {
                /* dest is smaller than source */
                if (!TYPE_IS_INT(stype) && !TYPE_IS_STRINGLIKE(stype)) {
                    err = SK_SCHEMA_ERR_BAD_TYPE;
                    goto END;
                }
                /* truncated copy */
                err = SK_SCHEMA_ERR_TRUNCATED;
                op.op_type = SK_SCHEMAMAP_COPY;
                op.op.copy.length = dlen;
#if SK_BIG_ENDIAN
                if (TYPE_IS_INT(stype)) {
                    op.op.copy.from += slen - dlen;
                }
#endif
            } else {
                /* dest is larger than source.  Part of the dest needs
                 * to be zeroed. */
                zero.op.zero.length = dlen - slen;
                zero.op.zero.offset = op.op.copy.to;
                op.op_type = SK_SCHEMAMAP_COPY;
                op.op.copy.length = slen;
                if (TYPE_IS_INT(stype)) {
#if SK_BIG_ENDIAN
                    op.op.copy.to += zero.op.zero.length;
#else
                    zero.op.zero.offset += op.op.copy.length;
#endif
                }
                sk_vector_append_value(vec, &zero);
                last = zero;
                lindex = sk_vector_get_count(vec) - 1;
            }
        } else {
            /* types differ */
            if (stype == FB_FLOAT_32 && dtype == FB_FLOAT_64) {
                /* float32 to float64 */
                if (slen != 4 || dlen != 8) {
                    err = SK_SCHEMA_ERR_BAD_SIZE;
                    goto END;
                }
                op.op_type = SK_SCHEMAMAP_COPY_F32_TO_F64;
                op.op.copy.length = 4;
            } else if (stype == FB_FLOAT_64 && dtype == FB_FLOAT_32) {
                /* float64 to float32 */
                if (slen != 8 || dlen != 4) {
                    err = SK_SCHEMA_ERR_BAD_SIZE;
                    goto END;
                }
                op.op_type = SK_SCHEMAMAP_COPY_F64_TO_F32;
                op.op.copy.length = 8;
            } else if (TYPE_IS_DT(stype) && TYPE_IS_DT(dtype)) {
                /* datetime to datetime */
                op.op_type = SK_SCHEMAMAP_COPY_DATETIME;
                op.op.dt.from = src->offset;
                op.op.dt.to = dest->offset;
                op.op.dt.from_type = stype;
                op.op.dt.to_type = dtype;
            } else {
                err = SK_SCHEMA_ERR_BAD_TYPE;
                goto END;
            }
        }

        if (op.op_type == SK_SCHEMAMAP_COPY
            && last.op_type == SK_SCHEMAMAP_COPY
            && (last.op.copy.from + last.op.copy.length
                == op.op.copy.from)
            && (last.op.copy.to + last.op.copy.length
                == op.op.copy.to))
        {
            /* Merge contiguous copies */
            last.op.copy.length += op.op.copy.length;
            sk_vector_set_value(vec, lindex, &last);
            continue;
        }
        /* Add new operation to list */
        sk_vector_append_value(vec, &op);
        last = op;
        lindex = sk_vector_get_count(vec) - 1;
    }
    op.op_type = SK_SCHEMAMAP_DONE;
    sk_vector_append_value(vec, &op);
    *map = (sk_schemamap_t *)sk_vector_to_array_alloc(vec);

  END:
    free(fields);
    sk_vector_destroy(vec);
    return err;
}

sk_schema_err_t
sk_schemamap_create_across_schemas(
    sk_schemamap_t    **map,
    const sk_schema_t  *dest,
    const sk_schema_t  *src)
{
    uint16_t s, d;
    const sk_field_t *sf, *df;
    sk_vector_t *vec;
    sk_bitmap_t *used;
    sk_schema_err_t err;

    assert(map);
    assert(src);
    assert(dest);
    if (NULL == dest->tmpl || NULL == src->tmpl) {
        return SK_SCHEMA_ERR_NOT_FROZEN;
    }
    if (dest == src) {
        *map = sk_alloc_array(sk_schemamap_t, 2);
        (*map)[0].op_type = SK_SCHEMAMAP_RECORD_COPY_INTO;
        (*map)[1].op_type = SK_SCHEMAMAP_DONE;
        return 0;
    }

    vec = sk_vector_create(sizeof(const sk_field_t *));
    skBitmapCreate(&used, sk_schema_get_count(src));

    /* For each destination field */
    for (d = 0; d < sk_schema_get_count(dest); ++d) {
        df = sk_schema_get_field(dest, d);
        /* Look for a matching source */
        for (s = 0; s < sk_schema_get_count(src); ++s) {
            if (skBitmapGetBit(used, s)) {
                /* Ignore src fields already used */
                continue;
            }
            sf = sk_schema_get_field(src, s);
            if (sf->ie->ent == df->ie->ent
                && sf->ie->num == df->ie->num)
            {
                skBitmapSetBit(used, s);
                sk_vector_append_value(vec, &sf);
                sk_vector_append_value(vec, &df);
                break;
            }
        }
    }
    err = sk_schemamap_create_across_fields(map, vec);
    sk_vector_destroy(vec);
    skBitmapDestroy(&used);
    return err;
}

sk_schema_err_t
sk_schema_timemap_create(
    sk_schema_timemap_t   **timemap_out,
    sk_schema_t            *schema)
{
    SK_SCHEMA_TIMEMAP_EN_DEFINE;
    sk_schema_timemap_t *timemap;
    unsigned time0_is_duration = 0;
    sk_field_t *f;
    sk_schema_err_t rv;

    assert(schema);
    assert(timemap_out);

    timemap = sk_alloc(sk_schema_timemap_t);

    /* look for a start time */
    if ((timemap->start_msec
         = sk_schema_get_field_by_ident(schema, start_milli, NULL)))
    {
        /* flowStartMilliseconds */

        if ((timemap->end_msec
             = sk_schema_get_field_by_ident(schema, end_milli, NULL)))
        {
            /* flowEndMilliseconds */

            /* record has the times fields we want; there is nothing
             * we need to do */
            timemap->schema = sk_schema_clone(schema);
            *timemap_out = timemap;
            return SK_SCHEMA_ERR_SUCCESS;
        }
        /* we will need to use the stime to compute the end time */
        timemap->rec_start = timemap->start_msec;
    } else if ((timemap->rec_start
                = sk_schema_get_field_by_ident(schema, start_sec, NULL)))
    {
        /* flowStartSeconds */

    } else if ((timemap->rec_start
                = sk_schema_get_field_by_ident(schema, start_micro, NULL)))
    {
        /* flowStartMicroseconds */

    } else if ((timemap->rec_start
                = sk_schema_get_field_by_ident(schema, start_nano, NULL)))
    {
        /* flowStartNanoseconds */

    } else if ((timemap->rec_start
                = sk_schema_get_field_by_ident(schema,start_delta_micro,NULL)))
    {
        /* flowStartDeltaMicroseconds */

    } else if ((timemap->rec_start
                = sk_schema_get_field_by_ident(schema, start_uptime, NULL)))
    {
        /* flowStartSysUpTime */

        /* systemInitTimeMilliseconds */
        timemap->rec_init
            = sk_schema_get_field_by_ident(schema, sys_init_time, NULL);

    } else if ((timemap->rec_start
                = sk_schema_get_field_by_ident(schema, dur_milli, NULL))
               || ((timemap->rec_start
                    = sk_schema_get_field_by_ident(schema, dur_micro, NULL))))
    {
        /* flowDurationMilliseconds or flowDurationMicroseconds */
        time0_is_duration = 1;

    }

    /* insert a milliseconds start-time field if we did not find one */
    if (NULL == timemap->start_msec) {
        rv = sk_schema_insert_field_by_ident(
            &f, schema, start_milli, NULL, NULL);
        if (rv) {
            free(timemap);
            return rv;
        }
        timemap->start_msec = f;
    }

    /* look for an end time */
    if ((timemap->rec_end
         = sk_schema_get_field_by_ident(schema, end_milli, NULL)))
    {
        /* flowEndMilliseconds */
        timemap->end_msec = timemap->rec_end;

    } else if ((timemap->rec_end
                = sk_schema_get_field_by_ident(schema, end_sec, NULL)))
    {
        /* flowEndSeconds */

    } else if ((timemap->rec_end
                = sk_schema_get_field_by_ident(schema, end_micro, NULL)))
    {
        /* flowEndMicroseconds */

    } else if ((timemap->rec_end
                = sk_schema_get_field_by_ident(schema, end_nano, NULL)))
    {
        /* flowEndNanoseconds */

    } else if ((timemap->rec_end
                = sk_schema_get_field_by_ident(schema, end_delta_micro, NULL)))
    {
        /* flowEndDeltaMicroseconds */

    } else if (time0_is_duration) {
        /* do not look for any other end-times */

    } else if ((timemap->rec_end
                = sk_schema_get_field_by_ident(schema, end_uptime, NULL)))
    {
        /* flowEndSysUpTime */

        /* find the systemInitTimeMilliseconds */
        if (NULL == timemap->rec_init) {
            timemap->rec_init
                = sk_schema_get_field_by_ident(schema, sys_init_time, NULL);
        }

    } else if ((timemap->rec_end
                = sk_schema_get_field_by_ident(schema, dur_milli, NULL))
               || ((timemap->rec_end
                    = sk_schema_get_field_by_ident(schema, dur_micro, NULL))))
    {
        /* flowDurationMilliseconds or flowDurationMicroseconds */

    } else {
        /* use the first field in the schema which will cause the
         * end time to be set the flow record's export time */
        timemap->rec_end = sk_schema_get_field(schema, 0);
    }

    /* insert a milliseconds end-time field if we did not find one */
    if (NULL == timemap->end_msec) {
        rv = sk_schema_insert_field_by_ident(
            &f, schema, end_milli, NULL, NULL);
        if (rv) {
            free(timemap);
            return rv;
        }
        timemap->end_msec = f;
    }

    if (NULL == timemap->rec_start) {
        /* if no start-time was found, set the start-time to whatever
         * end-time we found */
        assert(NULL != timemap->rec_end);
        timemap->rec_start = timemap->rec_end;
    }

    timemap->schema = sk_schema_clone(schema);
    *timemap_out = timemap;
    return SK_SCHEMA_ERR_SUCCESS;
}

void
sk_schema_timemap_destroy(
    sk_schema_timemap_t    *timemap)
{
    if (timemap) {
        sk_schema_destroy(timemap->schema);
        free(timemap);
    }
}

sk_schema_err_t
sk_schema_timemap_apply(
    const sk_schema_timemap_t  *timemap,
    sk_fixrec_t                *rec,
    uint32_t                    rec_export_time)
{
    SK_SCHEMA_TIMEMAP_EN_DEFINE;
    uint64_t val1;
    uint64_t val2;
    sktime_t t;
    sk_schema_err_t rv;

    assert(timemap);
    assert(rec);

    if (timemap->schema != rec->schema) {
        return SK_SCHEMA_ERR_INCOMPATIBLE;
    }

    if (NULL == timemap->rec_start) {
        if (NULL == timemap->rec_end) {
            return SK_SCHEMA_ERR_SUCCESS;
        }
        t = 0;
    } else {
        switch (sk_field_get_ident(timemap->rec_start)) {
          case start_milli:     /* flowStartMilliseconds */
            rv = sk_fixrec_get_datetime(rec, timemap->rec_start, &t);
            if (rv) { return rv; }
            break;

          case start_sec:       /* flowStartSeconds */
          case start_micro:     /* flowStartMicroseconds */
          case start_nano:      /* flowStartNanoseconds */
            rv = sk_fixrec_get_datetime(rec, timemap->rec_start, &t);
            if (rv) { return rv; }
            rv = sk_fixrec_set_datetime(rec, timemap->start_msec, t);
            if (rv) { return rv; }
            break;

          case start_delta_micro: /* flowStartDeltaMicroseconds */
            sk_fixrec_get_unsigned(rec, timemap->rec_start, &val1);
            t = sktimeCreate(rec_export_time, 0) - (val1 / 1000);
            rv = sk_fixrec_set_datetime(rec, timemap->start_msec, t);
            if (rv) { return rv; }
            break;

          case start_uptime:    /* flowStartSysUpTime */
            sk_fixrec_get_unsigned(rec, timemap->rec_start, &val1);
            if (timemap->rec_init) {
                /* systemInitTimeMilliseconds */
                rv = sk_fixrec_get_datetime(rec, timemap->rec_init, &t);
                if (rv) { return rv; }
                t += val1;
                rv = sk_fixrec_set_datetime(rec, timemap->start_msec, t);
                if (rv) { return rv; }

            } else if (end_uptime == sk_field_get_ident(timemap->rec_start)) {
                /* flowEndSysUpTime */
                sk_fixrec_get_unsigned(rec, timemap->rec_start, &val2);

                /* we do not know when the router booted; base the
                 * times on the IPFIX packet's export time, doing
                 * whatever we need to do to maintain the duration */
                t = sktimeCreate(rec_export_time, 0);
                if (val2 >= val1) {
                    rv = sk_fixrec_set_datetime(
                        rec, timemap->start_msec, t - val2);
                    if (rv) { return rv; }
                    rv = sk_fixrec_set_datetime(
                        rec, timemap->end_msec, t - val1);
                    if (rv) { return rv; }
                } else {
                    rv = sk_fixrec_set_datetime(
                        rec, timemap->start_msec, t - val1);
                    if (rv) { return rv; }
                    rv = sk_fixrec_set_datetime(
                        rec, timemap->end_msec, t - val2);
                    if (rv) { return rv; }
                }
                return SK_SCHEMA_ERR_SUCCESS;

            } else {
                /* set start and end times to flow export time */
                t = sktimeCreate(rec_export_time, 0);
                rv = sk_fixrec_set_datetime(rec, timemap->start_msec, t);
                if (rv) { return rv; }
                rv = sk_fixrec_set_datetime(rec, timemap->end_msec, t);
                if (rv) { return rv; }
                return SK_SCHEMA_ERR_SUCCESS;
            }
            break;

          case dur_milli:       /* flowDurationMilliseconds */
          case dur_micro:       /* flowDurationMicroseconds */
            sk_fixrec_get_unsigned(rec, timemap->rec_start, &val1);
            if (dur_micro == sk_field_get_ident(timemap->rec_start)) {
                val1 /= 1000;
            }
            if (NULL == timemap->rec_end) {
                /* Assume flow export time is the flow end time,
                 * subtract duration to get start time */
                t = sktimeCreate(rec_export_time, 0);
                rv = sk_fixrec_set_datetime(rec, timemap->end_msec, t);
                if (rv) { return rv; }
                rv = sk_fixrec_set_datetime(rec, timemap->start_msec, t-val1);
                if (rv) { return rv; }
            } else {
                switch (sk_field_get_ident(timemap->rec_end)) {
                  case end_milli: /* flowEndMilliseconds */
                    rv = sk_fixrec_get_datetime(rec, timemap->rec_end, &t);
                    if (rv) { return rv; }
                    break;

                  case end_sec:   /* flowEndSeconds */
                  case end_micro: /* flowEndMicroseconds */
                  case end_nano:  /* flowEndNanoseconds */
                    rv = sk_fixrec_get_datetime(rec, timemap->rec_end, &t);
                    if (rv) { return rv; }
                    rv = sk_fixrec_set_datetime(rec, timemap->end_msec, t);
                    if (rv) { return rv; }
                    break;

                  case end_delta_micro: /* flowEndDeltaMicroseconds */
                    sk_fixrec_get_unsigned(rec, timemap->rec_end, &val2);
                    t = sktimeCreate(rec_export_time, 0) - (val2 / 1000);
                    rv = sk_fixrec_set_datetime(rec, timemap->end_msec, t);
                    if (rv) { return rv; }
                    break;

                  default:
                    skAbortBadCase(sk_field_get_ident(timemap->rec_end));
                }

                rv = sk_fixrec_set_datetime(rec, timemap->start_msec, t-val1);
                if (rv) { return rv; }
            }
            return SK_SCHEMA_ERR_SUCCESS;

          default:
            /* set start time to flow export time */
            t = sktimeCreate(rec_export_time, 0);
            rv = sk_fixrec_set_datetime(rec, timemap->start_msec, t);
            if (rv) { return rv; }
            break;
        }
    }

    /* when timemap->rec_end is a duration, the code below assumes the
     * code above set 't' to the flow record's start time */

    switch (sk_field_get_ident(timemap->rec_end)) {
      case end_milli:           /* flowEndMilliseconds */
        break;

      case end_sec:             /* flowEndSeconds */
      case end_micro:           /* flowEndMicroseconds */
      case end_nano:            /* flowEndNanoseconds */
        rv = sk_fixrec_get_datetime(rec, timemap->rec_end, &t);
        if (rv) { return rv; }
        rv = sk_fixrec_set_datetime(rec, timemap->end_msec, t);
        if (rv) { return rv; }
        break;

      case end_delta_micro:     /* flowEndDeltaMicroseconds */
        sk_fixrec_get_unsigned(rec, timemap->rec_end, &val1);
        rv = sk_fixrec_set_datetime(
            rec, timemap->end_msec,
            (sktimeCreate(rec_export_time, 0) - (val1 / 1000)));
        if (rv) { return rv; }
        break;

      case end_uptime:          /* flowEndSysUpTime */
        sk_fixrec_get_unsigned(rec, timemap->rec_end, &val1);
        if (timemap->rec_init) {
            /* systemInitTimeMilliseconds */
            rv = sk_fixrec_get_datetime(rec, timemap->rec_init, &t);
            if (rv) { return rv; }
            rv = sk_fixrec_set_datetime(rec, timemap->end_msec, t + val1);
            if (rv) { return rv; }
        } else {
            /* set flow end time to start time */
            rv = sk_fixrec_set_datetime(rec, timemap->end_msec, t);
            if (rv) { return rv; }
        }
        break;

      case dur_milli:           /* flowDurationMilliseconds */
        sk_fixrec_get_unsigned(rec, timemap->rec_end, &val1);
        rv = sk_fixrec_set_datetime(rec, timemap->end_msec, t + val1);
        if (rv) { return rv; }
        break;

      case dur_micro:           /* flowDurationMicroseconds */
        sk_fixrec_get_unsigned(rec, timemap->rec_end, &val1);
        rv = sk_fixrec_set_datetime(rec, timemap->end_msec, t + val1 / 1000);
        if (rv) { return rv; }
        break;

      default:
        /* set end time to flow export time */
        rv = sk_fixrec_set_datetime(
            rec, timemap->end_msec, sktimeCreate(rec_export_time, 0));
        if (rv) { return rv; }
        break;
    }

    return SK_SCHEMA_ERR_SUCCESS;
}

sk_schema_err_t
sk_fixrec_get_value_length(
    const sk_fixrec_t  *rec,
    const sk_field_t   *field,
    uint16_t           *val)
{
    fbVarfield_t vf;

    ASSERT_FIELD_IN_REC(field, rec);
    if (field->len != FB_IE_VARLEN) {
        *val = field->len;
        return 0;
    }
    memcpy(&vf, rec->data + field->offset, sizeof(fbVarfield_t));
    *val = vf.len;
    return 0;
}

sk_schema_err_t
sk_fixrec_get_unsigned(
    const sk_fixrec_t  *rec,
    const sk_field_t   *field,
    uint64_t           *val)
{
    return sk_fixrec_get_sized_uint(rec, field, val, sizeof(uint64_t));
}

sk_schema_err_t
sk_fixrec_get_signed(
    const sk_fixrec_t  *rec,
    const sk_field_t   *field,
    int64_t            *val)
{
    switch (field->ie->type) {
      case FB_INT_8:
      case FB_INT_16:
      case FB_INT_32:
      case FB_INT_64:
        return sk_fixrec_get_sized_int(rec, field, val, sizeof(uint64_t));
      case FB_UINT_8:
      case FB_UINT_16:
      case FB_UINT_32:
        return sk_fixrec_get_sized_uint(rec, field, val, sizeof(uint64_t));
      case FB_UINT_64:
        /* Not safe to load a uint64_t into an int64_t, as the ranges
         * don't mesh */
      default:
        break;
    }
    return SK_SCHEMA_ERR_BAD_TYPE;
}

sk_schema_err_t
sk_fixrec_get_sized_uint(
    const sk_fixrec_t  *rec,
    const sk_field_t   *field,
    void               *val,
    size_t              val_size)
{
    ASSERT_FIELD_IN_REC(field, rec);
    switch (field->ie->type) {
      case FB_UINT_8:
      case FB_UINT_16:
      case FB_UINT_32:
      case FB_UINT_64:
        break;
      default:
        return SK_SCHEMA_ERR_BAD_TYPE;
    }
    if (field->len == FB_IE_VARLEN) {
        return SK_SCHEMA_ERR_BAD_SIZE;
    }
    if (val_size > field->len) {
        memset(val, 0, val_size);
    }
    MEM_TO_NUM(val, val_size, rec->data + field->offset, field->len);
    return 0;
}

sk_schema_err_t
sk_fixrec_get_sized_int(
    const sk_fixrec_t  *rec,
    const sk_field_t   *field,
    void               *val,
    size_t              val_size)
{
    ASSERT_FIELD_IN_REC(field, rec);
    switch (field->ie->type) {
      case FB_INT_8:
      case FB_INT_16:
      case FB_INT_32:
      case FB_INT_64:
        break;
      default:
        return SK_SCHEMA_ERR_BAD_TYPE;
    }
    if (field->len == FB_IE_VARLEN) {
        return SK_SCHEMA_ERR_BAD_SIZE;
    }
    if (val_size > field->len) {
        uint8_t fill;
        uint8_t *sign_byte = rec->data + field->offset;
#if !SK_BIG_ENDIAN
        sign_byte += field->len - 1;
#endif
        fill = ((*sign_byte) & 0x80) ? 0xff : 0;
        memset(val, fill, val_size);
    }
    MEM_TO_NUM(val, val_size, rec->data + field->offset, field->len);
    return 0;
}

sk_schema_err_t
sk_fixrec_get_float(
    const sk_fixrec_t  *rec,
    const sk_field_t   *field,
    double             *val)
{
    float f;

    ASSERT_FIELD_IN_REC(field, rec);
    switch (field->ie->type) {
      case FB_FLOAT_64:
        if (field->len == 8) {
            memcpy(val, rec->data + field->offset, 8);
            break;
        }
        /* fall through */
      case FB_FLOAT_32:
        REC_CHECK_SIZE_SET_VAR_FROM_FIELD(rec, field, f);
        *val = f;
        break;
      default:
        return SK_SCHEMA_ERR_BAD_TYPE;
    }
    return 0;
}

sk_schema_err_t
sk_fixrec_get_ip_address(
    const sk_fixrec_t  *rec,
    const sk_field_t   *field,
    skipaddr_t         *addr)
{
    ASSERT_FIELD_IN_REC(field, rec);
    switch (field->ie->type) {
      case FB_IP4_ADDR:
        if (field->len != 4) {
            return SK_SCHEMA_ERR_BAD_SIZE;
        }
        skipaddrSetV4(addr, rec->data + field->offset);
        break;
      case FB_IP6_ADDR:
        if (field->len != 16) {
            return SK_SCHEMA_ERR_BAD_SIZE;
        }
        skipaddrSetV6(addr, rec->data + field->offset);
        break;
      default:
        return SK_SCHEMA_ERR_BAD_TYPE;
    }
    return 0;
}


sk_schema_err_t
sk_fixrec_get_datetime(
    const sk_fixrec_t  *rec,
    const sk_field_t   *field,
    sktime_t           *val)
{
    uint64_t u64;
    uint32_t u32;

    ASSERT_FIELD_IN_REC(field, rec);
    switch (field->ie->type) {
      case FB_DT_SEC:
        REC_CHECK_SIZE_SET_VAR_FROM_FIELD(rec, field, u32);
        *val = sktimeCreate(u32, 0);
        break;
      case FB_DT_MILSEC:
        REC_CHECK_SIZE_SET_VAR_FROM_FIELD(rec, field, u64);
        *val = u64;
        break;
      case FB_DT_MICROSEC:
      case FB_DT_NANOSEC:
        REC_CHECK_SIZE_SET_VAR_FROM_FIELD(rec, field, u64);
        if (FB_DT_MICROSEC == field->ie->type) {
            u64 &= ~UINT64_C(0x7FF);
        }
        /* FIXME: Handle NTP wraparaound for Feb 8 2036 */
        *val = (((int64_t)(u64 >> 32) - NTP_EPOCH_TO_UNIX_EPOCH) * 1000
                + (u64 & UINT32_MAX) * 1000 / NTPFRAC);
        break;
      default:
        return SK_SCHEMA_ERR_BAD_TYPE;
    }
    return 0;
}

sk_schema_err_t
sk_fixrec_get_datetime_ntp(
    const sk_fixrec_t  *rec,
    const sk_field_t   *field,
    sk_ntp_time_t      *val)
{
    uint64_t u64;
    uint32_t u32;

    /* FIXME: Handle NTP wraparaound for Feb 8 2036 */
    ASSERT_FIELD_IN_REC(field, rec);
    switch (field->ie->type) {
      case FB_DT_SEC:
        REC_CHECK_SIZE_SET_VAR_FROM_FIELD(rec, field, u32);
        *val = (NTP_EPOCH_TO_UNIX_EPOCH + (uint64_t)u32) << 32;
        break;
      case FB_DT_MILSEC:
        REC_CHECK_SIZE_SET_VAR_FROM_FIELD(rec, field, u64);
        *val = u64 / 1000;
        *val = (((*val + NTP_EPOCH_TO_UNIX_EPOCH) << 32)
                + ((u64 - (*val * 1000)) * (double)NTPFRAC / 1000.0));
        break;
      case FB_DT_MICROSEC:
        REC_CHECK_SIZE_SET_VAR_FROM_FIELD(rec, field, u64);
        /* Mask off lower 11 bits for microseconds */
        *val = u64 & ~UINT64_C(0x7ff);
        break;
      case FB_DT_NANOSEC:
        REC_CHECK_SIZE_SET_VAR_FROM_FIELD(rec, field, *val);
        break;
      default:
        return SK_SCHEMA_ERR_BAD_TYPE;
    }
    return 0;
}

sk_schema_err_t
sk_fixrec_get_datetime_timespec(
    const sk_fixrec_t  *rec,
    const sk_field_t   *field,
    struct timespec    *val)
{
    uint64_t u64;
    uint32_t u32;

    ASSERT_FIELD_IN_REC(field, rec);
    switch (field->ie->type) {
      case FB_DT_SEC:
        /* record's time is a 32bit integer with the number of seconds
         * since UNIX epoch */
        REC_CHECK_SIZE_SET_VAR_FROM_FIELD(rec, field, u32);
        val->tv_sec = (time_t)u32;
        val->tv_nsec = 0;
        break;
      case FB_DT_MILSEC:
        /* record's time is a 64bit integer with the number of
         * milliseconds since UNIX epoch */
        REC_CHECK_SIZE_SET_VAR_FROM_FIELD(rec, field, u64);
        val->tv_sec = (time_t)(u64 / 1000);
        val->tv_nsec = 1000000L * (u64 % 1000);
        break;
      case FB_DT_MICROSEC:
        /* record's time is a 64bit integer in NTP timestamp format;
         * the value 0xFFFFF800 below is UINT32_MAX & ~0x7FF */
        REC_CHECK_SIZE_SET_VAR_FROM_FIELD(rec, field, u64);
        val->tv_sec = (time_t)(u64 >> 32) - NTP_EPOCH_TO_UNIX_EPOCH;
        val->tv_nsec = (long)(UINT64_C(1000000000) * (u64 & 0xFFFFF800)
                              / NTPFRAC);
        break;
      case FB_DT_NANOSEC:
        /* record's time is a 64bit integer in NTP timestamp format */
        REC_CHECK_SIZE_SET_VAR_FROM_FIELD(rec, field, u64);
        val->tv_sec = (time_t)(u64 >> 32) - NTP_EPOCH_TO_UNIX_EPOCH;
        val->tv_nsec = (long)(UINT64_C(1000000000) * (u64 & UINT32_MAX)
                              / NTPFRAC);
        break;
      default:
        return SK_SCHEMA_ERR_BAD_TYPE;
    }
    return 0;
}

sk_schema_err_t
sk_fixrec_get_octets(
    const sk_fixrec_t  *rec,
    const sk_field_t   *field,
    uint8_t            *val,
    uint16_t           *len)
{
    fbVarfield_t vf;
    size_t n;

    ASSERT_FIELD_IN_REC(field, rec);
    switch (field->ie->type) {
      case FB_BASIC_LIST:
      case FB_SUB_TMPL_LIST:
      case FB_SUB_TMPL_MULTI_LIST:
        return SK_SCHEMA_ERR_BAD_TYPE;
      case FB_OCTET_ARRAY:
      case FB_STRING:
        if (field->len == FB_IE_VARLEN) {
            memcpy(&vf, rec->data + field->offset, sizeof(fbVarfield_t));
            n = *len < vf.len ? *len : vf.len;
            *len = vf.len;
            memcpy(val, vf.buf, n);
            break;
        }
        /* FALLTHROUGH */
      default:
        n = *len < field->len ? *len : field->len;
        *len = field->len;
        memcpy(val, rec->data + field->offset, n);
        break;
    }
    return 0;
}

sk_schema_err_t
sk_fixrec_set_unsigned(
    sk_fixrec_t        *rec,
    const sk_field_t   *field,
    uint64_t            val)
{
    return sk_fixrec_set_sized_uint(rec, field, &val, sizeof(val));
}

sk_schema_err_t
sk_fixrec_set_signed(
    sk_fixrec_t        *rec,
    const sk_field_t   *field,
    int64_t             val)
{
    return sk_fixrec_set_sized_int(rec, field, &val, sizeof(val));
}

sk_schema_err_t
sk_fixrec_set_sized_uint(
    sk_fixrec_t        *rec,
    const sk_field_t   *field,
    const void         *val,
    size_t              val_size)
{
    ASSERT_FIELD_IN_REC(field, rec);
    switch (field->ie->type) {
      case FB_UINT_8:
      case FB_UINT_16:
      case FB_UINT_32:
      case FB_UINT_64:
        break;
      default:
        return SK_SCHEMA_ERR_BAD_TYPE;
    }
    if (field->len == FB_IE_VARLEN) {
        return SK_SCHEMA_ERR_BAD_SIZE;
    }
    if (field->len > val_size) {
        memset(rec->data + field->offset, 0, field->len);
    }
    NUM_TO_MEM(rec->data + field->offset, field->len, val, val_size);
    return 0;
}

sk_schema_err_t
sk_fixrec_set_sized_int(
    sk_fixrec_t        *rec,
    const sk_field_t   *field,
    const void         *val,
    size_t              val_size)
{
    ASSERT_FIELD_IN_REC(field, rec);
    switch (field->ie->type) {
      case FB_INT_8:
      case FB_INT_16:
      case FB_INT_32:
      case FB_INT_64:
        break;
      default:
        return SK_SCHEMA_ERR_BAD_TYPE;
    }
    if (field->len == FB_IE_VARLEN) {
        return SK_SCHEMA_ERR_BAD_SIZE;
    }
    if (field->len > val_size) {
        uint8_t fill;
        uint8_t *sign_byte = (uint8_t *)val;
#if !SK_BIG_ENDIAN
        sign_byte += val_size - 1;
#endif
        fill = ((*sign_byte) & 0x80) ? 0xff : 0;
        memset(rec->data + field->offset, fill, field->len);
    }
    NUM_TO_MEM(rec->data + field->offset, field->len, val, val_size);
    return 0;
}

sk_schema_err_t
sk_fixrec_set_float(
    sk_fixrec_t        *rec,
    const sk_field_t   *field,
    double              val)
{
    float f;
    ASSERT_FIELD_IN_REC(field, rec);
    switch (field->ie->type) {
      case FB_FLOAT_64:
        if (field->len == 8) {
            memcpy(rec->data + field->offset, &val, 8);
            break;
        }
        /* Fall through */
      case FB_FLOAT_32:
        f = val;
        REC_CHECK_SIZE_SET_FIELD_FROM_VAR(rec, field, f);
        break;
      default:
        return SK_SCHEMA_ERR_BAD_TYPE;
    }
    return 0;
}

sk_schema_err_t
sk_fixrec_set_ip_address(
    sk_fixrec_t        *rec,
    const sk_field_t   *field,
    const skipaddr_t   *addr)
{
    uint32_t u32;

    ASSERT_FIELD_IN_REC(field, rec);
    switch (field->ie->type) {
      case FB_IP4_ADDR:
        if (field->len != 4) {
            return SK_SCHEMA_ERR_BAD_SIZE;
        }
        if (skipaddrGetAsV4(addr, &u32)) {
            return SK_SCHEMA_ERR_NOT_IPV4;
        }
        memcpy(rec->data + field->offset, &u32, 4);
        break;
      case FB_IP6_ADDR:
        if (field->len != 16) {
            return SK_SCHEMA_ERR_BAD_SIZE;
        }
        skipaddrGetAsV6(addr, rec->data + field->offset);
        break;
      default:
        return SK_SCHEMA_ERR_BAD_TYPE;
    }
    return 0;
}

sk_schema_err_t
sk_fixrec_set_datetime(
    sk_fixrec_t        *rec,
    const sk_field_t   *field,
    sktime_t            val)
{
    uint64_t u64;
    uint32_t u32;

    ASSERT_FIELD_IN_REC(field, rec);
    switch (field->ie->type) {
      case FB_DT_SEC:
        u32 = val / 1000;
        REC_CHECK_SIZE_SET_FIELD_FROM_VAR(rec, field, u32);
        break;
      case FB_DT_MILSEC:
        u64 = val;
        REC_CHECK_SIZE_SET_FIELD_FROM_VAR(rec, field, u64);
        break;
      case FB_DT_MICROSEC:
      case FB_DT_NANOSEC:
        /* FIXME: Handle NTP wraparaound for Feb 8 2036 */
        u64 = val / 1000;
        u64 = (((u64 + NTP_EPOCH_TO_UNIX_EPOCH) << 32)
               | ((val - u64 * 1000) * NTPFRAC / 1000));
        if (field->ie->type == FB_DT_MICROSEC) {
            u64 &= ~UINT64_C(0x7FF);
        }
        REC_CHECK_SIZE_SET_FIELD_FROM_VAR(rec, field, u64);
        break;
      default:
        return SK_SCHEMA_ERR_BAD_TYPE;
    }
    return 0;
}

sk_schema_err_t
sk_fixrec_set_datetime_ntp(
    sk_fixrec_t        *rec,
    const sk_field_t   *field,
    sk_ntp_time_t       val)
{
    uint32_t u32;
    uint64_t u64;

    ASSERT_FIELD_IN_REC(field, rec);
    switch (field->ie->type) {
      case FB_DT_SEC:
        u32 = (val >> 32) - NTP_EPOCH_TO_UNIX_EPOCH;
        REC_CHECK_SIZE_SET_FIELD_FROM_VAR(rec, field, u32);
        break;
      case FB_DT_MILSEC:
        /* FIXME: Handle NTP wraparaound for Feb 8 2036 */
        u64 = (((val >> 32) - NTP_EPOCH_TO_UNIX_EPOCH) * UINT64_C(1000)
               + ((val & UINT32_MAX) * UINT64_C(1000) / NTPFRAC));
        REC_CHECK_SIZE_SET_FIELD_FROM_VAR(rec, field, u64);
        break;
      case FB_DT_MICROSEC:
        val &= ~UINT64_C(0x7FF);
        /* FALLTHROUGH */
      case FB_DT_NANOSEC:
        REC_CHECK_SIZE_SET_FIELD_FROM_VAR(rec, field, val);
        break;
      default:
        return SK_SCHEMA_ERR_BAD_TYPE;
    }
    return 0;
}

sk_schema_err_t
sk_fixrec_set_datetime_timespec(
    sk_fixrec_t            *rec,
    const sk_field_t       *field,
    const struct timespec  *val)
{
    uint64_t u64;
    uint32_t u32;

    ASSERT_FIELD_IN_REC(field, rec);
    switch (field->ie->type) {
      case FB_DT_SEC:
        /* record's time is a 32bit integer with the number of seconds
         * since UNIX epoch */
        if (field->len != 4) {
            return SK_SCHEMA_ERR_BAD_SIZE;
        }
        u32 = (uint32_t)val->tv_sec;
        REC_CHECK_SIZE_SET_FIELD_FROM_VAR(rec, field, u32);
        break;
      case FB_DT_MILSEC:
        /* record's time is a 64bit integer with the number of
         * milliseconds since UNIX epoch */
        u64 = val->tv_sec * 1000 + val->tv_nsec / 1000000;
        REC_CHECK_SIZE_SET_FIELD_FROM_VAR(rec, field, u64);
        break;
      case FB_DT_MICROSEC:
        /* record's time is a 64bit integer in NTP timestamp format;
         * the value 0xFFFFF800 below is UINT32_MAX & ~0x7FF */
        u64 = ((uint64_t)((double)val->tv_nsec / (double)UINT64_C(1000000000)
                          * (double)NTPFRAC)) & 0xFFFFF800;
        u64 |= (((uint64_t)(NTP_EPOCH_TO_UNIX_EPOCH + val->tv_sec)) << 32);
        REC_CHECK_SIZE_SET_FIELD_FROM_VAR(rec, field, u64);
        break;
      case FB_DT_NANOSEC:
        /* record's time is a 64bit integer in NTP timestamp format */
        u64 = ((uint64_t)((double)val->tv_nsec / (double)UINT64_C(1000000000)
                          * (double)NTPFRAC)) & UINT32_MAX;
        u64 |= (((uint64_t)(NTP_EPOCH_TO_UNIX_EPOCH + val->tv_sec) << 32));
        REC_CHECK_SIZE_SET_FIELD_FROM_VAR(rec, field, u64);
        break;
      default:
        return SK_SCHEMA_ERR_BAD_TYPE;
    }
    return 0;
}

sk_schema_err_t
sk_fixrec_set_octets(
    sk_fixrec_t        *rec,
    const sk_field_t   *field,
    const uint8_t      *val,
    uint16_t            len)
{
    sk_schema_err_t retval = 0;
    int number = 0;

    ASSERT_FIELD_IN_REC(field, rec);
    switch (field->ie->type) {
      case FB_BASIC_LIST:
      case FB_SUB_TMPL_LIST:
      case FB_SUB_TMPL_MULTI_LIST:
        return SK_SCHEMA_ERR_BAD_TYPE;
      case FB_UINT_16:
      case FB_UINT_32:
      case FB_UINT_64:
      case FB_INT_16:
      case FB_INT_32:
      case FB_INT_64:
        number = 1;
        break;
      default:
        break;
    }
    if (field->len == FB_IE_VARLEN) {
        /* we should not allow the user to change a varfield when the
         * record's data is owned by fixbuf unless the field was
         * added to the schema by a silk plugin.  right now we can
         * detect when fixbuf owns a record, but not whether a field
         * is from a plugin, so for now just hope the user respects
         * the "const" setting of an sk_fixrec_t. */
        fbVarfield_t vf;
        fbVarfield_t vf2;
        memcpy(&vf, rec->data + field->offset, sizeof(fbVarfield_t));
        if (len <= vf.len) {
            memcpy(vf.buf, val, len);
            memset(vf.buf + len, 0, vf.len - len);
            vf.len = len;
        } else {
            vf2.len = len;
            vf2.buf = (uint8_t*)val;
            sk_fixrec_free_varfield(&vf);
            sk_fixrec_copy_varfield(&vf, &vf2);
        }
        memcpy(rec->data + field->offset, &vf, sizeof(fbVarfield_t));
    } else {
        if (number) {
            if (field->len > len) {
                memset(rec->data + field->offset, 0, field->len);
            } else if (field->len < len) {
                retval = SK_SCHEMA_ERR_TRUNCATED;
            }
            NUM_TO_MEM(rec->data + field->offset, field->len, val, len);
        } else {
            uint16_t n;
            if (len > field->len) {
                n = field->len;
                retval = SK_SCHEMA_ERR_TRUNCATED;
            } else {
                n = len;
            }
            memcpy(rec->data + field->offset, val, n);
        }
    }

    return retval;
}

sk_schema_err_t
sk_fixrec_get_unsigned8(
    const sk_fixrec_t  *rec,
    const sk_field_t   *field,
    uint8_t            *val)
{
    ASSERT_FIELD_IN_REC(field, rec);
    FIELD_CHECK_TYPE(field, FB_UINT_8);
    REC_CHECK_SIZE_SET_VAR_FROM_FIELD(rec, field, *val);
    return 0;
}

/**
 *    Fill 'val' with the uint16_t represented by 'field' in 'rec'.
 */
sk_schema_err_t
sk_fixrec_get_unsigned16(
    const sk_fixrec_t  *rec,
    const sk_field_t   *field,
    uint16_t           *val)
{
    ASSERT_FIELD_IN_REC(field, rec);
    FIELD_CHECK_TYPE(field, FB_UINT_16);
    switch(field->len) {
      case 1:
        *val = *(rec->data + field->offset);
        break;
      case 2:
        memcpy(val, rec->data + field->offset, 2);
        break;
      default:
        return SK_SCHEMA_ERR_BAD_SIZE;
    }
    return 0;
}

/**
 *    Fill 'val' with the uint32_t represented by 'field' in 'rec'.
 */
sk_schema_err_t
sk_fixrec_get_unsigned32(
    const sk_fixrec_t  *rec,
    const sk_field_t   *field,
    uint32_t           *val)
{
    uint64_t u64;
    sk_schema_err_t err;

    FIELD_CHECK_TYPE(field, FB_UINT_32);
    switch(field->len) {
      case 1:
      case 2:
      case 3:
        if ((err = sk_fixrec_get_unsigned(rec, field, &u64))) {
            return err;
        }
        *val = u64;
        break;
      case 4:
        ASSERT_FIELD_IN_REC(field, rec);
        memcpy(val, rec->data + field->offset, 4);
        break;
      default:
        return SK_SCHEMA_ERR_BAD_SIZE;
    }
    return 0;
}


/**
 *    Fill 'val' with the uint64_t represented by 'field' in 'rec'.
 */
sk_schema_err_t
sk_fixrec_get_unsigned64(
    const sk_fixrec_t  *rec,
    const sk_field_t   *field,
    uint64_t           *val)
{
    FIELD_CHECK_TYPE(field, FB_UINT_64);
    switch(field->len) {
      case 1:
      case 2:
      case 3:
      case 4:
      case 5:
      case 6:
      case 7:
        return sk_fixrec_get_unsigned(rec, field, val);
      case 8:
        ASSERT_FIELD_IN_REC(field, rec);
        memcpy(val, rec->data + field->offset, 8);
        break;
      default:
        return SK_SCHEMA_ERR_BAD_SIZE;
    }
    return 0;
}

/**
 *    Fill 'val' with the int8_t represented by 'field' in 'rec'.
 */
sk_schema_err_t
sk_fixrec_get_signed8(
    const sk_fixrec_t  *rec,
    const sk_field_t   *field,
    int8_t             *val)
{
    ASSERT_FIELD_IN_REC(field, rec);
    FIELD_CHECK_TYPE(field, FB_INT_8);
    REC_CHECK_SIZE_SET_VAR_FROM_FIELD(rec, field, *val);
    return 0;
}

/**
 *    Fill 'val' with the int16_t represented by 'field' in 'rec'.
 */
sk_schema_err_t
sk_fixrec_get_signed16(
    const sk_fixrec_t  *rec,
    const sk_field_t   *field,
    int16_t            *val)
{
    int8_t i8;

    ASSERT_FIELD_IN_REC(field, rec);
    FIELD_CHECK_TYPE(field, FB_INT_16);
    switch(field->len) {
      case 1:
        memcpy(&i8, rec->data + field->offset, 1);
        *val = i8;
        break;
      case 2:
        memcpy(val, rec->data + field->offset, 2);
        break;
      default:
        return SK_SCHEMA_ERR_BAD_SIZE;
    }
    return 0;
}


/**
 *    Fill 'val' with the int32_t represented by 'field' in 'rec'.
 */
sk_schema_err_t
sk_fixrec_get_signed32(
    const sk_fixrec_t  *rec,
    const sk_field_t   *field,
    int32_t            *val)
{
    int64_t i64;
    sk_schema_err_t err;

    FIELD_CHECK_TYPE(field, FB_INT_32);
    switch(field->len) {
      case 1:
      case 2:
      case 3:
        if ((err = sk_fixrec_get_signed(rec, field, &i64))) {
            return err;
        }
        *val = i64;
        break;
      case 4:
        ASSERT_FIELD_IN_REC(field, rec);
        memcpy(val, rec->data + field->offset, 4);
        break;
      default:
        return SK_SCHEMA_ERR_BAD_SIZE;
    }
    return 0;
}

/**
 *    Fill 'val' with the int64_t represented by 'field' in 'rec'.
 */
sk_schema_err_t
sk_fixrec_get_signed64(
    const sk_fixrec_t  *rec,
    const sk_field_t   *field,
    int64_t            *val)
{
    FIELD_CHECK_TYPE(field, FB_INT_64);
    switch(field->len) {
      case 1:
      case 2:
      case 3:
      case 4:
      case 5:
      case 6:
      case 7:
        return sk_fixrec_get_signed(rec, field, val);
      case 8:
        ASSERT_FIELD_IN_REC(field, rec);
        memcpy(val, rec->data + field->offset, 8);
        break;
      default:
        return SK_SCHEMA_ERR_BAD_SIZE;
    }
    return 0;
}

/**
 *    Fill 'val' with the IPv4 address represented by 'field' in
 *    'rec'.
 */
sk_schema_err_t
sk_fixrec_get_ipv4_addr(
    const sk_fixrec_t  *rec,
    const sk_field_t   *field,
    uint32_t           *val)
{
    ASSERT_FIELD_IN_REC(field, rec);
    FIELD_CHECK_TYPE(field, FB_IP4_ADDR);
    REC_CHECK_SIZE_SET_VAR_FROM_FIELD(rec, field, *val);
    return 0;
}

/**
 *    Fill 'val' with the 16-byte IPv6 address represented by 'field'
 *    in 'rec'.
 */
sk_schema_err_t
sk_fixrec_get_ipv6_addr(
    const sk_fixrec_t  *rec,
    const sk_field_t   *field,
    uint8_t            *val)
{
    ASSERT_FIELD_IN_REC(field, rec);
    FIELD_CHECK_TYPE(field, FB_IP6_ADDR);
    if (field->len != 16) {
        return SK_SCHEMA_ERR_BAD_SIZE;
    }
    memcpy(val, rec->data + field->offset, 16);
    return 0;
}


/**
 *    Fill 'val' with the float represented by 'field' in 'rec'.
 */
sk_schema_err_t
sk_fixrec_get_float32(
    const sk_fixrec_t  *rec,
    const sk_field_t   *field,
    float              *val)
{
    ASSERT_FIELD_IN_REC(field, rec);
    FIELD_CHECK_TYPE(field, FB_FLOAT_32);
    REC_CHECK_SIZE_SET_VAR_FROM_FIELD(rec, field, *val);
    return 0;
}

/**
 *    Fill 'val' with the double represented by 'field' in 'rec'.
 */
sk_schema_err_t
sk_fixrec_get_float64(
    const sk_fixrec_t  *rec,
    const sk_field_t   *field,
    double             *val)
{
    float f;

    ASSERT_FIELD_IN_REC(field, rec);
    FIELD_CHECK_TYPE(field, FB_FLOAT_64);
    switch (field->len) {
      case 8:
        memcpy(val, rec->data + field->offset, 8);
        break;
      case 4:
        memcpy(&f, rec->data + field->offset, 4);
        *val = f;
        break;
      default:
        return SK_SCHEMA_ERR_BAD_SIZE;
    }

    return 0;
}


/**
 *    Fill 'val' with the boolean value represented by 'field' in
 *    'rec'.  (False is 0, true will be 1.)
 */
sk_schema_err_t
sk_fixrec_get_boolean(
    const sk_fixrec_t  *rec,
    const sk_field_t   *field,
    int                *val)
{
    uint8_t b;

    ASSERT_FIELD_IN_REC(field, rec);
    FIELD_CHECK_TYPE(field, FB_BOOL);
    if (field->len != 1) {
        return SK_SCHEMA_ERR_BAD_SIZE;
    }
    b = *(rec->data + field->offset);
    switch (b) {
      case 1:
        *val = 1;
        break;
      case 2:
        *val = 0;
        break;
      default:
        *val = b;
        return SK_SCHEMA_ERR_UNKNOWN_BOOL;
    }
    return 0;
}

/**
 *    Fill 'val' with the 6-byte MAC address represented by 'field' in
 *    'rec'.
 */
sk_schema_err_t
sk_fixrec_get_mac_address(
    const sk_fixrec_t  *rec,
    const sk_field_t   *field,
    uint8_t            *val)
{
    ASSERT_FIELD_IN_REC(field, rec);
    FIELD_CHECK_TYPE(field, FB_MAC_ADDR);
    if (field->len != 6) {
        return SK_SCHEMA_ERR_BAD_SIZE;
    }
    memcpy(val, rec->data + field->offset, 6);
    return 0;
}


/**
 *    Fill 'val' with the string represented by 'field' in 'rec'.
 *    'len' must be non-NULL and contain the length of the buffer
 *    'val' when called.  The resulting string will be truncated if
 *    'val' is too small, and the result will be null-terminated
 *    regardless.  The underlying length of the string (without null)
 *    will be returned in 'len'.
 */
sk_schema_err_t
sk_fixrec_get_string(
    const sk_fixrec_t  *rec,
    const sk_field_t   *field,
    char               *val,
    uint16_t           *len)
{
    fbVarfield_t vf;

    ASSERT_FIELD_IN_REC(field, rec);
    FIELD_CHECK_TYPE(field, FB_STRING);
    if (field->len == FB_IE_VARLEN) {
        memcpy(&vf, rec->data + field->offset, sizeof(fbVarfield_t));
    } else {
        vf.len = field->len;
        vf.buf = rec->data + field->offset;
    }
    if (*len == 0) {
        *len = vf.len;
    } else if (vf.len < *len) {
        *len = vf.len;
        memcpy(val, vf.buf, *len);
        val[*len] = '\0';
    } else {
        memcpy(val, vf.buf, *len - 1);
        val[*len - 1] = '\0';
        *len = vf.len;
    }
    return 0;
}


/**
 *    Fill 'val' with the octets represented by 'field' in 'rec'.
 *    'len' must be non-NULL and contain the length of the buffer
 *    'val' when called.  The underlying size of the octet array will
 *    be returned in 'len'.  If the size of the buffer is smaller than
 *    the data, the data will be truncated.
 */
sk_schema_err_t
sk_fixrec_get_octet_array(
    const sk_fixrec_t  *rec,
    const sk_field_t   *field,
    uint8_t            *val,
    uint16_t           *len)
{
    fbVarfield_t vf;

    ASSERT_FIELD_IN_REC(field, rec);
    FIELD_CHECK_TYPE(field, FB_OCTET_ARRAY);
    if (field->len == FB_IE_VARLEN) {
        memcpy(&vf, rec->data + field->offset, sizeof(fbVarfield_t));
    } else {
        vf.len = field->len;
        vf.buf = rec->data + field->offset;
    }
    if (*len == 0) {
        *len = vf.len;
    } else if (vf.len < *len) {
        *len = vf.len;
        memcpy(val, vf.buf, *len);
    } else {
        memcpy(val, vf.buf, *len);
        *len = vf.len;
    }
    return 0;
}

/**
 *    Fill 'val' with the number of seconds represented by 'field' in
 *    'rec'.
 */
sk_schema_err_t
sk_fixrec_get_datetime_seconds(
    const sk_fixrec_t  *rec,
    const sk_field_t   *field,
    uint32_t           *val)
{
    ASSERT_FIELD_IN_REC(field, rec);
    FIELD_CHECK_TYPE(field, FB_DT_SEC);
    REC_CHECK_SIZE_SET_VAR_FROM_FIELD(rec, field, *val);
    return 0;
}

/**
 *    Fill 'val' with the number of milliseconds represented by
 *    'field' in 'rec'.
 */
sk_schema_err_t
sk_fixrec_get_datetime_milliseconds(
    const sk_fixrec_t  *rec,
    const sk_field_t   *field,
    uint64_t           *val)
{
    ASSERT_FIELD_IN_REC(field, rec);
    FIELD_CHECK_TYPE(field, FB_DT_MILSEC);
    REC_CHECK_SIZE_SET_VAR_FROM_FIELD(rec, field, *val);
    return 0;
}


/**
 *    Fill 'val' with the date-time represented by 'field' in 'rec'.
 */
sk_schema_err_t
sk_fixrec_get_datetime_microseconds(
    const sk_fixrec_t  *rec,
    const sk_field_t   *field,
    sk_ntp_time_t      *val)
{
    ASSERT_FIELD_IN_REC(field, rec);
    FIELD_CHECK_TYPE(field, FB_DT_MICROSEC);
    REC_CHECK_SIZE_SET_VAR_FROM_FIELD(rec, field, *val);
    *val &= ~UINT64_C(0x7ff);
    return 0;
}

/**
 *    Fill 'val' with the date-time represented by 'field' in 'rec'.
 */
sk_schema_err_t
sk_fixrec_get_datetime_nanoseconds(
    const sk_fixrec_t  *rec,
    const sk_field_t   *field,
    sk_ntp_time_t      *val)
{
    ASSERT_FIELD_IN_REC(field, rec);
    FIELD_CHECK_TYPE(field, FB_DT_NANOSEC);
    REC_CHECK_SIZE_SET_VAR_FROM_FIELD(rec, field, *val);
    return 0;
}



/** Type specific get functions **/

/**
 *    Set 'field' in 'rec' to 'val'.
 */
sk_schema_err_t
sk_fixrec_set_unsigned8(
    sk_fixrec_t        *rec,
    const sk_field_t   *field,
    uint8_t             val)
{
    ASSERT_FIELD_IN_REC(field, rec);
    FIELD_CHECK_TYPE(field, FB_UINT_8);
    REC_CHECK_SIZE_SET_FIELD_FROM_VAR(rec, field, val);
    return 0;
}

/**
 *    Set 'field' in 'rec' to 'val'.
 */
sk_schema_err_t
sk_fixrec_set_unsigned16(
    sk_fixrec_t        *rec,
    const sk_field_t   *field,
    uint16_t            val)
{
    ASSERT_FIELD_IN_REC(field, rec);
    FIELD_CHECK_TYPE(field, FB_UINT_16);
    switch(field->len) {
      case 1:
        *(rec->data + field->offset) = val & 0xff;
        break;
      case 2:
        memcpy(rec->data + field->offset, &val, 2);
        break;
      default:
        return SK_SCHEMA_ERR_BAD_SIZE;
    }
    return 0;
}

/**
 *    Set 'field' in 'rec' to 'val'.
 */
sk_schema_err_t
sk_fixrec_set_unsigned32(
    sk_fixrec_t        *rec,
    const sk_field_t   *field,
    uint32_t            val)
{
    FIELD_CHECK_TYPE(field, FB_UINT_32);
    switch(field->len) {
      case 1:
      case 2:
      case 3:
        return sk_fixrec_set_unsigned(rec, field, val);
      case 4:
        memcpy(rec->data + field->offset, &val, 4);
        break;
      default:
        ASSERT_FIELD_IN_REC(field, rec);
        return SK_SCHEMA_ERR_BAD_SIZE;
    }
    return 0;
}

/**
 *    Set 'field' in 'rec' to 'val'.
 */
sk_schema_err_t
sk_fixrec_set_unsigned64(
    sk_fixrec_t        *rec,
    const sk_field_t   *field,
    uint64_t            val)
{
    FIELD_CHECK_TYPE(field, FB_UINT_64);
    switch(field->len) {
      case 1:
      case 2:
      case 3:
      case 4:
      case 5:
      case 6:
      case 7:
        return sk_fixrec_set_unsigned(rec, field, val);
      case 8:
        ASSERT_FIELD_IN_REC(field, rec);
        memcpy(rec->data + field->offset, &val, 8);
        break;
      default:
        return SK_SCHEMA_ERR_BAD_SIZE;
    }
    return 0;
}

/**
 *    Set 'field' in 'rec' to 'val'.
 */
sk_schema_err_t
sk_fixrec_set_signed8(
    sk_fixrec_t        *rec,
    const sk_field_t   *field,
    int8_t              val)
{
    ASSERT_FIELD_IN_REC(field, rec);
    FIELD_CHECK_TYPE(field, FB_INT_8);
    REC_CHECK_SIZE_SET_FIELD_FROM_VAR(rec, field, val);
    return 0;
}

/**
 *    Set 'field' in 'rec' to 'val'.
 */
sk_schema_err_t
sk_fixrec_set_signed16(
    sk_fixrec_t        *rec,
    const sk_field_t   *field,
    int16_t             val)
{
    ASSERT_FIELD_IN_REC(field, rec);
    FIELD_CHECK_TYPE(field, FB_INT_16);
    switch(field->len) {
      case 1:
        *(rec->data + field->offset) = *(uint8_t *)&val & 0xff;
        break;
      case 2:
        memcpy(rec->data + field->offset, &val, 2);
        break;
      default:
        return SK_SCHEMA_ERR_BAD_SIZE;
    }
    return 0;
}

/**
 *    Set 'field' in 'rec' to 'val'.
 */
sk_schema_err_t
sk_fixrec_set_signed32(
    sk_fixrec_t        *rec,
    const sk_field_t   *field,
    int32_t             val)
{
    FIELD_CHECK_TYPE(field, FB_INT_32);
    switch(field->len) {
      case 1:
      case 2:
      case 3:
        return sk_fixrec_set_signed(rec, field, val);
      case 4:
        ASSERT_FIELD_IN_REC(field, rec);
        memcpy(rec->data + field->offset, &val, 4);
        break;
      default:
        return SK_SCHEMA_ERR_BAD_SIZE;
    }
    return 0;
}

/**
 *    Set 'field' in 'rec' to 'val'.
 */
sk_schema_err_t
sk_fixrec_set_signed64(
    sk_fixrec_t        *rec,
    const sk_field_t   *field,
    int64_t             val)
{
    FIELD_CHECK_TYPE(field, FB_INT_64);
    switch(field->len) {
      case 1:
      case 2:
      case 3:
      case 4:
      case 5:
      case 6:
      case 7:
        return sk_fixrec_set_signed(rec, field, val);
      case 8:
        ASSERT_FIELD_IN_REC(field, rec);
        memcpy(rec->data + field->offset, &val, 8);
        break;
      default:
        return SK_SCHEMA_ERR_BAD_SIZE;
    }
    return 0;
}

/**
 *    Set 'field' in 'rec' to 'val'.
 */
sk_schema_err_t
sk_fixrec_set_ipv4_addr(
    sk_fixrec_t        *rec,
    const sk_field_t   *field,
    uint32_t            val)
{
    ASSERT_FIELD_IN_REC(field, rec);
    FIELD_CHECK_TYPE(field, FB_IP4_ADDR);
    REC_CHECK_SIZE_SET_FIELD_FROM_VAR(rec, field, val);
    return 0;
}


/**
 *    Set 'field' in 'rec' to 'val'.
 */
sk_schema_err_t
sk_fixrec_set_ipv6_addr(
    sk_fixrec_t        *rec,
    const sk_field_t   *field,
    const uint8_t      *val)
{
    ASSERT_FIELD_IN_REC(field, rec);
    FIELD_CHECK_TYPE(field, FB_IP6_ADDR);
    if (field->len != 16) {
        return SK_SCHEMA_ERR_BAD_SIZE;
    }
    memcpy(rec->data + field->offset, val, 16);
    return 0;
}

/**
 *    Set 'field' in 'rec' to 'val'.
 */
sk_schema_err_t
sk_fixrec_set_float32(
    sk_fixrec_t        *rec,
    const sk_field_t   *field,
    float               val)
{
    ASSERT_FIELD_IN_REC(field, rec);
    FIELD_CHECK_TYPE(field, FB_FLOAT_32);
    REC_CHECK_SIZE_SET_FIELD_FROM_VAR(rec, field, val);
    return 0;
}


/**
 *    Set 'field' in 'rec' to 'val'.
 */
sk_schema_err_t
sk_fixrec_set_float64(
    sk_fixrec_t        *rec,
    const sk_field_t   *field,
    double              val)
{
    float f;

    ASSERT_FIELD_IN_REC(field, rec);
    FIELD_CHECK_TYPE(field, FB_FLOAT_64);
    switch (field->len) {
      case 8:
        memcpy(rec->data + field->offset, &val, 8);
        break;
      case 4:
        f = val;
        memcpy(rec->data + field->offset, &f, 4);
        break;
      default:
        return SK_SCHEMA_ERR_BAD_SIZE;
    }

    return 0;
}


/**
 *    Set 'field' in 'rec' to 'val'.  A zero 'val' is considered true.
 *    A non-zero 'val' is considered false.
 */
sk_schema_err_t
sk_fixrec_set_boolean(
    sk_fixrec_t        *rec,
    const sk_field_t   *field,
    int                 val)
{
    ASSERT_FIELD_IN_REC(field, rec);
    FIELD_CHECK_TYPE(field, FB_BOOL);
    if (field->len != 1) {
        return SK_SCHEMA_ERR_BAD_SIZE;
    }
    *(rec->data + field->offset) = val ? 1 : 2;
    return 0;
}

/**
 *    Set 'field' in 'rec' to 'val'.  'val' us assumed to be 6 bytes
 *    long.
 */
sk_schema_err_t
sk_fixrec_set_mac_address(
    sk_fixrec_t        *rec,
    const sk_field_t   *field,
    const uint8_t      *val)
{
    ASSERT_FIELD_IN_REC(field, rec);
    FIELD_CHECK_TYPE(field, FB_MAC_ADDR);
    if (field->len != 6) {
        return SK_SCHEMA_ERR_BAD_SIZE;
    }
    memcpy(rec->data + field->offset, val, 6);
    return 0;
}

/**
 *    Set 'field' in 'rec' to 'val'.  'val' is assumed to be
 *    null-terminated.
 */
sk_schema_err_t
sk_fixrec_set_string(
    sk_fixrec_t        *rec,
    const sk_field_t   *field,
    const char         *val)
{
    FIELD_CHECK_TYPE(field, FB_STRING);
    return sk_fixrec_set_octets(rec, field, (uint8_t *)val, strlen(val));
}

/**
 *    Set 'field' in 'rec' to 'val'.  'len' should hold the length of
 *    'val'.
 */
sk_schema_err_t
sk_fixrec_set_octet_array(
    sk_fixrec_t        *rec,
    const sk_field_t   *field,
    const uint8_t      *val,
    uint16_t            len)
{
    FIELD_CHECK_TYPE(field, FB_OCTET_ARRAY);
    return sk_fixrec_set_octets(rec, field, val, len);
}


/**
 *    Set 'field' in 'rec' to 'val'.
 */
sk_schema_err_t
sk_fixrec_set_datetime_seconds(
    sk_fixrec_t        *rec,
    const sk_field_t   *field,
    uint32_t            val)
{
    ASSERT_FIELD_IN_REC(field, rec);
    FIELD_CHECK_TYPE(field, FB_DT_SEC);
    REC_CHECK_SIZE_SET_FIELD_FROM_VAR(rec, field, val);
    return 0;
}


/**
 *    Set 'field' in 'rec' to 'val'.
 */
sk_schema_err_t
sk_fixrec_set_datetime_milliseconds(
    sk_fixrec_t        *rec,
    const sk_field_t   *field,
    uint64_t            val)
{
    ASSERT_FIELD_IN_REC(field, rec);
    FIELD_CHECK_TYPE(field, FB_DT_MILSEC);
    REC_CHECK_SIZE_SET_FIELD_FROM_VAR(rec, field, val);
    return 0;
}

/**
 *    Set 'field' in 'rec' to 'val'.
 */
sk_schema_err_t
sk_fixrec_set_datetime_microseconds(
    sk_fixrec_t        *rec,
    const sk_field_t   *field,
    sk_ntp_time_t       val)
{
    ASSERT_FIELD_IN_REC(field, rec);
    FIELD_CHECK_TYPE(field, FB_DT_MICROSEC);
    val &= ~UINT64_C(0x7ff);
    REC_CHECK_SIZE_SET_FIELD_FROM_VAR(rec, field, val);
    return 0;
}


/**
 *    Set 'field' in 'rec' to 'val'.
 */
sk_schema_err_t
sk_fixrec_set_datetime_nanoseconds(
    sk_fixrec_t        *rec,
    const sk_field_t   *field,
    sk_ntp_time_t       val)
{
    ASSERT_FIELD_IN_REC(field, rec);
    FIELD_CHECK_TYPE(field, FB_DT_NANOSEC);
    REC_CHECK_SIZE_SET_FIELD_FROM_VAR(rec, field, val);
    return 0;
}


/*
 *  ******************************************************************
 *  Template Map (sk_fixrec_template_map_t)
 *  ******************************************************************
 */

/*
 *    Descend recursively into the lists contained in record 'rec' and
 *    add any templates used by the lists (or their sublists) to the
 *    template map 'tmpl_map'.
 *
 *    This function ignores the template used by the schema of 'rec'.
 */
static void
sk_fixrec_template_map_add_record(
    sk_fixrec_template_map_t   *tmpl_map,
    const sk_fixrec_t          *rec)
{
    const sk_field_t *field;
    size_t i;

    assert(rec);
    assert(tmpl_map);

    for (i = 0; i < FIELD_LIST_ARRAY_COUNT(rec->schema->listfields); ++i) {
        field = FIELD_LIST_ARRAY_GET(rec->schema->listfields, i);
        switch (field->ie->type) {
          case FB_BASIC_LIST:
            sk_fixrec_template_map_add_basic(
                tmpl_map, rec->data + field->offset);
            break;
          case FB_SUB_TMPL_LIST:
            sk_fixrec_template_map_add_subtemplate(
                tmpl_map, rec->data + field->offset);
            break;
          case FB_SUB_TMPL_MULTI_LIST:
            sk_fixrec_template_map_add_subtemplatemulti(
                tmpl_map, rec->data + field->offset);
            break;
          default:
            skAbortBadCase(field->ie->type);
        }
    }
}

/*
 *    Create and return a new template map.
 *
 *    If 'rec' is not NULL, add the template used by the schema of
 *    'rec' to the template map, then descend into the list and
 *    sublists of 'rec' and add their templates to the template map.
 */
static sk_fixrec_template_map_t *
sk_fixrec_template_map_create(
    const sk_fixrec_t  *rec)
{
    sk_fixrec_template_map_t *tmpl_map;
    sk_fixrec_template_map_entry_t entry;

    tmpl_map = sk_vector_create(sizeof(sk_fixrec_template_map_entry_t));
    if (NULL == rec || NULL == rec->schema || NULL == rec->schema->tmpl) {
        return tmpl_map;
    }
    memset(&entry, 0, sizeof(entry));
    entry.tmpl = rec->schema->tmpl;
    entry.tid = rec->schema->tid;
    sk_vector_append_value(tmpl_map, &entry);

    sk_fixrec_template_map_add_record(tmpl_map, rec);

    return tmpl_map;
}

/*
 *    If the template map 'tmpl_map' is not NULL, destroy it and all
 *    of its entries.
 */
static void
sk_fixrec_template_map_destroy(
    sk_fixrec_template_map_t   *tmpl_map)
{
    sk_vector_destroy(tmpl_map);
}

/*
 *    Search the template map 'tmpl_map' for the template pointer
 *    'tmpl'.  If found, set the memory referenced by 'tid' to the
 *    template's ID and return 1.  If not found, leave the value in
 *    'tid' unchanged and return 0.
 */
static int
sk_fixrec_template_map_find(
    sk_fixrec_template_map_t   *tmpl_map,
    const fbTemplate_t         *tmpl,
    uint16_t                   *tid)
{
    const sk_fixrec_template_map_entry_t *e;
    size_t i;

    assert(tmpl_map);
    assert(tmpl);
    assert(tid);

    for (i = 0;
         (e = ((const sk_fixrec_template_map_entry_t*)
               sk_vector_get_value_pointer(tmpl_map, i))) != NULL;
         ++i)
    {
        if (e->tmpl == tmpl) {
            *tid = e->tid;
            return 1;
        }
    }
    return 0;
}


/*
 *    Search the template map 'tmpl_map' for the template pointer
 *    'tmpl' or the template ID 'tid'.
 *
 *    If neither 'tmpl' nor 'tid' are found, add them as a new pair to
 *    the template map.  If 'tmpl' is found, do nothing and ignore the
 *    value in 'tid'.  If 'tid' is found (and 'tmpl' is not), add
 *    'tmpl' to the template map but do not assign it an ID.  The ID
 *    will be assigned when sk_fixrec_template_map_update_session() is
 *    called.
 */
static void
sk_fixrec_template_map_insert(
    sk_fixrec_template_map_t   *tmpl_map,
    fbTemplate_t               *tmpl,
    uint16_t                    tid)
{
    const sk_fixrec_template_map_entry_t *e;
    sk_fixrec_template_map_entry_t entry;
    size_t i;

    assert(tmpl_map);

    if (NULL == tmpl || 0 == tid) {
        return;
    }

    memset(&entry, 0, sizeof(entry));
    entry.tmpl = tmpl;
    entry.tid = tid;

    for (i = 0;
         (e = ((const sk_fixrec_template_map_entry_t*)
               sk_vector_get_value_pointer(tmpl_map, i))) != NULL;
         ++i)
    {
        if (e->tmpl == entry.tmpl) {
            /* template pointers match */
            return;
        }
        if (e->tid == entry.tid && FB_TID_AUTO != e->tid) {
            /* template ID is already in use; get new ID for entry */
            entry.tid = FB_TID_AUTO;
        }
    }
    sk_vector_append_value(tmpl_map, &entry);
}

/*
 *    Update the session object 'session' to hold the templates
 *    specified in the template map 'tmpl_map'.
 *
 *    Specifically, for each template ID in 'tmpl_map', add the
 *    template pointer/template ID pair to the session (removing any
 *    previous values, but do not replace a value with itself).
 *
 *    If a template ID 'tmpl_map' is not set (because that ID was
 *    already in used as described by
 *    sk_fixrec_template_map_insert()), determine whether the template
 *    pointer already exists in the session.  If so, use its ID.  If
 *    not, add the template pointer to the session, allow the session
 *    to assign the template ID, and store that template ID for that
 *    template pointer in the template map.
 */
static sk_schema_err_t
sk_fixrec_template_map_update_session(
    sk_fixrec_template_map_t   *tmpl_map,
    fbSession_t                *session)
{
    sk_fixrec_template_map_entry_t *e;
    const unsigned ext_int = 1;
    GError *gerr = NULL;
    void *cur_tmpl;
    uint16_t tid;
    int found;
    size_t i;

    assert(tmpl_map);
    assert(session);

    for (i = 0;
         (e = ((sk_fixrec_template_map_entry_t*)
               sk_vector_get_value_pointer(tmpl_map, i))) != NULL;
         ++i)
    {
        if (FB_TID_AUTO != e->tid) {
            cur_tmpl = fbSessionGetTemplate(session, ext_int, e->tid, NULL);
            if (e->tmpl != cur_tmpl) {
                TRACEMSG(4, (("%s:%d:Adding %s template %p 0x%04x"
                              " to session %p (replacing %p)"),
                             __FILE__, __LINE__,
                             ((0 == ext_int) ? "external" : "internal"),
                             (void*)e->tmpl, e->tid, session, cur_tmpl));
                if (!fbSessionAddTemplate(
                        session, ext_int, e->tid, e->tmpl, &gerr))
                {
                    TRACEMSG(2, (("Unable to add template %p 0x%04x"
                                  " to session %p: %s"),
                                 (void*)e->tmpl, e->tid, (void*)session,
                                 gerr->message));
                    g_clear_error(&gerr);
                    return SK_SCHEMA_ERR_FIXBUF;
                }
            }
        } else {
            /* FIXME: Change this so we do not search over all IDs
             * every time */
            found = 0;
            tid = 257;
            do {
                cur_tmpl = fbSessionGetTemplate(session, ext_int, tid, NULL);
                if (e->tmpl == cur_tmpl) {
                    found = 1;
                    e->tid = tid;
                }
            } while (!found && tid++ < UINT16_MAX);
            if (!found) {
                TRACEMSG(4,("%s:%d:Adding %s template %p 0x%04x to session %p",
                            __FILE__, __LINE__,
                            ((0 == ext_int) ? "external" : "internal"),
                            (void*)e->tmpl, e->tid, session));
                e->tid = (fbSessionAddTemplate(
                              session, ext_int, e->tid, e->tmpl, &gerr));
                if (!e->tid) {
                    TRACEMSG(2, (("Unable to add template %p 0x%04x"
                                  " to session %p: %s"),
                                 (void*)e->tmpl, e->tid, (void*)session,
                                 gerr->message));
                    g_clear_error(&gerr);
                    return SK_SCHEMA_ERR_FIXBUF;
                }
            }
        }
    }
    return SK_SCHEMA_ERR_SUCCESS;
}

#if 0
static int
sk_fixrec_template_map_entry_cmp(
    const void         *v_record_template_map_entry_a,
    const void         *v_record_template_map_entry_b,
    const void  UNUSED(*v_ctx_data))
{
    const sk_fixrec_template_map_entry_t *a;
    const sk_fixrec_template_map_entry_t *b;

    a = (const sk_fixrec_template_map_entry_t *)v_record_template_map_entry_a;
    b = (const sk_fixrec_template_map_entry_t *)v_record_template_map_entry_b;
    return ((a->tmpl < b->tmpl) ? -1 : (a->tmpl > b->tmpl));
}
#endif  /* 0 */


/*
 *  ******************************************************************
 *  Schema Structured Data (List) (sk_fixlist_t) Support
 *  ******************************************************************
 */


/*  *** BASIC LIST SUPPORT ***  */

/*
 *    Add to the template map 'tmpl_map' the templates used by any
 *    sub-lists contained by the fbBasicList_t structure at 'src_pos'.
 *
 *    Make no assumption about the alignment of 'src_pos'.
 */
static void
sk_fixrec_template_map_add_basic(
    sk_fixrec_template_map_t   *tmpl_map,
    const void                 *src_pos)
{
    fbBasicList_t src;
    void *src_elem = NULL;

    assert(tmpl_map);
    assert(src_pos);
    memcpy(&src, src_pos, sizeof(fbBasicList_t));

    if (0 == src.numElements) {
        return;
    }

    /* add templates used by each element in the list */
    switch (src.infoElement->type) {
      case FB_BASIC_LIST:
        while ((src_elem = fbBasicListGetNextPtr(&src, src_elem))) {
            sk_fixrec_template_map_add_basic(tmpl_map, src_elem);
        }
        break;
      case FB_SUB_TMPL_LIST:
        while ((src_elem = fbBasicListGetNextPtr(&src, src_elem))) {
            sk_fixrec_template_map_add_subtemplate(tmpl_map, src_elem);
        }
        break;
      case FB_SUB_TMPL_MULTI_LIST:
        while ((src_elem = fbBasicListGetNextPtr(&src, src_elem))) {
            sk_fixrec_template_map_add_subtemplatemulti(tmpl_map, src_elem);
        }
        break;
      default:
        break;
    }
}


/*
 *    Add to 'session' the templates used by any sub-lists contained
 *    by the fbBasicList_t structure at 'src_pos'.
 *
 *    Make no assumption about the alignment of 'src_pos'.
 */
static void
sk_fixrec_update_session_basic(
    fbSession_t        *session,
    const void         *src_pos,
    unsigned int        ext_int)
{
    fbBasicList_t src;
    void *src_elem = NULL;

    assert(session);
    assert(src_pos);
    memcpy(&src, src_pos, sizeof(fbBasicList_t));

    if (0 == src.numElements) {
        return;
    }

    /* add templates used by each element in the list */
    switch (src.infoElement->type) {
      case FB_BASIC_LIST:
        while ((src_elem = fbBasicListGetNextPtr(&src, src_elem))) {
            sk_fixrec_update_session_basic(session, src_elem, ext_int);
        }
        break;
      case FB_SUB_TMPL_LIST:
        while ((src_elem = fbBasicListGetNextPtr(&src, src_elem))) {
            sk_fixrec_update_session_subtemplate(session, src_elem, ext_int);
        }
        break;
      case FB_SUB_TMPL_MULTI_LIST:
        while ((src_elem = fbBasicListGetNextPtr(&src, src_elem))) {
            sk_fixrec_update_session_subtemplatemulti(
                session, src_elem, ext_int);
        }
        break;
      default:
        break;
    }
}

/*
 *    Copy the fbBasicList_t structure at 'src_pos' to the
 *    fbBasicList_t structure at 'dest_pos'.
 *
 *    Deep copy the contents of the basicList.
 *
 *    The template map 'tmpl_map' is expected to contain all the
 *    templates used by sublists of the basicList since those lists
 *    use 'tmpl_map' to get the template IDs.
 *
 *    'src_pos' and 'dest_pos' must point to different locations.
 *
 *    Make no assumptions about the alignment of 'src_pos' and
 *    'dest_pos'.
 */
static void
sk_fixrec_copy_list_basic(
    void                       *dest_pos,
    const void                 *src_pos,
    sk_fixrec_template_map_t   *tmpl_map)
{
    fbBasicList_t dest;
    fbBasicList_t src;
    void *bl_data;
    void *src_elem = NULL;
    void *dest_elem = NULL;

    assert(src_pos);
    assert(dest_pos);
    assert(dest_pos != src_pos);
    assert(tmpl_map);

    memset(&dest, 0, sizeof(fbBasicList_t));
    memcpy(&src, src_pos, sizeof(fbBasicList_t));
    bl_data = fbBasicListInit(&dest, src.semantic,
                              src.infoElement, src.numElements);
    assert(fbBasicListGetSemantic(&src) == dest.semantic);
    assert(fbBasicListGetInfoElement(&src) == dest.infoElement);
    assert(src.numElements == dest.numElements);
    TRACEMSG(4, ("%s:%d:Allocated basicList %u %u-byte elements %p",
                 __FILE__, __LINE__, dest.numElements,
                 SK_FB_ELEM_SIZE_BL(dest), (void*)dest.dataPtr));

    /* copy each element in the list */
    switch (dest.infoElement->type) {
      case FB_BASIC_LIST:
        while ((src_elem = fbBasicListGetNextPtr(&src, src_elem))) {
            dest_elem = fbBasicListGetNextPtr(&dest, dest_elem);
            assert(dest_elem);
            assert((uint8_t*)dest_elem < (uint8_t*)bl_data + dest.dataLength);
            sk_fixrec_copy_list_basic(dest_elem, src_elem, tmpl_map);
        }
        break;
      case FB_SUB_TMPL_LIST:
        while ((src_elem = fbBasicListGetNextPtr(&src, src_elem))) {
            dest_elem = fbBasicListGetNextPtr(&dest, dest_elem);
            assert(dest_elem);
            assert((uint8_t*)dest_elem < (uint8_t*)bl_data + dest.dataLength);
            sk_fixrec_copy_list_subtemplate(dest_elem, src_elem, tmpl_map);
        }
        break;
      case FB_SUB_TMPL_MULTI_LIST:
        while ((src_elem = fbBasicListGetNextPtr(&src, src_elem))) {
            dest_elem = fbBasicListGetNextPtr(&dest, dest_elem);
            assert(dest_elem);
            assert((uint8_t*)dest_elem < (uint8_t*)bl_data + dest.dataLength);
            sk_fixrec_copy_list_subtemplatemulti(
                dest_elem, src_elem, tmpl_map);
        }
        break;
      case FB_STRING:
      case FB_OCTET_ARRAY:
        if (FB_IE_VARLEN == dest.infoElement->len) {
            while ((src_elem = fbBasicListGetNextPtr(&src, src_elem))) {
                dest_elem = fbBasicListGetNextPtr(&dest, dest_elem);
                assert(dest_elem);
                assert((uint8_t*)dest_elem
                       < (uint8_t*)bl_data + dest.dataLength);
                sk_fixrec_copy_varfield(dest_elem, src_elem);
            }
            break;
        }
        /* FALLTHROUGH */
      default:
        assert(dest.infoElement->len != FB_IE_VARLEN);
        assert(dest.infoElement->len * dest.numElements >= dest.dataLength);
        memcpy(bl_data, src.dataPtr, dest.numElements * dest.infoElement->len);
        break;
    }

    memcpy(dest_pos, &dest, sizeof(fbBasicList_t));
}

/*
 *    Free the data for the fbBasicList_t structure at 'src_pos'.
 *    Does nothing if the infoElement member of the fbBasicList_t is
 *    NULL.
 */
static void
sk_fixrec_free_list_basic(
    void               *src_pos)
{
    fbBasicList_t src;
    void *src_elem = NULL;

    assert(src_pos);

    memcpy(&src, src_pos, sizeof(fbBasicList_t));
    if (NULL == src.infoElement) {
        return;
    }
    TRACEMSG(4, ("%s:%d:Freeing basicList %u %u-byte elements %p",
                 __FILE__, __LINE__, src.numElements,
                 SK_FB_ELEM_SIZE_BL(src), (void*)src.dataPtr));

    /* free the elements in the list */
    switch (src.infoElement->type) {
      case FB_BASIC_LIST:
        while ((src_elem = fbBasicListGetNextPtr(&src, src_elem))) {
            sk_fixrec_free_list_basic(src_elem);
        }
        break;
      case FB_SUB_TMPL_LIST:
        while ((src_elem = fbBasicListGetNextPtr(&src, src_elem))) {
            sk_fixrec_free_list_subtemplate(src_elem);
        }
        break;
      case FB_SUB_TMPL_MULTI_LIST:
        while ((src_elem = fbBasicListGetNextPtr(&src, src_elem))) {
            sk_fixrec_free_list_subtemplatemulti(src_elem);
        }
        break;
      case FB_STRING:
      case FB_OCTET_ARRAY:
        if (FB_IE_VARLEN == src.infoElement->len) {
            while ((src_elem
                    = (uint8_t*)fbBasicListGetNextPtr(&src, src_elem)))
            {
                sk_fixrec_free_varfield(src_elem);
            }
        }
        break;
    }
    fbBasicListClear(&src);
}

/*
 *    Fill 'rec' with the element on the basicList 'list' at position
 *    'idx'.
 *
 *    Helper function for sk_fixlist_get_element() and
 *    sk_fixlist_next_element().
 */
static sk_schema_err_t
sk_fixlist_get_element_basic(
    sk_fixlist_t       *list,
    uint16_t            idx,
    const sk_fixrec_t **rec)
{
    void *data;

    assert(FB_BASIC_LIST == list->type);
    if (idx < list->t.bl.fb_list.numElements
        && (data = fbBasicListGetIndexedDataPtr(&list->t.bl.fb_list, idx)))
    {
        sk_fixrec_set_data(&list->t.bl.element, data);
        *rec = &list->t.bl.element;
        return SK_SCHEMA_ERR_SUCCESS;
    }
    return SK_SCHEMA_ERR_FIELD_NOT_FOUND;
}

/*
 *    Append 'rec' or a field of 'rec' to the basicList 'list'.
 *
 *    If 'field' is specified, append that field from 'rec' to 'list'.
 *    If 'field' is NULL, then the schema of 'rec' must contain only
 *    one field and that field is added to 'list'.
 *
 *    This is a helper function for sk_fixlist_append_fixrec() and
 *    sk_fixlist_append_element().
 */
static sk_schema_err_t
sk_fixlist_append_to_basic(
    sk_fixlist_t       *list,
    const sk_fixrec_t  *rec,
    const sk_field_t   *field)
{
#if TRACEMSG_LEVEL >= 4
    const void *old_dataPtr = list->t.bl.fb_list.dataPtr;
#endif
    sk_fixrec_template_map_t *tmpl_map;
    const fbInfoElement_t *list_ie;
    const fbInfoElement_t *rec_ie;
    void *p;

    assert(list);
    assert(FB_BASIC_LIST == list->type);
    assert(0 == list->fixbuf_owns_vardata);
    assert(NULL == list->containing_rec);
    assert(list->session);

    list_ie = list->t.bl.ie;

    if (NULL == field) {
        /* when no field, the schemas must match */
        if (!sk_schema_matches_schema(rec->schema, list->t.bl.schema, NULL)) {
            return SK_SCHEMA_ERR_INCOMPATIBLE;
        }
        field = sk_schema_get_field(rec->schema, 0);
        rec_ie = sk_field_get_ie(field);
    } else {
        /* verify that the IEs are identical */
        /* FIXME: Do we want to allow different lengths? */
        rec_ie = sk_field_get_ie(field);
        if (list_ie->num != rec_ie->num
            || list_ie->ent != rec_ie->ent
            || list_ie->len != rec_ie->len
            || list_ie->ref.name != rec_ie->ref.name)
        {
            return SK_SCHEMA_ERR_BAD_TYPE;
        }
    }

    tmpl_map = sk_fixrec_template_map_create(NULL);
    sk_fixrec_template_map_add_basic(tmpl_map, &list->t.bl.fb_list);

    /* grow the basic list by one element */
    p = fbBasicListAddNewElements(&list->t.bl.fb_list, 1);
    TRACEMSG(
        4, ("%s:%d:Appended basicList %u %u-byte elements (old %p) %p",
            __FILE__, __LINE__, list->t.bl.fb_list.numElements,
            SK_FB_ELEM_SIZE_BL(list->t.bl.fb_list),
            (void*)old_dataPtr, (void*)list->t.bl.fb_list.dataPtr));

    switch (sk_field_get_type(field)) {
      case FB_BASIC_LIST:
        sk_fixrec_template_map_add_basic(
            tmpl_map, rec->data + field->offset);
        sk_fixrec_template_map_update_session(tmpl_map, list->session);
        sk_fixrec_copy_list_basic(
            p, rec->data + field->offset, tmpl_map);
        break;
      case FB_SUB_TMPL_LIST:
        sk_fixrec_template_map_add_subtemplate(
            tmpl_map, rec->data + field->offset);
        sk_fixrec_template_map_update_session(tmpl_map, list->session);
        sk_fixrec_copy_list_subtemplate(
            p, rec->data + field->offset, tmpl_map);
        break;
      case FB_SUB_TMPL_MULTI_LIST:
        sk_fixrec_template_map_add_subtemplatemulti(
            tmpl_map, rec->data + field->offset);
        sk_fixrec_template_map_update_session(tmpl_map, list->session);
        sk_fixrec_copy_list_subtemplatemulti(
            p, rec->data + field->offset, tmpl_map);
        break;
      case FB_OCTET_ARRAY:
      case FB_STRING:
        if (FB_IE_VARLEN == list->t.bl.field->len) {
            sk_fixrec_copy_varfield(p, rec->data + field->offset);
            break;
        }
        /* FALLTHROUGH */
      default:
        assert(list->t.bl.field->len == list->t.bl.item_len);
        memcpy(p, rec->data + field->offset, list->t.bl.item_len);
        break;
    }

    sk_fixrec_template_map_destroy(tmpl_map);

    return SK_SCHEMA_ERR_SUCCESS;
}

/*
 *    Create an sk_fixlist_t that holds an fbBasicList_t and store the
 *    list in the location referenced by 'out_list'.
 *
 *    Only one of 'ie' or 'existing_list' should be specified.
 *
 *    If 'ie' is non-NULL, a new fbBasicList_t is initialized to
 *    contain the information element 'ie'.  The list is initialized
 *    to hold zero elements.
 *
 *    If 'existing_list' is non-NULL, the function assumes data is
 *    being read from a file by fixbuf and the sk_fixlist_t is a
 *    wrapper over that fbBasicList_t and the information elements it
 *    contains.
 *
 *    'model' is the information model used when creating the "fake"
 *    schema for the elements of the basicList.
 *
 *    This is a helper function used by
 *    sk_fixlist_create_basiclist_from_ident() and
 *    sk_fixlist_create_basiclist_from_name(), and which may be used
 *    by sk_fixrec_create_list_iter() and sk_fixrec_get_list_schema()
 *    when the field is a basicList.
 */
static sk_schema_err_t
sk_fixlist_create_basic(
    sk_fixlist_t          **out_list,
    fbInfoModel_t          *model,
    const fbInfoElement_t  *ie,
    fbBasicList_t          *existing_list)
{
    fbInfoElementSpec_t spec[2] = { FB_IESPEC_NULL, FB_IESPEC_NULL };
    sk_schema_t *schema = NULL;
    sk_fixlist_t *list;
    sk_schema_err_t err;

    assert(out_list);
    assert(model);
    assert(ie || existing_list);
    assert(NULL == ie || NULL == existing_list);

    list = sk_alloc(sk_fixlist_t);
    list->type = FB_BASIC_LIST;

    if (!existing_list) {
        /* create an empty list */
        fbBasicListInit(&list->t.bl.fb_list, FB_LIST_SEM_UNDEFINED, ie, 0);
        TRACEMSG(4, ("%s:%d:Allocated empty basicList %p",
                     __FILE__, __LINE__, (void*)list->t.bl.fb_list.dataPtr));

        /* create a session to store templates used by this list or
         * its sublists (FIXME: Only necessary when this list contains
         * another list element) */
        list->session = fbSessionAlloc(model);
    } else {
        TRACEMSG(4, ("%s:%d:Handle to basicList %u %u-byte elements %p",
                     __FILE__, __LINE__, existing_list->numElements,
                     SK_FB_ELEM_SIZE_BL(*existing_list),
                     (void*)existing_list->dataPtr));

        memcpy(&list->t.bl.fb_list, existing_list, sizeof(fbBasicList_t));
        list->fixbuf_owns_vardata = 1;
        ie = fbBasicListGetInfoElement(existing_list);
    }
    list->t.bl.ie = ie;
    list->t.bl.item_len = IE_GET_DATALEN(ie);

    /* create the fake schema */
    spec[0].name = (char*)ie->ref.name;
    spec[0].len_override = ie->len;
    if ((err = sk_schema_create(&schema, model, spec, 0))
        || (err = sk_schema_set_tid(schema, BASICLIST_FAKE_SCHEMA_TID))
        || (err = sk_schema_freeze(schema)))
    {
        free(schema);
        free(list);
        return err;
    }
    list->t.bl.schema = schema;
    list->t.bl.field = sk_schema_get_field(schema, 0);
    sk_fixrec_init(&list->t.bl.element, schema);

    *out_list = list;
    return SK_SCHEMA_ERR_SUCCESS;
}

/*
 *    Free the memory allocated by sk_fixlist_create_basic().
 *    Specifically, free an sk_fixlist_t that wraps a basicList.
 */
static void
sk_fixlist_destroy_basic(
    sk_fixlist_t       *list)
{
    assert(list);
    assert(FB_BASIC_LIST == list->type);

    sk_fixrec_destroy(&list->t.bl.element);
    sk_schema_destroy(list->t.bl.schema);
    if (list->session) {
        /* list was created for writing */
        sk_fixrec_free_list_basic(&list->t.bl.fb_list);
        fbSessionFree(list->session);
    }
    memset(list, 0, sizeof(sk_fixlist_t));
    free(list);
}


/*  *** SUB TEMPLATE LIST SUPPORT ***  */

/*
 *    Add to the template map 'tmpl_map' the templates used by the
 *    fbSubTemplateList_t structure at 'src_pos' and by any sub-lists
 *    it contains recursively.
 *
 *    Make no assumption about the alignment of 'src_pos'.
 */
static void
sk_fixrec_template_map_add_subtemplate(
    sk_fixrec_template_map_t   *tmpl_map,
    const void                 *src_pos)
{
    fbSubTemplateList_t src;
    sk_schema_t *schema = NULL;
    void *src_elem = NULL;
    fbTemplate_t *tmpl;
    fbInfoElement_t *ie;
    sk_fixrec_t src_rec;
    sk_schema_err_t err;
    uint8_t *rec_data;
    int visit_recs;
    uint16_t tid;
    uint32_t i;

    assert(tmpl_map);
    assert(src_pos);
    memcpy(&src, src_pos, sizeof(fbSubTemplateList_t));

    /* add the STL's template to the template map */
    tmpl = (fbTemplate_t*)fbSubTemplateListGetTemplate(&src);
    tid = fbSubTemplateListGetTemplateID(&src);

    sk_fixrec_template_map_insert(tmpl_map, tmpl, tid);

    visit_recs = 0;
    if (src.numElements) {
        /* if the STL's template contains list elements, we need to visit
         * each record in the list */
        for (i = 0; (ie = fbTemplateGetIndexedIE(tmpl, i)) != NULL; ++i) {
            if (TYPE_IS_LIST(ie->type)) {
                visit_recs = 1;
                break;
            }
        }
    }
    if (visit_recs) {
        /* create a record for the list */
        if ((err = sk_schema_wrap_template(&schema, NULL, tmpl, tid))
            || (err = sk_fixrec_init(&src_rec, schema)))
        {
            TRACEMSG(2, ("Unable to create schema or record: %s",
                         sk_schema_strerror(err)));
            sk_schema_destroy(schema);
            return;
        }
        /* stash the record's current data */
        rec_data = src_rec.data;

        /* set record's data pointer to the element and recurse  */
        while ((src_elem = fbSubTemplateListGetNextPtr(&src, src_elem))) {
            /* manually set the data pointer */
            src_rec.data = (uint8_t*)src_elem;
            sk_fixrec_template_map_add_record(tmpl_map, &src_rec);
        }

        /* restore record's data and destroy the record */
        src_rec.data = rec_data;
        sk_fixrec_destroy(&src_rec);
        sk_schema_destroy(schema);
    }
}

/*
 *    Add to 'session' the templates used by the fbSubTemplateList_t
 *    structure at 'src_pos' and by any sub-lists it contains
 *    recursively.
 *
 *    Make no assumption about the alignment of 'src_pos'.
 */
static void
sk_fixrec_update_session_subtemplate(
    fbSession_t        *session,
    const void         *src_pos,
    unsigned int        ext_int)
{
    fbSubTemplateList_t src;
    sk_schema_t *schema = NULL;
    void *src_elem = NULL;
    fbTemplate_t *cur_tmpl;
    fbTemplate_t *tmpl;
    GError *gerr = NULL;
    fbInfoElement_t *ie;
    sk_fixrec_t src_rec;
    sk_schema_err_t err;
    uint8_t *rec_data;
    int visit_recs;
    uint16_t tid;
    uint32_t i;

    assert(session);
    assert(src_pos);
    memcpy(&src, src_pos, sizeof(fbSubTemplateList_t));

    /* add the STL's template to the session */
    tmpl = (fbTemplate_t*)fbSubTemplateListGetTemplate(&src);
    tid = fbSubTemplateListGetTemplateID(&src);
    cur_tmpl = fbSessionGetTemplate(session, ext_int, tid, NULL);
    if (tmpl != cur_tmpl) {
        TRACEMSG(4, (("%s:%d:Adding %s template %p 0x%04x to session %p"
                      " (replacing %p)"),
                     __FILE__, __LINE__,
                     ((0 == ext_int) ? "external" : "internal"),
                     tmpl, tid, session, cur_tmpl));
        if (!fbSessionAddTemplate(session, ext_int, tid, tmpl, &gerr)) {
            TRACEMSG(2, ("Unable to add template %p 0x%04x"
                         " to session %p: %s",
                         (void*)tmpl, tid, (void*)session, gerr->message));
            g_clear_error(&gerr);
            return;
        }
     }

    visit_recs = 0;
    if (src.numElements) {
        /* if the STL's template contains list elements, we need to visit
         * each record in the list */
        for (i = 0; (ie = fbTemplateGetIndexedIE(tmpl, i)) != NULL; ++i) {
            if (TYPE_IS_LIST(ie->type)) {
                visit_recs = 1;
                break;
            }
        }
    }
    if (visit_recs) {
        /* create a record for the list */
        if ((err = sk_schema_wrap_template(&schema, NULL, tmpl, tid))
            || (err = sk_fixrec_init(&src_rec, schema)))
        {
            TRACEMSG(2, ("Unable to create schema or record: %s",
                         sk_schema_strerror(err)));
            sk_schema_destroy(schema);
            return;
        }
        /* stash the record's current data */
        rec_data = src_rec.data;

        /* set record's data pointer to the element and recurse  */
        while ((src_elem = fbSubTemplateListGetNextPtr(&src, src_elem))) {
            /* manually set the data pointer */
            src_rec.data = (uint8_t*)src_elem;
            sk_fixrec_update_session(session, &src_rec, ext_int);
        }

        /* restore record's data and destroy the record */
        src_rec.data = rec_data;
        sk_fixrec_destroy(&src_rec);
        sk_schema_destroy(schema);
    }
}

/*
 *    Copy the fbSubTemplateList_t structure at 'src_pos' to the
 *    fbSubTemplateList_t structure at 'dest_pos'.
 *
 *    Deep copy the contents of the subTemplateList.
 *
 *    The template map 'tmpl_map' is expected to contain all the
 *    templates used by the subTemplateList and any of its sublists
 *    since those lists use 'tmpl_map' to get the template IDs.
 *
 *    'src_pos' and 'dest_pos' must point to different locations.
 *
 *    Make no assumptions about the alignment of 'src_pos' and
 *    'dest_pos'.
 */
static void
sk_fixrec_copy_list_subtemplate(
    void                       *dest_pos,
    const void                 *src_pos,
    sk_fixrec_template_map_t   *tmpl_map)
{
#ifndef NDEBUG
    uint8_t *stl_data;
#endif
    fbSubTemplateList_t dest;
    fbSubTemplateList_t src;
    void *src_elem = NULL;
    void *dest_elem = NULL;
    sk_schema_t *schema = NULL;
    fbTemplate_t *tmpl;
    fbInfoElement_t *ie;
    sk_fixrec_t src_rec;
    sk_schema_err_t err;
    uint8_t *rec_data;
    int visit_recs;
    uint16_t tid;
    uint32_t i;

    assert(src_pos);
    assert(dest_pos);
    assert(dest_pos != src_pos);
    assert(tmpl_map);

    memset(&dest, 0, sizeof(fbSubTemplateList_t));
    memcpy(&src, src_pos, sizeof(fbSubTemplateList_t));

    tmpl = (fbTemplate_t*)fbSubTemplateListGetTemplate(&src);
    if (!sk_fixrec_template_map_find(tmpl_map, tmpl, &tid)) {
        return;
    }
    assert(0 != tid);
#ifndef NDEBUG
    stl_data = (uint8_t*)
#endif
        fbSubTemplateListInit(&dest, src.semantic, tid, tmpl, src.numElements);
    assert(stl_data || 0 == src.numElements);
    assert(fbSubTemplateListGetSemantic(&src) == dest.semantic);
    assert(fbSubTemplateListGetTemplate(&src) == dest.tmpl);
    /* assert(fbSubTemplateListGetTemplateID(&src) == dest.tmplID); */
    assert(src.numElements == dest.numElements);
    assert(src.dataLength.length == dest.dataLength.length);
    TRACEMSG(4, ("%s:%d:Allocated subTemplateList %u %u-byte elements %p",
                 __FILE__, __LINE__, dest.numElements,
                 SK_FB_ELEM_SIZE_STL(dest), (void*)dest.dataPtr));

    visit_recs = 0;
    if (dest.numElements) {
        /* if the STL's template does not contain list elements or
         * varlen elements, copying the data simply involves copying
         * the bytes in the 'dataPtr'.  When list or vardata elements
         * are present, process each record in the STL
         * individually. */
        for (i = 0; (ie = fbTemplateGetIndexedIE(tmpl, i)) != NULL; ++i) {
            if ((FB_IE_VARLEN == ie->len) || TYPE_IS_LIST(ie->type)) {
                visit_recs = 1;
                break;
            }
        }
    }
    if (!visit_recs) {
        memcpy(dest.dataPtr, src.dataPtr, dest.dataLength.length);
     } else {
        /* create a record for this list */
        if ((err = sk_schema_wrap_template(&schema, NULL, tmpl, tid))
            || (err = sk_fixrec_init(&src_rec, schema)))
        {
            TRACEMSG(2, ("Unable to create schema or record: %s",
                         sk_schema_strerror(err)));
            sk_schema_destroy(schema);
            return;
        }
        /* stash the record's current data */
        rec_data = src_rec.data;

        /* set record's data pointer to the element and copy it  */
        while ((src_elem = fbSubTemplateListGetNextPtr(&src, src_elem))) {
            dest_elem = fbSubTemplateListGetNextPtr(&dest, dest_elem);
            assert(dest_elem);
            assert((uint8_t*)dest_elem < stl_data + dest.dataLength.length);
            /* manually set the data pointer */
            src_rec.data = (uint8_t*)src_elem;
            sk_fixrec_copy_data(dest_elem, &src_rec, tmpl_map);
        }

        /* restore record's data and destroy the record */
        src_rec.data = rec_data;
        sk_fixrec_destroy(&src_rec);
        sk_schema_destroy(schema);
    }

    memcpy(dest_pos, &dest, sizeof(fbSubTemplateList_t));
}

/*
 *    Free the data for the fbSubTemplateList_t structure at
 *    'src_pos'.  Does nothing if the tmpl member of the
 *    fbSubTemplateList_t is NULL.
 */
static void
sk_fixrec_free_list_subtemplate(
    void               *src_pos)
{
    sk_schema_t *schema = NULL;
    fbSubTemplateList_t src;
    void *src_elem = NULL;
    fbInfoElement_t *ie;
    sk_fixrec_t src_rec;
    sk_schema_err_t err;
    fbTemplate_t *tmpl;
    uint8_t *rec_data;
    int visit_recs;
    uint16_t tid;
    uint32_t i;

    assert(src_pos);

    memcpy(&src, src_pos, sizeof(fbSubTemplateList_t));
    if (NULL == src.tmpl) {
        return;
    }
    TRACEMSG(4, ("%s:%d:Freeing subTemplateList %u %u-byte elements %p",
                 __FILE__, __LINE__, src.numElements,
                 SK_FB_ELEM_SIZE_STL(src), (void*)src.dataPtr));

    tmpl = (fbTemplate_t*)fbSubTemplateListGetTemplate(&src);
    tid = fbSubTemplateListGetTemplateID(&src);

    visit_recs = 0;
    if (src.numElements) {
        /* if the STL's template does not contain list elements or
         * varlen elements, there is nothing else to do.  When list or
         * vardata elements are present, process each record in the
         * STL individually. */
        for (i = 0; (ie = fbTemplateGetIndexedIE(tmpl, i)) != NULL; ++i) {
            if ((FB_IE_VARLEN == ie->len) || TYPE_IS_LIST(ie->type)) {
                visit_recs = 1;
                break;
            }
        }
    }
    if (visit_recs) {
        /* create a record for the list */
        if ((err = sk_schema_wrap_template(&schema, NULL, tmpl, tid))
            || (err = sk_fixrec_init(&src_rec, schema)))
        {
            TRACEMSG(2, ("Unable to create schema or record: %s",
                         sk_schema_strerror(err)));
            sk_schema_destroy(schema);
            return;
        }
        /* stash the record's current data */
        rec_data = src_rec.data;

        /* set record's data pointer to the element and clear it  */
        while ((src_elem = fbSubTemplateListGetNextPtr(&src, src_elem))) {
            /* manually set the data pointer */
            src_rec.data = (uint8_t*)src_elem;
            sk_fixrec_clear(&src_rec);
        }

        /* restore record's data and destroy the record */
        src_rec.data = rec_data;
        sk_fixrec_destroy(&src_rec);
        sk_schema_destroy(schema);
    }
    fbSubTemplateListClear(&src);
}

/*
 *    Fill 'rec' with the element on the subTemplateList 'list' at
 *    position 'idx'.
 *
 *    Helper function for sk_fixlist_get_element() and
 *    sk_fixlist_next_element.
 */
static sk_schema_err_t
sk_fixlist_get_element_subtemplate(
    sk_fixlist_t       *list,
    uint16_t            idx,
    const sk_fixrec_t **rec)
{
    void *data;

    assert(FB_SUB_TMPL_LIST == list->type);
    if (idx < list->t.stl.fb_list.numElements
        && (data = fbSubTemplateListGetIndexedDataPtr(&list->t.stl.fb_list,
                                                      idx)))
    {
        sk_fixrec_set_data(&list->t.stl.element, data);
        *rec = &list->t.stl.element;
        return SK_SCHEMA_ERR_SUCCESS;
    }
    return SK_SCHEMA_ERR_FIELD_NOT_FOUND;
}

/*
 *    Append the record 'rec' to the subTemplateList 'list'.
 */
static sk_schema_err_t
sk_fixlist_append_to_subtemplate(
    sk_fixlist_t       *list,
    const sk_fixrec_t  *rec)
{
#if TRACEMSG_LEVEL >= 4
    const void *old_dataPtr = list->t.stl.fb_list.dataPtr;
#endif
    sk_fixrec_template_map_t *tmpl_map;
    void *p;

    assert(list);
    assert(FB_SUB_TMPL_LIST == list->type);
    assert(0 == list->fixbuf_owns_vardata);
    assert(NULL == list->containing_rec);
    assert(list->session);

    /* get templates used by the STL and by any sublists in the record
     * being added */
    tmpl_map = sk_fixrec_template_map_create(NULL);
    sk_fixrec_template_map_add_subtemplate(tmpl_map, &list->t.stl.fb_list);
    sk_fixrec_template_map_add_record(tmpl_map, rec);
    sk_fixrec_template_map_update_session(tmpl_map, list->session);

    /* grow the list by one element */
    p = fbSubTemplateListAddNewElements(&list->t.stl.fb_list, 1);
    TRACEMSG(
        4, ("%s:%d:Appended subTemplateList %u %u-byte elements (old %p) %p",
            __FILE__, __LINE__, list->t.stl.fb_list.numElements,
            SK_FB_ELEM_SIZE_STL(list->t.stl.fb_list),
            (void*)old_dataPtr, (void*)list->t.stl.fb_list.dataPtr));

    /* deep copy the record's data */
    assert((size_t)(rec->schema->len * list->t.stl.fb_list.numElements)
           <= list->t.stl.fb_list.dataLength.length);
    sk_fixrec_copy_data(p, rec, tmpl_map);

    sk_fixrec_template_map_destroy(tmpl_map);

    return SK_SCHEMA_ERR_SUCCESS;
}

/*
 *    Create an sk_fixlist_t that holds an fbSubTemplateList_t and
 *    store the list in the location referenced by 'out_list'.
 *
 *    Only one of 'schema' or 'existing_list' should be specified.
 *
 *    If 'schema' is non-NULL, a new fbSubTemplateList_t is
 *    initialized to contain the template of 'schema'.  The list is
 *    initialized to hold zero elements.
 *
 *    If 'existing_list' is non-NULL, the function assumes data is
 *    being read from a file by fixbuf and the sk_fixlist_t is a
 *    wrapper over that fbSubTemplateList_t and the templates it
 *    contains.
 *
 *    'model' is the information model used when creating the "fake"
 *    schema for the elements of the basicList.
 *
 *    This is a helper function used by sk_fixrec_create_list_iter()
 *    and sk_fixlist_create_subtemplatelist().
 */
static sk_schema_err_t
sk_fixlist_create_subtemplate(
    sk_fixlist_t          **out_list,
    fbInfoModel_t          *model,
    const sk_schema_t      *schema,
    fbSubTemplateList_t    *existing_list)
{
    sk_schema_t *s = NULL;
    sk_schema_err_t err;
    GError *gerr = NULL;
    fbTemplate_t *tmpl;
    sk_fixlist_t *list;
    uint16_t tid;

    assert(out_list);
    assert(model);
    assert(schema || existing_list);
    assert(NULL == schema || NULL == existing_list);

    list = sk_alloc(sk_fixlist_t);
    list->type = FB_SUB_TMPL_LIST;

    if (!existing_list) {
        /* create an empty list */
        list->t.stl.schema = sk_schema_clone(schema);

        sk_schema_get_template(schema, (fbTemplate_t**)&tmpl, &tid);
        fbSubTemplateListInit(&list->t.stl.fb_list, FB_LIST_SEM_UNDEFINED,
                              tid, tmpl, 0);
        TRACEMSG(4, ("%s:%d:Allocated empty subTemplateList %p",
                     __FILE__, __LINE__, (void*)list->t.stl.fb_list.dataPtr));

        /* create a session to store templates used by this list or
         * its sublists */
        list->session = fbSessionAlloc(model);
        if (!fbSessionAddTemplate(list->session, 1, tid, tmpl, &gerr)) {
            TRACEMSG(2, ("Unable to add template %p 0x%04x"
                         " to session %p: %s",
                         (void*)tmpl, tid, (void*)list->session,
                         gerr->message));
            g_clear_error(&gerr);
            sk_schema_destroy(schema);
            fbSessionFree(list->session);
            free(list);
            return SK_SCHEMA_ERR_FIXBUF;
        }
    } else {
        TRACEMSG(4, ("%s:%d:Handle to subTemplateList %u %u-byte elements %p",
                     __FILE__, __LINE__, existing_list->numElements,
                     SK_FB_ELEM_SIZE_STL(*existing_list),
                     (void*)existing_list->dataPtr));

        memcpy(&list->t.stl.fb_list, existing_list,
               sizeof(fbSubTemplateList_t));
        list->fixbuf_owns_vardata = 1;

        /* create a new schema from the list's template */
        tmpl = (fbTemplate_t*)fbSubTemplateListGetTemplate(existing_list);
        tid = fbSubTemplateListGetTemplateID(existing_list);
        err = sk_schema_wrap_template(&s, model, tmpl, tid);
        if (err) {
            TRACEMSG(2, ("Unable to create schema: %s",
                         sk_schema_strerror(err)));
            sk_schema_destroy(s);
            free(list);
            return err;
        }
        list->t.stl.schema = schema = s;
    }
    assert(fbSubTemplateListGetTemplateID(&list->t.stl.fb_list) == tid);
    sk_fixrec_init(&list->t.stl.element, schema);

    *out_list = list;
    return SK_SCHEMA_ERR_SUCCESS;
}

/*
 *    Free the memory allocated by sk_fixlist_create_subtemplate().
 *    Specifically, free an sk_fixlist_t that wraps a subTemplateList.
 */
static void
sk_fixlist_destroy_subtemplate(
    sk_fixlist_t       *list)
{
    assert(list);
    assert(FB_SUB_TMPL_LIST == list->type);

    sk_fixrec_destroy(&list->t.stl.element);
    sk_schema_destroy(list->t.stl.schema);

    if (list->session) {
        /* list was created for writing */
        sk_fixrec_free_list_subtemplate(&list->t.stl.fb_list);
        fbSessionFree(list->session);
    }
    memset(list, 0, sizeof(sk_fixlist_t));
    free(list);
}


/*  *** SUB TEMPLATE MULTI LIST SUPPORT ***  */

/*
 *    Add to the template map 'tmpl_map' the templates used by the
 *    entries of the fbSubTemplateMultiList_t structure at 'src_pos'
 *    and any sub-lists they contain recursively.
 *
 *    Make no assumption about the alignment of 'src_pos'.
 */
static void
sk_fixrec_template_map_add_subtemplatemulti(
    sk_fixrec_template_map_t   *tmpl_map,
    const void                 *src_pos)
{
    fbSubTemplateMultiListEntry_t *src_entry = NULL;
    fbSubTemplateMultiList_t src;
    sk_schema_t *schema = NULL;
    fbTemplate_t *tmpl;
    fbInfoElement_t *ie;
    sk_fixrec_t src_rec;
    sk_schema_err_t err;
    uint8_t *rec_data;
    int visit_recs;
    void *src_elem;
    uint16_t tid;
    uint32_t i;

    assert(tmpl_map);
    assert(src_pos);
    memcpy(&src, src_pos, sizeof(fbSubTemplateMultiList_t));

    while ((src_entry = fbSubTemplateMultiListGetNextEntry(&src, src_entry))) {
        tmpl = (fbTemplate_t*)fbSubTemplateMultiListEntryGetTemplate(src_entry);
        tid = fbSubTemplateMultiListEntryGetTemplateID(src_entry);

        sk_fixrec_template_map_insert(tmpl_map, tmpl, tid);

        visit_recs = 0;
        if (src_entry->numElements) {
            /* if the STML entry's template contains list elements, we
             * need to visit each record in the list */
            for (i = 0; (ie = fbTemplateGetIndexedIE(tmpl, i)) != NULL; ++i) {
                if (TYPE_IS_LIST(ie->type)) {
                    visit_recs = 1;
                    break;
                }
            }
        }
        if (visit_recs) {
            /* create a record for this set of entries */
            schema = NULL;
            if ((err = sk_schema_wrap_template(&schema, NULL, tmpl, tid))
                || (err = sk_fixrec_init(&src_rec, schema)))
            {
                TRACEMSG(2, ("Unable to create schema or record: %s",
                             sk_schema_strerror(err)));
                sk_schema_destroy(schema);
                return;
            }
            /* stash the record's current data */
            rec_data = src_rec.data;

            src_elem = NULL;
            while ((src_elem = (fbSubTemplateMultiListEntryNextDataPtr(
                                    src_entry, src_elem))))
            {
                /* manually set the data pointer */
                src_rec.data = (uint8_t*)src_elem;
                sk_fixrec_template_map_add_record(tmpl_map, &src_rec);
            }

            /* restore record's data and destroy the record */
            src_rec.data = rec_data;
            sk_fixrec_destroy(&src_rec);
            sk_schema_destroy(schema);
        }
    }
}

/*
 *    Add to 'session' the templates used by the entries of the
 *    fbSubTemplateMultiList_t at 'src_pos' and any sub-lists they
 *    contain recursively.
 *
 *    Make no assumption about the alignment of 'src_pos'.
 */
static void
sk_fixrec_update_session_subtemplatemulti(
    fbSession_t        *session,
    const void         *src_pos,
    unsigned int        ext_int)
{
    fbSubTemplateMultiListEntry_t *src_entry = NULL;
    fbSubTemplateMultiList_t src;
    sk_schema_t *schema = NULL;
    fbTemplate_t *cur_tmpl;
    fbTemplate_t *tmpl;
    GError *gerr = NULL;
    fbInfoElement_t *ie;
    sk_fixrec_t src_rec;
    sk_schema_err_t err;
    uint8_t *rec_data;
    int visit_recs;
    void *src_elem;
    uint16_t tid;
    uint32_t i;

    assert(session);
    assert(src_pos);
    memcpy(&src, src_pos, sizeof(fbSubTemplateMultiList_t));

    while ((src_entry = fbSubTemplateMultiListGetNextEntry(&src, src_entry))) {
        tmpl = (fbTemplate_t*)fbSubTemplateMultiListEntryGetTemplate(src_entry);
        tid = fbSubTemplateMultiListEntryGetTemplateID(src_entry);

        /* add the STML entry's template to the session */
        cur_tmpl = fbSessionGetTemplate(session, ext_int, tid, NULL);
        if (tmpl != cur_tmpl) {
            TRACEMSG(4, (("%s:%d:Adding %s template %p 0x%04x to session %p"
                          " (replacing %p)"),
                         __FILE__, __LINE__,
                         ((0 == ext_int) ? "external" : "internal"),
                         tmpl, tid, session, cur_tmpl));
            if (!fbSessionAddTemplate(session, ext_int, tid, tmpl, &gerr)){
                TRACEMSG(2, ("Unable to add template %p 0x%04x"
                             " to session %p: %s",
                             (void*)tmpl, tid, (void*)session,
                             gerr->message));
                g_clear_error(&gerr);
                return;
            }
        }

        visit_recs = 0;
        if (src_entry->numElements) {
            /* if the STML entry's template contains list elements, we
             * need to visit each record in the list */
            for (i = 0; (ie = fbTemplateGetIndexedIE(tmpl, i)) != NULL; ++i) {
                if (TYPE_IS_LIST(ie->type)) {
                    visit_recs = 1;
                    break;
                }
            }
        }
        if (visit_recs) {
            /* create a record for this set of entries */
            schema = NULL;
            if ((err = sk_schema_wrap_template(&schema, NULL, tmpl, tid))
                || (err = sk_fixrec_init(&src_rec, schema)))
            {
                TRACEMSG(2, ("Unable to create schema or record: %s",
                             sk_schema_strerror(err)));
                sk_schema_destroy(schema);
                return;
            }
            /* stash the record's current data */
            rec_data = src_rec.data;

            src_elem = NULL;
            while ((src_elem = (fbSubTemplateMultiListEntryNextDataPtr(
                                    src_entry, src_elem))))
            {
                /* manually set the data pointer */
                src_rec.data = (uint8_t*)src_elem;
                sk_fixrec_update_session(session, &src_rec, ext_int);
            }

            /* restore record's data and destroy the record */
            src_rec.data = rec_data;
            sk_fixrec_destroy(&src_rec);
            sk_schema_destroy(schema);
        }
    }
}

/*
 *    Copy the fbSubTemplateListMulti_t structure at 'src_pos' to the
 *    fbSubTemplateListMulti_t structure at 'dest_pos'.
 *
 *    Deep copy the contents of the subTemplateMultiList.
 *
 *    The template map 'tmpl_map' is expected to contain all the
 *    templates used by the elements of subTemplateMultiList and any
 *    of its sublists since those lists use 'tmpl_map' to get the
 *    template IDs.
 *
 *    'src_pos' and 'dest_pos' must point to different locations.
 *
 *    Make no assumptions about the alignment of 'src_pos' and
 *    'dest_pos'.
 */
static void
sk_fixrec_copy_list_subtemplatemulti(
    void                       *dest_pos,
    const void                 *src_pos,
    sk_fixrec_template_map_t   *tmpl_map)
{
#ifndef NDEBUG
    uint8_t *stmle_data;
#endif
    fbSubTemplateMultiListEntry_t *dest_entry = NULL;
    fbSubTemplateMultiListEntry_t *src_entry = NULL;
    fbSubTemplateMultiList_t dest;
    fbSubTemplateMultiList_t src;
    void *src_elem;
    void *dest_elem;
    sk_schema_t *schema = NULL;
    fbInfoElement_t *ie;
    fbTemplate_t *tmpl;
    sk_fixrec_t src_rec;
    sk_schema_err_t err;
    uint8_t *rec_data;
    int visit_recs;
    uint16_t tid;
    uint32_t i;

    assert(src_pos);
    assert(dest_pos);
    assert(dest_pos != src_pos);
    assert(tmpl_map);

    memset(&dest, 0, sizeof(fbSubTemplateMultiList_t));
    memcpy(&src, src_pos, sizeof(fbSubTemplateMultiList_t));

    /* allocate new list */
    fbSubTemplateMultiListInit(&dest, src.semantic, src.numElements);
    assert(fbSubTemplateMultiListGetSemantic(&src) == dest.semantic);
    assert(src.numElements == dest.numElements);
    TRACEMSG(4, ("%s:%d:Allocated subTemplateMultiList %u stmlEntries %p",
                 __FILE__, __LINE__, dest.numElements,(void*)dest.firstEntry));

    /* process each template/schema */
    while ((src_entry = fbSubTemplateMultiListGetNextEntry(&src, src_entry))) {
        tmpl = (fbTemplate_t*)fbSubTemplateMultiListEntryGetTemplate(src_entry);
        if (!sk_fixrec_template_map_find(tmpl_map, tmpl, &tid)) {
            return;
        }
        assert(0 != tid);

        dest_entry = fbSubTemplateMultiListGetNextEntry(&dest, dest_entry);
        assert(dest_entry);

#ifndef NDEBUG
        stmle_data = (uint8_t*)
#endif
            fbSubTemplateMultiListEntryInit(dest_entry, tid, tmpl,
                                            src_entry->numElements);
        assert(stmle_data || 0 == src_entry->numElements);
        assert(fbSubTemplateMultiListEntryGetTemplate(src_entry)
               == dest_entry->tmpl);
        /* assert(fbSubTemplateMultiListEntryGetTemplateID(&src)
         * == dest.tmplID); */
        assert(src_entry->numElements == dest_entry->numElements);
        assert(src_entry->dataLength == dest_entry->dataLength);
        TRACEMSG(4, (("%s:%d:Allocated subTemplateMultiListEntry"
                      " %u %u-byte elements %p"),
                     __FILE__, __LINE__, dest_entry->numElements,
                     SK_FB_ELEM_SIZE_STMLE(*dest_entry),
                     (void*)dest_entry->dataPtr));

        visit_recs = 0;
        if (src_entry->numElements) {
            /* if the STML entry's template does not contain list
             * elements or varlen elements, copying the data simply
             * involves copying the bytes in the 'dataPtr'.  When list
             * or vardata elements are present, process each record in
             * the STML entry individually. */
            for (i = 0; (ie = fbTemplateGetIndexedIE(tmpl, i)) != NULL; ++i) {
                if (FB_IE_VARLEN == ie->len || TYPE_IS_LIST(ie->type)) {
                    visit_recs = 1;
                    break;
                }
            }
        }
        if (!visit_recs) {
            memcpy(dest_entry->dataPtr, src_entry->dataPtr,
                   dest_entry->dataLength);
        } else {
            /* create a record for this set of entries */
            schema = NULL;
            if ((err = sk_schema_wrap_template(&schema, NULL, tmpl, tid))
                || (err = sk_fixrec_init(&src_rec, schema)))
            {
                TRACEMSG(2, ("Unable to create schema or record: %s",
                             sk_schema_strerror(err)));
                sk_schema_destroy(schema);
                return;
            }
            /* stash the record's current data */
            rec_data = src_rec.data;

            /* set record's data pointer to the element and copy it  */
            src_elem = NULL;
            dest_elem = NULL;
            while ((src_elem = (fbSubTemplateMultiListEntryNextDataPtr(
                                    src_entry, src_elem))))
            {
                dest_elem = (fbSubTemplateMultiListEntryNextDataPtr(
                                 dest_entry, dest_elem));
                assert(dest_elem);
                assert((uint8_t*)dest_elem
                       < stmle_data + dest_entry->dataLength);
                /* manually set the data pointer */
                src_rec.data = (uint8_t*)src_elem;
                sk_fixrec_copy_data(dest_elem, &src_rec, tmpl_map);
            }

            /* restore record's data and destroy the record */
            src_rec.data = rec_data;
            sk_fixrec_destroy(&src_rec);
            sk_schema_destroy(schema);
        }
    }

    memcpy(dest_pos, &dest, sizeof(fbSubTemplateMultiList_t));
}

/*
 *    Free the data for the fbSubTemplateListMulti_t structure at
 *    'src_pos'.  Does nothing if the firstEntry member of the
 *    fbSubTemplateMultiList_t is NULL.
 */
static void
sk_fixrec_free_list_subtemplatemulti(
    void               *src_pos)
{
    fbSubTemplateMultiListEntry_t *src_entry = NULL;
    fbSubTemplateMultiList_t src;
    sk_schema_t *schema;
    fbInfoElement_t *ie;
    sk_fixrec_t src_rec;
    sk_schema_err_t err;
    fbTemplate_t *tmpl;
    uint8_t *rec_data;
    void *src_elem;
    int visit_recs;
    uint16_t tid;
    uint32_t i;

    assert(src_pos);
    memcpy(&src, src_pos, sizeof(fbSubTemplateMultiList_t));
    if (NULL == src.firstEntry) {
        return;
    }
    TRACEMSG(4, ("%s:%d:Freeing subTemplateMultiList %u stmlEntries %p",
                 __FILE__, __LINE__, src.numElements, (void*)src.firstEntry));

    while ((src_entry = fbSubTemplateMultiListGetNextEntry(&src, src_entry))) {
        TRACEMSG(4, (("%s:%d:Freeing subTemplateMultiListEntry"
                      " %u %u-byte elements %p"),
                     __FILE__, __LINE__, src_entry->numElements,
                     SK_FB_ELEM_SIZE_STMLE(*src_entry),
                     (void*)src_entry->dataPtr));

        tmpl = (fbTemplate_t*)fbSubTemplateMultiListEntryGetTemplate(src_entry);
        tid = fbSubTemplateMultiListEntryGetTemplateID(src_entry);

        visit_recs = 0;
        if (src_entry->numElements) {
            /* if the STML entry's template does not contain list
             * elements or varlen elements, there is nothing to do.
             * When list or vardata elements are present, process each
             * record in the STML entry individually. */
            for (i = 0; (ie = fbTemplateGetIndexedIE(tmpl, i)) != NULL; ++i) {
                if (FB_IE_VARLEN == ie->len || TYPE_IS_LIST(ie->type)) {
                    visit_recs = 1;
                    break;
                }
            }
        }
        if (visit_recs) {
            /* create a record for this set of entries */
            schema = NULL;
            if ((err = sk_schema_wrap_template(&schema, NULL, tmpl, tid))
                || (err = sk_fixrec_init(&src_rec, schema)))
            {
                TRACEMSG(2, ("Unable to create schema or record: %s",
                             sk_schema_strerror(err)));
                sk_schema_destroy(schema);
                return;
            }
            /* stash the record's current data */
            rec_data = src_rec.data;

            /* set record's data pointer to the element and clear it  */
            src_elem = NULL;
            while ((src_elem = (fbSubTemplateMultiListEntryNextDataPtr(
                                    src_entry, src_elem))))
            {
                /* manually set the data pointer */
                src_rec.data = (uint8_t*)src_elem;
                sk_fixrec_clear(&src_rec);
            }

            /* restore record's data and destroy the record */
            src_rec.data = rec_data;
            sk_fixrec_destroy(&src_rec);
            sk_schema_destroy(schema);
        }
    }

    fbSubTemplateMultiListClear(&src);
}

/*
 *    Set the memory referenced by 'schema' to the schema associated
 *    with the 'idx'th set of entries in the subTemplateMultiList
 *    'list'.  'idx' must be a valid index for 'list'.
 *
 *    If a schema has not yet been created for that set of entries,
 *    create it and store it in the array.
 *
 *    'stmle' must be either the 'idx' entry for the
 *    subTemplateMultiList 'list' or NULL.  When it is NULL, this
 *    function uses 'idx' to find that entry.
 */
static sk_schema_err_t
sk_fixlist_get_schema_subtemplatemulti(
    sk_fixlist_t                   *list,
    fbSubTemplateMultiListEntry_t  *stmle,
    uint16_t                        idx,
    sk_schema_t                   **schema)
{
    sk_schema_err_t err;
    fbTemplate_t *tmpl;
    uint16_t tid;

    assert(list);
    assert(schema);
    assert(FB_SUB_TMPL_MULTI_LIST == list->type);
    assert(idx < list->t.stml.fb_list.numElements);
    assert(idx < sk_vector_get_count(list->t.stml.schema_vec));
    assert(stmle == NULL
           || stmle == (fbSubTemplateMultiListGetIndexedEntry(
                            &list->t.stml.fb_list, idx)));

    *schema = NULL;

    sk_vector_get_value(list->t.stml.schema_vec, idx, schema);
    if (NULL == *schema) {
        if (NULL == stmle) {
            stmle = (fbSubTemplateMultiListGetIndexedEntry(
                         &list->t.stml.fb_list, idx));
            if (NULL == stmle) {
                skAbort();
            }
        }
        tmpl = (fbTemplate_t*)fbSubTemplateMultiListEntryGetTemplate(stmle);
        tid = fbSubTemplateMultiListEntryGetTemplateID(stmle);
        err = sk_schema_wrap_template(schema, NULL, tmpl, tid);
        if (err) {
            TRACEMSG(2, ("Unable to create schema: %s",
                         sk_schema_strerror(err)));
            return err;
        }
        sk_vector_set_value(list->t.stml.schema_vec, idx, schema);
    }
    return SK_SCHEMA_ERR_SUCCESS;
}


/*
 *    Fill 'rec' with the element on the subTemplateList 'list' at
 *    position 'idx'.
 *
 *    Helper function for sk_fixlist_get_element().
 */
static sk_schema_err_t
sk_fixlist_get_element_subtemplatemulti(
    sk_fixlist_t       *list,
    uint16_t            idx,
    const sk_fixrec_t **rec)
{
    fbSubTemplateMultiListEntry_t *stmle;
    sk_schema_t *schema;
    sk_schema_err_t err;
    size_t vec_idx;
    void *data;

    assert(list);
    assert(FB_SUB_TMPL_MULTI_LIST == list->type);

    vec_idx = 0;
    stmle = NULL;
    sk_fixrec_destroy(&list->t.stml.rand_element);
    while (NULL != (stmle = (fbSubTemplateMultiListGetNextEntry(
                                 &list->t.stml.fb_list, stmle))))
    {
        if (idx >= stmle->numElements) {
            idx -= stmle->numElements;
            ++vec_idx;
        } else {
            data = fbSubTemplateMultiListEntryGetIndexedPtr(stmle, idx);
            if (NULL == data) {
                break;
            }
            if (sk_fixlist_get_schema_subtemplatemulti(
                    list, stmle, vec_idx, &schema))
            {
                break;
            }
            err = sk_fixrec_init(&list->t.stml.rand_element, schema);
            if (err) {
                TRACEMSG(2, ("Unable to create record: %s",
                             sk_schema_strerror(err)));
                break;
            }
            sk_fixrec_set_data(&list->t.stml.rand_element, data);
            *rec = &list->t.stml.rand_element;
            return SK_SCHEMA_ERR_SUCCESS;
        }
    }
    return SK_SCHEMA_ERR_FIELD_NOT_FOUND;
}

/*
 *    Use the index that specifies which subTemplateMultiListEntry we
 *    are at and which record we are at within that entry to create a
 *    schema and a record that uses that schema.  Return
 *    SK_ITERATOR_OK on success or SK_ITERATOR_NO_MORE_ENTRIES when
 *    the indexes are out of range.
 */
static skIteratorStatus_t
sk_fixlist_subtemplatemulti_iter_next_template(
    sk_fixlist_t       *list)
{
    fbSubTemplateMultiListEntry_t *stmle;
    sk_schema_t *schema;
    sk_schema_err_t err;

    assert(list);
    assert(FB_SUB_TMPL_MULTI_LIST == list->type);

    stmle = fbSubTemplateMultiListGetIndexedEntry(&list->t.stml.fb_list,
                                                  list->t.stml.iter_pos);
    if (NULL == stmle) {
        return SK_ITERATOR_NO_MORE_ENTRIES;
    }
    if (sk_fixlist_get_schema_subtemplatemulti(
            list, stmle, list->t.stml.iter_pos, &schema))
    {
        return SK_ITERATOR_NO_MORE_ENTRIES;
    }
    err = sk_fixrec_init(&list->t.stml.iter_element, schema);
    if (err) {
        TRACEMSG(2, ("Unable to create record: %s", sk_schema_strerror(err)));
        return SK_ITERATOR_NO_MORE_ENTRIES;
    }
    return SK_ITERATOR_OK;
}

/*
 *    Fill 'rec' with the next element on the subTemplateMultiList 'list'.
 *
 *    Helper function for sk_fixlist_next_element().
 */
static skIteratorStatus_t
sk_fixlist_next_element_subtemplatemulti(
    sk_fixlist_t       *list,
    const sk_fixrec_t **rec)
{
    fbSubTemplateMultiListEntry_t *stmle;
    void *data;

    assert(list);
    assert(FB_SUB_TMPL_MULTI_LIST == list->type);
    assert(0 == list->iter_no_more_entries);

    do {
        stmle = fbSubTemplateMultiListGetIndexedEntry(&list->t.stml.fb_list,
                                                      list->t.stml.iter_pos);
        assert(stmle);
        assert(list->t.stml.iter_element.schema);
        data = fbSubTemplateMultiListEntryGetIndexedPtr(stmle, list->iter_idx);
        if (data) {
            sk_fixrec_set_data(&list->t.stml.iter_element, data);
            *rec = &list->t.stml.iter_element;
            ++list->iter_idx;
            return SK_ITERATOR_OK;
        }
        /* done with records for this template */
        sk_fixrec_destroy(&list->t.stml.iter_element);

        /* get next template and create a schema and record that use
         * it */
        ++list->t.stml.iter_pos;
        list->iter_idx = 0;
    } while (sk_fixlist_subtemplatemulti_iter_next_template(list)
             == SK_ITERATOR_OK);

    list->iter_no_more_entries = 1;
    return SK_ITERATOR_NO_MORE_ENTRIES;
}

/*
 *    Reset the iterator for the subTemplateMultiList fixlist 'list'.
 *    Helper for sk_fixlist_reset_iter().
 */
static void
sk_fixlist_reset_iter_subtemplatemulti(
    sk_fixlist_t       *list)
{
    assert(list);
    assert(FB_SUB_TMPL_MULTI_LIST == list->type);

    sk_fixrec_destroy(&list->t.stml.iter_element);
    list->t.stml.iter_pos = 0;
    list->iter_idx = 0;
    if (sk_fixlist_subtemplatemulti_iter_next_template(list)
        != SK_ITERATOR_OK)
    {
        list->iter_no_more_entries = 1;
    }
}

/*
 *    Append the record 'rec' to the subTemplateMultiList 'list'.
 */
static void
sk_fixlist_append_to_subtemplatemulti(
    sk_fixlist_t       *list,
    const sk_fixrec_t  *rec)
{
    sk_fixrec_template_map_t *tmpl_map;
    fbSubTemplateMultiListEntry_t *stmle;
    fbTemplate_t *tmpl;
    uint16_t tid;
    void *p;

    assert(list);
    assert(FB_SUB_TMPL_MULTI_LIST == list->type);
    assert(0 == list->fixbuf_owns_vardata);
    assert(NULL == list->containing_rec);
    assert(list->session);

    /* get templates used by the STML */
    tmpl_map = sk_fixrec_template_map_create(NULL);
    sk_fixrec_template_map_add_subtemplatemulti(
        tmpl_map, &list->t.stml.fb_list);

    /* determine whether this record has the same template as the
     * previous record */
    if (list->t.stml.fb_list.numElements
        && (stmle = (fbSubTemplateMultiListGetIndexedEntry(
                         &list->t.stml.fb_list,
                         (list->t.stml.fb_list.numElements - 1))))
        && (sk_template_matches_template(
                fbSubTemplateMultiListEntryGetTemplate(stmle),
                rec->schema->tmpl)))
    {
#if TRACEMSG_LEVEL >= 4
        const void *old_dataPtr = stmle->dataPtr;
#endif
        /* templates match */

        /* add templates for any sub-lists in the record */
        sk_fixrec_template_map_add_record(tmpl_map, rec);
        sk_fixrec_template_map_update_session(tmpl_map, list->session);

        /* add one new element to entries array */
        p = fbSubTemplateMultiListEntryAddNewElements(stmle, 1);
        TRACEMSG(4, (("%s:%d:Appended subTemplateMultiListEntry"
                      " %u %u-byte elements (old %p) %p"),
                     __FILE__, __LINE__, stmle->numElements,
                     SK_FB_ELEM_SIZE_STMLE(*stmle),
                     (void*)old_dataPtr, (void*)stmle->dataPtr));

    } else {
#if TRACEMSG_LEVEL >= 4
        const void *old_firstEntry = list->t.stml.fb_list.firstEntry;
#endif
        /* add templates for this itself record and for any sub-lists
         * in the record */
        sk_schema_get_template(rec->schema, (fbTemplate_t**)&tmpl, &tid);
        sk_fixrec_template_map_insert(tmpl_map, tmpl, tid);
        sk_fixrec_template_map_add_record(tmpl_map, rec);
        sk_fixrec_template_map_update_session(tmpl_map, list->session);

        /* add schema to the vector */
        sk_schema_clone(rec->schema);
        sk_vector_append_value(list->t.stml.schema_vec, &rec->schema);

        /* add a new entry to the STML */
        stmle = fbSubTemplateMultiListAddNewEntries(&list->t.stml.fb_list, 1);
        TRACEMSG(4, (("%s:%d:Appended subTemplateMultiList"
                      " %u stmlEntries (old %p) %p"),
                     __FILE__, __LINE__,
                     list->t.stml.fb_list.numElements, (void*)old_firstEntry,
                     (void*)list->t.stml.fb_list.firstEntry));

        /* set the template for this entry and initialize it to hold
         * one element */
        if (!sk_fixrec_template_map_find(tmpl_map, tmpl, &tid)) {
            sk_fixrec_template_map_destroy(tmpl_map);
            return;
        }
        p = fbSubTemplateMultiListEntryInit(stmle, tid, tmpl, 1);
        TRACEMSG(4, (("%s:%d:Allocated subTemplateMultiListEntry"
                      " %u %u-byte elements %p"),
                     __FILE__, __LINE__,
                     stmle->numElements, SK_FB_ELEM_SIZE_STMLE(*stmle),
                     (void*)stmle->dataPtr));
    }

    /* deep copy the record's data */
    assert((size_t)(rec->schema->len * list->t.stl.fb_list.numElements)
           <= list->t.stl.fb_list.dataLength.length);
    sk_fixrec_copy_data(p, rec, tmpl_map);

    sk_fixrec_template_map_destroy(tmpl_map);
}

/*
 *    Create an sk_fixlist_t that holds a subTemplateMultiList and
 *    store the list in the location referenced by 'out_list'.
 *
 *    If 'existing_list' is non-NULL, the function assumes data is
 *    being read from a file by fixbuf and the sk_fixlist_t is a
 *    wrapper over that fbSubtemplateMultiList_t.  Otherwise, a new
 *    fbSubtemplateList_t is initialized.
 *
 *    This is a helper function used by sk_fixrec_create_list_iter()
 *    and sk_fixlist_create_subtemplatemultilist().
 */
static sk_schema_err_t
sk_fixlist_create_subtemplatemulti(
    sk_fixlist_t              **out_list,
    fbInfoModel_t              *model,
    fbSubTemplateMultiList_t   *existing_list)
{
    sk_fixlist_t *list;
    sk_schema_t *s = NULL;

    assert(out_list);

    list = sk_alloc(sk_fixlist_t);
    list->type = FB_SUB_TMPL_MULTI_LIST;
    sk_fixrec_init(&list->t.stml.iter_element, NULL);
    sk_fixrec_init(&list->t.stml.rand_element, NULL);
    list->t.stml.schema_vec = sk_vector_create(sizeof(sk_schema_t*));

    if (NULL == model) {
        model = list->t.stml.model = skipfix_information_model_create(0);
    }

    if (!existing_list) {
        /* create an empty list */
        fbSubTemplateMultiListInit(
            &list->t.stml.fb_list, FB_LIST_SEM_UNDEFINED, 0);
        TRACEMSG(4, ("%s:%d:Allocated empty subTemplateMultiList %p",
                     __FILE__, __LINE__,
                     (void*)list->t.stml.fb_list.firstEntry));

        /* create a session to store templates used by this list or
         * its sublists */
        list->session = fbSessionAlloc(model);
    } else {
        TRACEMSG(4, ("%s:%d:Handle to subTemplateMultiList %u stmlEntries %p",
                     __FILE__, __LINE__, existing_list->numElements,
                     (void*)existing_list->firstEntry));

        memcpy(&list->t.stml.fb_list, existing_list,
               sizeof(fbSubTemplateMultiList_t));

        /* ensure schema-vector has correct number of elements, but no
         * need to fill them in */
        sk_vector_set_capacity(
            list->t.stml.schema_vec, existing_list->numElements);
        sk_vector_set_value(
            list->t.stml.schema_vec, (existing_list->numElements - 1), &s);
        sk_fixlist_reset_iter_subtemplatemulti(list);
        list->fixbuf_owns_vardata = 1;
    }

    *out_list = list;
    return SK_SCHEMA_ERR_SUCCESS;
}

/*
 *    Free the memory allocated by
 *    sk_fixlist_create_subtemplatemulti().  Specifically, free an
 *    sk_fixlist_t that wraps a subTemplateMultiList.
 */
static void
sk_fixlist_destroy_subtemplatemulti(
    sk_fixlist_t       *list)
{
    sk_schema_t **schema;
    size_t i;

    assert(list);
    assert(FB_SUB_TMPL_MULTI_LIST == list->type);

    sk_fixrec_destroy(&list->t.stml.iter_element);
    sk_fixrec_destroy(&list->t.stml.rand_element);

    if (list->session) {
        /* list was created for writing */
        sk_fixrec_free_list_subtemplatemulti(&list->t.stml.fb_list);
        fbSessionFree(list->session);
    }
    for (i = sk_vector_get_count(list->t.stml.schema_vec); i > 0; ) {
        --i;
        schema = ((sk_schema_t**)
                  sk_vector_get_value_pointer(list->t.stml.schema_vec, i));
        sk_schema_destroy(*schema);
    }
    if (list->t.stml.model) {
        skipfix_information_model_destroy(list->t.stml.model);
    }
    sk_vector_destroy(list->t.stml.schema_vec);
    memset(list, 0, sizeof(sk_fixlist_t));
    free(list);
}


/*  *** PUBLIC LIST API ***  */

/* get a list from the 'field' of record 'rec' */
sk_schema_err_t
sk_fixrec_get_list(
    const sk_fixrec_t  *rec,
    const sk_field_t   *field,
    sk_fixlist_t      **out_fixlist)
{
    sk_fixlist_t *fixlist = NULL;
    fbInfoModel_t *model;
    sk_schema_err_t err = SK_SCHEMA_ERR_BAD_TYPE;

    assert(rec);
    assert(field);
    assert(out_fixlist);

    model = sk_schema_get_infomodel(rec->schema);

    ASSERT_FIELD_IN_REC(field, rec);
    switch (field->ie->type) {
      case FB_BASIC_LIST:
        {
            fbBasicList_t bl;

            memcpy(&bl, rec->data + field->offset, sizeof(bl));
            err = sk_fixlist_create_basic(&fixlist, model, NULL, &bl);
        }
        break;
      case FB_SUB_TMPL_LIST:
        {
            fbSubTemplateList_t stl;

            memcpy(&stl, rec->data + field->offset, sizeof(stl));
            err = sk_fixlist_create_subtemplate(&fixlist, model, NULL, &stl);
        }
        break;
      case FB_SUB_TMPL_MULTI_LIST:
        {
            fbSubTemplateMultiList_t stml;

            memcpy(&stml, rec->data + field->offset, sizeof(stml));
            err = sk_fixlist_create_subtemplatemulti(&fixlist, model, &stml);
        }
        break;
      default:
        break;
    }
    if (err) {
        return err;
    }
    fixlist->containing_rec = rec;

    *out_fixlist = fixlist;
    return SK_SCHEMA_ERR_SUCCESS;
}

/* Create a new basicList unattached to any record */
sk_schema_err_t
sk_fixlist_create_basiclist_from_ident(
    sk_fixlist_t      **list,
    fbInfoModel_t      *model,
    sk_field_ident_t    ident)
{
    const fbInfoElement_t *ie;

    ie = fbInfoModelGetElementByID(model, SK_FIELD_IDENT_GET_ID(ident),
                                   SK_FIELD_IDENT_GET_PEN(ident));
    if (NULL == ie) {
        return SK_SCHEMA_ERR_UNKNOWN_IE;
    }
    return sk_fixlist_create_basic(list, model, ie, NULL);
}

/* Create a new basicList unattached to any record */
sk_schema_err_t
sk_fixlist_create_basiclist_from_name(
    sk_fixlist_t      **list,
    fbInfoModel_t      *model,
    const char         *name)
{
    const fbInfoElement_t *ie;

    assert(model);

    ie = fbInfoModelGetElementByName(model, name);
    if (NULL == ie) {
        return SK_SCHEMA_ERR_UNKNOWN_IE;
    }
    return sk_fixlist_create_basic(list, model, ie, NULL);
}

/* Create a subTemplateList unattached to any record */
sk_schema_err_t
sk_fixlist_create_subtemplatelist(
    sk_fixlist_t      **list,
    const sk_schema_t  *schema)
{
    return sk_fixlist_create_subtemplate(list, sk_schema_get_infomodel(schema),
                                         schema, NULL);
}

/* Create a subTemplateMultiList unattached to any record */
sk_schema_err_t
sk_fixlist_create_subtemplatemultilist(
    sk_fixlist_t      **list,
    fbInfoModel_t      *model)
{
    return sk_fixlist_create_subtemplatemulti(list, model, NULL);
}

/* Destroy the record list 'list'. */
void
sk_fixlist_destroy(
    sk_fixlist_t       *list)
{
    if (list) {
        switch (list->type) {
          case FB_BASIC_LIST:
            sk_fixlist_destroy_basic(list);
            break;
          case FB_SUB_TMPL_LIST:
            sk_fixlist_destroy_subtemplate(list);
            break;
          case FB_SUB_TMPL_MULTI_LIST:
            sk_fixlist_destroy_subtemplatemulti(list);
            break;
          default:
            skAbortBadCase(list->type);
        }
    }
}

/* Return the semantic value associated with list. */
uint8_t
sk_fixlist_get_semantic(
    const sk_fixlist_t *list)
{
    /* the fbXXXList_t is the first element of each structure in the
     * union t, so case the address of the union to the appropriate
     * type of list and call the list-specfic function. */
#define GET_SEMANTIC(list_type)                                 \
    list_type ## GetSemantic((list_type ## _t*)&list->t)

    assert(list);
    switch (list->type) {
      case FB_BASIC_LIST:
        return GET_SEMANTIC(fbBasicList);
      case FB_SUB_TMPL_LIST:
        return GET_SEMANTIC(fbSubTemplateList);
      case FB_SUB_TMPL_MULTI_LIST:
        return GET_SEMANTIC(fbSubTemplateMultiList);
      default:
        skAbortBadCase(list->type);
    }

#undef GET_SEMANTIC
}

/* Set the semantic value associated with list. */
void
sk_fixlist_set_semantic(
    sk_fixlist_t       *list,
    uint8_t             semantic)
{
    /* the fbXXXList_t is the first element of each structure in the
     * union t, so cast the address of the union to the appropriate
     * type of list and call the list-specfic function. */
#define SET_SEMANTIC(list_type)                                 \
    list_type ## SetSemantic((list_type ## _t*)&list->t, semantic)

    assert(list);
    switch (list->type) {
      case FB_BASIC_LIST:
        SET_SEMANTIC(fbBasicList);
        return;
      case FB_SUB_TMPL_LIST:
        SET_SEMANTIC(fbSubTemplateList);
        return;
      case FB_SUB_TMPL_MULTI_LIST:
        SET_SEMANTIC(fbSubTemplateMultiList);
        return;
      default:
        skAbortBadCase(list->type);
    }

#undef SET_SEMANTIC
}

/* Return the underlying type of a list. */
int
sk_fixlist_get_type(
    const sk_fixlist_t *list)
{
    assert(list);
    return list->type;
}

/* Return the number of records in 'list'. */
uint16_t
sk_fixlist_count_elements(
    const sk_fixlist_t *list)
{
    fbSubTemplateMultiListEntry_t *entry;
    uint16_t count;

    assert(list);
    switch (list->type) {
      case FB_BASIC_LIST:
        return list->t.bl.fb_list.numElements;
      case FB_SUB_TMPL_LIST:
        return list->t.stl.fb_list.numElements;
      case FB_SUB_TMPL_MULTI_LIST:
        entry = NULL;
        count = 0;
        while ((entry = fbSubTemplateMultiListGetNextEntry(
                    (fbSubTemplateMultiList_t*)&list->t.stml.fb_list, entry)))
        {
            count += entry->numElements;
        }
        return count;
      default:
        skAbortBadCase(list->type);
    }
}

/* Return the number of schemas in 'list'. */
uint16_t
sk_fixlist_count_schemas(
    const sk_fixlist_t *list)
{
    fbSubTemplateMultiListEntry_t *entry;
    uint16_t count;

    assert(list);
    switch (list->type) {
      case FB_BASIC_LIST:
      case FB_SUB_TMPL_LIST:
        return 1;
      case FB_SUB_TMPL_MULTI_LIST:
        entry = NULL;
        count = 0;
        while ((entry = fbSubTemplateMultiListGetNextEntry(
                    (fbSubTemplateMultiList_t*)&list->t.stml.fb_list, entry)))
        {
            ++count;
        }
        return count;
      default:
        skAbortBadCase(list->type);
    }
}

/* Get element from list at position idx */
sk_schema_err_t
sk_fixlist_get_element(
    sk_fixlist_t       *list,
    uint16_t            idx,
    const sk_fixrec_t **rec)
{
    assert(list);

    switch (list->type) {
      case FB_BASIC_LIST:
        return sk_fixlist_get_element_basic(list, idx, rec);
      case FB_SUB_TMPL_LIST:
        return sk_fixlist_get_element_subtemplate(list, idx, rec);
      case FB_SUB_TMPL_MULTI_LIST:
        return sk_fixlist_get_element_subtemplatemulti(list, idx, rec);
      default:
        skAbortBadCase(list->type);
    }
    return SK_SCHEMA_ERR_FIELD_NOT_FOUND; /* NOTREACHED */
}

#if 0
/* Set element in list at position idx to 'rec' */
sk_schema_err_t
sk_fixlist_set_element(
    const sk_fixlist_t *list,
    uint16_t            idx,
    const sk_fixrec_t  *rec);
#endif  /* 0 */

/* Return the 'idx'th schema in 'list' */
const sk_schema_t *
sk_fixlist_get_schema(
    const sk_fixlist_t *list,
    uint16_t            idx)
{
    sk_schema_t *schema;

    assert(list);
    switch (list->type) {
      case FB_BASIC_LIST:
        if (0 == idx) {
            return list->t.bl.schema;
        }
        break;
      case FB_SUB_TMPL_LIST:
        if (0 == idx) {
            return list->t.stl.schema;
        }
        break;
      case FB_SUB_TMPL_MULTI_LIST:
        schema = NULL;
        if (idx < list->t.stml.fb_list.numElements) {
            if (sk_fixlist_get_schema_subtemplatemulti(
                    (sk_fixlist_t*)list, NULL, idx, &schema))
            {
                return NULL;
            }
            return schema;
        }
        break;
      default:
        skAbortBadCase(list->type);
    }
    return NULL;
}

/* When iterating over a list, return the next element */
skIteratorStatus_t
sk_fixlist_next_element(
    sk_fixlist_t       *list,
    const sk_fixrec_t **rec)
{
    assert(list);
    assert(rec);

    if (!list->iter_no_more_entries) {
        switch (list->type) {
          case FB_BASIC_LIST:
            if (sk_fixlist_get_element_basic(list, list->iter_idx, rec)) {
                break;
            }
            ++list->iter_idx;
            return SK_ITERATOR_OK;
          case FB_SUB_TMPL_LIST:
            if (sk_fixlist_get_element_subtemplate(list, list->iter_idx, rec)){
                break;
            }
            ++list->iter_idx;
            return SK_ITERATOR_OK;
          case FB_SUB_TMPL_MULTI_LIST:
            return sk_fixlist_next_element_subtemplatemulti(list, rec);
          default:
            skAbortBadCase(list->type);
        }
        list->iter_no_more_entries = 1;
    }
    return SK_ITERATOR_NO_MORE_ENTRIES;
}

/* Reset list iterator */
sk_schema_err_t
sk_fixlist_reset_iter(
    sk_fixlist_t       *list)
{
    sk_schema_err_t err = SK_SCHEMA_ERR_SUCCESS;

    assert(list);

    list->iter_no_more_entries = 0;
    list->iter_idx = 0;

    switch (list->type) {
      case FB_BASIC_LIST:
      case FB_SUB_TMPL_LIST:
        break;
      case FB_SUB_TMPL_MULTI_LIST:
        sk_fixlist_reset_iter_subtemplatemulti(list);
        break;
      default:
        skAbortBadCase(list->type);
    }

    return err;
}

/* Append the record 'rec' to the record list 'list'. */
sk_schema_err_t
sk_fixlist_append_fixrec(
    sk_fixlist_t       *list,
    const sk_fixrec_t  *rec)
{
    assert(list);
    assert(rec);

    if (list->fixbuf_owns_vardata || list->containing_rec) {
        return SK_SCHEMA_ERR_UNSPECIFIED;
    }

    switch (list->type) {
      case FB_BASIC_LIST:
        return sk_fixlist_append_to_basic(list, rec, NULL);
      case FB_SUB_TMPL_LIST:
        return sk_fixlist_append_to_subtemplate(list, rec);
      case FB_SUB_TMPL_MULTI_LIST:
        sk_fixlist_append_to_subtemplatemulti(list, rec);
        break;
      default:
        skAbortBadCase(list->type);
    }
    return SK_SCHEMA_ERR_SUCCESS;
}

/* Append the element at 'field' in 'rec' to basicList 'list'. */
sk_schema_err_t
sk_fixlist_append_element(
    sk_fixlist_t       *list,
    const sk_fixrec_t  *rec,
    const sk_field_t   *field)
{
    assert(list);
    assert(rec);

    ASSERT_FIELD_IN_REC(field, rec);
    if (list->fixbuf_owns_vardata) {
        return SK_SCHEMA_ERR_UNSPECIFIED;
    }
    /* requires a basic list */
    if (FB_BASIC_LIST != list->type) {
        return SK_SCHEMA_ERR_INCOMPATIBLE;
    }
    return sk_fixlist_append_to_basic(list, rec, field);
}

/* Set 'field' in 'rec' to the data in the given 'list'. */
sk_schema_err_t
sk_fixrec_set_list(
    sk_fixrec_t        *rec,
    const sk_field_t   *field,
    const sk_fixlist_t *list)
{
    sk_fixrec_template_map_t *tmpl_map;

    assert(rec);
    assert(field);
    assert(list);

    ASSERT_FIELD_IN_REC(field, rec);
#if 0
    /* this is #if 0'ed since we currently copy the 'list' argument
     * and it is fine if the list belongs to another record */
    if (NULL != list->containing_rec) {
        return SK_SCHEMA_ERR_UNSPECIFIED;
    }
#endif
    if (field->ie->type != list->type) {
        return SK_SCHEMA_ERR_BAD_TYPE;
    }

    /* FIXME: we should not allow the user to change a list when the
     * record's data is owned by fixbuf unless the field was added to
     * the schema by a silk plugin.  right now we can detect when
     * fixbuf owns a record, but not whether a field is from a plugin,
     * so for now just hope the user respects the "const" setting of
     * an sk_fixrec_t. */

    /* FIXME: Copy or take over ownership? */
    switch (field->ie->type) {
      case FB_BASIC_LIST:
        sk_fixrec_free_list_basic(rec->data + field->offset);
        tmpl_map = sk_fixrec_template_map_create(rec);
        sk_fixrec_template_map_add_basic(tmpl_map, &list->t.bl.fb_list);
        sk_fixrec_template_map_update_session(tmpl_map, rec->schema->session);
        sk_fixrec_copy_list_basic(
            rec->data + field->offset, &list->t.bl.fb_list, tmpl_map);
        sk_fixrec_template_map_destroy(tmpl_map);
        break;
      case FB_SUB_TMPL_LIST:
        sk_fixrec_free_list_subtemplate(rec->data + field->offset);
        tmpl_map = sk_fixrec_template_map_create(rec);
        sk_fixrec_template_map_add_subtemplate(
            tmpl_map, &list->t.bl.fb_list);
        sk_fixrec_template_map_update_session(tmpl_map, rec->schema->session);
        sk_fixrec_copy_list_subtemplate(
            rec->data + field->offset, &list->t.stl.fb_list, tmpl_map);
        sk_fixrec_template_map_destroy(tmpl_map);
        break;
      case FB_SUB_TMPL_MULTI_LIST:
        sk_fixrec_free_list_subtemplatemulti(rec->data + field->offset);
        tmpl_map = sk_fixrec_template_map_create(rec);
        sk_fixrec_template_map_add_subtemplatemulti(
            tmpl_map, &list->t.bl.fb_list);
        sk_fixrec_template_map_update_session(tmpl_map, rec->schema->session);
        sk_fixrec_copy_list_subtemplatemulti(
            rec->data + field->offset, &list->t.stml.fb_list, tmpl_map);
        sk_fixrec_template_map_destroy(tmpl_map);
        break;
      default:
        return SK_SCHEMA_ERR_BAD_TYPE;
    }
    return SK_SCHEMA_ERR_SUCCESS;
}



/*
 *  ******************************************************************
 *  Computed Field Support
 *  ******************************************************************
 */

typedef struct sk_field_computed_cbdata_st {
    sk_field_computed_update_fn_t update;
    sk_field_computed_data_t      data;
} sk_field_computed_cbdata_t;


static void
sk_computed_cbdata_free(
    sk_field_computed_cbdata_t *cbdata)
{
    size_t i;

    assert(cbdata != NULL);
    for (i = 0; i < cbdata->data.entries; ++i) {
        free((char *)cbdata->data.names[i]);
    }
    free(cbdata->data.names);
    free(cbdata->data.fields);
    free(cbdata);
}

static sk_schema_err_t
sk_field_computed_cbdata_free(
    sk_field_t         *field)
{
    sk_field_computed_cbdata_t *cbdata =
        (sk_field_computed_cbdata_t*)field->ops.cbdata;
    sk_computed_cbdata_free(cbdata);
    field->ops.cbdata = NULL;
    return 0;
}

static sk_schema_err_t
sk_field_computed_cbdata_copy(
    sk_field_t         *field,
    void              **new_cbdata)
{
    size_t i;
    sk_field_computed_cbdata_t *retval;
    sk_field_computed_cbdata_t *cbdata =
        (sk_field_computed_cbdata_t*)field->ops.cbdata;

    assert(cbdata != NULL);
    retval = sk_alloc(sk_field_computed_cbdata_t);
    retval->update = cbdata->update;
    retval->data.dest = cbdata->data.dest;
    retval->data.entries = cbdata->data.entries;
    retval->data.names = sk_alloc_array(const char *, cbdata->data.entries);
    retval->data.fields =
        sk_alloc_array(const sk_field_t *, cbdata->data.entries);
    for (i = 0; i < cbdata->data.entries; ++i) {
        retval->data.names[i] = sk_alloc_strdup(cbdata->data.names[i]);
        retval->data.fields[i] = cbdata->data.fields[i];
    }
    *new_cbdata = cbdata;
    return 0;
}

/*
 *    Callback to update a computed field.  This function is used as
 *    the 'compute' callback of the sk_field_ops_t (ops.compute).
 */
static sk_schema_err_t
sk_field_computed_compute(
    sk_fixrec_t        *rec,
    const sk_field_t   *field)
{
    sk_field_computed_cbdata_t *cbdata;
    cbdata = (sk_field_computed_cbdata_t*)field->ops.cbdata;
    return cbdata->update(rec, &cbdata->data);
}

const fbInfoElement_t *
sk_schema_get_ie_from_computed_description(
    const sk_field_computed_description_t  *desc,
    fbInfoModel_t                          *model)
{
    fbInfoElement_t ie;
    const fbInfoElement_t *iep;

    assert(desc);
    assert(model);

    switch (desc->lookup) {
      case SK_FIELD_COMPUTED_LOOKUP_BY_NAME:
        if (NULL == desc->name) {
            return NULL;
        }
        return fbInfoModelGetElementByName(model, desc->name);

      case SK_FIELD_COMPUTED_LOOKUP_BY_IDENT:
        return fbInfoModelGetElementByID(
            model, SK_FIELD_IDENT_GET_ID(desc->ident),
            SK_FIELD_IDENT_GET_PEN(desc->ident));

      case SK_FIELD_COMPUTED_CREATE:
        if (NULL == desc->name) {
            return NULL;
        }
        memset(&ie, 0, sizeof(ie));
        ie.ent = SK_FIELD_IDENT_GET_PEN(desc->ident);
        ie.num = SK_FIELD_IDENT_GET_ID(desc->ident);

        /* check whether the element already exists; the name, ident,
         * and datatype must match. */
        iep = fbInfoModelGetElementByName(model, desc->name);
        if (iep) {
            if (iep->type == desc->datatype
                && ((desc->ident == 0)
                    || (iep->ent == ie.ent && iep->num == ie.num)))
            {
                return iep;
            }
            return NULL;
        }
        iep = fbInfoModelGetElementByID(model, ie.ent, ie.num);
        if (iep) {
            /* name must not match or we would have found it */
            return NULL;
        }

        /* need to create field */
        ie.ref.name = desc->name;
        ie.len = desc->len;
        ie.min = desc->min;
        ie.max = desc->max;
        ie.flags = (((uint32_t)desc->units << 16)
                    | ((uint32_t)desc->semantics << 8));
        ie.type = desc->datatype;
        switch (ie.type) {
          case FB_UINT_8:
          case FB_UINT_16:
          case FB_UINT_32:
          case FB_UINT_64:
          case FB_INT_8:
          case FB_INT_16:
          case FB_INT_32:
          case FB_INT_64:
            ie.flags |= FB_IE_F_ENDIAN;
            break;
          default:
            break;
        }
        return sk_infomodel_add_element(model, &ie);

      default:
        skAbortBadCase(desc->lookup);
    }
    return NULL;
}


sk_schema_err_t
sk_schema_insert_computed_field(
    const sk_field_t                      **field,
    sk_schema_t                            *schema,
    const sk_field_computed_description_t  *desc,
    const sk_field_t                       *before)
{
    sk_field_computed_cbdata_t *cbdata;
    const fbInfoElement_t *iep;
    sk_field_ops_t ops;
    sk_schema_err_t rv;
    const sk_field_t *f;
    size_t field_names_len;
    size_t i;

    assert(schema);
    assert(desc);

    if (schema->tmpl) {
        return SK_SCHEMA_ERR_FROZEN;
    }

    /* Create the callback data for the computed field */
    cbdata = sk_alloc(sk_field_computed_cbdata_t);
    cbdata->update = desc->update;
    cbdata->data.caller_ctx = desc->caller_ctx;

    /* determine the number of strings in field_names[] */
    if (desc->field_names_len < 0) {
        field_names_len = SIZE_MAX;
    } else {
        field_names_len = desc->field_names_len;
    }
    if (field_names_len) {
        for (i = 0; i < field_names_len && NULL != desc->field_names[i]; ++i)
            ; /* empty */
        field_names_len = i;
    }

    /* copy the names and locate each field in the schema */
    if (field_names_len) {
        cbdata->data.entries = field_names_len;
        cbdata->data.names
            = (const char**)sk_alloc_array(char *, field_names_len);
        cbdata->data.fields
            = (const sk_field_t**)sk_alloc_array(sk_field_t*, field_names_len);

        for (i = 0; i < field_names_len; ++i) {
            cbdata->data.names[i] = sk_alloc_strdup(desc->field_names[i]);
            cbdata->data.fields[i]
                = sk_schema_get_field_by_name(schema, desc->field_names[i],
                                              NULL);
        }
    }

    /* Fill the field_ops structure */
    memset(&ops, 0, sizeof(ops));
    ops.copy_cbdata = sk_field_computed_cbdata_copy;
    ops.teardown = sk_field_computed_cbdata_free;
    ops.compute = sk_field_computed_compute;
    ops.cbdata = cbdata;

    if (NULL == field) {
        field = &f;
    }

    /* Create the field, using the ops structure */
    switch (desc->lookup) {
      case SK_FIELD_COMPUTED_LOOKUP_BY_IDENT:
        rv = sk_schema_insert_field_by_ident(
            (sk_field_t **)field, schema, desc->ident, &ops, before);
        break;
      case SK_FIELD_COMPUTED_LOOKUP_BY_NAME:
        rv = sk_schema_insert_field_by_name(
            (sk_field_t **)field, schema, desc->name, &ops, before);
        break;
      case SK_FIELD_COMPUTED_CREATE:
        /* Add a new infoelement to the infomodel to represent this
         * field */
        iep = (sk_schema_get_ie_from_computed_description(
                   desc, sk_schema_get_infomodel(schema)));
        if (iep == NULL) {
            rv = SK_SCHEMA_ERR_UNKNOWN_IE;
            break;
        }
        rv = sk_schema_insert_field_by_id(
            (sk_field_t **)field, schema, iep->ent, iep->num, &ops, before);
        break;
      default:
        skAbortBadCase(desc->lookup);
    }
    if (rv != 0) {
        sk_computed_cbdata_free(cbdata);
        goto END;
    }

    cbdata->data.dest = *field;

  END:
    return rv;
}


/*
** Local Variables:
** mode:c
** indent-tabs-mode:nil
** c-basic-offset:4
** End:
*/
