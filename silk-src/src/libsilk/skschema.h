/*
** Copyright (C) 2014-2017 by Carnegie Mellon University.
**
** @OPENSOURCE_LICENSE_START@
** See license information in ../../LICENSE.txt
** @OPENSOURCE_LICENSE_END@
*/
#ifndef _SKSCHEMA_H
#define _SKSCHEMA_H
#ifdef __cplusplus
extern "C" {
#endif

#include <silk/silk.h>

RCSIDENTVAR(rcsID_SKSCHEMA_H, "$SiLK: skschema.h efd886457770 2017-06-21 18:43:23Z mthomas $");

#include <silk/silk_types.h>
#include <silk/skfixbuf.h>

/*
 *  skschema.h
 *
 *  IPFIX-style handling of records in SiLK.
 *
 */


/*
 *    IPFIX-style handling of records in SiLK.
 *
 *    The primary unit for handling data in SiLK is the record
 *    (represented by the rwRec type).  A record consists of data
 *    which encodes the raw values of the record, and a schema which
 *    describes both the values' types and how to interpret the raw
 *    data.  Multiple records may share the same schema.
 *
 *    The standard record handling loop looks almost exactly as it did
 *    in previous versions of SiLK:
 *
 *        sk_fixstream_t *in_stream;
 *        sk_fixstream_t *out_stream;
 *        sk_fixrec_t rec;
 *
 *        sk_fixrec_init(&rec, NULL);
 *
 *        // open input stream
 *        skStreamOpenIpfix(&stream, "infile", SK_IO_READ);
 *        // open output stream
 *        skStreamOpenIpfix(&stream, "outfile", SK_IO_WRITE);
 *
 *        // read records
 *        while (sk_fixstream_read_record(in_stream, &rec)) {
 *            // process record
 *            ...
 *
 *            // write record
 *            sk_fixstream_write_record(out_stream, &rec, NULL);
 *        }
 *        sk_fixrec_destroy(&rec);
 *
 *    The primary user-visible differences are in how one processes
 *    the records and in how one sets them up (sk_fixrec_init) and
 *    tears them down (sk_fixrec_destroy).
 *
 *    A record is interpreted by its schema.  A schema consists of a
 *    set of fields.  Each field has a data type (e.g, 32-bit floating
 *    point, unsigned 16-bit integer) and size information describing
 *    the values that can be contained in that field of the record.
 *    Each record can have a different schema, though most streams
 *    will contain records that are represented by only a single or a
 *    small number of schemas.
 *
 *    In contrast, previous releases of SiLK supported a single schema
 *    defined by the rwGeneric C-struct structure, where the set of
 *    fields, their locations, their sizes, and their data types were
 *    fixed at compile time.
 *
 *    Records that are created by sk_fixrec_create() are freed by
 *    sk_fixrec_destroy().  Records initialized by sk_fixrec_init()
 *    are not freed by sk_fixrec_destroy().  If a record's data
 *    pointer is changed with sk_fixrec_set_data(), that data pointer
 *    will not be freed by sk_fixrec_destroy().
 *
 *    As different fields have different data types, different
 *    accessor functions will be needed to access the values.  To
 *    generically process all fields of a record, one will need a loop
 *    like this:
 *
 *        uint16_t i;
 *        rwRec *rec = ...;
 *        const sk_schema_t *schema;
 *        const sk_field *field;
 *        uint64_t u64;
 *        double d;
 *
 *        schema = sk_fixrec_get_schema(rec);
 *        for (i = 0; i < sk_schema_get_count(schema); ++i) {
 *            field = sk_schema_get_field(schema, i);
 *            switch (sk_field_get_type(field)) {
 *              case FB_UINT_8:
 *              case FB_UINT_16:
 *              case FB_UINT_32:
 *              case FB_UINT_64:
 *                sk_fixrec_get_unsigned(rec, field, &u64);
 *                // Do something with the u64 value
 *                break;
 *              case FB_FLOAT_32:
 *              case FB_FLOAT_64:
 *                sk_fixrec_get_float(rec, field, &d);
 *                // Do something with the d value
 *                break;
 *              ...
 *            }
 *        }
 *
 *    Here we loop over the fields of the schema of a record, get the
 *    type information of the field, and then call the appropriate
 *    accessor function based on the field type.  (In this example the
 *    generic accessor function are being used, which treat a small
 *    number of types -- such as all unsigned integers -- the same
 *    way.  There are also separate accessor function for each
 *    individual type. )
 *
 *    Modifying records works similarly, except one uses the setter
 *    functions instead of the accessor functions.
 *
 *    Schemas can be examined by looking at field by index (using
 *    sk_schema_get_field() and sk_schema_get_count()), by ID (using
 *    sk_schema_get_field_by_ident()), or by name (using
 *    sk_schema_get_field_by_name()).
 *
 *    There are a few operations on data that are so common that the
 *    ability to handle them are built into the fields themselves,
 *    such that one does not have to extract the underlying value to
 *    do the operation.  These operations are:
 *
 *        sk_fixrec_data_to_text(rec, field, format, buffer, size)
 *            Outputs a text representation of the field value in rec
 *            to buffer.
 *
 *        sk_fixrec_data_compare(rec_a, field_a, rec_b, field_b, &cmp)
 *            Returns the relative ordering of the values of the two
 *            fields from the two records.
 *
 *        sk_fixrec_data_merge(rec_a, field_a, rec_b, field_b)
 *            "Merges" the data from field_b of rec_b into field_a of
 *            rec_a.
 *            TODO: Default version only works on certain field types.
 *            Elaborate more later....
 *
 *    When one is interested in just a few fields rather than every
 *    field, one needs to be able to react to records that contain
 *    different schemas.  Once a new schema is examined, any important
 *    fields or data about the schema can be extracted and saved in
 *    the schema's context registry.  Every schema can map an
 *    sk_schema_ctx_ident_t to a pointer that can be accessed using
 *    sk_schema_get_context().  The pointer along with an optional
 *    free function can be set using sk_schema_set_context().  Before
 *    calling sk_schema_set_context(), first call
 *    sk_schema_context_ident_create() to get a unique
 *    identifier for your application's or library's use.  Then use
 *    that identifier as the "ident" key to
 *    sk_schema_{set,get}_context() calls.  The normal way to use
 *    these pointers is to check each record's schema's context
 *    pointer.  If it is NULL, it is a previously unencountered
 *    schema.  In this case, the program can examine the schema,
 *    determine what fields are important, and stash any important
 *    data in the context pointer.  If it is non-NULL, the information
 *    you need is right there.
 *
 *    One can also deal with new schemas as they are created by the
 *    stream by setting a schema callback function using
 *    sk_fixstream_set_schema_cb().  This function will be called
 *    every time the stream encounters a new schema, before any record
 *    data has been read.
 *
 *    When writing, it is often necessary to create a new schema
 *    and/or record.  There are two ways to create new schemas.  The
 *    first is to call sk_schema_create().  This can either be called
 *    with a information element specification, or with NULL.  In the
 *    first case this will create the fields in the specification and
 *    add them to the newly created schema.  In the latter case it
 *    will create an empty schema.  The second way to create a new
 *    schema is to copy an existing one with sk_schema_copy().  In
 *    both the above cases, the resulting schema can be modified with
 *    sk_schema_insert_field_by_{name,id}() and
 *    sk_schema_remove_field().
 *
 *    Schemas can exist in an frozen or unfrozen state.  Any schema
 *    created by a method in the preceding paragraph will be created
 *    in an unfrozen state.  When a schema is frozen, it will call all
 *    of its fields' initialization functions, and the schema's field
 *    offsets and record lengths will be calculated.  Only unfrozen
 *    schemas can be modified by sk_schema_insert_field_by_{name,id}()
 *    and sk_schema_remove_field().  Schemas can be frozen by calling
 *    sk_schema_freeze().
 *
 *    Schemas are reference counted.  When a schema is created, it has
 *    a reference count of one.  Calling sk_schema_destroy() will
 *    decrement a schema's reference counter, and destroy it if the
 *    count reaches zero.  Creating a record from a schema will
 *    increment the refcount.  Destroying a record or calling
 *    sk_fixrec_free_data() will call sk_schema_destroy() on the
 *    contained schema.  The refcount can be manually incremented by
 *    calling sk_schema_clone().
 *
 *    Records can exist on the stack, as a static variable, or
 *    dynamically allocated via sk_fixrec_create().  If a record is on
 *    the stack or is a static variable, sk_fixrec_init() must be
 *    called on it in order to initialize internal pointers before it
 *    can be used.  When done with a record, the record should be
 *    destroyed with sk_fixrec_destroy().  If the record was created
 *    using sk_fixrec_create(), sk_fixrec_destroy() will free the
 *    record and any associated data (including decrementing the
 *    schema refcount).  If the record was initialized using
 *    sk_fixrec_init(), sk_fixrec_destroy() will free the underlying
 *    data, and the record will be left in an uninitialized state.
 *
 *    Records can be duplicated using sk_fixrec_copy(), or copied to
 *    an existing (initialized) record with sk_fixrec_copy_into().
 *    Data from one record can be copied to another record of
 *    differing schema by using a mapping created by
 *    sk_schemamap_create_across_schemas().
 *
 *    Schema fields are added to schemas using
 *    sk_schema_insert_field_by_ident() and or
 *    sk_schema_insert_field_by_name().  Fields are principally
 *    described by their embedded information element
 *    (sk_field_get_ie()).  After added to a schema, (but before the
 *    schema is frozen), their size can be modified by
 *    sk_field_set_length().
 *
 *    Each field also contains a set of field data described by a
 *    sk_field_ops_t.  If every element of an sk_field_ops_t is zero,
 *    the behavior of the field will be governed by reasonable
 *    defaults based on the data type of the field.  Otherwise these
 *    elements can be used to change the behavior or the field or to
 *    implement plug-in fields.  See the documentation of
 *    sk_field_ops_t for more information.
 *
 *    IMPLEMENTATION NOTES
 *
 *    TODO: Figure out ownership of information models.
 */


/*** Types ***/

enum sk_schema_err_en {
    /** Success */
    SK_SCHEMA_ERR_SUCCESS         = 0,

    /** Memory failure */
    SK_SCHEMA_ERR_MEMORY          = -1,

    /** Fixbuf error */
    SK_SCHEMA_ERR_FIXBUF          = -2,

    /** Attempt to modify a frozen schema */
    SK_SCHEMA_ERR_FROZEN          = -3,

    /** Illegal operation on an unfrozen schema */
    SK_SCHEMA_ERR_NOT_FROZEN      = -4,

    /** IE cannot be found in the information model */
    SK_SCHEMA_ERR_UNKNOWN_IE      = -5,

    /** Field could not be found in the schema */
    SK_SCHEMA_ERR_FIELD_NOT_FOUND = -6,

    /**
     * An operation could not be done on two fields because their
     * types are incompatible.
     */
    SK_SCHEMA_ERR_INCOMPATIBLE    = -7,

    /** The function was called on the wrong type of field */
    SK_SCHEMA_ERR_BAD_TYPE        = -8,

    /** The field has an unsupported size */
    SK_SCHEMA_ERR_BAD_SIZE        = -9,

    /** IPv6 could not be converted to IPv4 */
    SK_SCHEMA_ERR_NOT_IPV4        = -10,

    /** A field was truncated on copy */
    SK_SCHEMA_ERR_TRUNCATED       = -11,

    /**
     * The underlying IPFIX boolean value was not true (1) or false
     * (2)
     */
    SK_SCHEMA_ERR_UNKNOWN_BOOL    = -12,

    /** Record does not have a schema */
    SK_SCHEMA_ERR_NO_SCHEMA       = -13,

    /** An unspecified error */
    SK_SCHEMA_ERR_UNSPECIFIED     = -127
};



/**
 *    The type for IPFIX records.
 */
struct sk_fixrec_st {
    const sk_schema_t  *schema;
    uint8_t            *data;
    uint8_t             flags;
};
/*  typedef struct sk_fixrec_st sk_fixrec_t; // silk_types.h */


/**
 *    The type of error return values.  0 for success, non-zero for
 *    failure.  This can be an enum once we have figured out what the
 *    errors can be.
 */
typedef int sk_schema_err_t;

#ifndef SK_FIELD_IDENT_CREATE
/**
 *    An information element identifier, comprising an enterpriseId
 *    (a.k.a, a Private Enterprise Number or PEN) and an elementId.
 *    An enterpriseId of 0 indicates a standard information element.
 *
 *    Currently implemented as a uint64_t, with the enterpriseId in
 *    the high 32 bits, and the elementId in the low 16 bits.
 */
typedef uint64_t sk_field_ident_t;
#endif

/**
 *    The type of the identifier used to store and retrieve context
 *    pointers from a schema.
 */
typedef size_t sk_schema_ctx_ident_t;

/**
 *    An invalid content identifier
 */
#define SK_SCHEMA_CTX_IDENT_INVALID SIZE_MAX

#ifndef SK_FIELD_IDENT_CREATE
/**
 *    Create an sk_field_ident_t from a PEN/ID pair.
 */
#define SK_FIELD_IDENT_CREATE(_pen_, _id_) \
    ((((uint64_t)(_pen_)) << 32) | ((_id_) & 0x7fff))

/**
 *    Return the PEN from an sk_field_ident_t, as a uint32_t.
 */
#define SK_FIELD_IDENT_GET_PEN(_ident_)                 \
    ((uint32_t)(((sk_field_ident_t)(_ident_)) >> 32))

/**
 *    Return the ID from an sk_field_ident_t, as a uint16_t.
 */
#define SK_FIELD_IDENT_GET_ID(_ident_)  ((uint16_t)((_ident_) & 0x7fff))
#endif  /* #ifndef SK_FIELD_IDENT_CREATE */

/**
 *    A field consists of name and type information represented by an
 *    fbInfoElement_t, and a set of basic operations represented by an
 *    sk_field_ops_t.  A field also contains an offset which is
 *    maintained by any owning schema.
 */
/* typedef struct sk_field_st sk_field_t;  // silk_types.h */

/**
 *    Field configuration.  See advanced documentation at the bottom
 *    of this file.
 */
struct sk_field_ops_st;
typedef struct sk_field_ops_st sk_field_ops_t;

/**
 *    A schema represents a set of fields and maintains the fields'
 *    offsets.  Schemas are reference counted, with
 *    sk_schema_destroy() decrementing the count, and
 *    sk_schema_clone() incrementing the count.  A schema can have
 *    multiple copies of the same field.
 */
/* typedef struct sk_schema_st sk_schema_t; */

/**
 *    Represents data on how to map/copy data between fields in schemas.
 */
struct sk_schemamap_st;
typedef struct sk_schemamap_st sk_schemamap_t;

/**
 *    Stores the location of time fields in a schema and can be used
 *    to modify a record's time fields.  See description at
 *    sk_schema_timemap_create().
 */
typedef struct sk_schema_timemap_st sk_schema_timemap_t;

/**
 *    A record consists of data, and a schema describing the data.
 *    The fundamental record type is rwRec.  In this API it is
 *    typedef-ed to sk_fixrec_t in order to emphasize that it is being
 *    used differently from a traditional rwRec.
 *
 *    Schemas are referenced by records.  When a record is destroyed,
 *    the schema's refcount is decremented.
 */
/* typedef rwRec sk_fixrec_t; */


/**
 *  Represents an NTP date-time field, which is a number of fractional
 *  seconds since an epoch date-time of Jan 1, 1900.  Each 'fraction'
 *  is a number of 1/(2^32) second intervals.  A value of 2^32
 *  represents one second.
 */
typedef uint64_t sk_ntp_time_t;


/**
 *    Return the number of integer seconds in an sl_ntp_time_t.
 */
#define SK_NTP_TIME_SECONDS(t) ((t) >> 32)

/**
 *    Return the number of fractional seconds in an sk_ntp_time_t.
 */
#define SK_NTP_TIME_FRACTIONAL(t) ((t) & 0xffffffff)


/*
 *    Value for sk_fixrec_t.fields: When set, the record was allocated
 *    and should be freed by sk_fixrec_destroy()
 */
#define SK_FIXREC_ALLOCATED         0x01

#if 0
/*
 *    Value for sk_fixrec_t.fields: When set, the record is a SiLK4
 *    style record.
 */
#define SK_FIXREC_SCHEMA_REC        0x02

/*
 *    Value for sk_fixrec_t.fields: When set, the silkrec portion of
 *    the record is not valid; that is, the record is an IPFIX record
 *    that has not been converted to the SiLK format.
 */
#define SK_FIXREC_SILKREC_INVALID   0x04
#endif  /* 0 */

/*
 *    Value for sk_fixrec_t.fields: When set, the data pointer is not
 *    owned by the record and must not be freed.
 */
#define SK_FIXREC_FOREIGN_DATA      0x08

/*
 *    Value for sk_fixrec_t.fields: When set, the varfields are owned
 *    by fixbuf and must not be freed by sk_fixrec_destroy() and
 *    sk_fixrec_clear().  Any plug-in/computed fields should always be
 *    freed by those functions.
 */
#define SK_FIXREC_FIXBUF_VARDATA    0x10


/*** Fields ***/

/**
 *    Sets the size of the field's value in memory.  This can raise
 *    errors for sizes that make no sense.  (See IPFIX specification
 *    for reduced length encoding.)
 */
sk_schema_err_t
sk_field_set_length(
    sk_field_t         *field,
    uint16_t            size);

/**
 *    Return the fbInfoElement_t from a field.  This IE always returns
 *    a canonical IE with a name element.
 */
const fbInfoElement_t *
sk_field_get_ie(
    const sk_field_t   *field);

/**
 *    Return the name of the given field.
 */
const char *
sk_field_get_name(
    const sk_field_t   *field);

/**
 *    Return the description of the given field.
 */
const char *
sk_field_get_description(
    const sk_field_t   *field);

/**
 *    Return the sk_field_ops_t from a field for modification.
 */
sk_field_ops_t *
sk_field_get_ops(
    sk_field_t         *field);

/**
 *   Return the length of the field.  Returns 65535 (0xffff) for a
 *   varlen field.
 */
uint16_t
sk_field_get_length(
    const sk_field_t   *field);


/**
 *    Return the field identifier (PEN/ID) of the field.
 */
sk_field_ident_t
sk_field_get_ident(
    const sk_field_t   *field);

/**
 *    Return the private enterprise number of the field.  Returns 0 if
 *    there is no PEN.
 */
uint32_t
sk_field_get_pen(
    const sk_field_t   *field);

/**
 *    Return the id of the field.
 */
uint16_t
sk_field_get_id(
    const sk_field_t   *field);

/**
 *    Return the data type of the field.
 */
uint8_t
sk_field_get_type(
    const sk_field_t   *field);

/**
 *    Return a static C string specifying the data type of the field.
 */
const char *
sk_field_get_type_string(
    const sk_field_t   *field);

/**
 *    Return the data type semantics of the field.
 */
uint8_t
sk_field_get_semantics(
    const sk_field_t   *field);

/**
 *    Return the units value of the field.
 */
uint16_t
sk_field_get_units(
    const sk_field_t   *field);

/**
 *    Return the maximum legal value for the field (0 means undefined)
 */
uint64_t
sk_field_get_max(
    const sk_field_t   *field);

/**
 *    Return the minimum legal value for the field
 */
uint64_t
sk_field_get_min(
    const sk_field_t   *field);

/**
 *    Print to the standard error information about the field.
 *    Primary used for debugging.
 */
void
sk_field_print_debug(
    const sk_field_t   *field);


/*** Schemas ***/

/**
 *    Create and return an schema from a fixbuf IE specification,
 *    looking up the names in 'model'.  If 'spec' is NULL, return an
 *    empty schema, and ignore flags.  If flags is non-zero and spec
 *    is non-NULL, create new fields for the elements from the
 *    specification whose flags field is either zero, or contains all
 *    the bits set in 'flags'.
 */
#ifndef SKSCHEMA_CREATE_TRACE
sk_schema_err_t
sk_schema_create(
    sk_schema_t               **schema,
    fbInfoModel_t              *model,
    const fbInfoElementSpec_t  *spec,
    uint32_t                    flags);
#else
/*    Create sk_schema_create() as a macro to the following function
 *    which reports from where the function was called. */
sk_schema_err_t
sk_schema_create_trace(
    sk_schema_t               **schema,
    fbInfoModel_t              *model,
    const fbInfoElementSpec_t  *spec,
    uint32_t                    flags,
    const char                 *filename,
    int                         linenum);
#define sk_schema_create(m_1, m_2, m_3, m_4)            \
    sk_schema_create_trace((m_1), (m_2), (m_3), (m_4),  \
                           __FILE__, __LINE__)
#endif  /* SKSCHEMA_CREATE_TRACE */


/**
 *    Create and return a schema from a fixbuf template and model.
 *    The resulting schema is not yet frozen and may be modified.
 *
 *    The fixbuf template object used by the created schema is
 *    different from the 'tmpl' parameter passed to this function.
 *
 *    See also sk_schema_wrap_template().
 */
sk_schema_err_t
sk_schema_create_from_template(
    sk_schema_t       **schema,
    fbInfoModel_t      *model,
    fbTemplate_t       *tmpl);

/**
 *    Create a schema from a fixbuf template and model and immediately
 *    freeze the schema.
 *
 *    The fixbuf templated used by the created schema is identical to
 *    the 'tmpl' argument passed to this function.
 *
 *    See also sk_schema_create_from_template().
 */
sk_schema_err_t
sk_schema_wrap_template(
    sk_schema_t       **schema,
    fbInfoModel_t      *model,
    fbTemplate_t       *tmpl,
    uint16_t            tid);

/**
 *    Increment the reference count of 'schema' and return 'schema'.
 */
#ifndef SKSCHEMA_CREATE_TRACE
const sk_schema_t *
sk_schema_clone(
    const sk_schema_t  *schema);
#else
/*    Create sk_schema_clone() as a macro to the following function
 *    which reports from where the function was called. */
const sk_schema_t *
sk_schema_clone_trace(
    const sk_schema_t          *schema,
    const char                 *filename,
    int                         linenum);
#define sk_schema_clone(m_1)                            \
    sk_schema_clone_trace((m_1), __FILE__, __LINE__)
#endif  /* SKSCHEMA_CREATE_TRACE */

/**
 *    Decrement the reference count of 'schema'.  Destroys the schema
 *    if the reference count reaches zero.  Return 1 if the reference
 *    count reached zero, otherwise return 0.  If the schema was NULL,
 *    return -1.
 */
int
sk_schema_destroy(
    const sk_schema_t  *schema);

/**
 *    Copy 'schema' into 'schema_copy'.  This is a new schema with a
 *    refcount of 1.  Does not copy the context pointer or context
 *    free function.
 */
sk_schema_err_t
sk_schema_copy(
    sk_schema_t       **schema_copy,
    const sk_schema_t  *schema);

/**
 *    Return non-zero if the schema is frozen.  Return zero otherwise.
 */
int
sk_schema_is_frozen(
    sk_schema_t        *schema);


/**
 *    Freezes a 'schema'.  This needs to be done before the schema is
 *    added to a record, or a record is created from a schema.  This
 *    also calls all the initialization functions in the schema's
 *    fields.  Calling sk_schema_freeze() on a previously frozen
 *    schema is acceptable and has no effect.
 *
 *    Return SK_SCHEMA_ERR_FIXBUF if there is a fixbuf error.
 */
sk_schema_err_t
sk_schema_freeze(
    sk_schema_t        *schema);

/**
 *    Insert a new field within a schema.  The 'ident' is looked up
 *    within the schema's information model.  If 'ops' is NULL, a
 *    standard set of operations is used based on the field type.  The
 *    new field is inserted before the field 'before'.  If 'before' is
 *    NULL, the field is appended to the schema.  Return the new field
 *    in 'field'.
 *
 *    Return SK_SCHEMA_ERR_FROZEN if the schema is frozen,
 *    SK_SCHEMA_ERR_UNKNOWN_IE if the IE cannot be found in the
 *    information model, or SK_SCHEMA_ERR_FIELD_NOT_FOUND if 'before'
 *    is not in the schema.
 */
sk_schema_err_t
sk_schema_insert_field_by_ident(
    sk_field_t            **field,
    sk_schema_t            *schema,
    sk_field_ident_t        ident,
    const sk_field_ops_t   *ops,
    const sk_field_t       *before);

/**
 *    Insert a new field within a schema.  The 'pen' and 'id' are
 *    looked up within the schema's information model.  If 'pen' == 0,
 *    use elements from the standard model.  If 'ops' is NULL, a
 *    standard set of operations is used based on the field type.  The
 *    new field is inserted before the field 'before'.  If 'before' is
 *    NULL, the field is appended to the schema.  Return the new field
 *    in 'field'.
 *
 *    Return SK_SCHEMA_ERR_FROZEN if the schema is frozen,
 *    SK_SCHEMA_ERR_UNKNOWN_IE if the IE cannot be found in the
 *    information model, or SK_SCHEMA_ERR_FIELD_NOT_FOUND if 'before'
 *    is not in the schema.
 *
 *    sk_schema_insert_field_by_id() is deprecated.  Use
 *    sk_schema_insert_field_by_ident() instead.
 */
#define sk_schema_insert_field_by_id(_field_, _schema_, _pen_, _id_,    \
                                     _ops_, _before_)                   \
    sk_schema_insert_field_by_ident(                                    \
        (_field_), (_schema_), SK_FIELD_IDENT_CREATE((_pen_), (_id_)),  \
        (_ops_), (_before_))

/**
 *    Insert a new field within a schema.  The 'name' is looked up
 *    within the schema's information model.  If 'ops' is NULL, a
 *    standard set of operations is used based on the field type.  The
 *    new field is inserted before the field 'before'.  If 'before' is
 *    NULL, the field is appended to the schema.  Return the new field
 *    in 'field'.
 *
 *    Return SK_SCHEMA_ERR_FROZEN if the schema is frozen,
 *    SK_SCHEMA_ERR_UNKNOWN_IE if the IE cannot be found in the
 *    information model, or SK_SCHEMA_ERR_FIELD_NOT_FOUND if 'before'
 *    is not in the schema.
 */
sk_schema_err_t
sk_schema_insert_field_by_name(
    sk_field_t            **field,
    sk_schema_t            *schema,
    const char             *name,
    const sk_field_ops_t   *ops,
    const sk_field_t       *before);

/**
 *    Remove 'field' from 'schema' and destroy 'field'.
 *
 *    Return SK_SCHEMA_ERR_FROZEN if the schema is frozen, or
 *    SK_SCHEMA_ERR_FIELD_NOT_FOUND if 'field' is not in the schema.
 */
sk_schema_err_t
sk_schema_remove_field(
    sk_schema_t        *schema,
    const sk_field_t   *field);


/**
 *    Set the template ID associated with the given schema.
 *
 *    Return SK_SCHEMA_ERR_FROZEN if the schema is frozen.
 */
sk_schema_err_t
sk_schema_set_tid(
    sk_schema_t        *schema,
    uint16_t            tid);

/**
 *    Return the information model being used by a schema.
 */
fbInfoModel_t *
sk_schema_get_infomodel(
    const sk_schema_t  *schema);

/**
 *    Set the memory referenced by the 'tmpl' and 'tid' parameters to
 *    the template and template ID associated with the given schema,
 *    respectively.  Either or both of 'tmpl' and 'tid' may be NULL.
 *
 *    Although the fbTemplate_t returned is non-const, it should be
 *    considered const, and should not be modified by the caller.  The
 *    template ID returned is the one set using sk_schema_set_tid(),
 *    and is FB_TID_AUTO by default.
 *
 *    Return SK_SCHEMA_ERR_NOT_FROZEN if the schema has not been
 *    frozen.  The 'tmpl' and 'tid' parameters are set regardless of
 *    whether the schema is frozen.
 */
sk_schema_err_t
sk_schema_get_template(
    const sk_schema_t  *schema,
    fbTemplate_t      **tmpl,
    uint16_t           *tid);

/**
 *    Return the record length of a schema.  Return zero for an
 *    unfrozen schema.
 */
size_t
sk_schema_get_record_length(
    const sk_schema_t  *schema);


/**
 *    Return the number of fields in 'schema'.
 */
uint16_t
sk_schema_get_count(
    const sk_schema_t  *schema);


/**
 *    Return the 'index'th field of 'schema'.  Returns NULL if index
 *    is out of bounds.
 */
const sk_field_t *
sk_schema_get_field(
    const sk_schema_t  *schema,
    uint16_t            index);

/**
 *    Return the field with the given 'ident' in 'schema'.
 *
 *    If 'from' is NULL, return the first field in 'schema' that
 *    matches.  If 'from' is non-NULL, return the first field *after*
 *    'from' in 'schema' that matches.  Return NULL if there is no
 *    such field.
 */
const sk_field_t *
sk_schema_get_field_by_ident(
    const sk_schema_t  *schema,
    sk_field_ident_t    ident,
    const sk_field_t   *from);

/**
 *    Return the field with the given 'pen' and 'id' in 'schema'.  A
 *    PEN of zero means no PEN, that is, from the the standard IPFIX
 *    information model.
 *
 *    If 'from' is NULL, return the first field in 'schema' that
 *    matches.  If 'from' is non-NULL, return the first field *after*
 *    'from' in 'schema' that matches.  Return NULL if there is no
 *    such field.
 *
 *    sk_schema_get_field_by_id() is deprecated.  Use
 *    sk_schema_get_field_by_ident() instead.
 */
#define sk_schema_get_field_by_id(_schema_, _pen_, _id_, _from_)        \
    sk_schema_get_field_by_ident(                                       \
        (_schema_), SK_FIELD_IDENT_CREATE((_pen_), (_id_)), (_from_))

/**
 *    Return the field named 'name' in 'schema'.
 *
 *    If 'from' is NULL, return the first field in 'schema' that
 *    matches.  If 'from' is non-NULL, return the first field *after*
 *    'from' in 'schema' that matches.  Return NULL if there is no
 *    such field.
 */
const sk_field_t *
sk_schema_get_field_by_name(
    const sk_schema_t  *schema,
    const char         *name,
    const sk_field_t   *from);

/**
 *    Return 1 if 'schema_a' and 'schema_b' match and leave the value
 *    reference by 'mismatch' unchanged.
 *
 *    Return 0 if 'schema_a' and 'schema_b' do not match and set the
 *    value reference by 'mismatch' to the field that differs between
 *    the schemas.  If 'mismatch' is NULL the position is not
 *    returned.  The first position is 0.
 *
 *    Two schemas are considered a match if they contain the same
 *    number of fields and for each field in each schema the fields
 *    are for the same information element and the lengths of the
 *    fields are the same.
 *
 *    Return 0 and leave 'mismatch' unchanged when 'schema_a' or
 *    'schema_b' is NULL or if 'schema_a' and 'schema_b' have
 *    different information models.
 */
int
sk_schema_matches_schema(
    const sk_schema_t  *schema_a,
    const sk_schema_t  *schema_b,
    uint16_t           *mismatch);


/**
 *    Set 'ident' to a unique identifier that can be used to store and
 *    retrieve context pointers from a schema.
 *
 *    The value in 'ident' is only modified when its value is
 *    SK_SCHEMA_CTX_IDENT_INVALID.  Otherwise the value is not changed.
 *
 *    When multiple threads call this function and reference the same
 *    'ident' location, all threads get the same ident value.
 */
void
sk_schema_context_ident_create(
    sk_schema_ctx_ident_t  *ident);

/**
 *    Set a context pointer and context free function for a schema.
 *    Calls any existing context free function before setting a new
 *    one.
 */
void
sk_schema_set_context(
    const sk_schema_t      *schema,
    sk_schema_ctx_ident_t   ident,
    void                   *ctx,
    void                  (*ctx_free)(void *));

/**
 *    Get the context pointer from a schema.
 */
void *
sk_schema_get_context(
    const sk_schema_t      *schema,
    sk_schema_ctx_ident_t   ident);

/**
 *    Return the string representation of a schema error code.
 */
const char *
sk_schema_strerror(
    sk_schema_err_t     errcode);


/*** Records ***/

/**
 *    Initialize a record for use and mark the record with a flag
 *    specifying that sk_fixrec_destroy() should destroy the record's
 *    data but not the record itself.  This function generally should
 *    be called on a variable if it is a static variable or is
 *    declared on the stack.
 *
 *    When 'schema' is provided, initializing a record allocates an
 *    internal buffer of a size sufficient to hold record data
 *    associated with that schema.  The schema refcount is incremented
 *    by this call.
 *
 *    A NULL 'schema' initializes a record without a schema.  This is
 *    useful when the record is due to be the destination of an
 *    sk_fixrec_copy_into() call.
 *
 *    Return SK_SCHEMA_ERR_NOT_FROZEN if the schema is not frozen.
 */
sk_schema_err_t
sk_fixrec_init(
    sk_fixrec_t        *rec,
    const sk_schema_t  *schema);

/**
 *    Allocate a record and assign a schema.  The record is
 *    initialized.  Increments the schema's refcount.  Return
 *    SK_SCHEMA_ERR_NOT_FROZEN if the schema is not frozen.
 *
 *    A later call to sk_fixrec_destroy() destroys both the data
 *    contained by this record and the record itself.  Contrast with
 *    sk_fixrec_init().
 */
sk_schema_err_t
sk_fixrec_create(
    sk_fixrec_t       **rec,
    const sk_schema_t  *schema);

/**
 *    Free a record and/or a record's data.  If a record was allocated
 *    with sk_fixrec_create(), frees both the data and the record.  If
 *    it was initialied using sk_fixrec_init(), frees the record's
 *    data, but not the record itself.  This function decrements the
 *    refcount of the included schema.
 *
 *    As an exception, if the record's data was set with
 *    sk_fixrec_set_data(), the underlying data pointer is not freed.
 */
void
sk_fixrec_destroy(
    sk_fixrec_t        *rec);

/**
 *    Change the underlying data pointer within the record to point to
 *    the raw data pointed to by 'data'.  This data should be in a
 *    format described by the record's schema.  This data is not
 *    considered to be owned by the record, and it is not freed by an
 *    sk_fixrec_destroy() call.  sk_fixrec_set_data() frees any
 *    existing data owned by the record before changing its data
 *    pointer.
 *
 *    FIXME: Currently, behavior is undefined if the new data contains
 *    non-zero length varlen data.
 */
void
sk_fixrec_set_data(
    sk_fixrec_t        *rec,
    void               *data);

/**
 *    Zeroes the data within a record that the record owns.  If the
 *    data was set by sk_fixrec_set_data(), no action is taken.
 *
 *    FIXME: doesn't properly handle list data
 */
void
sk_fixrec_clear(
    sk_fixrec_t        *rec);

/**
 *    Return the schema associated with 'rec'.
 *
 *    The caller should call sk_schema_clone() to increment the
 *    schema's reference count if the caller plans to maintain a
 *    handle to the schema.
 */
const sk_schema_t *
sk_fixrec_get_schema(
    const sk_fixrec_t  *rec);

/**
 *    Allocate a new record, copy the record in 'src' into that new
 *    record, and store the record in the location referenced by
 *    'dest'.  This is a full copy.  Increfs the underlying schema.
 *
 *    To copy a record without allocating a new record, use
 *    sk_fixrec_copy_into().
 */
sk_schema_err_t
sk_fixrec_copy(
    sk_fixrec_t       **dest,
    const sk_fixrec_t  *src);

/**
 *    Copy the record in 'src' into 'dest'.  'dest' must have been
 *    previously initialized either by sk_fixrec_create() or
 *    sk_fixrec_init().  This is a full copy.  Increfs the underlying
 *    schema.
 *
 *    Any existing data owned by 'dest' is freed before the copy,
 *    and its schema is decreffed.
 *
 *    If 'src' and 'dest' are the same record, no change is made and
 *    SK_SCHEMA_ERR_SUCCESS is returned.
 */
sk_schema_err_t
sk_fixrec_copy_into(
    sk_fixrec_t        *dest,
    const sk_fixrec_t  *src);

sk_schema_err_t
sk_fixrec_update_computed(
    sk_fixrec_t        *rec);

/**
 *    Fills a buffer 'dest' of size 'size' with a textual
 *    representation of 'field' in 'rec'.  The result is truncated
 *    (and still zero-terminated) if 'dest' is not large enough.
 */
sk_schema_err_t
sk_fixrec_data_to_text(
    const sk_fixrec_t  *rec,
    const sk_field_t   *field,
    char               *dest,
    size_t              size);

/**
 *    Compares 'field_a' in 'rec_a' to 'field_a' in 'rec_b' using
 *    'field_a's comparison logic.  Returns the comparison in 'cmp':
 *    negative for a < b, 0 for a == b, positive for a > b.
 *
 *    If the fields are not of compatible types, an error is returned.
 *    Ipv4Address and Ipv6Address are compatible.  All date times are
 *    compatible.  Otherwise a field is only compatible if the field
 *    type is identical.  This can be overridden by providing your own
 *    comparison function in the field's ops structure.
 *
 *    Return SK_SCHEMA_ERR_INCOMPATIBLE if the two fields are not
 *    compatible.
 */
sk_schema_err_t
sk_fixrec_data_compare(
    const sk_fixrec_t  *rec_a,
    const sk_field_t   *field_a,
    const sk_fixrec_t  *rec_b,
    const sk_field_t   *field_b,
    int                *cmp);

/**
 *    Modifies 'dest_field' in 'dest_rec' to include data from
 *    'src_field' in 'src_rec' using the logic in 'dest_field'.
 *
 *    If the fields are not of equal types, or if the field is not
 *    numeric, SK_SCHEMA_ERR_INCOMPATIBLE is returned.  This can be
 *    overridden by providing your own merge function in the field's
 *    ops structure.
 */
sk_schema_err_t
sk_fixrec_data_merge(
    sk_fixrec_t        *dest_rec,
    const sk_field_t   *dest_field,
    const sk_fixrec_t  *src_rec,
    const sk_field_t   *src_field);

/**
 *    Take a record whose data field was filled in by a call to
 *    fBufNext() and copy all fbVarfield_t data into buffers owned by
 *    the record.
 */
sk_schema_err_t
sk_fixrec_copy_fixbuf_data(
    sk_fixrec_t        *rec);

/**
 *    Add to 'session' the templates used by 'record', by all list
 *    elements 'record' contains, and recursively all sub-list
 *    elements those lists contain.  If 'add_to_internal' is true, the
 *    templates are added to the internal side of 'session'; otherwise
 *    they are added to the external side of 'session'.
 *
 *    If a template uses a TID that is already in use by 'session',
 *    the old template is removed and replaced with the new template
 *    unless the template pointers are equal.
 */
sk_schema_err_t
sk_fixrec_export_templates(
    const sk_fixrec_t  *record,
    fbSession_t        *session,
    unsigned int        add_to_internal);

/**
 *    Ensure the templates used by the list elements of 'rec' are
 *    added to the record's schema's session.
 *
 *    FIXME: Rename this function.
 */
sk_schema_err_t
sk_fixrec_copy_list_templates(
    const sk_fixrec_t  *rec);


/*** Record mapping ***/

/**
 *    Return a schema mapping that can be used to copy fields between
 *    records.  The vector 'src_dest_pairs' must be a vector
 *    containing an even number of elements of type "const sk_field_t
 *    *".  The vector should be treated as a list of pairs, with the
 *    first item describing a source, and the second item describing a
 *    destination.  The resulting mapping is used to copy each source
 *    field from a record to its designated destination field in
 *    another (or the same) record.  Once a mapping is created, it can
 *    be used by calling the sk_schemamap_apply() function.
 *
 *    Mappings can generally only map fields of a given type to fields
 *    of the same type.  The exceptions are: list fields cannot be
 *    copied using mappings, any datetime field may be mapped to any
 *    other datetime field, and float32 and float64 can mapped between
 *    each other.  Attempting to map two fields that can't be mapped
 *    results in an SK_SCHEMA_ERR_BAD_TYPE error.
 *
 *    Mappings properly handle fields that are of the same type, but
 *    different lengths.  However, if an integer, string, or octet
 *    array field would be copied to a destination with a shorter
 *    length, the mapping truncates the values upon copying, and an
 *    SK_SCHEMA_ERR_TRUNCATED warning is returned.
 *
 *    Return SK_SCHEMA_ERR_UNSPECIFIED when the vector contains an odd
 *    number of elements or the size of elements in the vector is not
 *    correct.
 */
sk_schema_err_t
sk_schemamap_create_across_fields(
    sk_schemamap_t    **map,
    const sk_vector_t  *src_dest_pairs);

/**
 *    Create a schema mapping that can be used by sk_schemamap_apply()
 *    to copy fields from 'src' to 'dest'.  The resulting map copies
 *    all fields of 'src' that are in 'dest' to matching locations in
 *    'dest'.
 *
 *    This function looks up each field in 'dest' trying to find a
 *    matching field in 'src'.  If such a field is found, 'src's value
 *    is copied to 'dest'.  If any destination field is smaller than
 *    the matching source field, the value is truncated, and
 *    SK_SCHEMA_ERR_TRUNCATED error is returned.  If there are
 *    multiples of a field, the first 'src' field is copied to the
 *    first 'dest' field, second to second, etc.  Fields in 'dest'
 *    that are not found in 'src' are left untouched.
 *
 *    If 'src' and 'dest' have the same schema, calling
 *    sk_schemamap_apply() with the resulting map is equivalent to
 *    calling sk_fixrec_copy_into().
 *
 *    Return SK_SCHEMA_ERR_NOT_FROZEN if either 'dest' or 'src' are
 *    not frozen.
 */
sk_schema_err_t
sk_schemamap_create_across_schemas(
    sk_schemamap_t    **map,
    const sk_schema_t  *dest,
    const sk_schema_t  *src);

/**
 *    Free a schema mapping.
 */
void
sk_schemamap_destroy(
    sk_schemamap_t     *map);

/**
 *    Apply the given 'map' from the 'src' record to the 'dest'
 *    record.
 *
 *    If 'src' and 'dest' are the same record, do nothing.
 */
sk_schema_err_t
sk_schemamap_apply(
    const sk_schemamap_t   *map,
    sk_fixrec_t            *dest,
    const sk_fixrec_t      *src);

/**
 *    Create a time mapping object and possibly modify the fields on
 *    'schema'.  Return the time mapping object in the location
 *    referenced by 'timemap'.
 *
 *    A time mapping is used to normalize the time fields in a schema,
 *    guaranteeing that the schema contains the fields
 *    flowStartMilliseconds and flowEndMilliseconds.  The time mapping
 *    records the location of other time fields (e.g.,
 *    flowStartMicroseconds).  When the time mapping is applied to a
 *    record via sk_schema_timemap_apply(), the flowStartMilliseconds
 *    and flowEndMilliseconds fields are set using the values in the
 *    other time fields that exist in the schema.  When the schema
 *    contains no time fields known to this function, the record's
 *    time fields are set to the export time of the record.
 *
 *    Return SK_SCHEMA_ERR_FROZEN if the schema is frozen.
 */
sk_schema_err_t
sk_schema_timemap_create(
    sk_schema_timemap_t   **timemap,
    sk_schema_t            *schema);

/**
 *    Destroy a time mapping object that was created by
 *    sk_schema_timemap_create().  Do nothing if 'timemap' is NULL.
 */
void
sk_schema_timemap_destroy(
    sk_schema_timemap_t    *timemap);

/**
 *    Apply the time mapping 'timemap' to the record 'rec'.
 *    'rec_export_time' should contain the export time of the record
 *    represented as seconds since the UNIX epoch.
 *
 *    After calling this function, 'rec' should have reasonable values
 *    for the fields flowStartMilliseconds and flowEndMilliseconds.
 *    See details on sk_schema_timemap_create().
 *
 *    Return SK_SCHEMA_ERR_INCOMPATIBLE if the schema on 'rec' is
 *    different than the schema used to create 'timemap'.
 */
sk_schema_err_t
sk_schema_timemap_apply(
    const sk_schema_timemap_t  *timemap,
    sk_fixrec_t                *rec,
    uint32_t                    rec_export_time);


/** Generic record/field accessor functions **/

/*
 *    All of the functions below return SK_SCHEMA_ERR_BAD_TYPE if
 *    there is a type mismatch, and SK_SCHEMA_ERR_BAD_SIZE if the
 *    field size is illegal.
 */


/**
 *    Fill 'val' with the length of the given field in bytes.
 */
sk_schema_err_t
sk_fixrec_get_value_length(
    const sk_fixrec_t  *rec,
    const sk_field_t   *field,
    uint16_t           *val);

/**
 *    Fill 'val' with the unsigned integer represented by 'field' in
 *    rec.  This function works with unsigned8, unsigned16,
 *    unsigned32, and unsigned64 field types.
 */
sk_schema_err_t
sk_fixrec_get_unsigned(
    const sk_fixrec_t  *rec,
    const sk_field_t   *field,
    uint64_t           *val);

/**
 *    Fill 'val' with the signed integer represented by 'field' in
 *    rec.  This function works with signed8, signed16, signed32,
 *    signed64, unsigned8, unsigned16, and unsigned32 field types.
 */
sk_schema_err_t
sk_fixrec_get_signed(
    const sk_fixrec_t  *rec,
    const sk_field_t   *field,
    int64_t            *val);

/**
 *    Fill 'val' of size 'val_size' with the unsigned integer
 *    represented by 'field' in rec.  This function works with
 *    unsigned8, unsigned16, unsigned32, and unsigned64 field types.
 */
sk_schema_err_t
sk_fixrec_get_sized_uint(
    const sk_fixrec_t  *rec,
    const sk_field_t   *field,
    void               *val,
    size_t              val_size);

/**
 *    Fill 'val' of size 'val_size' with the signed integer
 *    represented by 'field' in rec.  This function works with
 *    signed8, signed16, signed32, and signed64 field types.
 */
sk_schema_err_t
sk_fixrec_get_sized_int(
    const sk_fixrec_t  *rec,
    const sk_field_t   *field,
    void               *val,
    size_t              val_size);

/**
 *    Fill 'val' with the floating point number represented by 'field'
 *    in rec.  This function works with float32 and float64 field
 *    types.
 */
sk_schema_err_t
sk_fixrec_get_float(
    const sk_fixrec_t  *rec,
    const sk_field_t   *field,
    double             *val);

/**
 *    Fill 'val' with the IP Address represented by 'field' in rec.
 *    This function works with ipv4Address and ipv6Address field
 *    types.
 */
sk_schema_err_t
sk_fixrec_get_ip_address(
    const sk_fixrec_t  *rec,
    const sk_field_t   *field,
    skipaddr_t         *addr);

/**
 *    Fill 'val' with the datetime represented by 'field' in rec.
 *    This function works with dateTimeSeconds, dateTimeMilliseconds,
 *    dateTimeMicroseconds, and dateTimeNanoseconds field types.
 *    dateTimeMicroseconds and dateTimeNanoseconds types is truncated
 *    to fit in an sktime_t.
 */
sk_schema_err_t
sk_fixrec_get_datetime(
    const sk_fixrec_t  *rec,
    const sk_field_t   *field,
    sktime_t           *val);

/**
 *    Fill 'val' with the datetime represented by 'field' in rec.
 *    This function works with dateTimeSeconds, dateTimeMilliseconds,
 *    dateTimeMicroseconds, and dateTimeNanoseconds field types.
 */
sk_schema_err_t
sk_fixrec_get_datetime_ntp(
    const sk_fixrec_t  *rec,
    const sk_field_t   *field,
    sk_ntp_time_t      *val);

/**
 *    Fill 'val' with the datetime represented by 'field' in rec.
 *    This function works with dateTimeSeconds, dateTimeMilliseconds,
 *    dateTimeMicroseconds, and dateTimeNanoseconds field types.
 */
sk_schema_err_t
sk_fixrec_get_datetime_timespec(
    const sk_fixrec_t  *rec,
    const sk_field_t   *field,
    struct timespec    *val);

/**
 *    Fill 'val' with the octets representing 'field' for the record
 *    'rec'.
 *
 *    The parameter 'len' is both an input and an output parameter:
 *    The value it references must contain the length available in the
 *    buffer 'val' on input, and the function modifies it to contain
 *    the length of the field---which may be greater than the value it
 *    contained initially.  When the length of 'field' is greater than
 *    the value of 'len' on input, the first 'len' bytes of the field
 *    are written to 'val' and 'len' is set to value that would be
 *    required to hold 'field' without truncating.
 *
 *    When getting a string value, there is no guarantee that the
 *    value is NULL terminated.
 *
 *    When getting a numeric value, the raw bytes are written to 'val'
 *    and this function makes no effort to properly handle values that
 *    use reduced-length encoding.
 *
 *    'val' and 'len' are unchanged and SK_SCHEMA_ERR_BAD_TYPE is
 *    returned when the type of 'field' is a list type.
 */
sk_schema_err_t
sk_fixrec_get_octets(
    const sk_fixrec_t  *rec,
    const sk_field_t   *field,
    uint8_t            *val,
    uint16_t           *len);


/*** Generic record/field setting functions ***/

/*
 * These functions truncate the given value if the field is too small
 * to hold said value.
 */

/**
 *    Set 'field' in 'rec' to the unsigned integer represented by
 *    'val'.  This function works with unsigned8, unsigned16,
 *    unsigned32, and unsigned64 field types.
 */
sk_schema_err_t
sk_fixrec_set_unsigned(
    sk_fixrec_t        *rec,
    const sk_field_t   *field,
    uint64_t            val);

/**
 *    Set 'field' in 'rec' to the signed integer represented by 'val'.
 *    This function works with signed8, signed16, signed32, and
 *    signed64 field types.
 */
sk_schema_err_t
sk_fixrec_set_signed(
    sk_fixrec_t        *rec,
    const sk_field_t   *field,
    int64_t             val);

/**
 *    Set 'field' in 'rec' to the unsigned integer pointed to by 'val'
 *    of size 'val_size'.  This function works with unsigned8,
 *    unsigned16, unsigned32, and unsigned64 field types.
 */
sk_schema_err_t
sk_fixrec_set_sized_uint(
    sk_fixrec_t        *rec,
    const sk_field_t   *field,
    const void         *val,
    size_t              val_size);

/**
 *    Set 'field' in 'rec' to the signed integer pointed to by 'val'
 *    of size 'val_size'.  This function works with signed8, signed16,
 *    signed32, and signed64 field types.
 */
sk_schema_err_t
sk_fixrec_set_sized_int(
    sk_fixrec_t        *rec,
    const sk_field_t   *field,
    const void         *val,
    size_t              val_size);

/**
 *    Set 'field' in 'rec' to the floating point number represented by
 *    'val'.  This function works with float32 and float64 field
 *    types.
 */
sk_schema_err_t
sk_fixrec_set_float(
    sk_fixrec_t        *rec,
    const sk_field_t   *field,
    double              val);

/**
 *    Set 'field' in 'rec' to the IP address represented by 'val'.
 *    This function works with ipv4Address and ipv6Address field
 *    types.  Return SK_SCHEMA_ERR_NOT_IPV4 when attempting to insert
 *    a non-convertible IPv6 address into an IPv4 address field.
 */
sk_schema_err_t
sk_fixrec_set_ip_address(
    sk_fixrec_t        *rec,
    const sk_field_t   *field,
    const skipaddr_t   *addr);

/**
 *    Set 'field' in 'rec' to the datetime represented by 'val'.  This
 *    function works with dateTimeSeconds, dateTimeMilliseconds,
 *    dateTimeMicroseconds, and dateTimeMilliseconds field types.
 */
sk_schema_err_t
sk_fixrec_set_datetime(
    sk_fixrec_t        *rec,
    const sk_field_t   *field,
    sktime_t            val);

/**
 *    Set 'field' in 'rec' to the datetime represented by 'val'.  This
 *    function works with dateTimeSeconds, dateTimeMilliseconds,
 *    dateTimeMicroseconds, and dateTimeMilliseconds field types.
 */
sk_schema_err_t
sk_fixrec_set_datetime_ntp(
    sk_fixrec_t        *rec,
    const sk_field_t   *field,
    sk_ntp_time_t       val);

/**
 *    Set 'field' in 'rec' to the datetime represented by 'val'.  This
 *    function works with dateTimeSeconds, dateTimeMilliseconds,
 *    dateTimeMicroseconds, and dateTimeMilliseconds field types.
 *    Fractional seconds of finer precision than the field supports
 *    are truncated.
 */
sk_schema_err_t
sk_fixrec_set_datetime_timespec(
    sk_fixrec_t            *rec,
    const sk_field_t       *field,
    const struct timespec  *val);

/**
 *    Set the bytes represented by a string, octetArray, or
 *    macAddress.  'len' is the number of bytes in 'val'.
 *
 *    This function will also return the raw bytes for any non-list
 *    data type.
 *
 *    Returns SK_SCHEMA_ERR_TRUNCATED if the value could not
 *    fit in the field.
 */
sk_schema_err_t
sk_fixrec_set_octets(
    sk_fixrec_t        *rec,
    const sk_field_t   *field,
    const uint8_t      *val,
    uint16_t            len);

/*** Type specific accessor functions ***/

/**
 *    Fill 'val' with the uint8_t represented by 'field' in rec.
 */
sk_schema_err_t
sk_fixrec_get_unsigned8(
    const sk_fixrec_t  *rec,
    const sk_field_t   *field,
    uint8_t            *val);

/**
 *    Fill 'val' with the uint16_t represented by 'field' in 'rec'.
 */
sk_schema_err_t
sk_fixrec_get_unsigned16(
    const sk_fixrec_t  *rec,
    const sk_field_t   *field,
    uint16_t           *val);

/**
 *    Fill 'val' with the uint32_t represented by 'field' in 'rec'.
 */
sk_schema_err_t
sk_fixrec_get_unsigned32(
    const sk_fixrec_t  *rec,
    const sk_field_t   *field,
    uint32_t           *val);

/**
 *    Fill 'val' with the uint64_t represented by 'field' in 'rec'.
 */
sk_schema_err_t
sk_fixrec_get_unsigned64(
    const sk_fixrec_t  *rec,
    const sk_field_t   *field,
    uint64_t           *val);

/**
 *    Fill 'val' with the int8_t represented by 'field' in 'rec'.
 */
sk_schema_err_t
sk_fixrec_get_signed8(
    const sk_fixrec_t  *rec,
    const sk_field_t   *field,
    int8_t             *val);

/**
 *    Fill 'val' with the int16_t represented by 'field' in 'rec'.
 */
sk_schema_err_t
sk_fixrec_get_signed16(
    const sk_fixrec_t  *rec,
    const sk_field_t   *field,
    int16_t            *val);

/**
 *    Fill 'val' with the int32_t represented by 'field' in 'rec'.
 */
sk_schema_err_t
sk_fixrec_get_signed32(
    const sk_fixrec_t  *rec,
    const sk_field_t   *field,
    int32_t            *val);

/**
 *    Fill 'val' with the int64_t represented by 'field' in 'rec'.
 */
sk_schema_err_t
sk_fixrec_get_signed64(
    const sk_fixrec_t  *rec,
    const sk_field_t   *field,
    int64_t            *val);

/**
 *    Fill 'val' with the IPv4 address represented by 'field' in
 *    'rec'.
 */
sk_schema_err_t
sk_fixrec_get_ipv4_addr(
    const sk_fixrec_t  *rec,
    const sk_field_t   *field,
    uint32_t           *val);

/**
 *    Fill 'val' with the 16-byte IPv6 address represented by 'field'
 *    in 'rec'.
 */
sk_schema_err_t
sk_fixrec_get_ipv6_addr(
    const sk_fixrec_t  *rec,
    const sk_field_t   *field,
    uint8_t            *val);

/**
 *    Fill 'val' with the float represented by 'field' in 'rec'.
 */
sk_schema_err_t
sk_fixrec_get_float32(
    const sk_fixrec_t  *rec,
    const sk_field_t   *field,
    float              *val);

/**
 *    Fill 'val' with the double represented by 'field' in 'rec'.
 */
sk_schema_err_t
sk_fixrec_get_float64(
    const sk_fixrec_t  *rec,
    const sk_field_t   *field,
    double             *val);

/**
 *    Fill 'val' with the boolean value represented by 'field' in
 *    'rec'.  (False is 0, true is 1.)
 */
sk_schema_err_t
sk_fixrec_get_boolean(
    const sk_fixrec_t  *rec,
    const sk_field_t   *field,
    int                *val);

/**
 *    Fill 'val' with the 6-byte MAC address represented by 'field' in
 *    'rec'.
 */
sk_schema_err_t
sk_fixrec_get_mac_address(
    const sk_fixrec_t  *rec,
    const sk_field_t   *field,
    uint8_t            *val);

/**
 *    Fill 'val' with the string represented by 'field' in 'rec'.
 *    'len' must be non-NULL and contain the length of the buffer
 *    'val' when called.  The resulting string is truncated if 'val'
 *    is too small, and the result is null-terminated regardless.  The
 *    underlying length of the string (without null) is returned in
 *    'len'.
 */
sk_schema_err_t
sk_fixrec_get_string(
    const sk_fixrec_t  *rec,
    const sk_field_t   *field,
    char               *val,
    uint16_t           *len);

/**
 *    Fill 'val' with the octets represented by 'field' in 'rec'.
 *    'len' must be non-NULL and contain the length of the buffer
 *    'val' when called.  The underlying size of the octet array will
 *    be returned in 'len'.  If the size of the buffer is smaller than
 *    the data, the data is truncated.
 */
sk_schema_err_t
sk_fixrec_get_octet_array(
    const sk_fixrec_t  *rec,
    const sk_field_t   *field,
    uint8_t            *val,
    uint16_t           *len);

/**
 *    Fill 'val' with the number of seconds represented by 'field' in
 *    'rec'.
 */
sk_schema_err_t
sk_fixrec_get_datetime_seconds(
    const sk_fixrec_t  *rec,
    const sk_field_t   *field,
    uint32_t           *val);

/**
 *    Fill 'val' with the number of milliseconds represented by
 *    'field' in 'rec'.
 */
sk_schema_err_t
sk_fixrec_get_datetime_milliseconds(
    const sk_fixrec_t  *rec,
    const sk_field_t   *field,
    uint64_t           *val);

/**
 *    Fill 'val' with the date-time represented by 'field' in 'rec'.
 */
sk_schema_err_t
sk_fixrec_get_datetime_microseconds(
    const sk_fixrec_t  *rec,
    const sk_field_t   *field,
    sk_ntp_time_t      *val);

/**
 *    Fill 'val' with the date-time represented by 'field' in 'rec'.
 */
sk_schema_err_t
sk_fixrec_get_datetime_nanoseconds(
    const sk_fixrec_t  *rec,
    const sk_field_t   *field,
    sk_ntp_time_t      *val);



/** Type specific get functions **/

/**
 *    Set 'field' in 'rec' to 'val'.
 */
sk_schema_err_t
sk_fixrec_set_unsigned8(
    sk_fixrec_t        *rec,
    const sk_field_t   *field,
    uint8_t             val);

/**
 *    Set 'field' in 'rec' to 'val'.
 */
sk_schema_err_t
sk_fixrec_set_unsigned16(
    sk_fixrec_t        *rec,
    const sk_field_t   *field,
    uint16_t            val);

/**
 *    Set 'field' in 'rec' to 'val'.
 */
sk_schema_err_t
sk_fixrec_set_unsigned32(
    sk_fixrec_t        *rec,
    const sk_field_t   *field,
    uint32_t            val);

/**
 *    Set 'field' in 'rec' to 'val'.
 */
sk_schema_err_t
sk_fixrec_set_unsigned64(
    sk_fixrec_t        *rec,
    const sk_field_t   *field,
    uint64_t            val);

/**
 *    Set 'field' in 'rec' to 'val'.
 */
sk_schema_err_t
sk_fixrec_set_signed8(
    sk_fixrec_t        *rec,
    const sk_field_t   *field,
    int8_t              val);

/**
 *    Set 'field' in 'rec' to 'val'.
 */
sk_schema_err_t
sk_fixrec_set_signed16(
    sk_fixrec_t        *rec,
    const sk_field_t   *field,
    int16_t             val);

/**
 *    Set 'field' in 'rec' to 'val'.
 */
sk_schema_err_t
sk_fixrec_set_signed32(
    sk_fixrec_t        *rec,
    const sk_field_t   *field,
    int32_t             val);

/**
 *    Set 'field' in 'rec' to 'val'.
 */
sk_schema_err_t
sk_fixrec_set_signed64(
    sk_fixrec_t        *rec,
    const sk_field_t   *field,
    int64_t             val);

/**
 *    Set 'field' in 'rec' to 'val'.
 */
sk_schema_err_t
sk_fixrec_set_ipv4_addr(
    sk_fixrec_t        *rec,
    const sk_field_t   *field,
    uint32_t            val);

/**
 *    Set 'field' in 'rec' to 'val'.
 */
sk_schema_err_t
sk_fixrec_set_ipv6_addr(
    sk_fixrec_t        *rec,
    const sk_field_t   *field,
    const uint8_t      *val);

/**
 *    Set 'field' in 'rec' to 'val'.
 */
sk_schema_err_t
sk_fixrec_set_float32(
    sk_fixrec_t        *rec,
    const sk_field_t   *field,
    float               val);

/**
 *    Set 'field' in 'rec' to 'val'.
 */
sk_schema_err_t
sk_fixrec_set_float64(
    sk_fixrec_t        *rec,
    const sk_field_t   *field,
    double              val);

/**
 *    Set 'field' in 'rec' to 'val'.  A zero 'val' is considered true.
 *    A non-zero 'val' is considered false.
 */
sk_schema_err_t
sk_fixrec_set_boolean(
    sk_fixrec_t        *rec,
    const sk_field_t   *field,
    int                 val);

/**
 *    Set 'field' in 'rec' to 'val'.  'val' us assumed to be 6 bytes
 *    long.
 */
sk_schema_err_t
sk_fixrec_set_mac_address(
    sk_fixrec_t        *rec,
    const sk_field_t   *field,
    const uint8_t      *val);

/**
 *    Set 'field' in 'rec' to 'val'.  'val' is assumed to be
 *    null-terminated.
 *
 *    Returns SK_SCHEMA_ERR_TRUNCATED if the value could not fit in
 *    the field.
 */
sk_schema_err_t
sk_fixrec_set_string(
    sk_fixrec_t        *rec,
    const sk_field_t   *field,
    const char         *val);

/**
 *    Set 'field' in 'rec' to 'val'.  'len' should hold the length of
 *    'val'.
 *
 *    Returns SK_SCHEMA_ERR_TRUNCATED if the value could not fit in
 *    the field.
 */
sk_schema_err_t
sk_fixrec_set_octet_array(
    sk_fixrec_t        *rec,
    const sk_field_t   *field,
    const uint8_t      *val,
    uint16_t            len);

/**
 *    Set 'field' in 'rec' to 'val'.
 */
sk_schema_err_t
sk_fixrec_set_datetime_seconds(
    sk_fixrec_t        *rec,
    const sk_field_t   *field,
    uint32_t            val);

/**
 *    Set 'field' in 'rec' to 'val'.
 */
sk_schema_err_t
sk_fixrec_set_datetime_milliseconds(
    sk_fixrec_t        *rec,
    const sk_field_t   *field,
    uint64_t            val);

/**
 *    Set 'field' in 'rec' to 'val'.
 */
sk_schema_err_t
sk_fixrec_set_datetime_microseconds(
    sk_fixrec_t        *rec,
    const sk_field_t   *field,
    sk_ntp_time_t       val);

/**
 *    Set 'field' in 'rec' to 'val'.
 */
sk_schema_err_t
sk_fixrec_set_datetime_nanoseconds(
    sk_fixrec_t        *rec,
    const sk_field_t   *field,
    sk_ntp_time_t       val);



/*** Schema IPFIX List Support ***/

/**
 *    A fixlist is an extensible array of records.  These are used to
 *    create and read the data for IPFIX list elements.
 *
 *    To read an IPFIX list on an existing record, use
 *    sk_fixrec_get_list() to create a handle to the list and then
 *    invoke sk_fixlist_next_element() in a loop to visit the elements
 *    in the list.
 *
 *    To create and fill an IPFIX list, first call one of the list
 *    creation functions: sk_fixlist_create_basiclist_from_ident(),
 *    sk_fixlist_create_basiclist_from_name(),
 *    sk_fixlist_create_subtemplatelist(), or
 *    sk_fixlist_create_subtemplatemultilist().  Use
 *    sk_fixlist_append_fixrec() to add records to the list, and
 *    finally call sk_fixrec_set_list() to add that list to a record.
 *    If you wish to use sk_fixlist_next_element() on a list you have
 *    created, invoke sk_fixlist_reset_iter() to initialize the record
 *    list for iteration.
 */
typedef struct sk_fixlist_st sk_fixlist_t;

/**
 *    Creates a read-only record list (sk_fixlist_t) representing an
 *    IPFIX list in the record 'rec' at position 'field' and stores
 *    the record list at the location referenced by 'fixlist'.  Return
 *    SK_SCHEMA_ERR_BAD_TYPE if 'field' is not a list.
 *
 *    The sk_fixlist_t is initialized for iterating over the elements
 *    in the list.  Use sk_fixlist_next_element() to visit the
 *    elements in the list.
 *
 *    The sk_fixlist_t assumes the record is fixed and not being
 *    modified by a separate sk_fixlist_t.
 */
sk_schema_err_t
sk_fixrec_get_list(
    const sk_fixrec_t *rec,
    const sk_field_t  *field,
    sk_fixlist_t     **fixlist);

/**
 *    Create a record list representing a basic list holding IEs whose
 *    enterpriseId and elementId is specified in 'ident' and fill
 *    'list' with a reference to that list.  The 'model' parameter may
 *    not be NULL.
 *
 *    Return SK_SCHEMA_ERR_UNKNOWN_IE and do not create the list if
 *    'ident' does not represent a known information element.
 *
 *    See also sk_fixlist_create_basiclist_from_name(),
 *    sk_fixlist_create_subtemplatelist() and
 *    sk_fixlist_create_subtemplatemultilist().
 */
sk_schema_err_t
sk_fixlist_create_basiclist_from_ident(
    sk_fixlist_t      **list,
    fbInfoModel_t      *model,
    sk_field_ident_t    ident);

/**
 *    Create a record list representing a basic list holding IEs whose
 *    name is specified in 'name' and fill 'list' with a reference to
 *    that list.  The 'model' parameter may not be NULL.
 *
 *    Return SK_SCHEMA_ERR_UNKNOWN_IE and do not create the list if
 *    'name' does not represent a known information element.
 *
 *    See also sk_fixlist_create_basiclist_from_ident(),
 *    sk_fixlist_create_subtemplatelist() and
 *    sk_fixlist_create_subtemplatemultilist().
 */
sk_schema_err_t
sk_fixlist_create_basiclist_from_name(
    sk_fixlist_t      **list,
    fbInfoModel_t      *model,
    const char         *elementName);

/**
 *    Create a record list representing a sub-template list of
 *    elements whose schema is 'schema' and fill 'list' with a
 *    reference to that list.
 *
 *    Return SK_SCHEMA_ERR_NOT_FROZEN and do not create the list if
 *    'schema' is not frozen.
 *
 *    See also sk_fixlist_create_basiclist_from_ident(),
 *    sk_fixlist_create_basiclist_from_name() and
 *    sk_fixlist_create_subtemplatemultilist().
 */
sk_schema_err_t
sk_fixlist_create_subtemplatelist(
    sk_fixlist_t      **list,
    const sk_schema_t  *schema);

/**
 *    Create a record list representing a sub-template multi-list and
 *    fill 'list' with a reference to that list.  If 'model' is NULL,
 *    use the standard information model.
 *
 *    See also sk_fixlist_create_basiclist_from_ident(),
 *    sk_fixlist_create_basiclist_from_name() and
 *    sk_fixlist_create_subtemplatelist().
 */
sk_schema_err_t
sk_fixlist_create_subtemplatemultilist(
    sk_fixlist_t      **list,
    fbInfoModel_t      *model);

/**
 *    Destroy the record list 'list'.  Do nothing if 'list' is NULL.
 *
 *    This function causes the record filled by
 *    sk_fixlist_next_element() to point to freed memory.
 */
void
sk_fixlist_destroy(
    sk_fixlist_t       *list);

/**
 *    Return the semantic value associated with this list.  The
 *    semantic value is one of FB_LIST_SEM_UNDEFINED,
 *    FB_LIST_SEM_NONE_OF, FB_LIST_SEM_EXACTLY_ONE_OF,
 *    FB_LIST_SEM_ONE_OR_MORE_OF, FB_LIST_SEM_ALL_OF,
 *    FB_LIST_SEM_ORDERED.  These are defined by RFC6313 and at
 *    http://www.iana.org/assignments/ipfix/ipfix.xhtml#ipfix-structured-data-types-semantics
 */
uint8_t
sk_fixlist_get_semantic(
    const sk_fixlist_t *list);

/**
 *    Set the semantic value associated with this list.  For valid
 *    semantic values, see sk_fixlist_get_semantic().
 */
void
sk_fixlist_set_semantic(
    sk_fixlist_t       *list,
    uint8_t             semantic);

/**
 *    Return the underlying type of a list.  The return value is one
 *    of FB_BASIC_LIST, FB_SUB_TMPL_LIST, or FB_SUB_TMPL_MULTI_LIST.
 */
int
sk_fixlist_get_type(
    const sk_fixlist_t *list);

/**
 *    Return the number of elements in 'list'.
 */
uint16_t
sk_fixlist_count_elements(
    const sk_fixlist_t *list);

/**
 *    Return the number of schemas in 'list'.  The return value is
 *    always 1 for a basic list or a sub-template list.
 */
uint16_t
sk_fixlist_count_schemas(
    const sk_fixlist_t *list);

/**
 *    Return the 'idx' schema underlying the records in 'list', where
 *    0 is the first schema.
 *
 *    For a basic list, when 'idx' is 0 the return value is a
 *    one-element schema.  Return NULL for any other value of 'idx'.
 *
 *    For a sub-template list, when 'idx' is 0
 *    the return value is the schema with which the schema was
 *    created.  Return NULL for any other value of 'idx'.
 *
 *    For a sub-template multi-list, return the 'idx' schema, or
 *    return NULL when 'idx' is not less than
 *    sk_fixlist_count_schemas().
 *
 *    The caller should call sk_schema_clone() to increment the
 *    schema's reference count if the caller plans to maintain a
 *    handle to the schema.
 */
const sk_schema_t *
sk_fixlist_get_schema(
    const sk_fixlist_t *list,
    uint16_t            idx);

/**
 *    Set the memory referenced by 'rec' to point at the element in
 *    the record list 'list' at position 'idx'.  Return
 *    SK_SCHEMA_ERR_FIELD_NOT_FOUND if 'idx' is larger than the number
 *    of elements in the list.
 *
 *    The caller MUST NOT call sk_fixrec_clear() on the record stored
 *    in 'rec'.
 *
 *    This function and sk_fixlist_next_element() fill 'rec' with a
 *    reference to the same record.  Calling one function modifies the
 *    record used by the other.
 *
 *    'rec' becomes invalid (points to freed memory) when
 *    sk_fixlist_destroy() or sk_fixlist_reset_iter() is called.
 */
sk_schema_err_t
sk_fixlist_get_element(
    sk_fixlist_t       *list,
    uint16_t            idx,
    const sk_fixrec_t **rec);

/**
 *    Assume the record list 'list' has been initialized for iterating
 *    over the elements in the list and set the memory referenced by
 *    'rec' to point at the next element in the list.  Return
 *    SK_ITERATOR_NO_MORE_ENTRIES when there are no more entries, or
 *    SK_ITERATOR_OK otherwise.
 *
 *    The caller MUST NOT call sk_fixrec_clear() on the record stored
 *    in 'rec'.
 *
 *    This function and sk_fixlist_get_element() fill 'rec' with a
 *    reference to the same record.  Calling one function modifies the
 *    record used by the other.  The current position maintained by
 *    'list' is not modified by sk_fixlist_get_element().
 *
 *    'rec' becomes invalid (points to freed memory) when
 *    sk_fixlist_destroy() or sk_fixlist_reset_iter() is called.
 *
 *    A call to sk_fixrec_get_list() automatically initializes the
 *    record list for visiting the elements in the list.
 *
 *    A call to sk_fixlist_reset_iter() resets or initializes the
 *    record list for visiting the elements in the list.
 */
skIteratorStatus_t
sk_fixlist_next_element(
    sk_fixlist_t       *list,
    const sk_fixrec_t **rec);

/**
 *    Initialize or reset the record list 'list' so that the next call
 *    to sk_fixlist_next_element() returns first element in the list.
 *
 *    This function must be called before iterating over a list you
 *    have created with one of the list creation functions.
 */
sk_schema_err_t
sk_fixlist_reset_iter(
    sk_fixlist_t       *list);

/**
 *    Append the record 'rec' to the record list 'list'.
 *
 *    If 'list' is not a sub-template multi-list, the schema of 'rec'
 *    must match the schema of 'list'.  If they do not match, do not
 *    modify the list and return SK_SCHEMA_ERR_INCOMPATIBLE.
 *
 *    FIXME: Return SK_SCHEMA_ERR_UNSPECIFIED if 'list' was created by
 *    sk_fixrec_get_list().
 */
sk_schema_err_t
sk_fixlist_append_fixrec(
    sk_fixlist_t       *list,
    const sk_fixrec_t  *rec);

/**
 *    Append the element at position 'field' in the record 'rec' to
 *    the basic list 'list'.
 *
 *    If the type of 'field' does not match the basic list's element
 *    type, do not modify the list and return SK_SCHEMA_ERR_BAD_TYPE.
 *    If 'list' is not a basic list list, do not modify the list and
 *    return SK_SCHEMA_ERR_INCOMPATIBLE.
 *
 *    FIXME: Return SK_SCHEMA_ERR_UNSPECIFIED if 'list' was created by
 *    sk_fixrec_get_list().
 */
sk_schema_err_t
sk_fixlist_append_element(
    sk_fixlist_t       *list,
    const sk_fixrec_t  *rec,
    const sk_field_t   *field);

/**
 *    Set the list field 'field' in 'rec' to the data in the given
 *    record list 'list'.  Do not modify the record and return
 *    SK_SCHEMA_ERR_BAD_TYPE if 'field' is not a list or if type of
 *    list in 'field' is different than that of 'list'.
 *
 *    Any fixlist previously assigned to 'rec' is destroyed.
 *
 *    FIXME: Return SK_SCHEMA_ERR_UNKNOWN_IE if 'list' is owned by
 *    another record.  UNIMPLEMENTED since 'list' is currently copied
 *    onto 'rec'.
 *
 *    FIXME: Whether list should be constant or not (to possibly avoid
 *    copying too much data) needs to be determined during
 *    implementation.
 */
sk_schema_err_t
sk_fixrec_set_list(
    sk_fixrec_t        *rec,
    const sk_field_t   *field,
    const sk_fixlist_t *list);



/*** sk_field_ops_t definition ***/

/**
 *    Fills a buffer 'dest' of size 'size' with a textual
 *    representation of 'field' in 'rec'.  This text is
 *    null-terminated.  The result is truncated (and still
 *    zero-terminated) if 'dest' is not large enough.  This is the
 *    to_text field of sk_field_ops_t, and is used by
 *    sk_fixrec_data_to_text().
 *
 *    TODO: Figure out who is responsible for field-specific
 *    formatting.
 */
typedef sk_schema_err_t
(*skfield_to_text_fn_t)(
    const sk_fixrec_t *rec,
    const sk_field_t  *field,
    char              *dest,
    size_t             size);

/**
 *    Modifies 'dest_field' in 'dest_rec' to include data from
 *    'src_field' in 'src_rec'.  This is the merge field of
 *    sk_field_ops_t, and is used by sk_fixrec_data_merge().
 *
 *    If the fields are of differing types, an error is returned.
 */
typedef sk_schema_err_t
(*skfield_merge_fn_t)(
    sk_fixrec_t       *dest_rec,
    const sk_field_t  *dest_field,
    const sk_fixrec_t *src_rec,
    const sk_field_t  *src_field);

/**
 *    Compares 'field_a' in 'rec_a' to 'field_b' in 'rec_b'.  Returns
 *    the comparison in 'cmp'.  Returns negative for a < b, 0 for a ==
 *    b, or positive for a > b.  This is the compare field of
 *    sk_field_ops_t, and is used by sk_fixrec_data_compare().
 *
 *    If the fields are of differing types, an error is returned.
 */
typedef sk_schema_err_t
(*skfield_compare_fn_t)(
    const sk_fixrec_t *rec_a,
    const sk_field_t  *field_a,
    const sk_fixrec_t *rec_b,
    const sk_field_t  *field_b,
    int               *cmp);

/**
 *    A function called to update the value of 'field' on 'rec'.
 *
 *    This is the 'compute' field of sk_field_ops_t, and is used by
 *    sk_fixrec_update_computed().
 */
typedef sk_schema_err_t
(*skfield_compute_fn_t)(
    sk_fixrec_t       *rec,
    const sk_field_t  *field);


/**
 *    A function that is called on a field when its schema is frozen
 *    via sk_schema_freeze().  This is the init field of
 *    sk_field_ops_t.
 */
typedef sk_schema_err_t
(*skfield_init_fn_t)(
    sk_field_t        *field,
    const sk_schema_t *schema);

/**
 *    A function that is called before a field is freed.  It is meant
 *    to clean up the field's cbdata pointer.  This is the teardown
 *    field of sk_field_ops_t.
 */
typedef sk_schema_err_t
(*skfield_free_fn_t)(
    sk_field_t *field);

/**
 *    A function that is called when a field (not the field's data) is
 *    copied, generally due to a schema being copied.  This is used to
 *    copy the field's cbdata.  This is the copy_cbdata field of
 *    sk_field_ops_t.
 */
typedef sk_schema_err_t
(*skfield_copy_cbdata_fn_t)(
    sk_field_t  *field,
    void       **new_cbdata);


/**
 *    Operations which can be attached to a field.  All function
 *    pointers may be NULL, in which case a standard function for the
 *    field's data type is used.
 */
struct sk_field_ops_st {
    /** Function to output textual representation of field */
    skfield_to_text_fn_t        to_text;

    /** Function to merge data from two fields */
    skfield_merge_fn_t          merge;

    /** Function to compare data from two fields */
    skfield_compare_fn_t        compare;

    /** Called upon schema initialization */
    skfield_init_fn_t           init;

    /* Called by sk_fixrec_update_computed() */
    skfield_compute_fn_t        compute;

    /** Called when a field is copied to copy the cbdata */
    skfield_copy_cbdata_fn_t    copy_cbdata;

    /** Called upon schema destruction */
    skfield_free_fn_t           teardown;

    /** User data attached to field  */
    void                       *cbdata;
};

/*** Computed fields ***/

/*
 *    A computed field is a field whose value is computed from the
 *    values of other existing values in the record.  Space is
 *    allocated for the computed field in the record, and after
 *    sk_fixrec_update_computed() is called the value is placed in the
 *    given spot in the record.  This allows the record to be treated
 *    like an ordinary record from there on.
 *
 *    A computed field is described by the 'compute' in the field's
 *    ops struct.  In order to simplify the creation of computed
 *    fields, a computed field can be generated and inserted into a
 *    schema via the sk_schema_insert_computed_field() function.  This
 *    function takes a sk_field_computed_description_t struct as an
 *    argument.  This struct is used to determine what the information
 *    element of the field and the update function to be used by the
 *    field.  The update function
 */


/**
 *   sk_field_computed_lookup_t values determine how the
 *   sk_field_computed_description_t struct is interpreted by
 *   sk_schema_get_ie_from_computed_description().
 */
typedef enum {
    /**
     *    Create a new information element to represent this field
     *    unless the information already contains the field.
     */
    SK_FIELD_COMPUTED_CREATE,

    /**
     *    Use an existing information element to represent this field,
     *    finding the element in an information model by identifier.
     */
    SK_FIELD_COMPUTED_LOOKUP_BY_IDENT,

    /**
     *    Use an existing information element to represent this field,
     *    finding the element in an information model by name.
     */
    SK_FIELD_COMPUTED_LOOKUP_BY_NAME

} sk_field_computed_lookup_t;


/**
 *    sk_field_computed_data_t is passed to a computed field's update
 *    function.
 *
 *    Specifically, calling sk_fixrec_update_computed() calls a
 *    function having the signature defined by
 *    'sk_field_computed_update_fn_t', and that function is passed the
 *    record to update and this structure.
 *
 *    This structure contains the computed field to be updated, the
 *    context pointer and two lists that are determined by the field
 *    names specified in sk_field_computed_description_t.  One list
 *    contains the field pointers and the second contains the names of
 *    those fields.
 */
struct sk_field_computed_data_st {
    /**
     *    The computed field to update.
     */
    const sk_field_t   *dest;

    /**
     *    The context value the caller provided in the
     *    'sk_field_computed_description_t structure. */
    const void         *caller_ctx;

    /**
     *    The number of entries in the 'fields' and 'names' arrays.
     */
    size_t              entries;

    /**
     *    An array of field pointers for the current record.  These
     *    field pointers represent the fields selected by name in the
     *    sk_schema_insert_computed_field() function in the order in
     *    which they were referenced in that function.  The value for
     *    a particular field is NULL if that field is not in the
     *    record.
     */
    const sk_field_t  **fields;

    /**
     *    The array of field names, in order, that were passed to the
     *    sk_schema_insert_computed_field() function.
     */
    const char        **names;
};
typedef struct sk_field_computed_data_st sk_field_computed_data_t;


/**
 *    The type of the update function for a computed field specified
 *    on the 'sk_field_computed_description_t' structure.
 *
 *    Calling sk_fixrec_update_computed() calls a function with this
 *    signature.  The function is given the record and the computed
 *    field data, and the function should update the field (that is,
 *    the 'dest' member of the 'data' pointer) with the updated
 *    computed value.
 *
 *    The callback may return SK_SCHEMA_ERR_UNSPECIFIED to indicate an
 *    unspecified error when updating the record.
 */
typedef sk_schema_err_t
(*sk_field_computed_update_fn_t)(
    sk_fixrec_t                    *rec,
    const sk_field_computed_data_t *data);


/**
 *    sk_field_computed_description_t is a structure the caller
 *    populates to describe a computed field.
 *
 *    Two functions accept this structure,
 *    sk_schema_get_ie_from_computed_description() and
 *    sk_schema_insert_computed_field().
 *
 *    The caller may pass the structure to
 *    sk_schema_get_ie_from_computed_description() to add the field to
 *    an information model.  In that case, the 'lookup' member and the
 *    members it requires are used:
 *
 *    -- If 'lookup' is SK_FIELD_COMPUTED_LOOKUP_BY_NAME, the 'name'
 *       parameter is used to find the element in the information
 *       model, and that element is used with its base length.
 *
 *    -- If 'lookup' is SK_FIELD_COMPUTED_LOOKUP_BY_IDENT, the 'ident'
 *       parameter is used to find the element in the information
 *       model, and that element is used with its base length.
 *
 *    -- If 'lookup' is SK_FIELD_COMPUTED_CREATE, a new information
 *       element is added to the schema's information model unless the
 *       element already exists.  In this mode, all the members of the
 *       struct's parameters must be filled in, as they are used to
 *       create the information element.  If 'ident' is left as zero,
 *       a new unused ident is automatically generated.
 *
 *    Passing the structure to sk_schema_insert_computed_field() adds
 *    the computed field to a schema.  That function uses the same
 *    members as sk_schema_get_ie_from_computed_description() and the
 *    'update', 'caller_ctx', 'field_names_len', and 'field_names'
 *    members (whose values it copies).
 *
 *    The 'update' callback function is invoked when
 *    sk_fixrec_update_computed() is called to set the value of the
 *    computed fields.  It is passed an sk_field_computed_data_t
 *    structure whose contents are determined by the 'field_names'
 *    member.
 *
 *    The 'caller_ctx' value is added to the
 *    'sk_field_computed_data_t' structure so the caller has a way to
 *    pass context to the 'update' callback function.
 *
 *    The 'field_names' member specifies an array of names (as
 *    C-strings) of the information elements from which the computed
 *    field's 'update' callback function will calculate its value.
 *    The names are used to populate the 'fields' and 'name' members
 *    of the sk_field_computed_data_t structure.
 *
 *    The 'field_names' array may either be NULL terminated or the
 *    caller may set 'field_names_len' to the maximum number of values
 *    to consider.  A NULL that occurs before 'field_names_len' values
 *    have been considered marks the end of the list.
 *
 *    Once the structure has been used to add the computed field to
 *    the schema, it is no longer needed.
 */
struct sk_field_computed_description_st {
    /* How to search for the field */
    sk_field_computed_lookup_t      lookup;

    /* Creation and Look-up parameters*/
    const char         *name;
    uint64_t            ident;

    /**** Element Creation Parameters ****/

    /* type of the IE; this value must be specified.  use an enum
     * specified in fbInfoElementDataType_t */
    uint8_t             datatype;

    /* semenatic information for the IE; may be blank.  use a value
     * such as FB_IE_DEFAULT, FB_IE_TOTALCOUNTER, FB_IE_DELTACOUNTER,
     * FB_IE_IDENTIFIER, FB_IE_FLAGS, FB_IE_LIST */
    uint8_t             semantics;

    /* number of octets required by the IE */
    uint16_t            len;

    /* the units for the IE; may be blank.  To fill use the macro
     * values that beign with FB_UNITS_ */
    uint16_t            units;

    /* range of the values of the IE; may be blank */
    uint64_t            min;
    uint64_t            max;

    /**** Members Used When Adding Field to Schema ****/

    /* function called to set computed field's value */
    sk_field_computed_update_fn_t   update;

    /* context value the caller may use; appears in the
     * sk_field_computed_data_t structure passed to 'update' */
    const void         *caller_ctx;

    /* maximum entries to consider in the field_names[] */
    int                 field_names_len;

    /* fields this computed field may use to compute its value */
    const char        **field_names;
};
typedef struct sk_field_computed_description_st sk_field_computed_description_t;


/**
 *    Create the computed field described by 'desc' and insert it into
 *    the schema 'schema' before the field 'before' or append it to
 *    the schema if 'before' is NULL.  Store the new field in the
 *    location reference by 'field'.
 *
 *    If the 'lookup' member of 'desc' is SK_FIELD_COMPUTED_CREATE and
 *    the field is not present in the information model used by
 *    'schema', the field is added to that information model by
 *    calling sk_schema_get_ie_from_computed_description().
 *

 *
 *    The 'desc'
 *    argument is used to determine the information element for the
 *    field and the function used to compute the field's value.  The
 *    new field is inserted
 *
 *    The arguments after 'before' are a list of names (strings) of
 *    the fields from which the computed field's update function will
 *    calculate its value.  This list of names must be
 *    NULL-terminated.  The update function's 'data' arguments will
 *    contain an array of field pointers and names which map on a
 *    one-to-one basis to the names passed into this function.
 *
 *    Return SK_SCHEMA_ERR_FROZEN if the schema is frozen,
 *    SK_SCHEMA_ERR_UNKNOWN_IE if the IE cannot be found in the
 *    information model or cannot be created, or
 *    SK_SCHEMA_ERR_FIELD_NOT_FOUND if 'before' is not in the schema.
 */
sk_schema_err_t
sk_schema_insert_computed_field(
    const sk_field_t                      **field,
    sk_schema_t                            *schema,
    const sk_field_computed_description_t  *desc,
    const sk_field_t                       *before);


/**
 *    Check the information model 'model' for the field described by
 *    'desc' and return that field as a fixbuf information element.
 *
 *    If the 'lookup' member of 'desc' is SK_FIELD_COMPUTED_CREATE and
 *    the field is not present in 'model', the field is added to the
 *    information model and the element is returned.
 *
 *    This function ignores the values of the 'update',
 *    'field_names_len', and 'field_names' members of 'desc'.
 *
 *    For details, see the description of the
 *    sk_field_computed_description_t structure.  See also
 *    sk_schema_insert_computed_field().
 */
const fbInfoElement_t *
sk_schema_get_ie_from_computed_description(
    const sk_field_computed_description_t  *desc,
    fbInfoModel_t                          *model);


/*** Plug-in Support ***/

/**
 *    Basic callback: used for any startup/shutdown code.  Called by
 *    skPluginSchemaRunInititialize() or skPluginSchemaRunCleanup().
 *
 *    skplugin_schema_callbacks_t.init
 *    skplugin_schema_callbacks_t.cleanup
 */
typedef int
(*skplugin_schema_callback_fn_t)(
    void               *cbdata);

/**
 *    skplugin_schema_callbacks_t is used as the 'regdata' argument to
 *    the skpinRegSchemaField() function.  See that function for a
 *    detailed description of the members of this structure.
 */
struct skplugin_schema_callbacks_st {
    skplugin_schema_callback_fn_t      init;
    skplugin_schema_callback_fn_t      cleanup;
    sk_field_computed_description_t    desc;
};
typedef struct skplugin_schema_callbacks_st skplugin_schema_callbacks_t;

/**
 *    schema field iterator
 */
typedef sk_dll_iter_t skplugin_schema_field_iter_t;


void
skPluginSchemaFieldSetup(
    void);

void
skPluginSchemaFieldTeardown(
    void);


/**
 *    Register a new schema computed field.
 *
 *    Registering a field in an application that does not work in
 *    terms of schema computed fields results in skpinRegSchemaField()
 *    setting 'return_field', if provided, to NULL and returning
 *    SKPLUGIN_OK.
 *
 *    'name' is the string that is the name of the field and is used
 *    when looking up the field.  It should be the string "plugin."
 *    followed by the value specified in 'desc.name' below.
 *
 *    'regdata' is a struct of type skplugin_schema_callbacks_t, which
 *    should be filled out as follows:
 *
 *      // FIXME!! ADD SUPPORT FOR init() and cleanup()
 *
 *
 *      'init' is a callback to initialize the plug-in.  The
 *      application calls 'init(cbdata)' when the application has
 *      decided this field will be used, before processing data.  It
 *      may be NULL.
 *
 *      'cleanup' is a callback to delete any of the plug-in's state.
 *      The application calls 'cleanup(cbdata)' after all records have
 *      been processed.  It may be NULL.
 *
 *      'desc.update' is a callback function to set the value of the
 *      computed field.  It is passed an sk_field_computed_data_t
 *      structure (see skschema.h) that includes the field to be
 *      updated, the 'desc.caller_ctx' pointer, and a list of fields
 *      determined by the 'desc.field_names' member.
 *
 *      'desc.field_names' member specifies an array of names (as
 *      C-strings) of the information elements from which the computed
 *      field's 'update' callback function calculates its value.  The
 *      names are used to populate the 'fields' and 'name' members of
 *      the sk_field_computed_data_t structure.  A NULL entry
 *      terminates the list; see also 'desc.field_names_len'.
 *
 *      'desc.field_names_len' specifies the maximum number of valid
 *      values of the 'desc.field_names' array so that the array does
 *      not need to be NULL terminated.
 *
 *      'desc.caller_ctx' is an optional user context included in the
 *      'sk_field_computed_data_t' structure passed to the
 *      'desc.update' callback function.
 *
 *      'desc.lookup' determines how the schema field is located:
 *
 *      -- If SK_FIELD_COMPUTED_LOOKUP_BY_NAME, the 'desc.name'
 *         member must name an existing IE.
 *
 *      -- If SK_FIELD_COMPUTED_LOOKUP_BY_IDENT, the 'desc.ident' must
 *         be the ID of an existing IE.
 *
 *      -- If SK_FIELD_IDENT_CREATE, a new IE is created unless it
 *         already exists.  The new IE is given the name in
 *         'desc.name' and it is given the ID in 'desc.ident'.  A
 *         'desc.ident' of 0 means an new ident should be generated.
 *
 *      'desc.name' is used as described in 'desc.lookup'.
 *
 *      'desc.ident' is used as described in 'desc.lookup'.
 *
 *      'desc.datatype' must be set when creating a new IE; it
 *      specifies the type, such as FB_IP4_ADDR.
 *
 *      'desc.len' must be set when creating a new IE; it specifies
 *      the length.
 *
 *      'desc.units' may be set when creating a new IE; it specifies
 *      the units (e.g., FB_UNITS_BITS).
 *
 *      'desc.semantics' may be set when creating a new IE; it
 *      specifies the semantics (e.g., FB_IE_FLAGS).
 *
 *      'desc.min' may be set when creating a new IE; it specifies the
 *      minimum value.
 *
 *      'desc.max' may be set when creating a new IE; it specifies the
 *      maximum value.
 *
 *    The 'name' and all parts of the 'regdata' member are copied by
 *    this function.  The caller does not need to maintain the
 *    structure once this function returns.
 *
 *    If 'name', 'regdata', or 'desc.update' is NULL,
 *    skpinRegSchemaField() returns SKPLUGIN_ERR.
 */
int
skpinRegSchemaField(
    const char                         *name,
    const skplugin_schema_callbacks_t  *regdata,
    void                               *cbdata);


/**
 *    Binds an iterator around all schema fields.
 */
int
skPluginSchemaFieldIteratorBind(
    skplugin_schema_field_iter_t  *iter);

/**
 *    Retrieves the name for the next schema field.  Returns 1 on
 *    success, 0 on failure.
 */
int
skPluginSchemaFieldIteratorNext(
    skplugin_schema_field_iter_t  *iter,
    const char                   **name);

/**
 *    Finds the sk_field_computed_description_t that was created from
 *    the 'desc' member of the skplugin_schema_callbacks_t object
 *    ('regdata') when the 'name' field was registered by
 *    skpinRegSchemaField().  That field description is passed to
 *    sk_schema_get_ie_from_computed_description() to get or create
 *    the information element on 'model'.  The IE is stored in the
 *    location referenced by 'ie'.
 *
 *    Returns SKPLUGIN_ERR if 'name' doesn't refer to an existing
 *    schema field.
 */
int
skPluginSchemaFieldGetIE(
    const fbInfoElement_t **ie,
    fbInfoModel_t          *model,
    const char             *name);

/**
 *    Finds the sk_field_computed_description_t that was created from
 *    the 'desc' member of the skplugin_schema_callbacks_t object
 *    ('regdata') when the 'name' field was registered by
 *    skpinRegSchemaField().  That field description is passed to
 *    sk_schema_insert_computed_field() to add the field to 'schema'
 *    before the field 'before'.  The field is stored in the location
 *    referenced by 'field'.
 *
 *    Returns SKPLUGIN_ERR if 'name' doesn't refer to an existing
 *    schema field.
 */
int
skPluginSchemaFieldAdd(
    const sk_field_t **field,
    const char        *name,
    sk_schema_t       *schema,
    const sk_field_t  *before);


int
skPluginSchemaAddAsPlugin(
    const char             *name,
    int                   (*setup_fn)(void));


int
skCountryAddSchemaFields(
    void);


#ifdef __cplusplus
}
#endif
#endif /* _SKSCHEMA_H */

/*
** Local Variables:
** mode:c
** indent-tabs-mode:nil
** c-basic-offset:4
** End:
*/
