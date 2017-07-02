/*
** Copyright (C) 2015-2017 by Carnegie Mellon University.
**
** @OPENSOURCE_LICENSE_START@
** See license information in ../../LICENSE.txt
** @OPENSOURCE_LICENSE_END@
*/

/*
 *  skschema-test-computed.c
 *
 *    Regression testing application for computed fields in
 *    schemas/records.
 *
 *    January 2015
 */

/* enable assert() */
#undef NDEBUG

#include <silk/silk.h>

RCSIDENT("$SiLK: skschema-test-computed.c efd886457770 2017-06-21 18:43:23Z mthomas $");

#include <silk/skfixstream.h>
#include <silk/skschema.h>
#include <silk/skstream.h>
#include <silk/utils.h>

static sk_schema_ctx_ident_t ident = SK_SCHEMA_CTX_IDENT_INVALID;

static void
schema_callback_fn(
    sk_schema_t        *schema,
    uint16_t     UNUSED(tid),
    void               *cbdata)
{
    sk_schema_err_t rv;
    sk_field_computed_description_t *desc =
        (sk_field_computed_description_t *)cbdata;
    const sk_field_t *field;

    rv = sk_schema_insert_computed_field(&field, schema, desc, NULL);
    assert(rv == 0);
    if (desc->lookup == SK_FIELD_COMPUTED_CREATE) {
        desc->ident = sk_field_get_ident(field);
        desc->lookup = SK_FIELD_COMPUTED_LOOKUP_BY_IDENT;
    }

    sk_schema_set_context(schema, ident, (void *)field, NULL);
}

static sk_schema_err_t
calculate_bpp(
    sk_fixrec_t                    *rec,
    const sk_field_computed_data_t *data)
{
    const sk_field_t *bytes;
    const sk_field_t *packets;
    double ratio;
    uint64_t u64;
    double bytes_val;
    double packets_val;
    sk_schema_err_t rv;

    assert(data->entries == 4);

    bytes = data->fields[0] ? data->fields[0] : data->fields[1];
    packets = data->fields[2] ? data->fields[2] : data->fields[3];
    if (bytes == NULL || packets == NULL) {
        ratio = 0.0;
    } else {
        rv = sk_fixrec_get_unsigned64(rec, bytes, &u64);
        assert(rv == 0);
        bytes_val = u64;
        rv = sk_fixrec_get_unsigned64(rec, packets, &u64);
        assert(rv == 0);
        packets_val = u64;
        ratio = bytes_val / packets_val;
    }
    sk_fixrec_set_float64(rec, data->dest, ratio);
    return 0;
}


int main(int argc, const char **argv)
{
    SILK_FEATURES_DEFINE_STRUCT(features);
    sk_field_computed_description_t desc;
    sk_fixstream_t *stream;
    const sk_fixrec_t *rec;
    const char *field_list[] = {
        "octetDeltaCount",      /* 0 */
        "octetTotalCount",      /* 1 */
        "packetDeltaCount",     /* 2 */
        "packetTotalCount",     /* 3 */
    };
    int rv;

    skAppRegister(argv[0]);
    skAppVerifyFeatures(&features, NULL);

    if (argc != 2) {
        fprintf(stderr, "Usage: %s <ipfix-filename>\n", skAppName());
        exit(EXIT_FAILURE);
    }

    sk_schema_context_ident_create(&ident);

    memset(&desc, 0, sizeof(desc));
    desc.lookup = SK_FIELD_COMPUTED_CREATE;
    desc.name = "bytesPerPacket";
    desc.datatype = FB_FLOAT_64;
    desc.len = 8;
    desc.update = calculate_bpp;
    desc.field_names = field_list;
    desc.field_names_len = (sizeof(field_list) / sizeof(field_list[0]));

    if ((rv = sk_fixstream_create(&stream))
        || (rv = sk_fixstream_bind(stream, argv[1], SK_IO_READ))
        || (rv = sk_fixstream_set_schema_cb(
                stream, schema_callback_fn, &desc))
        || (rv = sk_fixstream_open(stream)))
    {
        skAppPrintErr("%s", sk_fixstream_strerror(stream));
        sk_fixstream_destroy(&stream);
        exit(EXIT_FAILURE);
    }

    while ((rv = sk_fixstream_read_record(stream, &rec)) == 0) {
        sk_schema_err_t err;
        double ratio;
        const sk_field_t *field;

        field = (const sk_field_t *)(sk_schema_get_context(
                                         sk_fixrec_get_schema(rec), ident));
        assert(field != NULL);
        err = sk_fixrec_get_float64(rec, field, &ratio);
        assert(err == 0);
        fprintf(stdout, "%f\n", ratio);
    }
    if (rv != SKSTREAM_ERR_EOF) {
        skAppPrintErr("%s", sk_fixstream_strerror(stream));
        sk_fixstream_destroy(&stream);
        exit(EXIT_FAILURE);
    }

    sk_fixstream_destroy(&stream);
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
