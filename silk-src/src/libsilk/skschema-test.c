/*
** Copyright (C) 2005-2017 by Carnegie Mellon University.
**
** @OPENSOURCE_LICENSE_START@
** See license information in ../../LICENSE.txt
** @OPENSOURCE_LICENSE_END@
*/

/*
 *  Test functions for skschema.c
 *
 */

#include <silk/silk.h>

RCSIDENT("$SiLK: skschema-test.c efd886457770 2017-06-21 18:43:23Z mthomas $");

#include <silk/skipaddr.h>
#include <silk/skipfixcert.h>
#include <silk/skschema.h>
#include <silk/skvector.h>
#include <silk/utils.h>


#define SECTION(s) fprintf(stderr, "\n **** " s " ****\n");
#define TEST(s)    fprintf(stderr, s "...");
#define RESULT(b)                               \
    if ((b)) {                                  \
        fprintf(stderr, "ok\n");                \
    } else {                                    \
        fprintf(stderr,                         \
                "failed at %s:%d (rv=%d)\n",    \
                __FILE__, __LINE__, rv);        \
        exit(EXIT_FAILURE);                     \
    }

/*    The number of seconds between Jan 1, 1900 (the NTP epoch) and
 *    Jan 1, 1970 (the UNIX epoch) */
#define NTP_EPOCH_TO_UNIX_EPOCH     UINT64_C(0x83AA7E80)

typedef union any_st {
    uint8_t u8;
    uint16_t u16;
    uint32_t u32;
    uint64_t u64;
    int8_t i8;
    int16_t i16;
    int32_t i32;
    int64_t i64;
    float f;
    double d;
    int b;
    sktime_t t;
} any_t;

static fbInfoElement_t test_elements[] = {
    FB_IE_INIT_FULL("testOctetArray", IPFIX_CERT_PEN, 0x1000, FB_IE_VARLEN,
                    0,              0, 0, FB_OCTET_ARRAY, NULL),
    FB_IE_INIT_FULL("testUnsigned8", IPFIX_CERT_PEN,  0x1001, 1,
                    FB_IE_F_ENDIAN, 0, 0, FB_UINT_8, NULL),
    FB_IE_INIT_FULL("testUnsigned16", IPFIX_CERT_PEN, 0x1002, 2,
                    FB_IE_F_ENDIAN , 0, 0, FB_UINT_16, NULL),
    FB_IE_INIT_FULL("testUnsigned32", IPFIX_CERT_PEN, 0x1003, 4,
                    FB_IE_F_ENDIAN, 0, 0, FB_UINT_32, NULL),
    FB_IE_INIT_FULL("testUnsigned64", IPFIX_CERT_PEN, 0x1004, 8,
                    FB_IE_F_ENDIAN, 0, 0, FB_UINT_64, NULL),
    FB_IE_INIT_FULL("testSigned8",    IPFIX_CERT_PEN,    0x1005, 1,
                    FB_IE_F_ENDIAN, 0, 0, FB_INT_8, NULL),
    FB_IE_INIT_FULL("testSigned16",    IPFIX_CERT_PEN,   0x1006, 2,
                    FB_IE_F_ENDIAN, 0, 0, FB_INT_16, NULL),
    FB_IE_INIT_FULL("testSigned32",    IPFIX_CERT_PEN,   0x1007, 4,
                    FB_IE_F_ENDIAN, 0, 0, FB_INT_32, NULL),
    FB_IE_INIT_FULL("testSigned64",    IPFIX_CERT_PEN,   0x1008, 8,
                    FB_IE_F_ENDIAN, 0, 0, FB_INT_64, NULL),
    FB_IE_INIT_FULL("testFloat32",     IPFIX_CERT_PEN,   0x1009, 4,
                    FB_IE_F_ENDIAN, 0, 0, FB_FLOAT_32, NULL),
    FB_IE_INIT_FULL("testFloat64",     IPFIX_CERT_PEN,   0x100a, 8,
                    FB_IE_F_ENDIAN, 0, 0, FB_FLOAT_64, NULL),
    FB_IE_INIT_FULL("testBoolean",     IPFIX_CERT_PEN,   0x100b, 1,
                    FB_IE_F_ENDIAN, 0, 0, FB_BOOL, NULL),
    FB_IE_INIT_FULL("testMacAddress",  IPFIX_CERT_PEN,   0x100c, 6,
                    FB_IE_F_ENDIAN, 0, 0, FB_MAC_ADDR, NULL),
    FB_IE_INIT_FULL("testString",      IPFIX_CERT_PEN,   0x100d, FB_IE_VARLEN,
                    0,              0, 0, FB_STRING, NULL),
    FB_IE_INIT_FULL("testDateTimeSeconds", IPFIX_CERT_PEN, 0x100e, 4,
                    FB_IE_F_ENDIAN, 0, 0, FB_DT_SEC, NULL),
    FB_IE_INIT_FULL("testDateTimeMilliseconds", IPFIX_CERT_PEN, 0x100f, 8,
                    FB_IE_F_ENDIAN, 0, 0, FB_DT_MILSEC, NULL),
    FB_IE_INIT_FULL("testDateTimeMicroseconds", IPFIX_CERT_PEN, 0x1010, 8,
                    FB_IE_F_ENDIAN, 0, 0, FB_DT_MICROSEC, NULL),
    FB_IE_INIT_FULL("testDateTimeNanoseconds", IPFIX_CERT_PEN, 0x1011, 8,
                    FB_IE_F_ENDIAN, 0, 0, FB_DT_NANOSEC, NULL),
    FB_IE_INIT_FULL("testIpv4Address", IPFIX_CERT_PEN, 0x1012, 4,
                    FB_IE_F_ENDIAN, 0, 0, FB_IP4_ADDR, NULL),
    FB_IE_INIT_FULL("testIpv6Address", IPFIX_CERT_PEN, 0x1013, 16,
                    0,              0, 0, FB_IP6_ADDR, NULL),
    FB_IE_NULL
};

static fbInfoElementSpec_t allspec[] = {
    {(char *)"testOctetArray", 0, 32},
    {(char *)"testUnsigned8", 0, 1},
    {(char *)"testUnsigned16", 0, 1},
    {(char *)"testUnsigned32", 0, 1},
    {(char *)"testUnsigned64", 0, 1},
    {(char *)"testSigned8", 0, 2},
    {(char *)"testSigned16", 0, 2},
    {(char *)"testSigned32", 0, 2},
    {(char *)"testSigned64", 0, 2},
    {(char *)"testFloat32", 0, 4},
    {(char *)"testFloat64", 0, 4},
    {(char *)"testBoolean", 0, 0},
    {(char *)"testMacAddress", 0, 32},
    {(char *)"testString", 0, 32},
    {(char *)"testDateTimeSeconds", 0, 8},
    {(char *)"testDateTimeMilliseconds", 0, 8},
    {(char *)"testDateTimeMicroseconds", 0, 8},
    {(char *)"testDateTimeNanoseconds", 0, 8},
    {(char *)"testIpv4Address", 0, 16},
    {(char *)"testIpv6Address", 0, 16},
    {NULL, 0, 0}
};

static const uint8_t octets[] = {0,1,2,3,4,5,6,7,8,9};
static uint16_t v6addr[] = {19, 20, 21, 22, 23, 24, 25, 26};

static const char *text_values[] = {
    "00010203",
    "1", "2", "3", "4", "5", "6", "7", "8", "9.000000", "10.000000",
    "True", "00:01:02:03:04:05", "13",
    "1970/01/01T00:00:14.000",
    "1970/01/01T00:00:15.000",
    "1970/01/01T00:00:16.000",
    "1970/01/01T00:00:17.000",
    "0.0.0.18",
    "13:14:15:16:17:18:19:1a",
    NULL
};

static void
check_to_text(
    sk_fixrec_t        *rec)
{
    uint16_t i;
    const sk_schema_t *s = sk_fixrec_get_schema(rec);
    const sk_field_t *f;
    const char **ex = text_values;
    char buf[100];
    int rv;

    SECTION("sk_fixrec_data_to_text");

    for (i = 0; i < sk_schema_get_count(s); ++i, ++ex) {
        f = sk_schema_get_field(s, i);
        TEST("sk_fixrec_data_to_text")
        rv = sk_fixrec_data_to_text(rec, f, buf, sizeof(buf));
        RESULT(rv == 0 && strcmp(buf, *ex) == 0);
    }
}


static void
basic_setrec(
    sk_fixrec_t        *rec)
{
    int rv;
    const sk_field_t *f;
    const sk_schema_t *s;

    TEST("sk_fixrec_get_schema");
    s = sk_fixrec_get_schema(rec);
    rv = (s != NULL);
    RESULT(rv);

    TEST("sk_fixrec_set_octet_array");
    f = sk_schema_get_field(s, 0);
    rv = sk_fixrec_set_octet_array(rec, f, octets, 4);
    RESULT(rv == 0);

    TEST("sk_fixrec_set_unsigned8");
    f = sk_schema_get_field(s, 1);
    rv = sk_fixrec_set_unsigned8(rec, f, 1);
    RESULT(rv == 0);

    TEST("sk_fixrec_set_unsigned16");
    f = sk_schema_get_field(s, 2);
    rv = sk_fixrec_set_unsigned16(rec, f, 2);
    RESULT(rv == 0);

    TEST("sk_fixrec_set_unsigned32");
    f = sk_schema_get_field(s, 3);
    rv = sk_fixrec_set_unsigned32(rec, f, 3);
    RESULT(rv == 0);

    TEST("sk_fixrec_set_unsigned64");
    f = sk_schema_get_field(s, 4);
    rv = sk_fixrec_set_unsigned64(rec, f, 4);
    RESULT(rv == 0);

    TEST("sk_fixrec_set_signed8");
    f = sk_schema_get_field(s, 5);
    rv = sk_fixrec_set_signed8(rec, f, 5);
    RESULT(rv == 0);

    TEST("sk_fixrec_set_signed16");
    f = sk_schema_get_field(s, 6);
    rv = sk_fixrec_set_signed16(rec, f, 6);
    RESULT(rv == 0);

    TEST("sk_fixrec_set_signed32");
    f = sk_schema_get_field(s, 7);
    rv = sk_fixrec_set_signed32(rec, f, 7);
    RESULT(rv == 0);

    TEST("sk_fixrec_set_signed64");
    f = sk_schema_get_field(s, 8);
    rv = sk_fixrec_set_signed64(rec, f, 8);
    RESULT(rv == 0);

    TEST("sk_fixrec_set_float32");
    f = sk_schema_get_field(s, 9);
    rv = sk_fixrec_set_float32(rec, f, 9.0);
    RESULT(rv == 0);

    TEST("sk_fixrec_set_float64");
    f = sk_schema_get_field(s, 10);
    rv = sk_fixrec_set_float64(rec, f, 10.0);
    RESULT(rv == 0);

    TEST("sk_fixrec_set_boolean");
    f = sk_schema_get_field(s, 11);
    rv = sk_fixrec_set_boolean(rec, f, 11); /* true */
    RESULT(rv == 0);

    TEST("sk_fixrec_set_mac_address");
    f = sk_schema_get_field(s, 12);
    rv = sk_fixrec_set_mac_address(rec, f, octets);
    RESULT(rv == 0);

    TEST("sk_fixrec_set_string");
    f = sk_schema_get_field(s, 13);
    rv = sk_fixrec_set_string(rec, f, "13");
    RESULT(rv == 0);

    TEST("sk_fixrec_set_datetime_seconds");
    f = sk_schema_get_field(s, 14);
    rv = sk_fixrec_set_datetime_seconds(rec, f, 14);
    RESULT(rv == 0);

    TEST("sk_fixrec_set_datetime_milliseconds");
    f = sk_schema_get_field(s, 15);
    rv = sk_fixrec_set_datetime_milliseconds(rec, f, 15000);
    RESULT(rv == 0);

    TEST("sk_fixrec_set_datetime_microseconds");
    f = sk_schema_get_field(s, 16);
    rv = sk_fixrec_set_datetime_microseconds(
        rec, f, (16 + NTP_EPOCH_TO_UNIX_EPOCH) << 32);
    RESULT(rv == 0);

    TEST("sk_fixrec_set_datetime_nanoseconds");
    f = sk_schema_get_field(s, 17);
    rv = sk_fixrec_set_datetime_nanoseconds(
        rec, f, (17 + NTP_EPOCH_TO_UNIX_EPOCH) << 32);
    RESULT(rv == 0);

    TEST("sk_fixrec_set_ipv4_addr");
    f = sk_schema_get_field(s, 18);
    rv = sk_fixrec_set_ipv4_addr(rec, f, 18);
    RESULT(rv == 0);

    TEST("sk_fixrec_set_ipv6_addr");
    f = sk_schema_get_field(s, 19);
    rv = sk_fixrec_set_ipv6_addr(rec, f, (uint8_t *)v6addr);
    RESULT(rv == 0);
}

static void
generic_setrec(
    sk_fixrec_t        *rec)
{
    int rv;
    const sk_field_t *f;
    const sk_schema_t *s;
    uint32_t u32;
    skipaddr_t addr;

    TEST("sk_fixrec_get_schema");
    s = sk_fixrec_get_schema(rec);
    rv = (s != NULL);
    RESULT(rv);

    TEST("sk_fixrec_set_octets");
    f = sk_schema_get_field(s, 0);
    rv = sk_fixrec_set_octets(rec, f, octets, 4);
    RESULT(rv == 0);

    TEST("sk_fixrec_set_unsigned");
    f = sk_schema_get_field(s, 1);
    rv = sk_fixrec_set_unsigned(rec, f, 1);
    RESULT(rv == 0);

    TEST("sk_fixrec_set_unsigned");
    f = sk_schema_get_field(s, 2);
    rv = sk_fixrec_set_unsigned(rec, f, 2);
    RESULT(rv == 0);

    TEST("sk_fixrec_set_unsigned");
    f = sk_schema_get_field(s, 3);
    rv = sk_fixrec_set_unsigned(rec, f, 3);
    RESULT(rv == 0);

    TEST("sk_fixrec_set_unsigned");
    f = sk_schema_get_field(s, 4);
    rv = sk_fixrec_set_unsigned(rec, f, 4);
    RESULT(rv == 0);

    TEST("sk_fixrec_set_signed");
    f = sk_schema_get_field(s, 5);
    rv = sk_fixrec_set_signed(rec, f, 5);
    RESULT(rv == 0);

    TEST("sk_fixrec_set_signed");
    f = sk_schema_get_field(s, 6);
    rv = sk_fixrec_set_signed(rec, f, 6);
    RESULT(rv == 0);

    TEST("sk_fixrec_set_signed");
    f = sk_schema_get_field(s, 7);
    rv = sk_fixrec_set_signed(rec, f, 7);
    RESULT(rv == 0);

    TEST("sk_fixrec_set_signed");
    f = sk_schema_get_field(s, 8);
    rv = sk_fixrec_set_signed(rec, f, 8);
    RESULT(rv == 0);

    TEST("sk_fixrec_set_float");
    f = sk_schema_get_field(s, 9);
    rv = sk_fixrec_set_float(rec, f, 9.0);
    RESULT(rv == 0);

    TEST("sk_fixrec_set_float");
    f = sk_schema_get_field(s, 10);
    rv = sk_fixrec_set_float(rec, f, 10.0);
    RESULT(rv == 0);

    TEST("sk_fixrec_set_boolean");
    f = sk_schema_get_field(s, 11);
    rv = sk_fixrec_set_boolean(rec, f, 11); /* true */
    RESULT(rv == 0);

    TEST("sk_fixrec_set_octets");
    f = sk_schema_get_field(s, 12);
    rv = sk_fixrec_set_octets(rec, f, octets, 6);
    RESULT(rv == 0);

    TEST("sk_fixrec_set_octets");
    f = sk_schema_get_field(s, 13);
    rv = sk_fixrec_set_octets(rec, f, (uint8_t *)"13", 2);
    RESULT(rv == 0);

    TEST("sk_fixrec_set_datetime");
    f = sk_schema_get_field(s, 14);
    rv = sk_fixrec_set_datetime(rec, f, 14000);
    RESULT(rv == 0);

    TEST("sk_fixrec_set_datetime");
    f = sk_schema_get_field(s, 15);
    rv = sk_fixrec_set_datetime(rec, f, 15000);
    RESULT(rv == 0);

    TEST("sk_fixrec_set_datetime");
    f = sk_schema_get_field(s, 16);
    rv = sk_fixrec_set_datetime(rec, f, 16000);
    RESULT(rv == 0);

    TEST("sk_fixrec_set_datetime");
    f = sk_schema_get_field(s, 17);
    rv = sk_fixrec_set_datetime(rec, f, 17000);
    RESULT(rv == 0);

    TEST("sk_fixrec_set_ip_address");
    f = sk_schema_get_field(s, 18);
    u32 = 18;
    skipaddrSetV4(&addr, &u32);
    rv = sk_fixrec_set_ip_address(rec, f, &addr);
    RESULT(rv == 0);

    TEST("sk_fixrec_set_ip_address");
    f = sk_schema_get_field(s, 19);
    skipaddrSetV6(&addr, (uint8_t *)v6addr);
    rv = sk_fixrec_set_ip_address(rec, f, &addr);
    RESULT(rv == 0);
}


static void
basic_getrec(
    sk_fixrec_t        *rec)
{
    int rv;
    const sk_field_t *f;
    const sk_schema_t *s;
    sk_ntp_time_t ntp;
    char buf[100];
    uint16_t len;
    any_t a;

    TEST("sk_fixrec_get_schema");
    s = sk_fixrec_get_schema(rec);
    rv = (s != NULL);
    RESULT(rv);

    TEST("sk_fixrec_get_value_length");
    f = sk_schema_get_field(s, 0);
    rv = sk_fixrec_get_value_length(rec, f, &len);
    RESULT(rv == 0 && len == 4);

    TEST("sk_fixrec_get_octet_array");
    len = sizeof(buf);
    rv = sk_fixrec_get_octet_array(rec, f, (uint8_t *)buf, &len);
    RESULT(rv == 0 && len == 4
           && buf[0] == 0 && buf[1] == 1 && buf[2] == 2 && buf[3] == 3);

    TEST("sk_fixrec_get_unsigned8");
    f = sk_schema_get_field(s, 1);
    rv = sk_fixrec_get_unsigned8(rec, f, &a.u8);
    RESULT(rv == 0 && a.u8 == 1);

    TEST("sk_fixrec_get_unsigned16");
    f = sk_schema_get_field(s, 2);
    rv = sk_fixrec_get_unsigned16(rec, f, &a.u16);
    RESULT(rv == 0 && a.u16 == 2);

    TEST("sk_fixrec_get_unsigned32");
    f = sk_schema_get_field(s, 3);
    rv = sk_fixrec_get_unsigned32(rec, f, &a.u32);
    RESULT(rv == 0 && a.u32 == 3);

    TEST("sk_fixrec_get_unsigned64");
    f = sk_schema_get_field(s, 4);
    rv = sk_fixrec_get_unsigned64(rec, f, &a.u64);
    RESULT(rv == 0 && a.u64 == 4);

    TEST("sk_fixrec_get_signed8");
    f = sk_schema_get_field(s, 5);
    rv = sk_fixrec_get_signed8(rec, f, &a.i8);
    RESULT(rv == 0 && a.i8 == 5);

    TEST("sk_fixrec_get_signed16");
    f = sk_schema_get_field(s, 6);
    rv = sk_fixrec_get_signed16(rec, f, &a.i16);
    RESULT(rv == 0 && a.i16 == 6);

    TEST("sk_fixrec_get_signed32");
    f = sk_schema_get_field(s, 7);
    rv = sk_fixrec_get_signed32(rec, f, &a.i32);
    RESULT(rv == 0 && a.i32 == 7);

    TEST("sk_fixrec_get_signed64");
    f = sk_schema_get_field(s, 8);
    rv = sk_fixrec_get_signed64(rec, f, &a.i64);
    RESULT(rv == 0 && a.i64 == 8);

    TEST("sk_fixrec_get_float32");
    f = sk_schema_get_field(s, 9);
    rv = sk_fixrec_get_float32(rec, f, &a.f);
    RESULT(rv == 0 && a.f == 9.0);

    TEST("sk_fixrec_get_float64");
    f = sk_schema_get_field(s, 10);
    rv = sk_fixrec_get_float64(rec, f, &a.d);
    RESULT(rv == 0 && a.d == 10.0);

    TEST("sk_fixrec_get_boolean");
    f = sk_schema_get_field(s, 11);
    rv = sk_fixrec_get_boolean(rec, f, &a.b); /* true */
    RESULT(rv == 0 && a.b);

    TEST("sk_fixrec_get_mac_address");
    f = sk_schema_get_field(s, 12);
    rv = sk_fixrec_get_mac_address(rec, f, (uint8_t *)buf);
    RESULT(rv == 0 && memcmp(buf, octets, 6) == 0);

    TEST("sk_fixrec_get_value_length");
    f = sk_schema_get_field(s, 13);
    rv = sk_fixrec_get_value_length(rec, f, &len);
    RESULT(rv == 0 && len == 2);

    TEST("sk_fixrec_get_string");
    len = sizeof(buf);
    rv = sk_fixrec_get_string(rec, f, buf, &len);
    RESULT(rv == 0 && len == 2 && strcmp(buf, "13") == 0);

    TEST("sk_fixrec_get_datetime_seconds");
    f = sk_schema_get_field(s, 14);
    rv = sk_fixrec_get_datetime_seconds(rec, f, &a.u32);
    RESULT(rv == 0 && a.u32 == 14);

    TEST("sk_fixrec_get_datetime_milliseconds");
    f = sk_schema_get_field(s, 15);
    rv = sk_fixrec_get_datetime_milliseconds(rec, f, &a.u64);
    RESULT(rv == 0 && a.u64 == 15000);

    TEST("sk_fixrec_get_datetime_microseconds");
    f = sk_schema_get_field(s, 16);
    rv = sk_fixrec_get_datetime_microseconds(rec, f, &ntp);
    RESULT(rv == 0 && ntp == ((16 + NTP_EPOCH_TO_UNIX_EPOCH) << 32));

    TEST("sk_fixrec_get_datetime_nanoseconds");
    f = sk_schema_get_field(s, 17);
    rv = sk_fixrec_get_datetime_nanoseconds(rec, f, &ntp);
    RESULT(rv == 0 && ntp == ((17 + NTP_EPOCH_TO_UNIX_EPOCH) << 32));

    TEST("sk_fixrec_get_ipv4_addr");
    f = sk_schema_get_field(s, 18);
    rv = sk_fixrec_get_ipv4_addr(rec, f, &a.u32);
    RESULT(rv == 0 && a.u32 == 18);

    TEST("sk_fixrec_get_ipv6_addr");
    f = sk_schema_get_field(s, 19);
    rv = sk_fixrec_get_ipv6_addr(rec, f, (uint8_t *)buf);
    RESULT(rv == 0 && memcmp(buf, v6addr, 16) == 0);
}

static void
generic_getrec(
    sk_fixrec_t        *rec)
{
    int rv;
    const sk_field_t *f;
    const sk_schema_t *s;
    skipaddr_t addr;
    char buf[100];
    uint16_t len;
    any_t a;

    TEST("sk_fixrec_get_schema");
    s = sk_fixrec_get_schema(rec);
    rv = (s != NULL);
    RESULT(rv);

    TEST("sk_fixrec_get_octets");
    f = sk_schema_get_field(s, 0);
    len = sizeof(buf);
    rv = sk_fixrec_get_octets(rec, f, (uint8_t *)buf, &len);
    RESULT(rv == 0 && len == 4
           && buf[0] == 0 && buf[1] == 1 && buf[2] == 2 && buf[3] == 3);

    TEST("sk_fixrec_get_unsigned");
    f = sk_schema_get_field(s, 1);
    rv = sk_fixrec_get_unsigned(rec, f, &a.u64);
    RESULT(rv == 0 && a.u64 == 1);

    TEST("sk_fixrec_get_signed");
    f = sk_schema_get_field(s, 1);
    rv = sk_fixrec_get_signed(rec, f, &a.i64);
    RESULT(rv == 0 && a.i64 == 1);

    TEST("sk_fixrec_get_unsigned");
    f = sk_schema_get_field(s, 2);
    rv = sk_fixrec_get_unsigned(rec, f, &a.u64);
    RESULT(rv == 0 && a.u64 == 2);

    TEST("sk_fixrec_get_signed");
    f = sk_schema_get_field(s, 2);
    rv = sk_fixrec_get_signed(rec, f, &a.i64);
    RESULT(rv == 0 && a.i64 == 2);

    TEST("sk_fixrec_get_unsigned");
    f = sk_schema_get_field(s, 3);
    rv = sk_fixrec_get_unsigned(rec, f, &a.u64);
    RESULT(rv == 0 && a.u64 == 3);

    TEST("sk_fixrec_get_signed");
    f = sk_schema_get_field(s, 3);
    rv = sk_fixrec_get_signed(rec, f, &a.i64);
    RESULT(rv == 0 && a.i64 == 3);

    TEST("sk_fixrec_get_unsigned");
    f = sk_schema_get_field(s, 4);
    rv = sk_fixrec_get_unsigned(rec, f, &a.u64);
    RESULT(rv == 0 && a.u64 == 4);

    TEST("sk_fixrec_get_signed");
    f = sk_schema_get_field(s, 4);
    rv = sk_fixrec_get_signed(rec, f, &a.i64);
    RESULT(rv == SK_SCHEMA_ERR_BAD_TYPE);

    TEST("sk_fixrec_get_signed");
    f = sk_schema_get_field(s, 5);
    rv = sk_fixrec_get_signed(rec, f, &a.i64);
    RESULT(rv == 0 && a.i64 == 5);

    TEST("sk_fixrec_get_signed");
    f = sk_schema_get_field(s, 6);
    rv = sk_fixrec_get_signed(rec, f, &a.i64);
    RESULT(rv == 0 && a.i64 == 6);

    TEST("sk_fixrec_get_signed");
    f = sk_schema_get_field(s, 7);
    rv = sk_fixrec_get_signed(rec, f, &a.i64);
    RESULT(rv == 0 && a.i64 == 7);

    TEST("sk_fixrec_get_signed");
    f = sk_schema_get_field(s, 8);
    rv = sk_fixrec_get_signed(rec, f, &a.i64);
    RESULT(rv == 0 && a.i64 == 8);

    TEST("sk_fixrec_get_float");
    f = sk_schema_get_field(s, 9);
    rv = sk_fixrec_get_float(rec, f, &a.d);
    RESULT(rv == 0 && a.d == 9.0);

    TEST("sk_fixrec_get_float");
    f = sk_schema_get_field(s, 10);
    rv = sk_fixrec_get_float(rec, f, &a.d);
    RESULT(rv == 0 && a.d == 10.0);

    TEST("sk_fixrec_get_boolean");
    f = sk_schema_get_field(s, 11);
    rv = sk_fixrec_get_boolean(rec, f, &a.b); /* true */
    RESULT(rv == 0 && a.b);

    TEST("sk_fixrec_get_octets");
    len = sizeof(buf);
    f = sk_schema_get_field(s, 12);
    rv = sk_fixrec_get_octets(rec, f, (uint8_t *)buf, &len);
    RESULT(rv == 0 && len == 6 && memcmp(buf, octets, 6) == 0);

    TEST("sk_fixrec_get_string");
    f = sk_schema_get_field(s, 13);
    len = sizeof(buf);
    rv = sk_fixrec_get_octets(rec, f, (uint8_t *)buf, &len);
    RESULT(rv == 0 && len == 2 && strncmp(buf, "13", 2) == 0);

    TEST("sk_fixrec_get_datetime");
    f = sk_schema_get_field(s, 14);
    rv = sk_fixrec_get_datetime(rec, f, &a.t);
    RESULT(rv == 0 && a.t == 14000);

    TEST("sk_fixrec_get_datetime");
    f = sk_schema_get_field(s, 15);
    rv = sk_fixrec_get_datetime(rec, f, &a.t);
    RESULT(rv == 0 && a.t == 15000);

    TEST("sk_fixrec_get_datetime");
    f = sk_schema_get_field(s, 16);
    rv = sk_fixrec_get_datetime(rec, f, &a.t);
    RESULT(rv == 0 && a.t == 16000);

    TEST("sk_fixrec_get_datetime");
    f = sk_schema_get_field(s, 17);
    rv = sk_fixrec_get_datetime(rec, f, &a.t);
    RESULT(rv == 0 && a.t == 17000);

    TEST("sk_fixrec_get_ip_address");
    f = sk_schema_get_field(s, 18);
    rv = sk_fixrec_get_ip_address(rec, f, &addr);
    RESULT(rv == 0 && !skipaddrIsV6(&addr) && skipaddrGetV4(&addr) == 18);

    TEST("sk_fixrec_get_ip_address");
    f = sk_schema_get_field(s, 19);
    rv = sk_fixrec_get_ip_address(rec, f, &addr);
    skipaddrGetV6(&addr, buf);
    RESULT(rv == 0 && skipaddrIsV6(&addr) && memcmp(buf, v6addr, 16) == 0);
}

static fbInfoElementSpec_t sizedspec[] = {
    {(char *)"testOctetArray", 4, 1},
    {(char *)"testUnsigned8",  0, 1},
    {(char *)"testUnsigned16", 1, 1},
    {(char *)"testUnsigned32", 2, 1},
    {(char *)"testUnsigned64", 3, 1},
    {(char *)"testSigned8",    0, 2},
    {(char *)"testSigned16",   1, 2},
    {(char *)"testSigned32",   1, 2},
    {(char *)"testSigned64",   2, 2},
    {(char *)"testFloat32",    0, 4},
    {(char *)"testFloat64",    4, 4},
    {(char *)"testBoolean",    0, 0},
    {(char *)"testMacAddress", 0, 32},
    {(char *)"testString",     2, 32},
    {(char *)"testDateTimeSeconds", 0, 8},
    {(char *)"testDateTimeMilliseconds", 0, 8},
    {(char *)"testDateTimeMicroseconds", 0, 8},
    {(char *)"testDateTimeNanoseconds", 0, 8},
    {(char *)"testIpv4Address", 0, 16},
    {(char *)"testIpv6Address", 0, 16},
    {NULL, 0, 0}
};

static void
check_differently_sized_fields(
    fbInfoModel_t      *model)
{
    sk_schema_t *s;
    const sk_field_t *f;
    uint16_t len;
    sk_fixrec_t srec;
    int rv;

    TEST("sk_schema_create");
    rv = sk_schema_create(&s, model, sizedspec, 0);
    RESULT(rv == 0);

    TEST("sk_schema_init");
    rv = sk_schema_freeze(s);
    RESULT(rv == 0);

    TEST("sk_fixrec_init");
    sk_fixrec_init(&srec, s);
    rv = (srec.data != NULL && srec.schema == (void *)s);
    RESULT(rv);

    basic_setrec(&srec);
    basic_getrec(&srec);
    generic_getrec(&srec);

    generic_setrec(&srec);
    basic_getrec(&srec);
    generic_getrec(&srec);

    TEST("sk_fixrec_get_value_length");
    f = sk_schema_get_field(s, 0);
    rv = sk_fixrec_get_value_length(&srec, f, &len);
    RESULT(rv == 0 && len == 4);

    TEST("sk_fixrec_get_value_length");
    f = sk_schema_get_field(s, 2);
    rv = sk_fixrec_get_value_length(&srec, f, &len);
    RESULT(rv == 0 && len == 1);

    TEST("sk_fixrec_destroy");
    sk_fixrec_destroy(&srec);
    rv = (srec.data == NULL && srec.schema == NULL);
    RESULT(1);

    TEST("sk_schema_destroy");
    rv = sk_schema_destroy(s);
    RESULT(rv == 1);
}

static fbInfoElementSpec_t duplicated[] = {
    {(char *)"testOctetArray", 4, 1},
    {(char *)"testUnsigned8",  0, 1},
    {(char *)"testUnsigned16", 1, 1},
    {(char *)"testUnsigned32", 2, 1},
    {(char *)"testOctetArray", 4, 1},
    {(char *)"testUnsigned8",  0, 1},
    {(char *)"testUnsigned16", 1, 1},
    {(char *)"testUnsigned32", 2, 1},
    {NULL, 0, 0}
};

static void
check_get_field_by(
    fbInfoModel_t      *model)
{
    sk_schema_t *s;
    const sk_field_t *f, *g;
    int rv;

    SECTION("get_field_by...")

    TEST("sk_schema_create");
    rv = sk_schema_create(&s, model, duplicated, 0);
    RESULT(rv == 0);

    TEST("sk_schema_get_field_by_ident");
    f = sk_schema_get_field_by_ident(
        s, SK_FIELD_IDENT_CREATE(IPFIX_CERT_PEN, 0x1001), NULL);
    rv = (f != NULL);
    RESULT(rv);

    TEST("sk_schema_get_field_by_ident");
    g = sk_schema_get_field_by_ident(
        s, SK_FIELD_IDENT_CREATE(IPFIX_CERT_PEN, 0x1001), f);
    rv = (g != NULL);
    RESULT(rv);

    TEST("sk_schema_get_field_by_ident");
    f = sk_schema_get_field_by_ident(
        s, SK_FIELD_IDENT_CREATE(IPFIX_CERT_PEN, 0x1001), g);
    rv = (f == NULL);
    RESULT(rv);

    TEST("sk_schema_get_field_by_name");
    f = sk_schema_get_field_by_name(s, "testUnsigned32", NULL);
    rv = (f != NULL);
    RESULT(rv);

    TEST("sk_schema_get_field_by_name");
    g = sk_schema_get_field_by_name(s, "testUnsigned32", f);
    rv = (g != NULL);
    RESULT(rv);

    TEST("sk_schema_get_field_by_name");
    f = sk_schema_get_field_by_name(s, "testUnsigned32", g);
    rv = (f == NULL);
    RESULT(rv);

    TEST("sk_schema_destroy");
    rv = sk_schema_destroy(s);
    RESULT(rv == 1);
}

static fbInfoElementSpec_t mapdiftypestest[] = {
    {(char *)"testDateTimeSeconds", 0, 1},
    {(char *)"testDateTimeMilliseconds", 0, 1},
    {(char *)"testDateTimeMicroseconds", 0, 1},
    {(char *)"testDateTimeNanoseconds", 0, 1},
    {(char *)"testFloat32", 0, 1},
    {(char *)"testFloat64", 0, 1},
    {NULL, 0, 0}
};


static const char *datefields[] = {
    "testDateTimeSeconds",
    "testDateTimeMilliseconds",
    "testDateTimeMicroseconds",
    "testDateTimeNanoseconds",
    NULL
};


static void
check_map_differing_types(
    fbInfoModel_t      *model,
    sk_fixrec_t        *rec)
{
    sk_schema_t *sd;
    const sk_schema_t *ss;
    sk_vector_t *vec;
    const sk_field_t *f, *g;
    const char **ns, **nd;
    sk_schemamap_t *map;
    sk_ntp_time_t ntpa, ntpb;
    sk_fixrec_t srec;
    int rv;
    double dv;
    float fv;

    SECTION("mapping differing types");

    TEST("sk_schema_create");
    rv = sk_schema_create(&sd, model, mapdiftypestest, 0);
    RESULT(rv == 0);

    TEST("sk_schema_freeze");
    rv = sk_schema_freeze(sd);
    RESULT(rv == 0);

    TEST("sk_fixrec_init");
    sk_fixrec_init(&srec, sd);
    rv = (srec.data != NULL && srec.schema == (void *)sd);
    RESULT(rv);

    vec = sk_vector_create(sizeof(const sk_field_t *));

    TEST("sk_fixrec_get_schema");
    ss = sk_fixrec_get_schema(rec);
    rv = (ss != NULL);
    RESULT(rv);

    /* Test datetime conversions */
    for (ns = datefields; *ns; ++ns) {
        f = sk_schema_get_field_by_name(ss, *ns, NULL);
        for (nd = datefields; *nd; ++nd) {
            g = sk_schema_get_field_by_name(sd, *nd, NULL);
            sk_vector_append_value(vec, &f);
            sk_vector_append_value(vec, &g);
        }

        TEST("sk_schemamap_create_across_fields");
        rv = sk_schemamap_create_across_fields(&map, vec);
        RESULT(rv == 0);

        TEST("sk_schemamap_apply");
        rv = sk_schemamap_apply(map, &srec, rec);
        RESULT(rv == 0);

        TEST("sk_fixrec_get_datetime_ntp");
        rv = sk_fixrec_get_datetime_ntp(rec, f, &ntpa);
        RESULT(rv == 0);

        for (nd = datefields; *nd; ++nd) {
            g = sk_schema_get_field_by_name(sd, *nd, NULL);

            TEST("sk_fixrec_get_datetime_ntp");
            rv = sk_fixrec_get_datetime_ntp(&srec, g, &ntpb);
            RESULT(rv == 0 && ((ntpb & ~UINT64_C(0xffffff))
                               == (ntpa & ~UINT64_C(0xffffff))));
        }

        sk_schemamap_destroy(map);
        sk_vector_clear(vec);
    }

    /* Test float conversions */
    f = sk_schema_get_field_by_name(sd, "testFloat32", NULL);
    g = sk_schema_get_field_by_name(sd, "testFloat64", NULL);

    sk_vector_append_value(vec, &f);
    sk_vector_append_value(vec, &g);

    TEST("sk_schemamap_create_across_fields");
    rv = sk_schemamap_create_across_fields(&map, vec);
    RESULT(rv == 0);

    TEST("sk_fixrec_set_float32");
    rv = sk_fixrec_set_float32(&srec, f, 3.125);
    RESULT(rv == 0);

    TEST("sk_schemamap_apply");
    rv = sk_schemamap_apply(map, &srec, &srec);
    RESULT(rv == 0);

    TEST("sk_fixrec_get_float64");
    rv = sk_fixrec_get_float64(&srec, g, &dv);
    RESULT(rv == 0 && dv == 3.125);

    sk_schemamap_destroy(map);
    sk_vector_clear(vec);

    sk_vector_append_value(vec, &g);
    sk_vector_append_value(vec, &f);

    TEST("sk_schemamap_create_across_fields");
    rv = sk_schemamap_create_across_fields(&map, vec);
    RESULT(rv == 0);

    TEST("sk_fixrec_set_float64");
    rv = sk_fixrec_set_float64(&srec, g, 2.5625);
    RESULT(rv == 0);

    TEST("sk_schemamap_apply");
    rv = sk_schemamap_apply(map, &srec, &srec);
    RESULT(rv == 0);

    TEST("sk_fixrec_get_float32");
    rv = sk_fixrec_get_float32(&srec, f, &fv);
    RESULT(rv == 0 && fv == 2.5625);

    sk_schemamap_destroy(map);
    sk_vector_destroy(vec);
    sk_fixrec_destroy(&srec);

    TEST("sk_schema_destroy");
    rv = sk_schema_destroy(sd);
    RESULT(rv == 1);
}


static void
check_boolean(
    fbInfoModel_t      *model)
{
    sk_schema_t *s;
    sk_field_t *f;
    sk_fixrec_t srec;
    int b;
    int rv;

    SECTION("boolean fields");

    TEST("sk_schema_create");
    rv = sk_schema_create(&s, model, NULL, 0);
    RESULT(rv == 0);

    TEST("sk_schema_insert_field_by_name");
    rv = sk_schema_insert_field_by_name(&f, s, "testBoolean", NULL, NULL);
    RESULT(rv == 0);

    TEST("sk_schema_freeze");
    rv = sk_schema_freeze(s);
    RESULT(rv == 0);

    TEST("sk_fixrec_init");
    sk_fixrec_init(&srec, s);
    rv = (srec.data != NULL && srec.schema == (void *)s);
    RESULT(rv);

    TEST("sk_fixrec_get_boolean");
    srec.data[0] = 0;
    rv = sk_fixrec_get_boolean(&srec, f, &b);
    RESULT(rv == SK_SCHEMA_ERR_UNKNOWN_BOOL && b == 0);

    TEST("sk_fixrec_get_boolean");
    srec.data[0] = 1;
    rv = sk_fixrec_get_boolean(&srec, f, &b);
    RESULT(rv == 0 && b == 1);

    TEST("sk_fixrec_get_boolean");
    srec.data[0] = 2;
    rv = sk_fixrec_get_boolean(&srec, f, &b);
    RESULT(rv == 0 && b == 0);

    TEST("sk_fixrec_get_boolean");
    srec.data[0] = 3;
    rv = sk_fixrec_get_boolean(&srec, f, &b);
    RESULT(rv == SK_SCHEMA_ERR_UNKNOWN_BOOL && b == 3);

    TEST("sk_fixrec_destroy");
    sk_fixrec_destroy(&srec);
    rv = (srec.data == NULL && srec.schema == NULL);
    RESULT(1);

    TEST("sk_schema_destroy");
    rv = sk_schema_destroy(s);
    RESULT(rv == 1);
}

static void
check_compare(
    sk_fixrec_t        *rec)
{
    const sk_schema_t *s;
    const sk_field_t *f1, *f2;
    sk_fixrec_t *copy;
    uint16_t i;
    int b;
    int rv;

    SECTION("sk_fixrec_data_compare");

    TEST("sk_fixrec_get_schema");
    s = sk_fixrec_get_schema(rec);
    rv = (s != NULL);
    RESULT(rv);

    TEST("sk_fixrec_copy");
    rv = sk_fixrec_copy(&copy, rec);
    RESULT(rv == 0);

    for (i = 0; i < sk_schema_get_count(s); ++i) {
        TEST("sk_fixrec_data_compare");
        f1 = sk_schema_get_field(s, i);
        rv = sk_fixrec_data_compare(rec, f1, copy, f1, &b);
        RESULT(rv == 0 && b == 0);
    }

    TEST("sk_fixrec_data_compare");
    f1 = sk_schema_get_field_by_name(s, "testIpv4Address", NULL);
    f2 = sk_schema_get_field_by_name(s, "testIpv6Address", NULL);
    rv = sk_fixrec_data_compare(rec, f1, rec, f2, &b);
    RESULT(rv == 0 && b < 0);

    TEST("sk_fixrec_data_compare");
    rv = sk_fixrec_data_compare(rec, f2, rec, f1, &b);
    RESULT(rv == 0 && b > 0);

    TEST("sk_fixrec_data_compare");
    f1 = sk_schema_get_field_by_name(s, "testDateTimeSeconds", NULL);
    f2 = sk_schema_get_field_by_name(s, "testDateTimeNanoseconds", NULL);
    rv = sk_fixrec_data_compare(rec, f1, rec, f2, &b);
    RESULT(rv == 0 && b < 0);

    TEST("sk_fixrec_data_compare");
    f1 = sk_schema_get_field_by_name(s, "testDateTimeMilliseconds", NULL);
    f2 = sk_schema_get_field_by_name(s, "testDateTimeMicroseconds", NULL);
    rv = sk_fixrec_data_compare(rec, f1, rec, f2, &b);
    RESULT(rv == 0 && b < 0);

    TEST("sk_fixrec_data_compare");
    f1 = sk_schema_get_field_by_name(s, "testUnsigned8", NULL);
    f2 = sk_schema_get_field_by_name(s, "testSigned8", NULL);
    rv = sk_fixrec_data_compare(rec, f1, rec, f2, &b);
    RESULT(rv == SK_SCHEMA_ERR_INCOMPATIBLE);

    TEST("sk_fixrec_set_signed16");
    f1 = sk_schema_get_field_by_name(s, "testSigned16", NULL);
    rv = sk_fixrec_set_signed16(copy, f1, -1);
    RESULT(rv == 0);

    TEST("sk_fixrec_data_compare");
    rv = sk_fixrec_data_compare(rec, f1, copy, f1, &b);
    RESULT(rv == 0 && b > 0);

    TEST("sk_fixrec_destroy");
    sk_fixrec_destroy(copy);
    RESULT(1);
}

static void
check_merge(
    sk_fixrec_t        *rec)
{
    const sk_schema_t *s;
    const sk_field_t *f;
    sk_fixrec_t *copy;
    int64_t a, b;
    int rv;

    SECTION("sk_fixrec_data_merge");

    TEST("sk_fixrec_get_schema");
    s = sk_fixrec_get_schema(rec);
    rv = (s != NULL);
    RESULT(rv);

    TEST("sk_fixrec_copy");
    rv = sk_fixrec_copy(&copy, rec);
    RESULT(rv == 0);

    TEST("sk_fixrec_data_merge");
    f = sk_schema_get_field_by_name(s, "testSigned16", NULL);
    sk_fixrec_get_signed(rec, f, &a);
    rv = sk_fixrec_data_merge(copy, f, rec, f);
    sk_fixrec_get_signed(copy, f, &b);
    RESULT(rv == 0 && b == 2 * a);

    TEST("sk_fixrec_data_merge");
    f = sk_schema_get_field_by_name(s, "testUnsigned16", NULL);
    sk_fixrec_get_signed(rec, f, &a);
    rv = sk_fixrec_data_merge(copy, f, rec, f);
    sk_fixrec_get_signed(copy, f, &b);
    RESULT(rv == 0 && b == 2 * a);

    TEST("sk_fixrec_data_merge");
    rv = sk_fixrec_data_merge(copy, f, copy, f);
    sk_fixrec_get_signed(copy, f, &b);
    RESULT(rv == 0 && b == 4 * a);

    TEST("sk_fixrec_data_merge");
    f = sk_schema_get_field_by_name(s, "testDateTimeSeconds", NULL);
    rv = sk_fixrec_data_merge(copy, f, rec, f);
    RESULT(rv == SK_SCHEMA_ERR_INCOMPATIBLE);

    TEST("sk_fixrec_destroy");
    sk_fixrec_destroy(copy);
    RESULT(1);
}

static void
check_copy(
    fbInfoModel_t      *model)
{
    sk_schema_t *s1, *s2;
    sk_fixrec_t srec1, srec2;
    sk_schemamap_t *map;
    int rv;

    SECTION("record copy/transcode");

    TEST("sk_schema_create");
    rv = sk_schema_create(&s1, model, allspec, 0);
    RESULT(rv == 0);

    TEST("sk_schema_freeze");
    rv = sk_schema_freeze(s1);
    RESULT(rv == 0);

    TEST("sk_fixrec_init");
    sk_fixrec_init(&srec1, s1);
    rv = (srec1.data != NULL && srec1.schema == (void *)s1);
    RESULT(rv);

    TEST("sk_schema_create");
    rv = sk_schema_create(&s2, model, sizedspec, 0);
    RESULT(rv == 0);

    TEST("sk_schema_freeze");
    rv = sk_schema_freeze(s2);
    RESULT(rv == 0);

    TEST("sk_fixrec_init");
    sk_fixrec_init(&srec2, s2);
    rv = (srec2.data != NULL && srec2.schema == (void *)s2);
    RESULT(rv);

    basic_setrec(&srec2);

    TEST("sk_schemamap_create_across_schemas");
    map = NULL;
    rv = sk_schemamap_create_across_schemas(&map, s1, s2);
    RESULT(rv == 0 && map != NULL);

    TEST("sk_schemamap_apply");
    rv = sk_schemamap_apply(map, &srec1, &srec2);
    RESULT(rv == 0);

    basic_getrec(&srec1);

    sk_schemamap_destroy(map);

    TEST("sk_fixrec_destroy");
    sk_fixrec_destroy(&srec2);
    rv = (srec2.data == NULL && srec2.schema == NULL);
    RESULT(1);

    TEST("sk_fixrec_init");
    sk_fixrec_init(&srec2, s2);
    rv = (srec2.data != NULL && srec2.schema == (void *)s2);
    RESULT(rv);

    TEST("sk_schemamap_create_across_schemas");
    map = NULL;
    rv = sk_schemamap_create_across_schemas(&map, s2, s1);
    RESULT(rv == SK_SCHEMA_ERR_TRUNCATED && map != NULL);

    TEST("sk_schemamap_apply");
    rv = sk_schemamap_apply(map, &srec2, &srec1);
    RESULT(rv == 0);

    basic_getrec(&srec2);

    sk_schemamap_destroy(map);

    TEST("sk_fixrec_destroy");
    sk_fixrec_destroy(&srec1);
    rv = (srec1.data == NULL && srec1.schema == NULL);
    RESULT(1);

    TEST("sk_fixrec_destroy");
    sk_fixrec_destroy(&srec2);
    rv = (srec2.data == NULL && srec2.schema == NULL);
    RESULT(1);

    TEST("sk_schema_destroy");
    rv = sk_schema_destroy(s1);
    RESULT(rv == 1);

    TEST("sk_schema_destroy");
    rv = sk_schema_destroy(s2);
    RESULT(rv == 1);
}


int
main(int argc, char **argv)
{
    fbInfoModel_t *model;
    sk_schema_t   *s1, *s2, *s3;
    const sk_schema_t *cs;
    const sk_field_t *cf;
    sk_field_t *f;
    fbInfoElementSpec_t *spec;
    uint16_t i;
    int *ip;
    void *vp;
    int rv;
    sk_fixrec_t srec;
    sk_fixrec_t *rec;
    sk_schema_ctx_ident_t ctx_ident = SK_SCHEMA_CTX_IDENT_INVALID;
    sk_schema_ctx_ident_t ctx_ident2 = SK_SCHEMA_CTX_IDENT_INVALID;

    skAppRegister(argv[0]);
    (void)argc;

    for (i = 0; i < 8; ++i) {
        v6addr[i] = htons(v6addr[i]);
    }

    skipfix_initialize(0);

    model = fbInfoModelAlloc();
    assert(model);
    fbInfoModelAddElementArray(model, test_elements);


    /* Schme creation  */

    SECTION("Schema creation");

    TEST("sk_schema_create");
    rv = sk_schema_create(&s1, model, allspec, 0);
    RESULT(rv == 0);

    TEST("sk_schema_get_count");
    rv = sk_schema_get_count(s1);
    RESULT(rv == 20);

    TEST("sk_schema_get_field");
    for (spec=allspec, i = 0; i < sk_schema_get_count(s1); ++i, ++spec) {
        cf = sk_schema_get_field(s1, i);
        rv = strcmp(sk_field_get_name(cf), spec->name);
        if (rv != 0) {
            break;
        }
    }
    RESULT(rv == 0);

    TEST("sk_schema_create");
    rv = sk_schema_create(&s2, model, allspec, 3);
    RESULT(rv == 0);

    TEST("sk_schema_get_count");
    rv = sk_schema_get_count(s2);
    RESULT(rv == 9);

    TEST("sk_schema_clone");
    cs = sk_schema_clone(s2);
    rv = (cs == s2);
    RESULT(rv);

    TEST("sk_schema_count");
    rv = sk_schema_get_count(cs);
    RESULT(rv == 9);

    TEST("sk_schema_destroy");
    rv = sk_schema_destroy(cs);
    RESULT(rv == 0);

    TEST("sk_schema_copy");
    rv = sk_schema_copy(&s3, s2);
    RESULT(rv == 0);

    TEST("sk_schema_count");
    rv = sk_schema_get_count(s3);
    RESULT(rv == 9);

    TEST("sk_schema_destroy");
    rv = sk_schema_destroy(s3);
    RESULT(rv == 1);

    /* Schema manipulation */

    SECTION("Schema manipulation");

    TEST("sk_schema_insert_field_by_ident");
    rv = sk_schema_insert_field_by_ident(
        &f, s2, SK_FIELD_IDENT_CREATE(IPFIX_CERT_PEN, 0x1001), NULL, NULL);
    RESULT(rv == 0);

    TEST("sk_schema_count");
    rv = sk_schema_get_count(s2);
    RESULT(rv == 10);

    TEST("sk_schema_insert_by_ident correctness");
    cf = sk_schema_get_field(s2, 9);
    RESULT(strcmp(sk_field_get_name(cf), "testUnsigned8") == 0);

    TEST("sk_schema_insert_field_by_ident");
    cf = sk_schema_get_field(s2, 0);
    rv = sk_schema_insert_field_by_ident(
        &f, s2, SK_FIELD_IDENT_CREATE(IPFIX_CERT_PEN, 0x1002), NULL, cf);
    RESULT(rv == 0);

    TEST("sk_schema_count");
    rv = sk_schema_get_count(s2);
    RESULT(rv == 11);

    TEST("sk_schema_insert_by_ident correctness");
    cf = sk_schema_get_field(s2, 10);
    RESULT(strcmp(sk_field_get_name(cf), "testUnsigned8") == 0);

    TEST("sk_schema_insert_by_ident correctness");
    cf = sk_schema_get_field(s2, 0);
    RESULT(strcmp(sk_field_get_name(cf), "testUnsigned16") == 0);

    TEST("sk_schema_insert_field_by_name");
    rv = sk_schema_insert_field_by_name(
        &f, s2, "testUnsigned32", NULL, NULL);
    RESULT(rv == 0);

    TEST("sk_schema_count");
    rv = sk_schema_get_count(s2);
    RESULT(rv == 12);

    TEST("sk_schema_insert_by_ident correctness");
    cf = sk_schema_get_field(s2, 11);
    RESULT(strcmp(sk_field_get_name(cf), "testUnsigned32") == 0);

    TEST("sk_schema_remove_field");
    cf = sk_schema_get_field(s2, 0);
    rv = sk_schema_remove_field(s2, cf);
    RESULT(rv == 0);

    TEST("sk_schema_count");
    rv = sk_schema_get_count(s2);
    RESULT(rv == 11);

    TEST("sk_schema_remove_field correctness");
    cf = sk_schema_get_field(s2, 10);
    RESULT(strcmp(sk_field_get_name(cf), "testUnsigned32") == 0);

    TEST("sk_schema_get_context");
    sk_schema_context_ident_create(&ctx_ident);
    vp = sk_schema_get_context(s2, ctx_ident);
    rv = (vp == NULL);
    RESULT(rv);

    TEST("sk_schema_set_context");
    ip = (int *)malloc(sizeof(int));
    *ip = 12;
    sk_schema_set_context(s2, ctx_ident, ip, free);
    vp = sk_schema_get_context(s2, ctx_ident);
    rv = (vp == (void *)ip) && *ip == 12;
    RESULT(rv);

    /* create several idents */
    sk_schema_context_ident_create(&ctx_ident2);
    assert(ctx_ident != ctx_ident2);
    for (i = 0; i < 32; ++i) {
        ctx_ident2 = SK_SCHEMA_CTX_IDENT_INVALID;
        sk_schema_context_ident_create(&ctx_ident2);
    }

    TEST("sk_schema_get_context");
    vp = sk_schema_get_context(s2, ctx_ident2);
    rv = (vp == NULL);
    RESULT(rv);

    TEST("sk_schema_set_context");
    ip = (int *)malloc(sizeof(int));
    *ip = 13;
    sk_schema_set_context(s2, ctx_ident2, ip, free);
    vp = sk_schema_get_context(s2, ctx_ident2);
    rv = (vp == (void *)ip) && *ip == 13;
    RESULT(rv);

    /* reset pointer of first ctx_ident */
    TEST("sk_schema_set_context");
    ip = (int *)malloc(sizeof(int));
    *ip = 14;
    sk_schema_set_context(s2, ctx_ident, ip, free);
    vp = sk_schema_get_context(s2, ctx_ident);
    rv = (vp == (void *)ip) && *ip == 14;
    RESULT(rv);

    TEST("sk_schema_destroy");
    rv = sk_schema_destroy(s2);
    RESULT(rv == 1);

    /* Schema initialization */

    SECTION("Schema initialization");

    TEST("sk_schema_freeze");
    rv = sk_schema_freeze(s1);
    RESULT(rv == 0);

    TEST("sk_schema_insert_by_ident");
    rv = sk_schema_insert_field_by_ident(
        &f, s1, SK_FIELD_IDENT_CREATE(IPFIX_CERT_PEN, 0x1001), NULL, NULL);
    RESULT(rv == SK_SCHEMA_ERR_FROZEN);

    TEST("sk_schema_remove_field");
    cf = sk_schema_get_field(s1, 0);
    rv = sk_schema_remove_field(s1, cf);
    RESULT(rv == SK_SCHEMA_ERR_FROZEN);

    TEST("sk_schema_copy");
    rv = sk_schema_copy(&s3, s1);
    RESULT(rv == 0);

    TEST("sk_schema_insert_by_ident");
    rv = sk_schema_insert_field_by_ident(
        &f, s3, SK_FIELD_IDENT_CREATE(IPFIX_CERT_PEN, 0x1001), NULL, NULL);
    RESULT(rv == 0);

    TEST("sk_schema_freeze");
    rv = sk_schema_freeze(s3);
    RESULT(rv == 0);

    TEST("sk_schema_destroy");
    rv = sk_schema_destroy(s3);
    RESULT(rv == 1);

    /* Records */

    SECTION("Records");

    TEST("sk_fixrec_init");
    sk_fixrec_init(&srec, s1);
    rv = (srec.data != NULL && srec.schema == (void *)s1);
    RESULT(rv);

    basic_setrec(&srec);
    basic_getrec(&srec);
    generic_getrec(&srec);

    generic_setrec(&srec);
    basic_getrec(&srec);
    generic_getrec(&srec);

    TEST("sk_fixrec_copy");
    rv = sk_fixrec_copy(&rec, &srec);
    RESULT(rv == 0);

    basic_getrec(rec);

    TEST("sk_fixrec_destroy");
    sk_fixrec_destroy(rec);
    RESULT(1);

    TEST("sk_fixrec_destroy");
    sk_fixrec_destroy(&srec);
    rv = (srec.data == NULL && srec.schema == NULL);
    RESULT(1);

    TEST("sk_fixrec_create");
    rv = sk_fixrec_create(&rec, s1);
    RESULT(rv == 0);

    basic_setrec(rec);
    basic_getrec(rec);

    /* Other */

    check_get_field_by(model);
    check_differently_sized_fields(model);
    check_to_text(rec);
    check_boolean(model);
    check_compare(rec);
    check_merge(rec);
    check_copy(model);
    check_map_differing_types(model, rec);

    /* Cleanup */

    SECTION("cleanup");

    TEST("sk_fixrec_destroy");
    sk_fixrec_destroy(rec);
    RESULT(1);

    TEST("sk_schema_destroy");
    rv = sk_schema_destroy(s1);
    RESULT(rv == 1);

    fbInfoModelFree(model);
    skAppUnregister();

    return 0;
}


/*
** Local Variables:
** mode:c
** indent-tabs-mode:nil
** c-basic-offset:4
** End:
*/
