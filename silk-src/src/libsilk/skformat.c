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

RCSIDENT("$SiLK: skformat.c efd886457770 2017-06-21 18:43:23Z mthomas $");

#include <silk/rwrec.h>
#include <silk/skformat.h>
#include <silk/skipaddr.h>
#include <silk/sksite.h>
#include <silk/skvector.h>
#include <silk/utils.h>


/* DEFINES AND TYPEDEFS */

/*
 *    The initial size of output buffer; this is also the step size
 *    when the buffer must be resized.
 */
#define SK_FORMATTER_DEFAULT_BUFSIZE  256

/*
 *    sk_formatter_t is the record formatter
 */
struct sk_formatter_st {
    /* the buffer holding the output */
    char                   *buffer;
    /* the currently allocated length of 'buffer' */
    size_t                  bufsize;
    /* an array of sk_formatter_field_t objects; one for each field
     * that is to be formatted */
    sk_vector_t            *fields;
    /* default timestamp format */
    uint32_t                timestamp_fmt;
    /* default ip address format */
    skipaddr_flags_t        ipaddr_fmt;
    /* character to put between fields */
    char                    delimeter;
    /* when true, no changes are allowed to the formatter */
    unsigned                finalized        : 1;
    /* when true, do not produce columnar output */
    unsigned                no_columns       : 1;
    /* when true, set field width so complete title is printed */
    unsigned                full_titles      : 1;
    /* when true, do not put a delimeter after the final field */
    unsigned                no_final_delim   : 1;
    /* when true, do not put a newline after the final field */
    unsigned                no_final_newline : 1;
    /* when true and output is columnar, set column widths on the
     * assumption that all IPs are IPv4 */
    unsigned                assume_ipv4_ips  : 1;
};
/* sk_formatter_t */


/*
 *    Signature of a callback function to return the number of
 *    characters required to format 'field'.
 */
typedef size_t (*fmtr_field_get_length_fn)(
    const sk_formatter_field_t *feild);

/*
 *    Signature of a callback function to format the 'field' field of
 *    'rec' into the buffer 'buf' where 'buflen' is the size of 'buf'.
 */
typedef size_t (*fmtr_field_to_string_fn)(
    const sk_formatter_field_t *field,
    const rwRec                *rec,
    char                       *buf,
    size_t                      buflen);

/*
 *    sk_formatter_field_t is an individual field formatter
 */
struct sk_formatter_field_st {
    fmtr_field_to_string_fn  to_string;
    fmtr_field_get_length_fn get_length;

    sk_formatter_field_extra_t  get_value_extra_fn;
    void                       *extra_callback_data;

    /* Title for field.  If NULL, a default title will be generated
     * from the IE */
    char                   *title;

    /* Name of the field.  If NULL, a default title will be generated
     * from the IE */
    char                   *name;

    /* The data type of the field. */
    sk_sidecar_type_t       data_type;

    /* Maximum field width.  Value ignored unless the 'max_width_set'
     * bit below is nonzero. */
    size_t                  max_width;

    /* Minimum (and desired) field width.  Value ignored unless the
     * 'min_width_set' bit below is nonzero. */
    size_t                  min_width;

    /* Timestamp format */
    uint32_t                timestamp_fmt;

    /* text to print after this field, typically "|" or "|\n" */
    char                    delim[4];

    sk_field_ident_t        ipfix_ident;

    /* IP address flags */
    skipaddr_flags_t        ipaddr_fmt;

    /* SiLK field */
    rwrec_field_id_t        rwrec_field;

    /* Precision (for floating point numbers) */
    uint8_t                 precision;

    unsigned                left_justify      : 1;
    unsigned                hexadecimal       : 1;
    unsigned                decimal           : 1;
    unsigned                space_pad         : 1;
    unsigned                assume_ipv4       : 1;
    unsigned                full_title        : 1;

    unsigned                empty             : 1;

    unsigned                min_width_set     : 1;
    unsigned                max_width_set     : 1;
    unsigned                ipaddr_fmt_set    : 1;
    unsigned                timestamp_fmt_set : 1;
};
/* sk_formatter_field_t */


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

/*    Return number of fields in 'm_fmtr' */
#define FMTR_GET_FIELD_COUNT(m_fmtr)            \
    skVectorGetCount((m_fmtr)->fields)

/*    Return field at position 'm_pos' in 'm_fmtr' or NULL when
 *    'm_pos' is too large. */
#define FMTR_GET_FIELD_AT(m_fmtr, m_pos)                        \
    ((sk_formatter_field_t*)                                    \
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
 *    Return the number of characters required to format 'field',
 *    where 'field' is a traditional rwRec field.
 */
static size_t
fmtr_rwrec_get_length(
    const sk_formatter_field_t *field)
{
    if (NULL == field) {
        return 0;
    }
    switch (field->rwrec_field) {
      case RWREC_FIELD_SIP:
      case RWREC_FIELD_DIP:
      case RWREC_FIELD_NHIP:
        /* ip numbers */
        if (field->assume_ipv4) {
            switch (field->ipaddr_fmt) {
              case SKIPADDR_DECIMAL:
                return 10;
              case SKIPADDR_HEXADECIMAL:
                return 8;
              case SKIPADDR_FORCE_IPV6:
                return 16;
              default:
                return 15;
            }
        }
        if (field->ipaddr_fmt == SKIPADDR_HEXADECIMAL) {
            return 32;
        }
        return 39;

      case RWREC_FIELD_SPORT:
      case RWREC_FIELD_DPORT:
        /* sport and dport */
      case RWREC_FIELD_APPLICATION:
        /* application */
        return ((field->hexadecimal) ? 4 : 5);

      case RWREC_FIELD_PROTO:
        /* protocol */
      case RWREC_FIELD_ICMP_TYPE:
      case RWREC_FIELD_ICMP_CODE:
        /* single column with ICMP type or code */
        return ((field->hexadecimal) ? 2 : 3);

      case RWREC_FIELD_PKTS:
      case RWREC_FIELD_BYTES:
        /* packets, bytes (potentially 20 chars wide) */
        return ((field->hexadecimal) ? 8 : 10);
        /* return ((field->hexadecimal) ? 16 : 20); */

      case RWREC_FIELD_INPUT:
      case RWREC_FIELD_OUTPUT:
        /* these are now 32 bit values, but continue to use the 16 bit
         * width so "make check" tests will pass. */
        /* input,output */
        return ((field->hexadecimal) ? 4 : 5);

      case RWREC_FIELD_FLAGS:
      case RWREC_FIELD_INIT_FLAGS:
      case RWREC_FIELD_REST_FLAGS:
        /* tcp flags, init-flags, non-init-flags */
        return ((field->decimal)
                ? 3
                : ((field->hexadecimal)
                   ? 2
                   : SK_TCPFLAGS_STRLEN - 1));

      case RWREC_FIELD_TCP_STATE:
        /* tcp-state */
        return ((field->decimal)
                ? 3
                : ((field->hexadecimal)
                   ? 2
                   : SK_TCP_STATE_STRLEN - 1));

      case RWREC_FIELD_ELAPSED:
        /* elapsed/duration */
        if (field->timestamp_fmt & SKTIMESTAMP_NOMSEC) {
            return 5;
        }
        return 9;

      case RWREC_FIELD_STIME:
      case RWREC_FIELD_ETIME:
        /* sTime and end time */
        if (field->timestamp_fmt & SKTIMESTAMP_EPOCH) {
            if (field->timestamp_fmt & SKTIMESTAMP_NOMSEC) {
                return 10;
            }
            return 14;
        }
        if (field->timestamp_fmt & SKTIMESTAMP_NOMSEC) {
            return 19;
        }
        return 23;

      case RWREC_FIELD_SID:
        /* sensor */
        return ((field->decimal)
                ? 5
                : ((field->hexadecimal)
                   ? 4
                   : sksiteSensorGetMaxNameStrLen()));

      case RWREC_FIELD_FTYPE_CLASS:
        /* flow-type class */
        return (uint8_t)sksiteClassGetMaxNameStrLen();

      case RWREC_FIELD_FTYPE_TYPE:
        /* flow-type type */
        return (uint8_t)sksiteFlowtypeGetMaxTypeStrLen();
    }

    return 0;                   /* NOTREACHED */
}


/**
 *    Return the number of characters required to format 'field',
 *    where 'field' is a traditional rwRec field.
 */
static size_t
fmtr_rwrec_to_string(
    const sk_formatter_field_t *field,
    const rwRec                *rec,
    char                       *buf,
    size_t                      len)
{

#define FMTR_DO_RWREC_IPADDR(ipaddr_getter)                     \
    {                                                           \
        char addrbuf[SK_NUM2DOT_STRLEN];                        \
        skipaddr_t addr;                                        \
                                                                \
        ipaddr_getter(rec, &addr);                              \
        skipaddrString(addrbuf, &addr, field->ipaddr_fmt);      \
        return snprintf(buf, len, "%s", addrbuf);               \
    }

#define FMTR_DO_RWREC_NUMBER(number_getter)             \
    {                                                   \
        uint64_t u64;                                   \
                                                        \
        u64 = number_getter(rec);                       \
        if (field->hexadecimal) {                       \
            return snprintf(buf, len, "%" PRIx64, u64); \
        }                                               \
        return snprintf(buf, len, "%" PRIu64, u64);     \
    }

#define FMTR_DO_RWREC_TCP_FLAGS(tcpflags_getter)                        \
    {                                                                   \
        char flagsbuf[SK_NUM2DOT_STRLEN];                               \
        uint8_t flags;                                                  \
                                                                        \
        flags = tcpflags_getter(rec);                                   \
        if (field->hexadecimal) {                                       \
            return snprintf(buf, len, "%x", flags);                     \
        }                                                               \
        if (field->decimal) {                                           \
            return snprintf(buf, len, "%u", flags);                     \
        }                                                               \
        skTCPFlagsString(flags, flagsbuf,                               \
                         field->space_pad ? SK_PADDED_FLAGS : 0);       \
        return snprintf(buf, len, "%s", flagsbuf);                      \
    }

#define FMTR_DO_RWREC_TIME(time_getter)                         \
    {                                                           \
        char timebuf[SKTIMESTAMP_STRLEN];                       \
        sktime_t t;                                             \
                                                                \
        t = time_getter(rec);                                   \
        sktimestamp_r(timebuf, t, field->timestamp_fmt);        \
        return snprintf(buf, len, "%s", timebuf);               \
    }

    assert(field);
    assert(rec);
    assert(buf);

    if (len) {
        buf[0] = '\0';
    }
    if (NULL == field) {
        return 0;
    }
    switch (field->rwrec_field) {
      case RWREC_FIELD_SIP:
        FMTR_DO_RWREC_IPADDR(rwRecMemGetSIP);
      case RWREC_FIELD_DIP:
        FMTR_DO_RWREC_IPADDR(rwRecMemGetDIP);
      case RWREC_FIELD_NHIP:
        FMTR_DO_RWREC_IPADDR(rwRecMemGetNhIP);

      case RWREC_FIELD_SPORT:
        FMTR_DO_RWREC_NUMBER(rwRecGetSPort);
      case RWREC_FIELD_DPORT:
        FMTR_DO_RWREC_NUMBER(rwRecGetDPort);
      case RWREC_FIELD_APPLICATION:
        FMTR_DO_RWREC_NUMBER(rwRecGetApplication);
      case RWREC_FIELD_PROTO:
        FMTR_DO_RWREC_NUMBER(rwRecGetProto);
      case RWREC_FIELD_PKTS:
        FMTR_DO_RWREC_NUMBER(rwRecGetPkts);
      case RWREC_FIELD_BYTES:
        FMTR_DO_RWREC_NUMBER(rwRecGetBytes);
      case RWREC_FIELD_INPUT:
        FMTR_DO_RWREC_NUMBER(rwRecGetInput);
      case RWREC_FIELD_OUTPUT:
        FMTR_DO_RWREC_NUMBER(rwRecGetOutput);

      case RWREC_FIELD_ICMP_TYPE:
        if (rwRecIsICMP(rec)) {
            FMTR_DO_RWREC_NUMBER(rwRecGetIcmpType);
        }
        return 0;
      case RWREC_FIELD_ICMP_CODE:
        if (rwRecIsICMP(rec)) {
            FMTR_DO_RWREC_NUMBER(rwRecGetIcmpCode);
        }
        return 0;

      case RWREC_FIELD_FLAGS:
        FMTR_DO_RWREC_TCP_FLAGS(rwRecGetFlags);
      case RWREC_FIELD_INIT_FLAGS:
        FMTR_DO_RWREC_TCP_FLAGS(rwRecGetInitFlags);
      case RWREC_FIELD_REST_FLAGS:
        FMTR_DO_RWREC_TCP_FLAGS(rwRecGetRestFlags);

      case RWREC_FIELD_TCP_STATE:
        {
            char flagsbuf[SK_NUM2DOT_STRLEN];
            uint8_t flags;

            flags = rwRecGetTcpState(rec);
            if (field->hexadecimal) {
                return snprintf(buf, len, "%x", flags);
            }
            if (field->decimal) {
                return snprintf(buf, len, "%u", flags);
            }
            skTCPStateString(flags, flagsbuf,
                             field->space_pad ? SK_PADDED_FLAGS : 0);
            return snprintf(buf, len, "%s", flagsbuf);
        }

      case RWREC_FIELD_ELAPSED:
        /* elapsed/duration */
        {
            imaxdiv_t dur;

            if (field->timestamp_fmt & SKTIMESTAMP_NOMSEC) {
                return snprintf(buf, len, "%u", rwRecGetElapsedSeconds(rec));
            }
            dur = imaxdiv(rwRecGetElapsed(rec), 1000);
            return snprintf(buf, len, ("%" PRIdMAX ".%03" PRIdMAX),
                            dur.quot, dur.rem);
        }

      case RWREC_FIELD_STIME:
        FMTR_DO_RWREC_TIME(rwRecGetStartTime);
      case RWREC_FIELD_ETIME:
        FMTR_DO_RWREC_TIME(rwRecGetEndTime);

      case RWREC_FIELD_SID:
        {
            char sensorbuf[SK_MAX_STRLEN_SENSOR+1];
            sk_sensor_id_t sensor;

            sensor = rwRecGetSensor(rec);
            if (field->hexadecimal) {
                return snprintf(buf, len, "%x", sensor);
            }
            if (field->decimal) {
                return snprintf(buf, len, "%u", sensor);
            }
            sksiteSensorGetName(sensorbuf, sizeof(sensorbuf), sensor);
            return snprintf(buf, len, "%s", sensorbuf);
        }

      case RWREC_FIELD_FTYPE_CLASS:
        {
            char flowtypebuf[SK_MAX_STRLEN_FLOWTYPE+1];
            sksiteFlowtypeGetClass(flowtypebuf, sizeof(flowtypebuf),
                                   rwRecGetFlowType(rec));
            return snprintf(buf, len, "%s", flowtypebuf);
        }

      case RWREC_FIELD_FTYPE_TYPE:
        {
            char flowtypebuf[SK_MAX_STRLEN_FLOWTYPE+1];
            sksiteFlowtypeGetType(flowtypebuf, sizeof(flowtypebuf),
                                  rwRecGetFlowType(rec));
            return snprintf(buf, len, "%s", flowtypebuf);
        }
    }

    return 0;                   /* NOTREACHED */
}


/**
 *    Return the number of characters required to format 'field'.
 *
 *    This is the default get_length() function when no other has been
 *    set/provided.
 */
static size_t
fmtr_default_get_length(
    const sk_formatter_field_t *field)
{
    if (NULL == field) {
        return 0;
    }
    switch (field->data_type) {
      case SK_SIDECAR_UINT8:
        return field->hexadecimal ? 2 : 3;
      case SK_SIDECAR_UINT16:
        return field->hexadecimal ? 4 : 5;
      case SK_SIDECAR_UINT32:
        return field->hexadecimal ? 8 : 10;
      case SK_SIDECAR_UINT64:
        return field->hexadecimal ? 16 : 19;
      case SK_SIDECAR_DOUBLE:
        return 20;
      case SK_SIDECAR_STRING:
      case SK_SIDECAR_BINARY:
        return 20;              /* FIXME */
      case SK_SIDECAR_ADDR_IP6:
        if (!field->assume_ipv4) {
            if (field->ipaddr_fmt == SKIPADDR_HEXADECIMAL) {
                return 32;
            }
            return 39;
        }
        /* FALLTHROUGH */
      case SK_SIDECAR_ADDR_IP4:
        switch (field->ipaddr_fmt) {
          case SKIPADDR_DECIMAL:
            return 10;
          case SKIPADDR_HEXADECIMAL:
            return 8;
          default:
            return 15;
        }
      case SK_SIDECAR_DATETIME:
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
      case SK_SIDECAR_BOOLEAN:
        return 1;
      case SK_SIDECAR_EMPTY:
        return 0;
      case SK_SIDECAR_LIST:
      case SK_SIDECAR_TABLE:
        return 0;               /* FIXME */
      case SK_SIDECAR_UNKNOWN:
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
    const sk_formatter_field_t *field,
    const rwRec                *rec,
    char                       *buf,
    size_t                      len)
{
    lua_State *L;
    int obj_type;
    size_t rv = 0;

    /* FIXME: should we put an error string into buffer on error? */

    assert(field);
    assert(rec);
    assert(buf);

    if (len) {
        buf[0] = '\0';
    }
    L = rec->lua_state;
    if (NULL == L) {
        return 0;
    }

    /* get the sidecar table from the record */
    if (lua_rawgeti(L, LUA_REGISTRYINDEX, rwRecGetSidecar(rec))
        != LUA_TTABLE)
    {
        /* no sidecar data */
        lua_pop(L, 1);
        return 0;
    }
    /* get the field */
    obj_type = lua_getfield(L, -1, field->name);
    if (LUA_TNIL == obj_type) {
        /* field is not present */
        goto END;
    }

    /* format the field */
    switch (field->data_type) {
      case SK_SIDECAR_UINT8:
      case SK_SIDECAR_UINT16:
      case SK_SIDECAR_UINT32:
      case SK_SIDECAR_UINT64:
        {
            lua_Integer n;
            int is_num;

            n = lua_tointegerx(L, -1, &is_num);
            if (is_num) {
                if (field->hexadecimal) {
                    rv = snprintf(buf, len, "%" PRIx64, (uint64_t)n);
                } else {
                    rv = snprintf(buf, len, "%" PRIu64, (uint64_t)n);
                }
            }
            break;
        }
      case SK_SIDECAR_DOUBLE:
        {
            lua_Number n;
            int is_num;

            n = lua_tonumberx(L, -1, &is_num);
            if (is_num) {
                rv = snprintf(buf, len, "%.*f", field->precision, (double)n);
            }
            break;
        }
      case SK_SIDECAR_STRING:
        {
            const char *str;
            str = lua_tolstring(L, -1, NULL);
            if (str) {
                rv = snprintf(buf, len, "%s", str);
            }
            break;
        }
      case SK_SIDECAR_BINARY:
        {
            const char *str;
            char *bp;
            size_t sz = 0;
            size_t i;

            str = lua_tolstring(L, -1, &sz);
            bp = buf;
            if (str && sz) {
                rv = 1;
                for (i = 0; i < sz; ++i) {
                    switch (str[i]) {
                      case '\\':
                        rv += 2;
                        if (rv < len) {
                            *bp++ = '\\';
                            *bp++ = '\\';
                        }
                        break;
                      case '\t':
                        rv += 2;
                        if (rv < len) {
                            *bp++ = '\\';
                            *bp++ = 't';
                        }
                        break;
                      case '\n':
                        rv += 2;
                        if (rv < len) {
                            *bp++ = '\\';
                            *bp++ = 'n';
                        }
                        break;
                      case '\v':
                        rv += 2;
                        if (rv < len) {
                            *bp++ = '\\';
                            *bp++ = 'v';
                        }
                        break;
                      case '\f':
                        rv += 2;
                        if (rv < len) {
                            *bp++ = '\\';
                            *bp++ = 'f';
                        }
                        break;
                      case '\r':
                        rv += 2;
                        if (rv < len) {
                            *bp++ = '\\';
                            *bp++ = 'r';
                        }
                        break;
                      default:
                        if (isprint((int)str[i])) {
                            ++rv;
                            if (rv < len) {
                                *bp++ = str[i];
                            }
                            break;
                        }
                        rv = 1 + 2 * len;
                        if (len >= rv) {
                            for (i = 0, bp = buf; i < sz; ++i, bp += 2) {
                                assert(bp - buf < (ssize_t)len);
                                sprintf(bp, "%02x", str[i]);
                            }
                            *bp = '\0';
                            goto END;
                        }
                        break;
                    }
                }
            }
            *bp = '\0';
            break;
        }
      case SK_SIDECAR_ADDR_IP4:
      case SK_SIDECAR_ADDR_IP6:
        {
            char addrbuf[SK_NUM2DOT_STRLEN];
            skipaddr_t *addr;

            addr = sk_lua_toipaddr(L, -1);
            if (addr) {
                skipaddrString(addrbuf, addr, field->ipaddr_fmt);
                rv = snprintf(buf, len, "%s", addrbuf);
            }
            break;
        }
      case SK_SIDECAR_DATETIME:
        {
            char datebuf[SKTIMESTAMP_STRLEN];
            sktime_t *t;

            t = sk_lua_todatetime(L, -1);
            if (t) {
                sktimestamp_r(datebuf, *t, field->timestamp_fmt);
                rv = snprintf(buf, len, "%s", datebuf);
            }
            break;
        }
      case SK_SIDECAR_BOOLEAN:
        rv = snprintf(buf, len, "%d", lua_toboolean(L, -1));
        break;
      case SK_SIDECAR_EMPTY:
        break;
      case SK_SIDECAR_LIST:
      case SK_SIDECAR_TABLE:
        rv = snprintf(buf, len, "unimplemented");
        break;
      case SK_SIDECAR_UNKNOWN:
        break;
    }

  END:
    lua_pop(L, 2);
    return rv;
}



static const char *
fmtr_field_get_title(
    const sk_formatter_field_t *field)
{
    assert(field);
    if (field->title) {
        return field->title;
    }
    if (field->name) {
        return field->name;
    }
    assert(field->get_length == fmtr_rwrec_get_length);
    switch (field->rwrec_field) {
      case RWREC_FIELD_SIP:          return "sIP";
      case RWREC_FIELD_DIP:          return "dIP";
      case RWREC_FIELD_SPORT:        return "sPort";
      case RWREC_FIELD_DPORT:        return "dPort";
      case RWREC_FIELD_PROTO:        return "protocol";
      case RWREC_FIELD_PKTS:         return "packets";
      case RWREC_FIELD_BYTES:        return "bytes";
      case RWREC_FIELD_FLAGS:        return "flags";
      case RWREC_FIELD_STIME:        return "sTime";
      case RWREC_FIELD_ELAPSED:      return "duration";
      case RWREC_FIELD_ETIME:        return "eTime";
      case RWREC_FIELD_SID:          return "sensor";
      case RWREC_FIELD_INPUT:        return "in";
      case RWREC_FIELD_OUTPUT:       return "out";
      case RWREC_FIELD_NHIP:         return "nhIP";
      case RWREC_FIELD_INIT_FLAGS:   return "initialFlags";
      case RWREC_FIELD_REST_FLAGS:   return "sessionFlags";
      case RWREC_FIELD_TCP_STATE:    return "attributes";
      case RWREC_FIELD_APPLICATION:  return "application";
      case RWREC_FIELD_FTYPE_CLASS:  return "class";
      case RWREC_FIELD_FTYPE_TYPE:   return "type";
      case RWREC_FIELD_ICMP_TYPE:    return "iType";
      case RWREC_FIELD_ICMP_CODE:    return "iCode";
    }
    return "<unknown>";
}


/**
 *    Return the position of 'field' in 'fmtr'.  Return SIZE_MAX when
 *    'field' is not in 'fmtr'.
 */
static size_t
fmtr_find_field(
    const sk_formatter_t       *fmtr,
    const sk_formatter_field_t *field)
{
    const sk_formatter_field_t *f;
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
    sk_formatter_t             *fmtr,
    const sk_formatter_field_t *field,
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

        fmtr->bufsize += plen + SK_FORMATTER_DEFAULT_BUFSIZE;
        fmtr->buffer = (char*)realloc(fmtr->buffer, fmtr->bufsize);
        if (NULL == fmtr->buffer) {
            free(oldbuf);
            skAppPrintOutOfMemory("Could not resize formatter print buffer");
            exit(EXIT_FAILURE);
        }
        bufpos->pos = fmtr->buffer + bufpos->len;
        bufpos->left += SK_FORMATTER_DEFAULT_BUFSIZE;
        /* we must format the same field again */
        rv = 0;
    }

    return rv;
}


sk_formatter_t *
sk_formatter_create(
    void)
{
    sk_formatter_t *fmtr;

    fmtr = sk_alloc(sk_formatter_t);
    fmtr->delimeter = '|';

    fmtr->fields = skVectorNew(sizeof(sk_formatter_field_t));
    return fmtr;
}


void
sk_formatter_destroy(
    sk_formatter_t     *fmtr)
{
    sk_formatter_field_t *f;
    size_t i;

    if (fmtr) {
        for (i = 0; i < sk_vector_get_count(fmtr->fields); ++i) {
            f = ((sk_formatter_field_t *)
                 sk_vector_get_value_pointer(fmtr->fields, i));
            free(f->title);
            free(f->name);
        }
        skVectorDestroy(fmtr->fields);
        free(fmtr->buffer);
        memset(fmtr, 0, sizeof(*fmtr));
        free(fmtr);
    }
}


size_t
sk_formatter_get_field_count(
    const sk_formatter_t   *fmtr)
{
    assert(fmtr);
    return FMTR_GET_FIELD_COUNT(fmtr);
}


sk_formatter_field_t *
sk_formatter_get_field(
    const sk_formatter_t   *fmtr,
    size_t                  position)
{
    assert(fmtr);
    return FMTR_GET_FIELD_AT(fmtr, position);
}


/* Finalize a formatter so that it is ready to be used for output */
int
sk_formatter_finalize(
    sk_formatter_t     *fmtr)
{
    sk_formatter_field_t *field;
    const char *title;
    size_t len;
    size_t i;
    size_t total_width;

    assert(fmtr);
    if (FMTR_IS_FINALIZED(fmtr)) {
        return 0;
    }

    fmtr->finalized = 1;
    total_width = 0;
    for (i = 0; (field = FMTR_GET_FIELD_AT(fmtr, i)) != NULL; ++i) {
        field->assume_ipv4 = fmtr->assume_ipv4_ips;
        if (!field->ipaddr_fmt_set) {
            field->ipaddr_fmt = fmtr->ipaddr_fmt;
        }
        if (!field->timestamp_fmt_set) {
            field->timestamp_fmt = fmtr->timestamp_fmt;
        }
        if (!field->min_width_set) {
            field->min_width = field->get_length(field);
        }
        if (fmtr->full_titles || field->full_title) {
            field->full_title = 1;
            title = fmtr_field_get_title(field);
            len = strlen(title);
            if (len > field->min_width) {
                field->min_width = len;
            }
        }
        field->min_width_set = 1;
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

    /* Note: we accounted for the final delimiter twice (once in the
     * for() loop and again immediately above).  Subtract one to
     * account for the delimiter we added in the for(), then add one
     * to account for final '\0'. */

    if (total_width < SK_FORMATTER_DEFAULT_BUFSIZE) {
        total_width = SK_FORMATTER_DEFAULT_BUFSIZE;
    }
    if (fmtr->bufsize < total_width) {
        free(fmtr->buffer);
        fmtr->bufsize = total_width;
        fmtr->buffer = sk_alloc_array(char, fmtr->bufsize);
    }

    return 0;
}


sk_formatter_field_t *
sk_formatter_add_silk_field(
    sk_formatter_t     *fmtr,
    rwrec_field_id_t    id)
{
    sk_formatter_field_t field;
    size_t last;

    assert(fmtr);
    if (FMTR_IS_FINALIZED(fmtr)) {
        return NULL;
    }

    memset(&field, 0, sizeof(field));
    field.to_string = fmtr_rwrec_to_string;
    field.get_length = fmtr_rwrec_get_length;
    field.rwrec_field = id;
    if (0 == fmtr_rwrec_get_length(&field)) {
        /* unknown field */
        return NULL;
    }

    last = FMTR_GET_FIELD_COUNT(fmtr);
    if (skVectorAppendValue(fmtr->fields, &field)) {
        return NULL;
    }
    return FMTR_GET_FIELD_AT(fmtr, last);
}


sk_formatter_field_t *
sk_formatter_add_field(
    sk_formatter_t         *fmtr,
    const char             *name,
    size_t                  namelen,
    sk_sidecar_type_t       data_type,
    sk_field_ident_t        ident)
{
    sk_formatter_field_t field;
    size_t last;

    assert(fmtr);
    if (FMTR_IS_FINALIZED(fmtr)) {
        return NULL;
    }

    memset(&field, 0, sizeof(field));
    field.name = sk_alloc_array(char, namelen);
    memcpy(field.name, name, namelen);
    field.name[namelen - 1] = '\0';
    field.data_type = data_type;
    field.ipfix_ident = ident;
    field.to_string = fmtr_default_to_string;
    field.get_length = fmtr_default_get_length;

    field.precision = SK_FORMATTER_DEFAULT_FP_PRECISION;
    switch (field.data_type) {
      case SK_SIDECAR_BOOLEAN:
      case SK_SIDECAR_UINT8:
      case SK_SIDECAR_UINT16:
      case SK_SIDECAR_UINT32:
      case SK_SIDECAR_UINT64:
      case SK_SIDECAR_DOUBLE:
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


sk_formatter_field_t *
sk_formatter_add_extra_field(
    sk_formatter_t             *fmtr,
    sk_formatter_field_extra_t  get_value_extra_fn,
    void                       *callback_data,
    size_t                      min_width)
{
    sk_formatter_field_t field;
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
sk_formatter_add_from_sidecar(
    sk_formatter_t     *fmtr,
    const sk_sidecar_t *sidecar)
{
    sk_sidecar_iter_t iter;
    const sk_sidecar_elem_t *elem;
    char buf[4096];
    size_t len;

    assert(fmtr);
    if (FMTR_IS_FINALIZED(fmtr)) {
        return -1;
    }

    sk_sidecar_iter_bind(sidecar, &iter);
    while (sk_sidecar_iter_next(&iter, &elem) == SK_ITERATOR_OK) {
        len = sizeof(buf);
        if (NULL == sk_sidecar_elem_get_name(elem, buf, &len)) {
            /* name too long for 'buf' */
            continue;
        }
        if (sk_formatter_add_field(fmtr, buf, len,
                                   sk_sidecar_elem_get_data_type(elem),
                                   sk_sidecar_elem_get_ipfix_ident(elem))
            == NULL)
        {
            return -1;
        }
    }
    return 0;
}


size_t
sk_formatter_fill_title_buffer(
    sk_formatter_t     *fmtr,
    char              **output_buffer)
{
    sk_formatter_field_t *field;
    const char *title;
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

    i = 0;
    while ((field = FMTR_GET_FIELD_AT(fmtr, i)) != NULL) {
        title = fmtr_field_get_title(field);
        if (field->empty) {
            plen = 0;
        } else {
            plen = snprintf(bp.pos, bp.left, "%s", title);
        }
        if (plen > field->min_width) {
            plen = field->min_width;
        }
        if (!fmtr->no_columns && plen < field->min_width) {
            /* Add spaces to field to be at least min_width in size */
            size = ((field->min_width < bp.left) ? field->min_width : bp.left);
            spaces = ((plen >= size) ? 0 : (size - plen));
            if (spaces) {
                if (field->left_justify) {
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
sk_formatter_record_to_string_extra(
    sk_formatter_t     *fmtr,
    const rwRec        *record,
    void               *extra,
    char              **output_buffer)
{
    sk_formatter_field_t *field;
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

    i = 0;
    while ((field = FMTR_GET_FIELD_AT(fmtr, i)) != NULL) {
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
                if (field->left_justify) {
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
sk_formatter_record_to_string(
    sk_formatter_t     *fmtr,
    const rwRec        *record,
    char              **output_buffer)
{
    return sk_formatter_record_to_string_extra(
        fmtr, record, NULL, output_buffer);
}

void
sk_formatter_set_delimeter(
    sk_formatter_t     *fmtr,
    char                delimeter)
{
    assert(fmtr);
    if (FMTR_IS_FINALIZED(fmtr)) {
        return;
    }
    fmtr->delimeter = delimeter;
}

void
sk_formatter_set_no_columns(
    sk_formatter_t     *fmtr)
{
    assert(fmtr);
    if (FMTR_IS_FINALIZED(fmtr)) {
        return;
    }
    fmtr->no_columns = 1;
    fmtr->full_titles = 1;
}

void
sk_formatter_set_full_titles(
    sk_formatter_t     *fmtr)
{
    assert(fmtr);
    if (FMTR_IS_FINALIZED(fmtr)) {
        return;
    }
    fmtr->full_titles = 1;
}

void
sk_formatter_set_no_final_delimeter(
    sk_formatter_t     *fmtr)
{
    assert(fmtr);
    if (FMTR_IS_FINALIZED(fmtr)) {
        return;
    }
    fmtr->no_final_delim = 1;
}

void
sk_formatter_set_no_final_newline(
    sk_formatter_t     *fmtr)
{
    assert(fmtr);
    if (FMTR_IS_FINALIZED(fmtr)) {
        return;
    }
    fmtr->no_final_newline = 1;
}

void
sk_formatter_set_default_ipaddr_format(
    sk_formatter_t     *fmtr,
    skipaddr_flags_t    flags)
{
    assert(fmtr);
    if (FMTR_IS_FINALIZED(fmtr)) {
        return;
    }
    fmtr->ipaddr_fmt = flags;
}

void
sk_formatter_set_assume_ipv4_ips(
    sk_formatter_t     *fmtr)
{
    assert(fmtr);
    if (FMTR_IS_FINALIZED(fmtr)) {
        return;
    }
    fmtr->assume_ipv4_ips = 1;
}

void
sk_formatter_set_default_timestamp_format(
    sk_formatter_t     *fmtr,
    uint32_t            flags)
{
    assert(fmtr);
    if (FMTR_IS_FINALIZED(fmtr)) {
        return;
    }
    fmtr->timestamp_fmt = flags;
}

void
sk_formatter_field_set_empty(
    const sk_formatter_t   *fmtr,
    sk_formatter_field_t   *field)
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
sk_formatter_field_set_full_title(
    const sk_formatter_t   *fmtr,
    sk_formatter_field_t   *field)
{
    assert(fmtr);
    assert(field);
    if (FMTR_IS_FINALIZED(fmtr)) {
        return;
    }
    field->full_title = 1;
}

void
sk_formatter_field_set_ipaddr_format(
    const sk_formatter_t   *fmtr,
    sk_formatter_field_t   *field,
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
    field->ipaddr_fmt_set = 1;
}

void
sk_formatter_field_set_justification(
    const sk_formatter_t   *fmtr,
    sk_formatter_field_t   *field,
    sk_formatter_lr_t       left_or_right)
{
    assert(fmtr);
    assert(field);
    if (FMTR_IS_FINALIZED(fmtr)) {
        return;
    }
    if (SIZE_MAX == fmtr_find_field(fmtr, field)) {
        return;
    }
    field->left_justify = (SK_FMTR_LEFT == left_or_right);
}

void
sk_formatter_field_set_max_width(
    const sk_formatter_t   *fmtr,
    sk_formatter_field_t   *field,
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
sk_formatter_field_set_min_width(
    const sk_formatter_t   *fmtr,
    sk_formatter_field_t   *field,
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
sk_formatter_field_set_number_format(
    const sk_formatter_t   *fmtr,
    sk_formatter_field_t   *field,
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
sk_formatter_field_set_precision(
    const sk_formatter_t   *fmtr,
    sk_formatter_field_t   *field,
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
sk_formatter_field_set_space_padded(
    const sk_formatter_t   *fmtr,
    sk_formatter_field_t   *field)
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
sk_formatter_field_set_timestamp_format(
    const sk_formatter_t   *fmtr,
    sk_formatter_field_t   *field,
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
    field->timestamp_fmt_set = 1;
}

void
sk_formatter_field_set_title(
    const sk_formatter_t   *fmtr,
    sk_formatter_field_t   *field,
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
