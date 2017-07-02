/*
** Copyright (C) 2014-2017 by Carnegie Mellon University.
**
** @OPENSOURCE_LICENSE_START@
** See license information in ../../LICENSE.txt
** @OPENSOURCE_LICENSE_END@
*/

/*
 *    IPFIX printing functions
 *
 */

#include <silk/silk.h>

RCSIDENT("$SiLK: skfixformatter.c efd886457770 2017-06-21 18:43:23Z mthomas $");

#include <silk/skfixformatter.h>
#include <silk/skipaddr.h>
#include <silk/skipfixcert.h>
#include <silk/skschema.h>
#include <silk/sksite.h>
#include <silk/skvector.h>
#include <silk/utils.h>


/* DEFINES AND TYPEDEFS */

/*
 *    The initial size of output buffer; this is also the step size
 *    when the buffer must be resized.
 */
#define SK_FIXFORMATTER_DEFAULT_BUFSIZE  256

/*
 *    For each unique record schema seen, a 'fmtr_schema_to_map_t'
 *    object is created.  The object contains a reference to the
 *    schema and a vector of sk_fixformatter_field_t, where each
 *    formatter-field points to the location of the schema-field (the
 *    sk_field_t) within that particular schema.
 *
 *    If the sk_field_t does not exist in the record's schema, the
 *    sk_fixformatter_field_t note that the field should be empty (that
 *    is, contain no value).
 */
struct fmtr_schema_to_map_st {
    const sk_schema_t      *schema;
    sk_vector_t            *field_vec;
};
typedef struct fmtr_schema_to_map_st fmtr_schema_to_map_t;


/*
 *    sk_fixformatter_t is the record formatter
 */
struct sk_fixformatter_st {
    /* the buffer holding the output */
    char                   *buffer;
    /* the currently allocated length of 'buffer' */
    size_t                  bufsize;
    /* the schema containing the fields the caller wants to format */
    sk_schema_t            *schema;
    /* the info model for the schema */
    fbInfoModel_t          *model;
    /* an array of sk_fixformatter_field_t objects; one for each field
     * that is to be formatted */
    sk_vector_t            *fields;
    /* an array of fmtr_schema_to_map_t objects that know which fields
     * in a record's schema are to be formatted.  there is one of
     * these for each unique schema the formatter sees */
    sk_vector_t            *mappers;
    /* the most recently used mapping object, for quick access */
    fmtr_schema_to_map_t    cur_map;
    /* character to put between fields */
    char                    delimeter;
    /* when true, do not produce columnar output */
    unsigned                no_columns       : 1;
    /* when true, set field width so complete title is printed */
    unsigned                full_titles      : 1;
    /* when true, do not put a delimeter after the final field */
    unsigned                no_final_delim   : 1;
    /* when true, do not put a newline after the final field */
    unsigned                no_final_newline : 1;
    /* when true, no changes are allowed to the formatter */
    unsigned                finalized        : 1;
};
/* sk_fixformatter_t */


/*
 *    Signature of a function to return the number of characters
 *    required to format 'field'.
 */
typedef size_t (*fmtr_field_get_length_fn)(
    const sk_fixformatter_field_t *feild);

/*
 *    Signature of a function to format the 'field' field of 'rec'
 *    into the buffer 'buf' where 'buflen' is the size of 'buf'.
 */
typedef size_t (*fmtr_field_to_string_fn)(
    const sk_fixformatter_field_t *field,
    const sk_fixrec_t          *rec,
    char                       *buf,
    size_t                      buflen);

/*
 *    sk_fixformatter_field_t is an individual field formatter
 */
struct sk_fixformatter_field_st {
    const sk_field_t        *rec_field;

    fmtr_field_to_string_fn  to_string;
    fmtr_field_get_length_fn get_length;

    sk_fixformatter_get_extra_t   get_value_extra_fn;
    void                         *extra_callback_data;

    /* Title for field.  If NULL, a default title will be generated
     * from the IE */
    char                   *title;

    /* Maximum field width.  Value ignored unless the 'max_width_set'
     * bit below is non    zero. */
    size_t                  max_width;

    /* Minimum (and desired) field width.  Value ignored unless the
     * 'min_width_set'     bit below is nonzero. */
    size_t                  min_width;

    /* Timestamp format */
    uint32_t                timestamp_fmt;

    /* text to print after this field */
    char                    delim[4];

    /* IP address flags */
    skipaddr_flags_t        ipaddr_fmt;

    /* Precision (for floating point numbers) */
    uint8_t                 precision;

    unsigned                right_justify : 1;
    unsigned                hexadecimal   : 1;
    unsigned                decimal       : 1;
    unsigned                space_pad     : 1;

    unsigned                empty         : 1;

    unsigned                min_width_set : 1;
    unsigned                max_width_set : 1;
};
/* sk_fixformatter_field_t */


/*
 *    fmtr_bufpos_t maintains the current position in the formatter's
 *    output buffer.
 */
struct fmtr_bufpos_st {
    /* number of bytes written to buffer */
    size_t      len;
    /* amount of space left in the buffer */
    size_t      left;
    /* current position in the buffer */
    char       *pos;
};
typedef struct fmtr_bufpos_st fmtr_bufpos_t;



/*    Return true if 'm_fmtr' is finalized */
#define FMTR_IS_FINALIZED(m_fmtr)   (1 == (m_fmtr)->finalized)

/*    Return number of mapping objects in 'm_fmtr' */
#define FMTR_GET_MAPPER_COUNT(m_fmtr)           \
    skVectorGetCount((m_fmtr)->mappers)

/*    Return mapper at position 'm_pos' in 'm_fmtr' or NULL when
 *    'm_pos' is too large. */
#define FMTR_GET_MAPPER_AT(m_fmtr, m_pos)                       \
    ((fmtr_schema_to_map_t*)                                    \
     skVectorGetValuePointer((m_fmtr)->mappers, (m_pos)))

/*    Return number of fields in 'm_fmtr' */
#define FMTR_GET_FIELD_COUNT(m_fmtr)            \
    skVectorGetCount((m_fmtr)->fields)

/*    Return field at position 'm_pos' in 'm_fmtr' or NULL when
 *    'm_pos' is too large. */
#define FMTR_GET_FIELD_AT(m_fmtr, m_pos)                        \
    ((sk_fixformatter_field_t*)                                    \
     skVectorGetValuePointer((m_fmtr)->fields, (m_pos)))

/*    Initialize the fmtr_bufpos_t 'm_pos' based on the values in the
 *    formatter 'm_fmtr'. */
#define FMTR_BUFPOS_INITIALIZE(m_fmtr, m_bufpos)        \
    {                                                   \
        (m_bufpos)->left = (m_fmtr)->bufsize;           \
        (m_bufpos)->pos  = (m_fmtr)->buffer;            \
        (m_bufpos)->len  = 0;                           \
    }


/* FUNCTION DEFINITIONS */

/**
 *    Return the number of characters required to format 'field'.
 *
 *    This is the default get_length() function when no other has been
 *    set/provided.
 */
static size_t
fmtr_default_get_length(
    const sk_fixformatter_field_t *field)
{
    if (field == NULL || field->rec_field == NULL) {
        return 0;
    }

    if (sk_field_get_max(field->rec_field)) {
        char buf[2] = {'\0', '\0'};
        return snprintf(buf, 0,
                        field->hexadecimal ? ("%" PRIu64) : ("%" PRIx64),
                        sk_field_get_max(field->rec_field));
    }
    switch ((enum fbInfoElementDataType_en)sk_field_get_type(field->rec_field))
    {
      case FB_BOOL:
        return 1;
      case FB_UINT_8:
        return field->hexadecimal ? 2 : 3;
      case FB_UINT_16:
        return field->hexadecimal ? 4 : 5;
      case FB_UINT_32:
        return field->hexadecimal ? 8 : 10;
      case FB_UINT_64:
        return field->hexadecimal ? 16 : 19;
      case FB_INT_8:
        return field->hexadecimal ? 2 : 4;
      case FB_INT_16:
        return field->hexadecimal ? 4 : 6;
      case FB_INT_32:
        return field->hexadecimal ? 8 : 11;
      case FB_INT_64:
        return field->hexadecimal ? 16 : 20;
      case FB_FLOAT_32:
        return 20;              /* FIXME */
      case FB_FLOAT_64:
        return 20;              /* FIXME */
      case FB_IP4_ADDR:
        switch (field->ipaddr_fmt) {
          case SKIPADDR_DECIMAL:
            return 10;
          case SKIPADDR_HEXADECIMAL:
            return 8;
          default:
            return 15;
        }
      case FB_IP6_ADDR:
        if (field->ipaddr_fmt == SKIPADDR_HEXADECIMAL) {
            return 32;
        }
        return 39;
      case FB_MAC_ADDR:
        return 17;
      case FB_DT_SEC:
      case FB_DT_MILSEC:
      case FB_DT_MICROSEC:
      case FB_DT_NANOSEC:
        if (field->timestamp_fmt & SKTIMESTAMP_NOMSEC) {
            if (field->timestamp_fmt & SKTIMESTAMP_EPOCH) {
                return 10;
            }
            return 19;
        }
        if (field->timestamp_fmt & SKTIMESTAMP_EPOCH) {
            return 14;
        }
        return 23;
      case FB_OCTET_ARRAY:
        if (UINT16_MAX == sk_field_get_length(field->rec_field)) {
            return 20;          /* FIXME */
        }
        return sk_field_get_length(field->rec_field) * 4;
      case FB_STRING:
        if (UINT16_MAX == sk_field_get_length(field->rec_field)) {
            return 20;          /* FIXME */
        }
        return sk_field_get_length(field->rec_field);
      case FB_BASIC_LIST:
      case FB_SUB_TMPL_LIST:
      case FB_SUB_TMPL_MULTI_LIST:
        return 0;
    }
    return (size_t)(-1);                  /* NOTREACHED */
}

/**
 *    Using the data in 'rec', get 'field' and format it into the
 *    buffer 'buf' where 'len' is the length of 'buf'.
 *
 *    This is the default to_string() function when no other has been
 *    set/provided.
 */
static size_t
fmtr_default_to_string(
    const sk_fixformatter_field_t *field,
    const sk_fixrec_t          *rec,
    char                       *buf,
    size_t                      len)
{
    assert(field);
    assert(rec);

    switch ((enum fbInfoElementDataType_en)sk_field_get_type(field->rec_field))
    {
      case FB_BOOL:
      case FB_UINT_8:
      case FB_UINT_16:
      case FB_UINT_32:
      case FB_UINT_64:
        {
            uint64_t u64;
            if (sk_fixrec_get_unsigned(rec, field->rec_field, &u64)) {
                return snprintf(buf, len, "ERR");
            }
            if (field->hexadecimal) {
                return snprintf(buf, len, "%" PRIx64, u64);
            }
            return snprintf(buf, len, "%" PRIu64, u64);
        }
      case FB_INT_8:
      case FB_INT_16:
      case FB_INT_32:
      case FB_INT_64:
        {
            int64_t i64;
            if (sk_fixrec_get_signed(rec, field->rec_field, &i64)) {
                return snprintf(buf, len, "ERR");
            }
            if (field->hexadecimal) {
                return snprintf(buf, len, "%" PRIx64, i64);
            }
            return snprintf(buf, len, "%" PRId64, i64);
        }
      case FB_FLOAT_32:
      case FB_FLOAT_64:
        {
            double d;
            if (sk_fixrec_get_float(rec, field->rec_field, &d)) {
                return snprintf(buf, len, "ERR");
            }
            return snprintf(
                buf, len, "%.*f", field->precision, d);
        }
      case FB_IP4_ADDR:
      case FB_IP6_ADDR:
        {
            char addrbuf[SK_NUM2DOT_STRLEN];
            skipaddr_t addr;
            if (sk_fixrec_get_ip_address(rec, field->rec_field, &addr)) {
                return snprintf(buf, len, "ERR");
            }
            skipaddrString(addrbuf, &addr, field->ipaddr_fmt);
            return snprintf(buf, len, "%s", addrbuf);

        }
      case FB_DT_SEC:
      case FB_DT_MILSEC:
      case FB_DT_MICROSEC:
      case FB_DT_NANOSEC:
        {
            char datebuf[SKTIMESTAMP_STRLEN];
            sktime_t t;
            if (sk_fixrec_get_datetime(rec, field->rec_field, &t)) {
                return snprintf(buf, len, "ERR");
            }
            sktimestamp_r(datebuf, t, field->timestamp_fmt);
            return snprintf(buf, len, "%s", datebuf);
        }
      case FB_MAC_ADDR:
      case FB_STRING:
      case FB_OCTET_ARRAY:
        if (sk_fixrec_data_to_text(rec, field->rec_field, buf, len)) {
            return snprintf(buf, len, "ERR");
        }
        return strlen(buf);

      case FB_BASIC_LIST:
      case FB_SUB_TMPL_LIST:
      case FB_SUB_TMPL_MULTI_LIST:
        return 0;
    }
    return (size_t)(-1);                  /* NOTREACHED */
}


/**
 *    Get length requried for a SiLK sensor field.
 */
static size_t
fmtr_sensor_get_length(
    const sk_fixformatter_field_t *field)
{
    assert(field);

    if (field->hexadecimal || field->decimal) {
        return fmtr_default_get_length(field);
    }
    return sksiteSensorGetMaxNameStrLen();
}

/**
 *    Format a SiLK sensor field.
 */
static size_t
fmtr_sensor_to_string(
    const sk_fixformatter_field_t *field,
    const sk_fixrec_t          *rec,
    char                       *buf,
    size_t                      len)
{
    sk_sensor_id_t sid;

    assert(field);
    assert(rec);

    if (sk_field_get_type(field->rec_field) != FB_UINT_16) {
        return fmtr_default_to_string(field, rec, buf, len);
    }
    if (sk_fixrec_get_unsigned16(rec, field->rec_field, &sid)) {
        return snprintf(buf, len, "ERR");
    }
    if (field->hexadecimal || field->decimal)    {
        if (SK_INVALID_SENSOR == sid) {
            return snprintf(buf, len, "-1");
        }
        return fmtr_default_to_string(field, rec, buf, len);
    }
    return sksiteSensorGetName(buf, len, sid);
}

/**
 *    Get length requried for a SiLK flowtype field.
 */
static size_t
fmtr_flowtype_get_length(
    const sk_fixformatter_field_t *field)
{
    assert(field);

    if (field->hexadecimal || field->decimal) {
        return fmtr_default_get_length(field);
    }
    return sksiteSensorGetMaxNameStrLen();
}

/**
 *    Format a SiLK flowtype field.
 */
static size_t
fmtr_flowtype_to_string(
    const sk_fixformatter_field_t *field,
    const sk_fixrec_t          *rec,
    char                       *buf,
    size_t                      len)
{
    sk_flowtype_id_t ft;

    assert(field);
    assert(rec);

    if (field->hexadecimal || field->decimal
        || sk_field_get_type(field->rec_field) != FB_UINT_8)
    {
        return fmtr_default_to_string(field, rec, buf, len);
    }
    if (sk_fixrec_get_unsigned8(rec, field->rec_field, &ft)) {
        return snprintf(buf, len, "ERR");
    }
    return sksiteFlowtypeGetName(buf, len, ft);
}

/**
 *    Get length requried for a TCP flags field.
 */
static size_t
fmtr_tcpflags_get_length(
    const sk_fixformatter_field_t *field)
{
    assert(field);

    if (field->hexadecimal || field->decimal) {
        return fmtr_default_get_length(field);
    }
    return SK_TCPFLAGS_STRLEN-1;
}

/**
 *    Format a TCP flags field.
 */
static size_t
fmtr_tcpflags_to_string(
    const sk_fixformatter_field_t *field,
    const sk_fixrec_t          *rec,
    char                       *buf,
    size_t                      len)
{
    char flagsbuf[SK_TCPFLAGS_STRLEN];
    uint64_t val;
    uint8_t flags;

    assert(field);
    assert(rec);

    if (field->hexadecimal || field->decimal) {
        return fmtr_default_to_string(field, rec, buf, len);
    }
    switch ((enum fbInfoElementDataType_en)sk_field_get_type(field->rec_field))
    {
      case FB_UINT_8:
      case FB_UINT_16:
        if (sk_fixrec_get_unsigned(rec, field->rec_field, &val)) {
            return snprintf(buf, len, "ERR");
        }
        flags = val & 0xff;
        break;
      default:
        return fmtr_default_to_string(field, rec, buf, len);
    }
    skTCPFlagsString(flags, flagsbuf, field->space_pad ? SK_PADDED_FLAGS : 0);
    return snprintf(buf, len, "%s", flagsbuf);
}

/**
 *    Get length requried for a SiLK attributes (tcp_state) field.
 */
static size_t
fmtr_tcpstate_get_length(
    const sk_fixformatter_field_t *field)
{
    assert(field);

    if (field->hexadecimal || field->decimal) {
        return fmtr_default_get_length(field);
    }
    return SK_TCP_STATE_STRLEN-1;
}

/**
 *    Format a SiLK attributes (tcp_state) field.
 */
static size_t
fmtr_tcpstate_to_string(
    const sk_fixformatter_field_t *field,
    const sk_fixrec_t          *rec,
    char                       *buf,
    size_t                      len)
{
    char strbuf[SK_TCP_STATE_STRLEN];
    uint8_t val;

    assert(field);
    assert(rec);

    if (field->hexadecimal || field->decimal
        || sk_field_get_type(field->rec_field) != FB_UINT_8)
    {
        return fmtr_default_to_string(field, rec, buf, len);
    }

    sk_fixrec_get_unsigned8(rec, field->rec_field, &val);
    skTCPStateString(val, strbuf, field->space_pad ? SK_PADDED_FLAGS : 0);
    return snprintf(buf, len, "%s", strbuf);
}


/**
 *    Return the position of 'field' in 'fmtr'.  Return SIZE_MAX when
 *    'field' is not in 'fmtr'.
 */
static size_t
fmtr_find_field(
    const sk_fixformatter_t       *fmtr,
    const sk_fixformatter_field_t *field)
{
    const sk_fixformatter_field_t *f;
    size_t i;

    assert(fmtr);
    assert(field);

    for (i = 0; (f = FMTR_GET_FIELD_AT(fmtr, i)) != NULL; ++i) {
        if (f == field) {
            return i;
        }
    }
    return SIZE_MAX;
}


/**
 *    Examine a new record schema and create a new vector of field
 *    pointers that point to the fields within that schema that are to
 *    be printed.
 */
static int
fmtr_examine_schema(
    sk_fixformatter_t     *fmtr,
    const sk_schema_t  *schema)
{
    const sk_fixformatter_field_t *f;
    fmtr_schema_to_map_t new_map;
    sk_fixformatter_field_t fmtr_field;
    size_t i;

    memset(&new_map, 0, sizeof(new_map));
    new_map.schema = sk_schema_clone(schema);
    new_map.field_vec = skVectorNew(sizeof(sk_fixformatter_field_t));

    for (i = 0; (f = FMTR_GET_FIELD_AT(fmtr, i)) != NULL; ++i) {
        /* for each field to be printed, create a new formatter field
         * that points to the schema-fields in this particular
         * schema, and push the fmtr_field onto 'vec'. */
        memcpy(&fmtr_field, f, sizeof(sk_fixformatter_field_t));
        if (f->rec_field) {
            /* does field exist on the schema */
            fmtr_field.rec_field = sk_schema_get_field_by_ident(
                schema, sk_field_get_ident(f->rec_field), 0);
            if (NULL == fmtr_field.rec_field) {
                fmtr_field.empty = 1;
            }
        }
        skVectorAppendValue(new_map.field_vec, &fmtr_field);
    }

    skVectorAppendValue(fmtr->mappers, &new_map);
    return 0;
}


/*
 *    Given the values in buffer position 'bufpos', determine whether
 *    the buffer in 'fmtr' contained 'plen' characters of space to
 *    format the the field 'field'.
 *
 *    If the buffer did have enough space, add the delimieter for
 *    'field' to the output buffer, update 'bufpos' to the current
 *    length of the string in buffer, and return 1.
 *
 *    If the buffer does not have enough room, grow the buffer on
 *    'fmtr', update 'bufpos' to contain the new size of the buffer,
 *    and return 0.
 */
static size_t
fmtr_bufpos_next_field(
    sk_fixformatter_t             *fmtr,
    const sk_fixformatter_field_t *field,
    fmtr_bufpos_t              *bufpos,
    size_t                      plen)
{
    size_t rv;

    if (plen + sizeof(field->delim) < bufpos->left) {
        /* there was enough space in the buffer for the field and the
         * delimiter */
        bufpos->left -= plen;
        bufpos->pos  += plen;
        bufpos->len  += plen;

        /* add delimiter */
        plen = snprintf(bufpos->pos, bufpos->left, "%s", field->delim);
        bufpos->left -= plen;
        bufpos->pos  += plen;
        bufpos->len  += plen;

        /* go to next field */
        rv = 1;
    } else {
        /* must reallocate the buffer */
        void *oldbuf = fmtr->buffer;

        fmtr->bufsize += plen + SK_FIXFORMATTER_DEFAULT_BUFSIZE;
        fmtr->buffer = (char*)realloc(fmtr->buffer, fmtr->bufsize);
        if (NULL == fmtr->buffer) {
            free(oldbuf);
            skAppPrintOutOfMemory("Could not resize formatter print buffer");
            exit(EXIT_FAILURE);
        }
        bufpos->pos = fmtr->buffer + bufpos->len;
        bufpos->left += SK_FIXFORMATTER_DEFAULT_BUFSIZE;
        /* we must format the same field again */
        rv = 0;
    }

    return rv;
}


sk_fixformatter_t *
sk_fixformatter_create(
    fbInfoModel_t *model)
{
    sk_fixformatter_t *fmtr;

    fmtr = sk_alloc(sk_fixformatter_t);
    fmtr->delimeter = '|';

    fmtr->fields = skVectorNew(sizeof(sk_fixformatter_field_t));
    fmtr->mappers = skVectorNew(sizeof(fmtr_schema_to_map_t));
    fmtr->model = model;
    return fmtr;
}


void
sk_fixformatter_destroy(
    sk_fixformatter_t     *fmtr)
{
    fmtr_schema_to_map_t *map;
    size_t i;

    if (fmtr) {
        sk_schema_destroy(fmtr->schema);
        for (i = 0; i < sk_vector_get_count(fmtr->fields); ++i) {
            sk_fixformatter_field_t *f =
                (sk_fixformatter_field_t *)sk_vector_get_value_pointer(
                    fmtr->fields, i);
            free(f->title);
        }
        skVectorDestroy(fmtr->fields);
        for (i = 0; (map = FMTR_GET_MAPPER_AT(fmtr, i)) != NULL; ++i) {
            sk_schema_destroy(map->schema);
            skVectorDestroy(map->field_vec);
        }
        skVectorDestroy(fmtr->mappers);
        free(fmtr->buffer);
        memset(fmtr, 0, sizeof(*fmtr));
        free(fmtr);
    }
}


size_t
sk_fixformatter_get_field_count(
    const sk_fixformatter_t   *fmtr)
{
    assert(fmtr);
    return FMTR_GET_FIELD_COUNT(fmtr);
}


sk_fixformatter_field_t *
sk_fixformatter_get_field(
    const sk_fixformatter_t   *fmtr,
    size_t                  position)
{
    assert(fmtr);
    return FMTR_GET_FIELD_AT(fmtr, position);
}


/* Finalize a record format so that it is ready to be used for output */
int
sk_fixformatter_finalize(
    sk_fixformatter_t     *fmtr)
{
    sk_fixformatter_field_t *field;
    size_t len;
    size_t i;
    size_t width;
    size_t total_width;

    assert(fmtr);
    if (FMTR_IS_FINALIZED(fmtr)) {
        return 0;
    }

    sk_schema_freeze(fmtr->schema);
    fmtr->finalized = 1;

    total_width = 0;
    for (i = 0; (field = FMTR_GET_FIELD_AT(fmtr, i)) != NULL; ++i) {        ;
        if (!field->min_width_set) {
            width = field->get_length(field);
            field->min_width = width;
            field->min_width_set = 1;
        }
        if (field->title == NULL) {
            field->title = strdup(sk_field_get_name(field->rec_field));
        }
        len = strlen(field->title);
        if (fmtr->full_titles && len > field->min_width) {
            field->min_width = len;
            field->min_width_set = 1;
        }
        field->delim[0] = fmtr->delimeter;
        field->delim[1] = '\0';
        total_width += field->min_width + 1;
    }

    /* set the end-of-line string */
    field = FMTR_GET_FIELD_AT(fmtr, FMTR_GET_FIELD_COUNT(fmtr)-1);

    len = 0;
    if (!fmtr->no_final_delim) {
        field->delim[len] = fmtr->delimeter;
        ++len;
    }
    if (!fmtr->no_final_newline) {
        field->delim[len] = '\n';
        ++len;
    }
    field->delim[len] = '\0';
    total_width += len;

    if (total_width < SK_FIXFORMATTER_DEFAULT_BUFSIZE) {
        total_width = SK_FIXFORMATTER_DEFAULT_BUFSIZE;
    }
    if (fmtr->bufsize < total_width) {
        free(fmtr->buffer);
        fmtr->bufsize = total_width;
        fmtr->buffer = sk_alloc_array(char, fmtr->bufsize);
    }

    return 0;
}


sk_fixformatter_field_t *
sk_fixformatter_add_ie(
    sk_fixformatter_t         *fmtr,
    const fbInfoElement_t  *ie)
{
    sk_field_t *rec_field = NULL;
    sk_fixformatter_field_t field;
    size_t last;

    assert(fmtr);
    if (FMTR_IS_FINALIZED(fmtr)) {
        return NULL;
    }

    if (NULL == fmtr->schema) {
        sk_schema_create(&fmtr->schema, fmtr->model, NULL, 0);
    }

    if (sk_schema_insert_field_by_ident(
            &rec_field, fmtr->schema, SK_FIELD_IDENT_CREATE(ie->ent, ie->num),
            NULL, NULL))
    {
        return NULL;
    }

    memset(&field, 0, sizeof(field));
    field.rec_field = rec_field;
    field.to_string = fmtr_default_to_string;
    field.get_length = fmtr_default_get_length;

    switch (sk_field_get_pen(rec_field)) {
      case IPFIX_CERT_PEN:
        switch (sk_field_get_id(rec_field)) {
          case 14:
          case 14 | FB_IE_VENDOR_BIT_REVERSE:
            /* initialTCPFlags */
          case 15:
          case 15 | FB_IE_VENDOR_BIT_REVERSE:
            /* unionTCPFlags */
            field.to_string = fmtr_tcpflags_to_string;
            field.get_length = fmtr_tcpflags_get_length;
            break;
          case 30:
            /* silkFlowType */
            field.to_string = fmtr_flowtype_to_string;
            field.get_length = fmtr_flowtype_get_length;
            break;
          case 31:
            /* silkFlowSensor*/
            field.to_string = fmtr_sensor_to_string;
            field.get_length = fmtr_sensor_get_length;
            break;
          case 32:
            /* silkTCPState*/
            field.to_string = fmtr_tcpstate_to_string;
            field.get_length = fmtr_tcpstate_get_length;
            break;
        }
        break;

      case 0:
      case FB_IE_PEN_REVERSE:
        switch (sk_field_get_id(rec_field)) {
          case 6:
            /* tcpControlBits */
            field.to_string = fmtr_tcpflags_to_string;
            field.get_length = fmtr_tcpflags_get_length;
            break;
        }
        break;
    }

    field.precision = SK_FIXFORMATTER_DEFAULT_FP_PRECISION;
    switch ((enum fbInfoElementDataType_en)sk_field_get_type(rec_field)) {
      case FB_BOOL:
      case FB_UINT_8:
      case FB_UINT_16:
      case FB_UINT_32:
      case FB_UINT_64:
      case FB_INT_8:
      case FB_INT_16:
      case FB_INT_32:
      case FB_INT_64:
        {
            uint8_t sem;
            sem = sk_field_get_semantics(rec_field);
            if (sem == FB_IE_SEMANTIC(FB_IE_IDENTIFIER)
                || sem == FB_IE_SEMANTIC(FB_IE_FLAGS))
            {
                break;
            }
            field.right_justify = 1;
        }
        /* fall through */
      case FB_FLOAT_32:
      case FB_FLOAT_64:
        field.decimal = 1;
        break;
      default:
        break;
    }

    last = FMTR_GET_FIELD_COUNT(fmtr);
    if (skVectorAppendValue(fmtr->fields, &field)) {
        return NULL;
    }
    return FMTR_GET_FIELD_AT(fmtr, last);
}


sk_fixformatter_field_t *
sk_fixformatter_add_extra_field(
    sk_fixformatter_t                 *fmtr,
    sk_fixformatter_get_extra_t     get_value_extra_fn,
    void                           *callback_data,
    size_t                          min_width)
{
    sk_fixformatter_field_t field;
    size_t last;

    assert(fmtr);
    if (FMTR_IS_FINALIZED(fmtr)) {
        return NULL;
    }

    memset(&field, 0, sizeof(field));
    field.get_value_extra_fn = get_value_extra_fn;
    field.extra_callback_data = callback_data;
    field.min_width = min_width;
    field.min_width_set = 1;
    last = FMTR_GET_FIELD_COUNT(fmtr);
    if (skVectorAppendValue(fmtr->fields, &field)) {
        return NULL;
    }
    return FMTR_GET_FIELD_AT(fmtr, last);
}


int
sk_fixformatter_add_from_schema(
    sk_fixformatter_t     *fmtr,
    const sk_schema_t  *schema)
{
    const sk_field_t *rec_field;
    uint16_t i;

    assert(fmtr);
    if (FMTR_IS_FINALIZED(fmtr)) {
        return -1;
    }

    for (i = 0; (rec_field = sk_schema_get_field(schema, i)) != NULL; ++i) {
        if (NULL == sk_fixformatter_add_ie(fmtr, sk_field_get_ie(rec_field))) {
            return -1;
        }
    }
    return 0;
}


size_t
sk_fixformatter_fill_title_buffer(
    sk_fixformatter_t     *fmtr,
    char              **output_buffer)
{
    sk_fixformatter_field_t *field;
    fmtr_bufpos_t bp;
    size_t i;
    size_t plen;

    assert(fmtr);
    if (!FMTR_IS_FINALIZED(fmtr)) {
        return 0;
    }

    FMTR_BUFPOS_INITIALIZE(fmtr, &bp);

    i = 0;
    while ((field = FMTR_GET_FIELD_AT(fmtr, i)) != NULL) {
        if (field->empty) {
            plen = 0;
        } else {
            plen = snprintf(bp.pos, bp.left, "%s", field->title);
        }
        if (plen > field->min_width) {
            plen = field->min_width;
        }
        if (!fmtr->no_columns && plen < field->min_width) {
            /* Add spaces to field to be at least min_width in size */
            size_t n;
            if (bp.left > plen) {
                if (field->min_width > bp.left) {
                    n = bp.left - plen;
                } else {
                    n = field->min_width - plen;
                }
                memset(bp.pos + plen, ' ', n);
            }
            plen = field->min_width;
        }

        i += fmtr_bufpos_next_field(fmtr, field, &bp, plen);
    }

    *bp.pos = '\0';
    *output_buffer = fmtr->buffer;
    return bp.len;
}


size_t
sk_fixformatter_record_to_string_extra(
    sk_fixformatter_t     *fmtr,
    const sk_fixrec_t  *record,
    void               *extra,
    char              **output_buffer)
{
    sk_fixformatter_field_t *field;
    fmtr_schema_to_map_t *map;
    fmtr_bufpos_t bp;
    size_t i;
    size_t spaces;
    size_t plen;
    size_t size;

    assert(fmtr);
    if (!FMTR_IS_FINALIZED(fmtr)) {
        return 0;
    }

    FMTR_BUFPOS_INITIALIZE(fmtr, &bp);

    if (fmtr->cur_map.schema != record->schema) {
        /* FIXME: consider replacing this linear search; we could
         * store the map in the schema's context pointer */
        for (i = 0; (map = FMTR_GET_MAPPER_AT(fmtr, i)) != NULL; ++i) {
            if (map->schema == record->schema) {
                fmtr->cur_map = *map;
                break;
            }
        }
        if (NULL == map) {
            fmtr_examine_schema(fmtr, record->schema);
            map = FMTR_GET_MAPPER_AT(fmtr, (FMTR_GET_MAPPER_COUNT(fmtr) - 1));
            fmtr->cur_map = *map;
        }
    }

    i = 0;
    while ((field = ((sk_fixformatter_field_t*)
                     skVectorGetValuePointer(fmtr->cur_map.field_vec, i)))
           != NULL)
    {
        if (field->empty) {
            if (fmtr->no_columns) {
                plen = 0;
            } else if (field->min_width < bp.left) {
                memset(bp.pos, ' ', field->min_width);
                plen = field->min_width;
            } else {
                memset(bp.pos, ' ', bp.left);
                plen = bp.left;
            }
        } else if (field->get_value_extra_fn) {
            plen = field->get_value_extra_fn(
                record, bp.pos, bp.left, field->extra_callback_data, extra);
        } else {
            plen = field->to_string(field, record, bp.pos, bp.left);
        }
        if (field->max_width_set && plen > field->max_width) {
            plen = field->max_width;
        }
        if (!fmtr->no_columns && plen < field->min_width) {
            /* Add spaces to field to be at least min_width in size */
            size = ((field->min_width < bp.left) ? field->min_width : bp.left);
            spaces = (plen >= size) ? 0 : (size - plen);
            if (spaces) {
                if (!field->right_justify) {
                    memset(bp.pos + plen, ' ', spaces);
                } else {
                    memmove(bp.pos + spaces, bp.pos, plen);
                    memset(bp.pos, ' ', spaces);
                }
            }
            plen = field->min_width;
        }

        i += fmtr_bufpos_next_field(fmtr, field, &bp, plen);
    }

    *bp.pos = '\0';
    *output_buffer = fmtr->buffer;
    return bp.len;
}


size_t
sk_fixformatter_record_to_string(
    sk_fixformatter_t     *fmtr,
    const sk_fixrec_t  *record,
    char              **output_buffer)
{
    return sk_fixformatter_record_to_string_extra(
        fmtr, record, NULL, output_buffer);
}

void
sk_fixformatter_set_delimeter(
    sk_fixformatter_t     *fmtr,
    char                delimeter)
{
    assert(fmtr);
    if (FMTR_IS_FINALIZED(fmtr)) {
        return;
    }
    fmtr->delimeter = delimeter;
}

void
sk_fixformatter_set_no_columns(
    sk_fixformatter_t     *fmtr)
{
    assert(fmtr);
    if (FMTR_IS_FINALIZED(fmtr)) {
        return;
    }
    fmtr->no_columns = 1;
    fmtr->full_titles = 1;
}

void
sk_fixformatter_set_full_titles(
    sk_fixformatter_t     *fmtr)
{
    assert(fmtr);
    if (FMTR_IS_FINALIZED(fmtr)) {
        return;
    }
    fmtr->full_titles = 1;
}

void
sk_fixformatter_set_no_final_delimeter(
    sk_fixformatter_t     *fmtr)
{
    assert(fmtr);
    if (FMTR_IS_FINALIZED(fmtr)) {
        return;
    }
    fmtr->no_final_delim = 1;
}

void
sk_fixformatter_set_no_final_newline(
    sk_fixformatter_t     *fmtr)
{
    assert(fmtr);
    if (FMTR_IS_FINALIZED(fmtr)) {
        return;
    }
    fmtr->no_final_newline = 1;
}

void
sk_fixformatter_field_set_empty(
    const sk_fixformatter_t   *fmtr,
    sk_fixformatter_field_t   *field)
{
    assert(fmtr);
    assert(field);
    if (FMTR_IS_FINALIZED(fmtr)) {
        return;
    }
    if (SIZE_MAX == fmtr_find_field(fmtr, field)) {
        return;
    }
    field->empty = 1;
}

void
sk_fixformatter_field_set_ipaddr_format(
    const sk_fixformatter_t   *fmtr,
    sk_fixformatter_field_t   *field,
    skipaddr_flags_t        flags)
{
    assert(fmtr);
    assert(field);
    if (FMTR_IS_FINALIZED(fmtr)) {
        return;
    }
    if (SIZE_MAX == fmtr_find_field(fmtr, field)) {
        return;
    }
    field->ipaddr_fmt = flags;
}

void
sk_fixformatter_field_set_justification(
    const sk_fixformatter_t   *fmtr,
    sk_fixformatter_field_t   *field,
    sk_fixformatter_lr_t       left_or_right)
{
    assert(fmtr);
    assert(field);
    if (FMTR_IS_FINALIZED(fmtr)) {
        return;
    }
    if (SIZE_MAX == fmtr_find_field(fmtr, field)) {
        return;
    }
    field->right_justify = (SK_FMTR_RIGHT == left_or_right);
}

void
sk_fixformatter_field_set_max_width(
    const sk_fixformatter_t   *fmtr,
    sk_fixformatter_field_t   *field,
    size_t                  max_width)
{
    assert(fmtr);
    assert(field);
    if (FMTR_IS_FINALIZED(fmtr)) {
        return;
    }
    if (SIZE_MAX == fmtr_find_field(fmtr, field)) {
        return;
    }
    field->max_width_set = 1;
    field->max_width = max_width;
}

void
sk_fixformatter_field_set_min_width(
    const sk_fixformatter_t   *fmtr,
    sk_fixformatter_field_t   *field,
    size_t                  min_width)
{
    assert(fmtr);
    assert(field);
    if (FMTR_IS_FINALIZED(fmtr)) {
        return;
    }
    if (SIZE_MAX == fmtr_find_field(fmtr, field)) {
        return;
    }
    field->min_width_set = 1;
    field->min_width = min_width;
}

void
sk_fixformatter_field_set_number_format(
    const sk_fixformatter_t   *fmtr,
    sk_fixformatter_field_t   *field,
    uint8_t                 base)
{
    assert(fmtr);
    assert(field);
    if (FMTR_IS_FINALIZED(fmtr)) {
        return;
    }
    if (SIZE_MAX == fmtr_find_field(fmtr, field)) {
        return;
    }
    if (10 == base) {
        field->decimal = 1;
        field->hexadecimal = 0;
        field->ipaddr_fmt = SKIPADDR_DECIMAL;
    } else if (16 == base) {
        field->decimal = 0;
        field->hexadecimal = 1;
        field->ipaddr_fmt = SKIPADDR_HEXADECIMAL;
    } else {
        /* fallback to decimal */
        field->decimal = 1;
        field->hexadecimal = 0;
        field->ipaddr_fmt = SKIPADDR_DECIMAL;
    }
}

void
sk_fixformatter_field_set_precision(
    const sk_fixformatter_t   *fmtr,
    sk_fixformatter_field_t   *field,
    uint8_t                 precision)
{
    assert(fmtr);
    assert(field);
    if (FMTR_IS_FINALIZED(fmtr)) {
        return;
    }
    if (SIZE_MAX == fmtr_find_field(fmtr, field)) {
        return;
    }
    field->precision = precision;
}

void
sk_fixformatter_field_set_space_padded(
    const sk_fixformatter_t   *fmtr,
    sk_fixformatter_field_t   *field)
{
    assert(fmtr);
    assert(field);
    if (FMTR_IS_FINALIZED(fmtr)) {
        return;
    }
    if (SIZE_MAX == fmtr_find_field(fmtr, field)) {
        return;
    }
    field->space_pad = 1;
}

void
sk_fixformatter_field_set_timestamp_format(
    const sk_fixformatter_t   *fmtr,
    sk_fixformatter_field_t   *field,
    uint32_t                flags)
{
    assert(fmtr);
    assert(field);
    if (FMTR_IS_FINALIZED(fmtr)) {
        return;
    }
    if (SIZE_MAX == fmtr_find_field(fmtr, field)) {
        return;
    }
    field->timestamp_fmt = flags;
}

void
sk_fixformatter_field_set_title(
    const sk_fixformatter_t   *fmtr,
    sk_fixformatter_field_t   *field,
    const char             *title)
{
    assert(fmtr);
    assert(field);
    if (FMTR_IS_FINALIZED(fmtr)) {
        return;
    }
    if (SIZE_MAX == fmtr_find_field(fmtr, field)) {
        return;
    }
    if (field->title) {
        free(field->title);
    }
    field->title = strdup(title);
}




/*
** Local Variables:
** mode:c
** indent-tabs-mode:nil
** c-basic-offset:4
** End:
*/
