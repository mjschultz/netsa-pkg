/*
** Copyright (C) 2015-2017 by Carnegie Mellon University.
**
** @OPENSOURCE_LICENSE_START@
** See license information in ../../LICENSE.txt
** @OPENSOURCE_LICENSE_END@
*/

/*
 *  skschema-test-lists.c
 *
 *    Application to test creating lists in schemas/records.
 *
 *    Mark Thomas
 *    2015 May
 */

/* make certain assert() is enabled */
#undef NDEBUG

#include <silk/silk.h>

RCSIDENT("$SiLK: skschema-test-lists.c efd886457770 2017-06-21 18:43:23Z mthomas $");

#include <silk/skdllist.h>
#include <silk/skipaddr.h>
#include <silk/skfixstream.h>
#include <silk/skipfixcert.h>
#include <silk/skschema.h>
#include <silk/utils.h>

/* #define TRACEMSG_LEVEL 4 */
/* #define TRACE_ENTRY_RETURN 1 */

#define TRACEMSG(lvl, msg)                      \
    TRACEMSG_TO_TRACEMSGLVL(lvl, msg)
#include <silk/sktracemsg.h>


#ifndef TRACE_ENTRY_RETURN
#  define TRACE_ENTRY
#  define TRACE_RETURN       return
#else
#  define TRACE_ENTRY                                                   \
    TRACEMSG(4, ("%s:%d: Entering %s", __FILE__, __LINE__, __func__))

#  define TRACE_RETURN                                                  \
    TRACEMSG(4, ("%s:%d: Exiting %s", __FILE__, __LINE__, __func__));   \
    return
#endif  /* TRACE_ENTRY_RETURN */


#define ASSERT_SCHEMA_SUCCESS(e)                                        \
    if (SK_SCHEMA_ERR_SUCCESS != e) {                                   \
        skAppPrintErr("schema function failed at %s:%d: [%d] %s",       \
                      __FILE__, __LINE__, e, sk_schema_strerror(e));    \
        assert(SK_SCHEMA_ERR_SUCCESS == e);                             \
    }


/* values used when creating varfield strings */
#define BUF_OFFSET  250
#define BUF_LEN      50


static fbInfoModel_t *model;
static sk_dllist_t *dllist_schema;


static void
appUsage(
    void)
{
#define USAGE_MSG                                                       \
    ("\n"                                                               \
     "\tOutput an IPFIX file that contains lists contains every other\n" \
     "\ttype of list.\n")

    FILE *fh = stderr;

    fprintf(fh, "Usage: %s %s", skAppName(), USAGE_MSG);
}


/*
 *    Callback function passed to skDLListCreate() to free each of the
 *    schemas it holds.
 */
static void
free_schema(
    void               *v_schema)
{
    TRACEMSG(3, ("freeing schema in dllist %p", v_schema));
    sk_schema_destroy((sk_schema_t*)v_schema);
}


/*
 *    Function to create a schema from a spec.
 */
#define create_schema(cs_spec)                          \
    create_schema_helper(cs_spec, __FILE__, __LINE__)

static sk_schema_t *
create_schema_helper(
    const fbInfoElementSpec_t   spec[],
    const char                 *file,
    int                         lineno)
{
    sk_schema_t *schema = NULL;
    sk_schema_err_t e;

    e = sk_schema_create(&schema, model, spec, 0);
    if (SK_SCHEMA_ERR_SUCCESS != e) {
        skAppPrintErr("schema create failed at %s:%d: [%d] %s",
                      file, lineno, e, sk_schema_strerror(e));
        assert(SK_SCHEMA_ERR_SUCCESS == e);
    }

    e = sk_schema_freeze(schema);
    if (SK_SCHEMA_ERR_SUCCESS != e) {
        skAppPrintErr("schema freeze failed at %s:%d: [%d] %s",
                      file, lineno, e, sk_schema_strerror(e));
        assert(SK_SCHEMA_ERR_SUCCESS == e);
    }

    TRACEMSG(3, ("%s:%d: adding schema to dllist %p",
                 file, lineno, (void*)schema));
    skDLListPushHead(dllist_schema, (void*)schema);

    return schema;
}


/*
 *    When 'num' is 0, return a (non-incref'ed) schema that contains
 *    egressInterface.
 *
 *    When 'num' is greater than 0, create 'num' records that contain
 *    egressInterface and append them to the sk_fixlist_t 'fixlist'.
 */
static sk_schema_t *
append_fixlist_egress(
    uint64_t            num,
    sk_fixlist_t       *fixlist)
{
#define NUM_FIELDS 1
    const uint64_t num_fields = NUM_FIELDS;
    static fbInfoElementSpec_t spec[1+NUM_FIELDS] = {
        {(char*)"egressInterface",              0, 0},
        FB_IESPEC_NULL
    };
    const sk_field_t *field[NUM_FIELDS];
#undef NUM_FIELDS
    static sk_schema_t *schema = NULL;
    static uint64_t counter = 1;
    sk_schema_err_t e;
    sk_fixrec_t rec;
    uint64_t i;
    uint32_t egress;

    TRACE_ENTRY;

    /* create the schema if it does not exist */
    if (!schema) {
        schema = create_schema(spec);
    }

    if (0 == num) {
        TRACE_RETURN schema;
    }

    for (i = 0; i < num_fields; ++i) {
        field[i] = sk_schema_get_field(schema, i);
        assert(0 == strcmp(spec[i].name, sk_field_get_name(field[i])));
    }

    /* create a record using that schema */
    e = sk_fixrec_init(&rec, schema);
    ASSERT_SCHEMA_SUCCESS(e);

    /* repeatedly set the fields on the record then append the record
     * to the fixlist */
    for ( ; num > 0; --num, ++counter) {
        egress = counter & UINT32_MAX;
        e = sk_fixrec_set_unsigned32(&rec, field[0], egress);
        ASSERT_SCHEMA_SUCCESS(e);

        e = sk_fixlist_append_fixrec(fixlist, &rec);
        ASSERT_SCHEMA_SUCCESS(e);
        sk_fixrec_clear(&rec);
    }

    sk_fixrec_destroy(&rec);
    TRACE_RETURN schema;
}


/*
 *    Return a fixlist that is a basicList of egressInterface where
 *    the fixlist contains 'num' elements.
 */
static sk_fixlist_t *
create_blist_egress(
    uint64_t            num)
{
    sk_fixlist_t *list;
    const sk_schema_t *schema;
    const sk_field_t *field;
    sk_schema_err_t e;

    TRACE_ENTRY;

    assert(num > 0);

    schema = append_fixlist_egress(0, NULL);
    assert(schema);
    field = sk_schema_get_field(schema, 0);

    e = sk_fixlist_create_basiclist_from_ident(&list, model,
                                               sk_field_get_ident(field));
    ASSERT_SCHEMA_SUCCESS(e);
    schema = append_fixlist_egress(num, list);
    assert(schema);

    TRACE_RETURN list;
}


/*
 *    When 'num' is 0, return a (non-incref'ed) schema that contains
 *    flowEndMilliseconds.
 *
 *    When 'num' is greater than 0, create 'num' records that contain
 *    flowEndMilliseconds and append them to the sk_fixlist_t
 *    'fixlist'.
 */
static sk_schema_t *
append_fixlist_etime(
    uint64_t            num,
    sk_fixlist_t       *fixlist)
{
#define NUM_FIELDS 1
    const uint64_t num_fields = NUM_FIELDS;
    static fbInfoElementSpec_t spec[1+NUM_FIELDS] = {
        {(char*)"flowEndMilliseconds",          0, 0},
        FB_IESPEC_NULL
    };
    const sk_field_t *field[NUM_FIELDS];
#undef NUM_FIELDS
    static sk_schema_t *schema = NULL;
    static uint64_t counter = 1;
    /* 2000-12-31 */
    sk_schema_err_t e;
    sk_fixrec_t rec;
    uint64_t i;
    const int64_t starting_etime = 86400 * (7 + 31 * 365);
    sktime_t etime;

    TRACE_ENTRY;

    /* create the schema if it does not exist */
    if (!schema) {
        schema = create_schema(spec);
    }

    if (0 == num) {
        TRACE_RETURN schema;
    }

    for (i = 0; i < num_fields; ++i) {
        field[i] = sk_schema_get_field(schema, i);
        assert(0 == strcmp(spec[i].name, sk_field_get_name(field[i])));
    }

    /* create a record using that schema */
    e = sk_fixrec_init(&rec, schema);
    ASSERT_SCHEMA_SUCCESS(e);

    /* repeatedly set the fields on the record then append the record
     * to the fixlist */
    for ( ; num > 0; --num, ++counter) {
        etime = sktimeCreate(starting_etime + counter * 86400, 0);
        e = sk_fixrec_set_datetime(&rec, field[0], etime);
        ASSERT_SCHEMA_SUCCESS(e);

        e = sk_fixlist_append_fixrec(fixlist, &rec);
        ASSERT_SCHEMA_SUCCESS(e);
        sk_fixrec_clear(&rec);
    }

    sk_fixrec_destroy(&rec);
    TRACE_RETURN schema;
}

/*
 *    Return a fixlist that is a basicList of flowEndMilliseconds
 *    where the fixlist contains 'num' elements.
 */
static sk_fixlist_t *
create_blist_etime(
    uint64_t            num)
{
    sk_fixlist_t *list;
    const sk_schema_t *schema;
    const sk_field_t *field;
    sk_schema_err_t e;

    TRACE_ENTRY;

    assert(num > 0);

    schema = append_fixlist_etime(0, NULL);
    assert(schema);
    field = sk_schema_get_field(schema, 0);

    e = sk_fixlist_create_basiclist_from_name(&list, model,
                                              sk_field_get_name(field));
    ASSERT_SCHEMA_SUCCESS(e);
    schema = append_fixlist_etime(num, list);
    assert(schema);

    TRACE_RETURN list;
}


/*
 *    When 'num' is 0, return a (non-incref'ed) schema that contains
 *    interfaceName.
 *
 *    When 'num' is greater than 0, create 'num' records that contain
 *    interfaceName and append them to the sk_fixlist_t
 *    'fixlist'.
 */
static sk_schema_t *
append_fixlist_iface(
    uint64_t            num,
    sk_fixlist_t       *fixlist)
{
#define NUM_FIELDS 1
    const uint64_t num_fields = NUM_FIELDS;
    static fbInfoElementSpec_t spec[1+NUM_FIELDS] = {
        {(char*)"interfaceName",                0, 0},
        FB_IESPEC_NULL
    };
    const sk_field_t *field[NUM_FIELDS];
#undef NUM_FIELDS
    static sk_schema_t *schema = NULL;
    static uint64_t counter = 1;
    sk_schema_err_t e;
    sk_fixrec_t rec;
    uint64_t i;
    char buf[BUF_OFFSET + BUF_LEN];

    TRACE_ENTRY;

    /* create the schema if it does not exist */
    if (!schema) {
        schema = create_schema(spec);
    }

    if (0 == num) {
        TRACE_RETURN schema;
    }

    for (i = 0; i < num_fields; ++i) {
        field[i] = sk_schema_get_field(schema, i);
        assert(0 == strcmp(spec[i].name, sk_field_get_name(field[i])));
    }

    /* create a record using that schema */
    e = sk_fixrec_init(&rec, schema);
    ASSERT_SCHEMA_SUCCESS(e);

    /* repeatedly set the fields on the record then append the record
     * to the fixlist */
    for ( ; num > 0; --num, ++counter) {
        memset(buf, '+', sizeof(buf));
        snprintf(buf + BUF_OFFSET, BUF_LEN, " %" PRIu64, counter);
        e = sk_fixrec_set_string(&rec, field[0], buf);
        ASSERT_SCHEMA_SUCCESS(e);

        e = sk_fixlist_append_fixrec(fixlist, &rec);
        ASSERT_SCHEMA_SUCCESS(e);
        sk_fixrec_clear(&rec);
    }

    sk_fixrec_destroy(&rec);
    TRACE_RETURN schema;
}


/*
 *    Return a fixlist that is a basicList of interfaceName where the
 *    fixlist contains 'num' elements.
 */
static sk_fixlist_t *
create_blist_iface(
    uint64_t            num)
{
    sk_fixlist_t *list;
    const sk_schema_t *schema;
    const sk_field_t *field;
    sk_schema_err_t e;

    TRACE_ENTRY;

    assert(num > 0);

    schema = append_fixlist_iface(0, NULL);
    assert(schema);
    field = sk_schema_get_field(schema, 0);

    e = sk_fixlist_create_basiclist_from_name(&list, model,
                                              sk_field_get_name(field));
    ASSERT_SCHEMA_SUCCESS(e);
    schema = append_fixlist_iface(num, list);
    assert(schema);

    TRACE_RETURN list;
}


/*
 *    Return a fixlist that is a basicList of basicList.
 */
static sk_fixlist_t *
create_blist_blist(
    void)
{
#define NUM_FIELDS 1
    const uint64_t num_fields = NUM_FIELDS;
    static fbInfoElementSpec_t spec[1+NUM_FIELDS] = {
        {(char*)"basicList",                    0, 0},
        FB_IESPEC_NULL
    };
    const sk_field_t *field[NUM_FIELDS];
#undef NUM_FIELDS
    static sk_schema_t *schema = NULL;
    sk_schema_err_t e;
    sk_fixrec_t rec;
    uint64_t i;
    sk_fixlist_t *bl_outer;
    sk_fixlist_t *bl_inner;

    TRACE_ENTRY;

    /* create the schema if it does not exist */
    if (!schema) {
        schema = create_schema(spec);
    }

    for (i = 0; i < num_fields; ++i) {
        field[i] = sk_schema_get_field(schema, i);
        assert(0 == strcmp(spec[i].name, sk_field_get_name(field[i])));
    }

    /* create a record using that schema */
    e = sk_fixrec_init(&rec, schema);
    ASSERT_SCHEMA_SUCCESS(e);

    /* create the (outer) bl */
    e = sk_fixlist_create_basiclist_from_ident(&bl_outer, model,
                                               sk_field_get_ident(field[0]));
    ASSERT_SCHEMA_SUCCESS(e);

    /* repeatedly set the fields on the record then append the record
     * to the fixlist */
    bl_inner = create_blist_egress(8);
    e = sk_fixrec_set_list(&rec, field[0], bl_inner);
    ASSERT_SCHEMA_SUCCESS(e);
    sk_fixlist_destroy(bl_inner);

    e = sk_fixlist_append_fixrec(bl_outer, &rec);
    ASSERT_SCHEMA_SUCCESS(e);
    sk_fixrec_clear(&rec);

    bl_inner = create_blist_iface(5);
    e = sk_fixrec_set_list(&rec, field[0], bl_inner);
    ASSERT_SCHEMA_SUCCESS(e);
    sk_fixlist_destroy(bl_inner);

    e = sk_fixlist_append_fixrec(bl_outer, &rec);
    ASSERT_SCHEMA_SUCCESS(e);
    sk_fixrec_clear(&rec);

    bl_inner = create_blist_etime(11);
    e = sk_fixrec_set_list(&rec, field[0], bl_inner);
    ASSERT_SCHEMA_SUCCESS(e);
    sk_fixlist_destroy(bl_inner);

    e = sk_fixlist_append_fixrec(bl_outer, &rec);
    ASSERT_SCHEMA_SUCCESS(e);
    sk_fixrec_clear(&rec);

    sk_fixrec_destroy(&rec);
    TRACE_RETURN bl_outer;
}


/*
 *    When 'num' is 0, return a (non-incref'ed) schema that contains
 *    sourceIPv4Address and destinationTransportPort.
 *
 *    When 'num' is greater than 0, create 'num' records that contain
 *    sourceIPv4Address and destinationTransportPort and append them
 *    to the sk_fixlist_t 'fixlist'.
 */
static sk_schema_t *
append_fixlist_sip_dport(
    uint64_t            num,
    sk_fixlist_t       *fixlist)
{
#define NUM_FIELDS 2
    const uint64_t num_fields = NUM_FIELDS;
    static fbInfoElementSpec_t spec[1+NUM_FIELDS] = {
        {(char*)"sourceIPv4Address",            0, 0},
        {(char*)"destinationTransportPort",     0, 0},
        FB_IESPEC_NULL
    };
    const sk_field_t *field[NUM_FIELDS];
#undef NUM_FIELDS
    static sk_schema_t *schema = NULL;
    static uint64_t counter = 1;
    sk_schema_err_t e;
    sk_fixrec_t rec;
    uint64_t i;
    uint32_t u32;
    skipaddr_t ip;
    uint16_t port;

    TRACE_ENTRY;

    /* create the schema if it does not exist */
    if (!schema) {
        schema = create_schema(spec);
    }

    if (0 == num) {
        TRACE_RETURN schema;
    }

    for (i = 0; i < num_fields; ++i) {
        field[i] = sk_schema_get_field(schema, i);
        assert(0 == strcmp(spec[i].name, sk_field_get_name(field[i])));
    }

    /* create a record using that schema */
    e = sk_fixrec_init(&rec, schema);
    ASSERT_SCHEMA_SUCCESS(e);

    /* repeatedly set the fields on the record then append the record
     * to the fixlist */
    for ( ; num > 0; --num, ++counter) {
        u32 = counter & UINT32_MAX;
        skipaddrSetV4(&ip, &u32);
        port = counter & UINT16_MAX;
        e = sk_fixrec_set_ip_address(&rec, field[0], &ip);
        ASSERT_SCHEMA_SUCCESS(e);
        e = sk_fixrec_set_unsigned16(&rec, field[1], port);
        ASSERT_SCHEMA_SUCCESS(e);

        e = sk_fixlist_append_fixrec(fixlist, &rec);
        ASSERT_SCHEMA_SUCCESS(e);
        sk_fixrec_clear(&rec);
    }

    sk_fixrec_destroy(&rec);
    TRACE_RETURN schema;
}


/*
 *    Return a fixlist that is a subTemplateList of sourceIPv4Address
 *    and destinationTransportPort where the fixlist contains 'num'
 *    elements.
 */
static sk_fixlist_t *
create_stl_sip_dport(
    uint64_t            num)
{
    sk_fixlist_t *list;
    const sk_schema_t *schema;
    sk_schema_err_t e;

    TRACE_ENTRY;

    assert(num > 0);

    schema = append_fixlist_sip_dport(0, NULL);
    assert(schema);

    e = sk_fixlist_create_subtemplatelist(&list, schema);
    ASSERT_SCHEMA_SUCCESS(e);
    schema = append_fixlist_sip_dport(num, list);
    assert(schema);

    TRACE_RETURN list;
}


/*
 *    When 'num' is 0, return a (non-incref'ed) schema that contains
 *    protocolIdentifier and flowStartMilliseconds.
 *
 *    When 'num' is greater than 0, create 'num' records that contain
 *    protocolIdentifier and flowStartMilliseconds and append them
 *    to the sk_fixlist_t 'fixlist'.
 */
static sk_schema_t *
append_fixlist_proto_stime(
    uint64_t            num,
    sk_fixlist_t       *fixlist)
{
#define NUM_FIELDS 2
    const uint64_t num_fields = NUM_FIELDS;
    static fbInfoElementSpec_t spec[1+NUM_FIELDS] = {
        {(char*)"protocolIdentifier",           0, 0},
        {(char*)"flowStartMilliseconds",        0, 0},
        FB_IESPEC_NULL
    };
    const sk_field_t *field[NUM_FIELDS];
#undef NUM_FIELDS
    static sk_schema_t *schema = NULL;
    static uint64_t counter = 1;
    sk_schema_err_t e;
    sk_fixrec_t rec;
    /* 1999-12-31 */
    uint64_t i;
    const int64_t starting_stime = 86400 * (6 + 30 * 365);
    sktime_t t;
    uint8_t proto;

    TRACE_ENTRY;

    /* create the schema if it does not exist */
    if (!schema) {
        schema = create_schema(spec);
    }

    if (0 == num) {
        TRACE_RETURN schema;
    }

    for (i = 0; i < num_fields; ++i) {
        field[i] = sk_schema_get_field(schema, i);
        assert(0 == strcmp(spec[i].name, sk_field_get_name(field[i])));
    }

    /* create a record using that schema */
    e = sk_fixrec_init(&rec, schema);
    ASSERT_SCHEMA_SUCCESS(e);

    /* repeatedly set the fields on the record then append the record
     * to the fixlist */
    for ( ; num > 0; --num, ++counter) {
        proto = counter & UINT8_MAX;
        t = sktimeCreate(starting_stime + counter * 86400, 0);
        e = sk_fixrec_set_unsigned8(&rec, field[0], proto);
        ASSERT_SCHEMA_SUCCESS(e);
        e = sk_fixrec_set_datetime(&rec, field[1], t);
        ASSERT_SCHEMA_SUCCESS(e);

        e = sk_fixlist_append_fixrec(fixlist, &rec);
        ASSERT_SCHEMA_SUCCESS(e);
        sk_fixrec_clear(&rec);
    }

    sk_fixrec_destroy(&rec);
    TRACE_RETURN schema;
}


/*
 *    Return a fixlist that is a subTemplateList of protocolIdentifier
 *    and flowStartMilliseconds where the fixlist contains 'num'
 *    elements.
 */
static sk_fixlist_t *
create_stl_proto_stime(
    uint64_t            num)
{
    sk_fixlist_t *list;
    const sk_schema_t *schema;
    sk_schema_err_t e;

    TRACE_ENTRY;

    assert(num > 0);

    schema = append_fixlist_proto_stime(0, NULL);
    assert(schema);

    e = sk_fixlist_create_subtemplatelist(&list, schema);
    ASSERT_SCHEMA_SUCCESS(e);
    schema = append_fixlist_proto_stime(num, list);
    assert(schema);

    TRACE_RETURN list;
}


/*
 *    When 'num' is 0, return a (non-incref'ed) schema that contains
 *    wlanSSID and sourceTransportPort
 *
 *    When 'num' is greater than 0, create 'num' records that contain
 *    wlanSSID and sourceTransportPort and append them to the
 *    sk_fixlist_t 'fixlist'.
 */
static sk_schema_t *
append_fixlist_ssid_sport(
    uint64_t            num,
    sk_fixlist_t       *fixlist)
{
#define NUM_FIELDS 2
    const uint64_t num_fields = NUM_FIELDS;
    static fbInfoElementSpec_t spec[1+NUM_FIELDS] = {
        {(char*)"wlanSSID",                     0, 0},
        {(char*)"sourceTransportPort",          0, 0},
        FB_IESPEC_NULL
    };
    const sk_field_t *field[NUM_FIELDS];
#undef NUM_FIELDS
    static sk_schema_t *schema = NULL;
    static uint64_t counter = 1;
    sk_schema_err_t e;
    sk_fixrec_t rec;
    uint64_t i;
    char buf[BUF_OFFSET + BUF_LEN];
    uint16_t port;

    TRACE_ENTRY;

    /* create the schema if it does not exist */
    if (!schema) {
        schema = create_schema(spec);
    }

    if (0 == num) {
        TRACE_RETURN schema;
    }

    for (i = 0; i < num_fields; ++i) {
        field[i] = sk_schema_get_field(schema, i);
        assert(0 == strcmp(spec[i].name, sk_field_get_name(field[i])));
    }

    /* create a record using that schema */
    e = sk_fixrec_init(&rec, schema);
    ASSERT_SCHEMA_SUCCESS(e);

    /* repeatedly set the fields on the record then append the record
     * to the fixlist */
    for ( ; num > 0; --num, ++counter) {
        memset(buf, '=', sizeof(buf));
        snprintf(buf + BUF_OFFSET, BUF_LEN, " %" PRIu64, counter);
        port = counter & UINT16_MAX;
        e = sk_fixrec_set_string(&rec, field[0], buf);
        ASSERT_SCHEMA_SUCCESS(e);
        e = sk_fixrec_set_unsigned16(&rec, field[1], port);
        ASSERT_SCHEMA_SUCCESS(e);

        e = sk_fixlist_append_fixrec(fixlist, &rec);
        ASSERT_SCHEMA_SUCCESS(e);
        sk_fixrec_clear(&rec);
    }

    sk_fixrec_destroy(&rec);
    TRACE_RETURN schema;
}


/*
 *    Return a fixlist that is a subTemplateList of wlanSSID and
 *    sourceTransportPort where the fixlist contains 'num' elements.
 */
static sk_fixlist_t *
create_stl_ssid_sport(
    uint64_t            num)
{
    sk_fixlist_t *list;
    const sk_schema_t *schema;
    sk_schema_err_t e;

    TRACE_ENTRY;

    assert(num > 0);

    schema = append_fixlist_ssid_sport(0, NULL);
    assert(schema);

    e = sk_fixlist_create_subtemplatelist(&list, schema);
    ASSERT_SCHEMA_SUCCESS(e);
    schema = append_fixlist_ssid_sport(num, list);
    assert(schema);

    TRACE_RETURN list;
}


/*
 *    When 'num' is 0, return a (non-incref'ed) schema that contains
 *    octetDeltaCount and basicList (of destinationIPv6Address).
 *
 *    When 'num' is greater than 0, create 'num' records that contain
 *    octetDeltaCount and basicList (of 10 destinationIPv6Addresses)
 *    and append them to the sk_fixlist_t 'fixlist'.
 */
static sk_schema_t *
append_fixlist_octet_blist(
    uint64_t            num,
    sk_fixlist_t       *fixlist)
{
#define NUM_FIELDS 2
    const uint64_t num_fields = NUM_FIELDS;
    static fbInfoElementSpec_t spec[1+NUM_FIELDS] = {
        {(char*)"octetDeltaCount",              0, 0},
        {(char*)"basicList",                    0, 0},
        FB_IESPEC_NULL
    };
    const sk_field_t *field[NUM_FIELDS];
#undef NUM_FIELDS
    static sk_schema_t *schema = NULL;
    static fbInfoElementSpec_t ipv6_spec[] = {
        {(char*)"destinationIPv6Address",       0, 0},
        FB_IESPEC_NULL
    };
    static sk_schema_t *ipv6_schema = NULL;
    static uint64_t counter = 1;
    const sk_field_t *ipv6_field;
    sk_schema_err_t e;
    sk_fixrec_t rec;
    uint64_t i;
    uint64_t j;
    sk_fixrec_t ipv6_rec;
    sk_fixlist_t *blist_ipv6;
    union ipv6_un {
        uint8_t     arr[16];
        uint64_t    u64[2];
    } ipv6;
    skipaddr_t ip;

    TRACE_ENTRY;

    /* create the schema if it does not exist */
    if (!schema) {
        schema = create_schema(spec);
    }

    if (0 == num) {
        TRACE_RETURN schema;
    }

    for (i = 0; i < num_fields; ++i) {
        field[i] = sk_schema_get_field(schema, i);
        assert(0 == strcmp(spec[i].name, sk_field_get_name(field[i])));
    }

    /* create a record using that schema */
    e = sk_fixrec_init(&rec, schema);
    ASSERT_SCHEMA_SUCCESS(e);

    /* create the schema used to create the basicList */
    if (!ipv6_schema) {
        ipv6_schema = create_schema(ipv6_spec);
    }

    /* create a record using the schema containing the IPv6 record */
    e = sk_fixrec_init(&ipv6_rec, ipv6_schema);
    ASSERT_SCHEMA_SUCCESS(e);
    ipv6_field = (sk_schema_get_field_by_name(
                      ipv6_schema, ipv6_spec[0].name, NULL));
    assert(ipv6_field);

    /* repeatedly set the fields on the record then append the record
     * to the fixlist */
    for ( ; num > 0; --num, ++counter) {
        e = sk_fixrec_set_unsigned64(&rec, field[0], counter);
        ASSERT_SCHEMA_SUCCESS(e);

        /* create and fill the basic list */
        e = sk_fixlist_create_basiclist_from_name(
            &blist_ipv6, model, ipv6_spec[0].name);
        ASSERT_SCHEMA_SUCCESS(e);

        ipv6.u64[0] = hton64(counter);
        for (j = 1; j <= 10; ++j) {
            ipv6.u64[1] = hton64(j);
            skipaddrSetV6(&ip, ipv6.arr);
            e = sk_fixrec_set_ip_address(&ipv6_rec, ipv6_field, &ip);
            ASSERT_SCHEMA_SUCCESS(e);

            e = sk_fixlist_append_element(blist_ipv6, &ipv6_rec, ipv6_field);
            ASSERT_SCHEMA_SUCCESS(e);
            sk_fixrec_clear(&ipv6_rec);
        }

        /* set the blist field on 'rec', and destroy the blist */
        e = sk_fixrec_set_list(&rec, field[1], blist_ipv6);
        ASSERT_SCHEMA_SUCCESS(e);
        sk_fixlist_destroy(blist_ipv6);

        e = sk_fixlist_append_fixrec(fixlist, &rec);
        ASSERT_SCHEMA_SUCCESS(e);
        sk_fixrec_clear(&rec);
    }

    sk_fixrec_destroy(&ipv6_rec);
    sk_fixrec_destroy(&rec);
    TRACE_RETURN schema;
}


/*
 *    Return a fixlist that is a subTemplateList of octetDeltaCount
 *    and a basicList (or destinationIPv6Addresses) where the fixlist
 *    contains 'num' elements.
 */
static sk_fixlist_t *
create_stl_octet_blist(
    uint64_t            num)
{
    sk_fixlist_t *list;
    const sk_schema_t *schema;
    sk_schema_err_t e;

    TRACE_ENTRY;

    assert(num > 0);

    schema = append_fixlist_octet_blist(0, NULL);
    assert(schema);

    e = sk_fixlist_create_subtemplatelist(&list, schema);
    ASSERT_SCHEMA_SUCCESS(e);
    schema = append_fixlist_octet_blist(num, list);
    assert(schema);

    TRACE_RETURN list;
}


/*
 *    Return a fixlist that is a subTemplateMultiList containing
 *    'num_proto_stime' records containing protocolIdentifier and
 *    flowStartMilliseconds and then contains 'num_sip_dport' records
 *    containing sourceIPv4Address and destinationTransportPort.
 */
static sk_fixlist_t *
create_stml_proto_stime__sip_dport(
    uint64_t            num_proto_stime,
    uint64_t            num_sip_dport)
{
    sk_fixlist_t *list;
    const sk_schema_t *schema;
    sk_schema_err_t e;

    TRACE_ENTRY;

    assert(num_proto_stime > 0);
    assert(num_sip_dport > 0);

    schema = append_fixlist_octet_blist(0, NULL);
    assert(schema);

    e = sk_fixlist_create_subtemplatemultilist(&list, model);
    ASSERT_SCHEMA_SUCCESS(e);
    schema = append_fixlist_proto_stime(num_proto_stime, list);
    assert(schema);
    schema = append_fixlist_sip_dport(num_sip_dport, list);
    assert(schema);

    TRACE_RETURN list;
}


/*
 *    Return a fixlist that is a subTemplateMultiList containing
 *    'num_ssid_sport' records containing wlanSSID and
 *    sourceTransportPort and then contains 'num_octets_blist' records
 *    containing octetDeltaCount and basicList (or
 *    destinationIPv6Addresses).
 */
static sk_fixlist_t *
create_stml_ssid_sport__octets_blist(
    uint64_t            num_ssid_sport,
    uint64_t            num_octets_blist)
{
    sk_fixlist_t *list;
    const sk_schema_t *schema;
    sk_schema_err_t e;

    TRACE_ENTRY;

    assert(num_ssid_sport > 0);
    assert(num_octets_blist > 0);

    schema = append_fixlist_octet_blist(0, NULL);
    assert(schema);

    e = sk_fixlist_create_subtemplatemultilist(&list, model);
    ASSERT_SCHEMA_SUCCESS(e);
    schema = append_fixlist_ssid_sport(num_ssid_sport, list);
    assert(schema);
    schema = append_fixlist_octet_blist(num_octets_blist, list);
    assert(schema);

    TRACE_RETURN list;
}


/*
 *    Return a fixlist that is a subTemplateList whose schema contains
 *    packetDeltaCount and subTemplateMultiList.
 */
static sk_fixlist_t *
create_stl_packets_stml(
    void)
{
#define NUM_FIELDS 2
    const uint64_t num_fields = NUM_FIELDS;
    static fbInfoElementSpec_t spec[1+NUM_FIELDS] = {
        {(char*)"packetDeltaCount",             0, 0},
        {(char*)"subTemplateMultiList",         0, 0},
        FB_IESPEC_NULL
    };
    const sk_field_t *field[NUM_FIELDS];
#undef NUM_FIELDS
    static sk_schema_t *schema = NULL;
    static uint64_t counter = 1;
    sk_schema_err_t e;
    sk_fixrec_t rec;
    uint64_t i;
    sk_fixlist_t *stl;
    sk_fixlist_t *stml;

    TRACE_ENTRY;

    /* create the schema if it does not exist */
    if (!schema) {
        schema = create_schema(spec);
    }

    for (i = 0; i < num_fields; ++i) {
        field[i] = sk_schema_get_field(schema, i);
        assert(0 == strcmp(spec[i].name, sk_field_get_name(field[i])));
    }

    /* create a record using that schema */
    e = sk_fixrec_init(&rec, schema);
    ASSERT_SCHEMA_SUCCESS(e);

    /* create the stl */
    e = sk_fixlist_create_subtemplatelist(&stl, schema);
    ASSERT_SCHEMA_SUCCESS(e);

    /* repeatedly set the fields on the record then append the record
     * to the fixlist */
    e = sk_fixrec_set_unsigned64(&rec, field[0], counter);
    ASSERT_SCHEMA_SUCCESS(e);

    stml = create_stml_ssid_sport__octets_blist(3, 7);
    e = sk_fixrec_set_list(&rec, field[1], stml);
    ASSERT_SCHEMA_SUCCESS(e);
    sk_fixlist_destroy(stml);

    e = sk_fixlist_append_fixrec(stl, &rec);
    ASSERT_SCHEMA_SUCCESS(e);
    sk_fixrec_clear(&rec);
    ++counter;

    e = sk_fixrec_set_unsigned64(&rec, field[0], counter);
    ASSERT_SCHEMA_SUCCESS(e);

    stml = create_stml_proto_stime__sip_dport(6, 4);
    e = sk_fixrec_set_list(&rec, field[1], stml);
    ASSERT_SCHEMA_SUCCESS(e);
    sk_fixlist_destroy(stml);

    e = sk_fixlist_append_fixrec(stl, &rec);
    ASSERT_SCHEMA_SUCCESS(e);
    sk_fixrec_clear(&rec);
    ++counter;

    sk_fixrec_destroy(&rec);
    TRACE_RETURN stl;
}


/*
 *    Return a fixlist that is a subTemplateList whose schema contains
 *    ingressInterface and subTemplateList.
 */
static sk_fixlist_t *
create_stl_ingress_stl(
    void)
{
#define NUM_FIELDS 2
    const uint64_t num_fields = NUM_FIELDS;
    static fbInfoElementSpec_t spec[1+NUM_FIELDS] = {
        {(char*)"ingressInterface",             0, 0},
        {(char*)"subTemplateList",              0, 0},
        FB_IESPEC_NULL
    };
    const sk_field_t *field[NUM_FIELDS];
#undef NUM_FIELDS
    static sk_schema_t *schema = NULL;
    static uint64_t counter = 1;
    sk_schema_err_t e;
    sk_fixrec_t rec;
    uint64_t i;
    sk_fixlist_t *stl_outer;
    sk_fixlist_t *stl_inner;

    TRACE_ENTRY;

    /* create the schema if it does not exist */
    if (!schema) {
        schema = create_schema(spec);
    }

    for (i = 0; i < num_fields; ++i) {
        field[i] = sk_schema_get_field(schema, i);
        assert(0 == strcmp(spec[i].name, sk_field_get_name(field[i])));
    }

    /* create a record using that schema */
    e = sk_fixrec_init(&rec, schema);
    ASSERT_SCHEMA_SUCCESS(e);

    /* create the (outer) stl */
    e = sk_fixlist_create_subtemplatelist(&stl_outer, schema);
    ASSERT_SCHEMA_SUCCESS(e);

    /* repeatedly set the fields on the record then append the record
     * to the fixlist */
    e = sk_fixrec_set_unsigned32(&rec, field[0], counter & UINT32_MAX);
    ASSERT_SCHEMA_SUCCESS(e);

    stl_inner = create_stl_proto_stime(5);
    e = sk_fixrec_set_list(&rec, field[1], stl_inner);
    ASSERT_SCHEMA_SUCCESS(e);
    sk_fixlist_destroy(stl_inner);

    e = sk_fixlist_append_fixrec(stl_outer, &rec);
    ASSERT_SCHEMA_SUCCESS(e);
    sk_fixrec_clear(&rec);
    ++counter;

    e = sk_fixrec_set_unsigned32(&rec, field[0], counter & UINT32_MAX);
    ASSERT_SCHEMA_SUCCESS(e);

    stl_inner = create_stl_sip_dport(5);
    e = sk_fixrec_set_list(&rec, field[1], stl_inner);
    ASSERT_SCHEMA_SUCCESS(e);
    sk_fixlist_destroy(stl_inner);

    e = sk_fixlist_append_fixrec(stl_outer, &rec);
    ASSERT_SCHEMA_SUCCESS(e);
    sk_fixrec_clear(&rec);
    ++counter;

    sk_fixrec_destroy(&rec);
    TRACE_RETURN stl_outer;
}


/*
 *    Return a fixlist that is a basicList of subTemplateList.
 */
static sk_fixlist_t *
create_blist_stl(
    void)
{
#define NUM_FIELDS 1
    const uint64_t num_fields = NUM_FIELDS;
    static fbInfoElementSpec_t spec[1+NUM_FIELDS] = {
        {(char*)"subTemplateList",              0, 0},
        FB_IESPEC_NULL
    };
    const sk_field_t *field[NUM_FIELDS];
#undef NUM_FIELDS
    static sk_schema_t *schema = NULL;
    sk_schema_err_t e;
    sk_fixrec_t rec;
    uint64_t i;
    sk_fixlist_t *bl;
    sk_fixlist_t *stl;

    TRACE_ENTRY;

    /* create the schema if it does not exist */
    if (!schema) {
        schema = create_schema(spec);
    }

    for (i = 0; i < num_fields; ++i) {
        field[i] = sk_schema_get_field(schema, i);
        assert(0 == strcmp(spec[i].name, sk_field_get_name(field[i])));
    }

    /* create a record using that schema */
    e = sk_fixrec_init(&rec, schema);
    ASSERT_SCHEMA_SUCCESS(e);

    /* create the bl */
    e = sk_fixlist_create_basiclist_from_name(&bl, model,
                                              sk_field_get_name(field[0]));
    ASSERT_SCHEMA_SUCCESS(e);

    /* repeatedly set the fields on the record then append the record
     * to the fixlist */
    stl = create_stl_ssid_sport(4);
    e = sk_fixrec_set_list(&rec, field[0], stl);
    ASSERT_SCHEMA_SUCCESS(e);
    sk_fixlist_destroy(stl);

    e = sk_fixlist_append_fixrec(bl, &rec);
    ASSERT_SCHEMA_SUCCESS(e);
    sk_fixrec_clear(&rec);

    stl = create_stl_octet_blist(7);
    e = sk_fixrec_set_list(&rec, field[0], stl);
    ASSERT_SCHEMA_SUCCESS(e);
    sk_fixlist_destroy(stl);

    e = sk_fixlist_append_fixrec(bl, &rec);
    ASSERT_SCHEMA_SUCCESS(e);
    sk_fixrec_clear(&rec);

    stl = create_stl_packets_stml();
    e = sk_fixrec_set_list(&rec, field[0], stl);
    ASSERT_SCHEMA_SUCCESS(e);
    sk_fixlist_destroy(stl);

    e = sk_fixlist_append_fixrec(bl, &rec);
    ASSERT_SCHEMA_SUCCESS(e);
    sk_fixrec_clear(&rec);

    sk_fixrec_destroy(&rec);
    TRACE_RETURN bl;
}


/*
 *    Return a fixlist that is a basicList of subTemplateMultiList.
 */
static sk_fixlist_t *
create_blist_stml(
    void)
{
#define NUM_FIELDS 1
    const uint64_t num_fields = NUM_FIELDS;
    static fbInfoElementSpec_t spec[1+NUM_FIELDS] = {
        {(char*)"subTemplateMultiList",         0, 0},
        FB_IESPEC_NULL
    };
    const sk_field_t *field[NUM_FIELDS];
#undef NUM_FIELDS
    static sk_schema_t *schema = NULL;
    sk_schema_err_t e;
    sk_fixrec_t rec;
    uint64_t i;
    sk_fixlist_t *bl;
    sk_fixlist_t *stml;

    TRACE_ENTRY;

    /* create the schema if it does not exist */
    if (!schema) {
        schema = create_schema(spec);
    }

    for (i = 0; i < num_fields; ++i) {
        field[i] = sk_schema_get_field(schema, i);
        assert(0 == strcmp(spec[i].name, sk_field_get_name(field[i])));
    }

    /* create a record using that schema */
    e = sk_fixrec_init(&rec, schema);
    ASSERT_SCHEMA_SUCCESS(e);

    /* create the bl */
    e = sk_fixlist_create_basiclist_from_ident(&bl, model,
                                               sk_field_get_ident(field[0]));
    ASSERT_SCHEMA_SUCCESS(e);

    /* repeatedly set the fields on the record then append the record
     * to the fixlist */
    stml = create_stml_ssid_sport__octets_blist(8, 3);
    e = sk_fixrec_set_list(&rec, field[0], stml);
    ASSERT_SCHEMA_SUCCESS(e);
    sk_fixlist_destroy(stml);

    e = sk_fixlist_append_fixrec(bl, &rec);
    ASSERT_SCHEMA_SUCCESS(e);
    sk_fixrec_clear(&rec);

    stml = create_stml_proto_stime__sip_dport(4, 5);
    e = sk_fixrec_set_list(&rec, field[0], stml);
    ASSERT_SCHEMA_SUCCESS(e);
    sk_fixlist_destroy(stml);

    e = sk_fixlist_append_fixrec(bl, &rec);
    ASSERT_SCHEMA_SUCCESS(e);
    sk_fixrec_clear(&rec);

    sk_fixrec_destroy(&rec);
    TRACE_RETURN bl;
}


/*
 *    Append to the subTemplateMultiList 'stml' records that
 *    contain tcpControlBits and subTemplateList.
 */
static sk_schema_t *
append_fixlist_tcpcontrol_stl(
    sk_fixlist_t       *stml)
{
#define NUM_FIELDS 2
    const uint64_t num_fields = NUM_FIELDS;
    static fbInfoElementSpec_t spec[1+NUM_FIELDS] = {
        {(char*)"tcpControlBits",               0, 0},
        {(char*)"subTemplateList",              0, 0},
        FB_IESPEC_NULL
    };
    const sk_field_t *field[NUM_FIELDS];
#undef NUM_FIELDS
    static sk_schema_t *schema = NULL;
    static uint64_t counter = 1;
    sk_schema_err_t e;
    sk_fixrec_t rec;
    uint64_t i;
    sk_fixlist_t *stl;
    uint8_t tcp_flags;

    TRACE_ENTRY;

    /* create the schema if it does not exist */
    if (!schema) {
        schema = create_schema(spec);
    }

    for (i = 0; i < num_fields; ++i) {
        field[i] = sk_schema_get_field(schema, i);
        assert(0 == strcmp(spec[i].name, sk_field_get_name(field[i])));
    }

    /* create a record using that schema */
    e = sk_fixrec_init(&rec, schema);
    ASSERT_SCHEMA_SUCCESS(e);

    /* repeatedly set the fields on the record then append the record
     * to the fixlist */
    tcp_flags = counter & UINT8_MAX;
    e = sk_fixrec_set_unsigned8(&rec, field[0], tcp_flags);
    ASSERT_SCHEMA_SUCCESS(e);
    stl = create_stl_ingress_stl();
    e = sk_fixrec_set_list(&rec, field[1], stl);
    ASSERT_SCHEMA_SUCCESS(e);
    sk_fixlist_destroy(stl);

    e = sk_fixlist_append_fixrec(stml, &rec);
    ASSERT_SCHEMA_SUCCESS(e);
    sk_fixrec_clear(&rec);
    ++counter;

    tcp_flags = counter & UINT8_MAX;
    e = sk_fixrec_set_unsigned8(&rec, field[0], tcp_flags);
    ASSERT_SCHEMA_SUCCESS(e);
    stl = create_stl_ssid_sport(9);
    e = sk_fixrec_set_list(&rec, field[1], stl);
    ASSERT_SCHEMA_SUCCESS(e);
    sk_fixlist_destroy(stl);

    e = sk_fixlist_append_fixrec(stml, &rec);
    ASSERT_SCHEMA_SUCCESS(e);
    sk_fixrec_clear(&rec);
    ++counter;

    sk_fixrec_destroy(&rec);
    TRACE_RETURN schema;
}


/*
 *    Append to the subTemplateMultiList 'stml' records that
 *    contain basicList and flowDurationMilliseconds.
 */
static sk_schema_t *
append_fixlist_blist_elapsed(
    sk_fixlist_t       *stml)
{
#define NUM_FIELDS 2
    const uint64_t num_fields = NUM_FIELDS;
    static fbInfoElementSpec_t spec[1+NUM_FIELDS] = {
        {(char*)"basicList",                    0, 0},
        {(char*)"flowDurationMilliseconds",     0, 0},
        FB_IESPEC_NULL
    };
    const sk_field_t *field[NUM_FIELDS];
#undef NUM_FIELDS
    static sk_schema_t *schema = NULL;
    static uint64_t counter = 1;
    sk_schema_err_t e;
    sk_fixrec_t rec;
    uint64_t i;
    sk_fixlist_t *bl;

    TRACE_ENTRY;

    /* create the schema if it does not exist */
    if (!schema) {
        schema = create_schema(spec);
    }

    for (i = 0; i < num_fields; ++i) {
        field[i] = sk_schema_get_field(schema, i);
        assert(0 == strcmp(spec[i].name, sk_field_get_name(field[i])));
    }

    /* create a record using that schema */
    e = sk_fixrec_init(&rec, schema);
    ASSERT_SCHEMA_SUCCESS(e);

    /* repeatedly set the fields on the record then append the record
     * to the fixlist */
    bl = create_blist_blist();
    e = sk_fixrec_set_list(&rec, field[0], bl);
    ASSERT_SCHEMA_SUCCESS(e);
    e = sk_fixrec_set_unsigned32(&rec, field[1], counter & UINT32_MAX);
    ASSERT_SCHEMA_SUCCESS(e);
    sk_fixlist_destroy(bl);

    e = sk_fixlist_append_fixrec(stml, &rec);
    ASSERT_SCHEMA_SUCCESS(e);
    sk_fixrec_clear(&rec);
    ++counter;

    bl = create_blist_iface(9);
    e = sk_fixrec_set_list(&rec, field[0], bl);
    ASSERT_SCHEMA_SUCCESS(e);
    e = sk_fixrec_set_unsigned32(&rec, field[1], counter & UINT32_MAX);
    ASSERT_SCHEMA_SUCCESS(e);
    sk_fixlist_destroy(bl);

    e = sk_fixlist_append_fixrec(stml, &rec);
    ASSERT_SCHEMA_SUCCESS(e);
    sk_fixrec_clear(&rec);
    ++counter;

    bl = create_blist_stml();
    e = sk_fixrec_set_list(&rec, field[0], bl);
    ASSERT_SCHEMA_SUCCESS(e);
    e = sk_fixrec_set_unsigned32(&rec, field[1], counter & UINT32_MAX);
    ASSERT_SCHEMA_SUCCESS(e);
    sk_fixlist_destroy(bl);

    e = sk_fixlist_append_fixrec(stml, &rec);
    ASSERT_SCHEMA_SUCCESS(e);
    sk_fixrec_clear(&rec);
    ++counter;

    bl = create_blist_stl();
    e = sk_fixrec_set_list(&rec, field[0], bl);
    ASSERT_SCHEMA_SUCCESS(e);
    e = sk_fixrec_set_unsigned32(&rec, field[1], counter & UINT32_MAX);
    ASSERT_SCHEMA_SUCCESS(e);
    sk_fixlist_destroy(bl);

    e = sk_fixlist_append_fixrec(stml, &rec);
    ASSERT_SCHEMA_SUCCESS(e);
    sk_fixrec_clear(&rec);
    ++counter;

    sk_fixrec_destroy(&rec);
    TRACE_RETURN schema;
}


/*
 *    Append to the subTemplateMultiList 'stml' records that
 *    contains ipClassOfService and a subTemplateMultiList
 */
static sk_schema_t *
append_fixlist_flowcount_stml(
    sk_fixlist_t       *stml)
{
#define NUM_FIELDS 2
    const uint64_t num_fields = NUM_FIELDS;
    static fbInfoElementSpec_t spec[1+NUM_FIELDS] = {
        {(char*)"ipClassOfService",             0, 0},
        {(char*)"subTemplateMultiList",         0, 0},
        FB_IESPEC_NULL
    };
    const sk_field_t *field[NUM_FIELDS];
#undef NUM_FIELDS
    static sk_schema_t *schema = NULL;
    static uint64_t counter = 1;
    sk_schema_err_t e;
    sk_fixrec_t rec;
    uint64_t i;
    uint8_t tos;
    sk_fixlist_t *inner_stml;
    sk_schema_t *s;

    TRACE_ENTRY;

    /* create the schema if it does not exist */
    if (!schema) {
        schema = create_schema(spec);
    }

    for (i = 0; i < num_fields; ++i) {
        field[i] = sk_schema_get_field(schema, i);
        assert(0 == strcmp(spec[i].name, sk_field_get_name(field[i])));
    }

    /* create a record using that schema */
    e = sk_fixrec_init(&rec, schema);
    ASSERT_SCHEMA_SUCCESS(e);

    /* repeatedly set the fields on the record then append the record
     * to the fixlist */
    tos = counter & UINT8_MAX;
    e = sk_fixrec_set_unsigned8(&rec, field[0], tos);
    ASSERT_SCHEMA_SUCCESS(e);
    e = sk_fixlist_create_subtemplatemultilist(&inner_stml, model);
    s = append_fixlist_egress(8, inner_stml);
    assert(s);
    e = sk_fixrec_set_list(&rec, field[1], inner_stml);
    ASSERT_SCHEMA_SUCCESS(e);
    sk_fixlist_destroy(inner_stml);

    e = sk_fixlist_append_fixrec(stml, &rec);
    ASSERT_SCHEMA_SUCCESS(e);
    sk_fixrec_clear(&rec);
    ++counter;

    tos = counter & UINT8_MAX;
    e = sk_fixrec_set_unsigned8(&rec, field[0], tos);
    ASSERT_SCHEMA_SUCCESS(e);
    e = sk_fixlist_create_subtemplatemultilist(&inner_stml, model);
    s = append_fixlist_etime(7, inner_stml);
    assert(s);
    e = sk_fixrec_set_list(&rec, field[1], inner_stml);
    ASSERT_SCHEMA_SUCCESS(e);
    sk_fixlist_destroy(inner_stml);

    e = sk_fixlist_append_fixrec(stml, &rec);
    ASSERT_SCHEMA_SUCCESS(e);
    sk_fixrec_clear(&rec);
    ++counter;

    tos = counter & UINT8_MAX;
    e = sk_fixrec_set_unsigned8(&rec, field[0], tos);
    ASSERT_SCHEMA_SUCCESS(e);
    e = sk_fixlist_create_subtemplatemultilist(&inner_stml, model);
    s = append_fixlist_iface(3, inner_stml);
    assert(s);
    e = sk_fixrec_set_list(&rec, field[1], inner_stml);
    ASSERT_SCHEMA_SUCCESS(e);
    sk_fixlist_destroy(inner_stml);

    e = sk_fixlist_append_fixrec(stml, &rec);
    ASSERT_SCHEMA_SUCCESS(e);
    sk_fixrec_clear(&rec);
    ++counter;

    sk_fixrec_destroy(&rec);
    TRACE_RETURN schema;
}


/*
 *    Create a record that contains a interfaceDescription,
 *    packetTotalCount, and subTemplateMultiList that contains all
 *    other lists.
 */
static sk_fixrec_t *
create_rec_ifacedesc_pkts_stml(
    void)
{
#define NUM_FIELDS 3
    const uint64_t num_fields = NUM_FIELDS;
    static fbInfoElementSpec_t spec[1+NUM_FIELDS] = {
        {(char*)"interfaceDescription",         0, 0},
        {(char*)"packetTotalCount",             0, 0},
        {(char*)"subTemplateMultiList",         0, 0},
        FB_IESPEC_NULL
    };
    const sk_field_t *field[NUM_FIELDS];
#undef NUM_FIELDS
    static sk_schema_t *schema = NULL;
    static uint64_t counter = 1;
    sk_schema_err_t e;
    sk_fixrec_t *rec;
    char buf[BUF_OFFSET + BUF_LEN];
    uint64_t i;
    sk_fixlist_t *stml;
    const sk_schema_t *s;

    TRACE_ENTRY;

    /* create the schema if it does not exist */
    if (!schema) {
        schema = create_schema(spec);
    }

    for (i = 0; i < num_fields; ++i) {
        field[i] = sk_schema_get_field(schema, i);
        assert(0 == strcmp(spec[i].name, sk_field_get_name(field[i])));
    }

    /* create a record using that schema */
    e = sk_fixrec_create(&rec, schema);
    ASSERT_SCHEMA_SUCCESS(e);

    /* set the fields on the record */
    memset(buf, '-', sizeof(buf));
    snprintf(buf + BUF_OFFSET, BUF_LEN, " %" PRIu64, counter);
    e = sk_fixrec_set_string(rec, field[0], buf);
    ASSERT_SCHEMA_SUCCESS(e);

    e = sk_fixrec_set_unsigned64(rec, field[1], counter);
    ASSERT_SCHEMA_SUCCESS(e);

    e = sk_fixlist_create_subtemplatemultilist(&stml, model);
    ASSERT_SCHEMA_SUCCESS(e);
    s = append_fixlist_blist_elapsed(stml);
    assert(s);
    s = append_fixlist_tcpcontrol_stl(stml);
    assert(s);
    s = append_fixlist_flowcount_stml(stml);
    assert(s);
    e = sk_fixrec_set_list(rec, field[2], stml);
    ASSERT_SCHEMA_SUCCESS(e);
    sk_fixlist_destroy(stml);

    ++counter;

    TRACE_RETURN rec;
}


int main(int argc, const char **argv)
{
    SILK_FEATURES_DEFINE_STRUCT(features);
    sk_fixstream_t *stream;
    sk_fixrec_t *rec;
    ssize_t rv;

    TRACE_ENTRY;

    skAppRegister(argv[0]);
    skAppVerifyFeatures(&features, NULL);

    if (argc > 1) {
        appUsage();
        exit(EXIT_FAILURE);
    }

    skipfix_initialize(0);
    model = skipfix_information_model_create(0);

    dllist_schema = skDLListCreate(free_schema);
    assert(dllist_schema);

    rec = create_rec_ifacedesc_pkts_stml();

    /* open an output stream to stdout */
    if ((rv = sk_fixstream_create(&stream))
        || (rv = sk_fixstream_bind(stream, "-", SK_IO_WRITE))
        || (rv = sk_fixstream_open(stream)))
    {
        skAppPrintErr("%s", sk_fixstream_strerror(stream));
        sk_fixstream_destroy(&stream);
        exit(EXIT_FAILURE);
    }

    /* FIXME: Need some way to add all templates to output stream */

    /* write the record to the stream */
    rv = sk_fixstream_write_record(stream, rec, NULL);
    if (rv) {
        skAppPrintErr("%s", sk_fixstream_strerror(stream));
        sk_fixstream_destroy(&stream);
        exit(EXIT_FAILURE);
    }
    rv = sk_fixstream_close(stream);
    if (rv) {
        skAppPrintErr("%s", sk_fixstream_strerror(stream));
        sk_fixstream_destroy(&stream);
        exit(EXIT_FAILURE);
    }
    sk_fixstream_destroy(&stream);

    sk_fixrec_destroy(rec);

    /* done */
#if TRACEMSG_LEVEL >= 3
    {
        sk_dll_iter_t dliter;
        void *dldata;
        size_t i = 0;

        TRACEMSG(3, ("dllist contains {"));
        skDLLAssignIter(&dliter, dllist_schema);
        while (0 == skDLLIterForward(&dliter, &dldata)) {
            TRACEMSG(3, ("  %2" SK_PRIuZ "  %p", i, dldata));
            ++i;
        }
        TRACEMSG(3, ("} %" SK_PRIuZ " elements", i));
    }
#endif  /* TRACEMSG_LEVEL */

    skDLListDestroy(dllist_schema);
    skipfix_information_model_destroy(model);
    skAppUnregister();
    TRACE_RETURN 0;
}




/*
** Local Variables:
** mode:c
** indent-tabs-mode:nil
** c-basic-offset:4
** End:
*/
