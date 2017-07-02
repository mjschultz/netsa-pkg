/*
** Copyright (C) 2014-2017 by Carnegie Mellon University.
**
** @OPENSOURCE_LICENSE_START@
** See license information in ../../LICENSE.txt
** @OPENSOURCE_LICENSE_END@
*/

/*
**  Test functions for various IPFIX time fields in skschema.c
**
*/

#include <silk/silk.h>

RCSIDENT("$SiLK: skschema-test-times.c efd886457770 2017-06-21 18:43:23Z mthomas $");

#include <silk/skfixstream.h>
#include <silk/skipfixcert.h>
#include <silk/skschema.h>
#include <silk/skstream.h>
#include <silk/utils.h>

static fbInfoElementSpec_t model[] = {
    {(char*)"flowStartSeconds",       0, 0},
    {(char*)"flowStartMilliseconds",  0, 0},
    {(char*)"flowStartMicroseconds",  0, 0},
    {(char*)"flowStartNanoseconds",   0, 0},
    FB_IESPEC_NULL
};


static void
print_times(
    const sk_schema_t  *s,
    const sk_fixrec_t  *rec)
{
    size_t i;
    uint32_t secs;
    sktime_t t2;
    sk_ntp_time_t ntp;
    struct timespec ts;
    char buf[128];
    char buf2[128];
    unsigned int flags = (SKTIMESTAMP_UTC);

    for (i = 0; model[i].name; ++i) {
        const sk_field_t *f = sk_schema_get_field(s, i);
        sk_fixrec_get_datetime(rec, f, &t2);
        sk_fixrec_data_to_text(rec, f, buf, sizeof(buf));
        sk_fixrec_get_datetime_seconds(rec, f, &secs);
        sk_fixrec_get_datetime_ntp(rec, f, &ntp);
        sk_fixrec_get_datetime_timespec(rec, f, &ts);

        printf("%s\n"
               "\t%-10s %" PRId64 "  %s 0x%" PRIx64 "\n"
               "\t%-10s '%s'\n"
               "\t%-10s %" PRIu32 "  0x%" PRIx32 "\n"
               "\t%-10s %" PRIu64 ".%010" PRIu64
               "  0x%" PRIx64 ".%08" PRIx64 "\n"
               "\t%-10s %ld.%09ld  0x%lx.%08lx\n",
               model[i].name,
               "datetime", t2, sktimestamp_r(buf2, t2, flags), t2,
               "to_text", buf,
               "seconds", secs, secs,
               "ntp", SK_NTP_TIME_SECONDS(ntp), SK_NTP_TIME_FRACTIONAL(ntp),
               SK_NTP_TIME_SECONDS(ntp), SK_NTP_TIME_FRACTIONAL(ntp),
               "timespec", ts.tv_sec, ts.tv_nsec, ts.tv_sec, ts.tv_nsec);
    }
}


int main(int argc, char **argv)
{
    const uint32_t secs = 2000000000;
    const sktime_t t = sktimeCreate(2000000000, 125);
    const sk_ntp_time_t ntp1
        = (((UINT64_C(2000000000) + UINT64_C(0x83AA7E80)) << 32) | 0x40000000);
    const sk_ntp_time_t ntp2
        = (((UINT64_C(2000000000) + UINT64_C(0x83AA7E80)) << 32) | 0x80000000);
    const struct timespec ts = {2000000000, 123456789};

    fbInfoModel_t *info_model = NULL;
    ssize_t rv;
    sk_schema_t *s;
    const sk_field_t *f;
    sk_fixrec_t *rec;
    sk_fixstream_t *stream = NULL;
    char output[PATH_MAX];
    unsigned int i;

    /* register the application */
    skAppRegister(argv[0]);

    skipfix_initialize(0);

    /* Prepare the information model */
    info_model = skipfix_information_model_create(0);

    if (argc > 1) {
        /* write output */
        skstream_t *tmp;

        snprintf(output, sizeof(output), "/tmp/output-%s.XXXXXX", skAppName());

        if ((rv = skStreamCreate(&tmp, SK_IO_WRITE, SK_CONTENT_OTHERBINARY))
            || (rv = skStreamBind(tmp, output))
            || (rv = skStreamMakeTemp(tmp)))
        {
            skStreamPrintLastErr(tmp, rv, NULL);
            skStreamDestroy(&tmp);
            exit(EXIT_FAILURE);
        }
        printf("Writing output to '%s'\n", skStreamGetPathname(tmp));

        sk_fixstream_create(&stream);
        sk_fixstream_set_stream(stream, tmp);
        rv = sk_fixstream_open(stream);
        if (rv) {
            skAppPrintErr("%s", sk_fixstream_strerror(stream));
            sk_fixstream_destroy(&stream);
        }
    }

    rv = sk_schema_create(&s, info_model, model, 0);
    if (rv) {
        skAppPrintErr("failed to create schema: %s", sk_schema_strerror(rv));
        exit(EXIT_FAILURE);
    }
    sk_schema_freeze(s);


    /* **** EXPLICITLY SET FIELDS **** */

    printf("\nSetting fields explicitly\n");

    sk_fixrec_create(&rec, s);

    i = 0;
    f = sk_schema_get_field(s, i);
    rv = sk_fixrec_set_datetime_seconds(rec, f, secs);
    if (rv) {
        skAppPrintErr("Cannot set field %s: %s",
                      model[i].name, sk_schema_strerror(rv));
    }

    ++i;
    f = sk_schema_get_field(s, i);
    rv = sk_fixrec_set_datetime_milliseconds(rec, f, (uint64_t)t);
    if (rv) {
        skAppPrintErr("Cannot set field %s: %s",
                      model[i].name, sk_schema_strerror(rv));
    }

    ++i;
    f = sk_schema_get_field(s, i);
    rv = sk_fixrec_set_datetime_microseconds(rec, f, ntp1);
    if (rv) {
        skAppPrintErr("Cannot set field %s: %s",
                      model[i].name, sk_schema_strerror(rv));
    }

    ++i;
    f = sk_schema_get_field(s, i);
    rv = sk_fixrec_set_datetime_nanoseconds(rec, f, ntp2);
    if (rv) {
        skAppPrintErr("Cannot set field %s: %s",
                      model[i].name, sk_schema_strerror(rv));
    }

    if (stream) {
        sk_fixstream_write_record(stream, rec, NULL);
    }

    print_times(s, rec);

    sk_fixrec_destroy(rec);


    /* **** SKTIME_T **** */

    printf("\nSetting the datetime to sktime = %" PRId64 " (0x%" PRIx64 ")\n",
           t, t);

    sk_fixrec_create(&rec, s);
    for (i = 0; model[i].name; ++i) {
        f = sk_schema_get_field(s, i);
        rv = sk_fixrec_set_datetime(rec, f, t);
        if (rv) {
            skAppPrintErr("Cannot set field %s: %s",
                          model[i].name, sk_schema_strerror(rv));
        }
    }
    if (stream) {
        sk_fixstream_write_record(stream, rec, NULL);
    }

    print_times(s, rec);

    sk_fixrec_destroy(rec);


    /* **** NTP TIME **** */

    printf("\nSetting the datetime ntp to %" PRIu64 ".%09" PRIu64
           " (%" PRIx64 ")\n",
           SK_NTP_TIME_SECONDS(ntp1), SK_NTP_TIME_FRACTIONAL(ntp1), ntp1);

    sk_fixrec_create(&rec, s);
    for (i = 0; model[i].name; ++i) {
        f = sk_schema_get_field(s, i);
        rv = sk_fixrec_set_datetime_ntp(rec, f, ntp1);
        if (rv) {
            skAppPrintErr("Cannot set field %s: %s",
                          model[i].name, sk_schema_strerror(rv));
        }
    }
    if (stream) {
        sk_fixstream_write_record(stream, rec, NULL);
    }

    print_times(s, rec);

    sk_fixrec_destroy(rec);


    /* **** TIMESPEC **** */

    printf("\nSetting the datetime timespec to %ld.%09ld\n",
           ts.tv_sec, ts.tv_nsec);

    sk_fixrec_create(&rec, s);
    for (i = 0; model[i].name; ++i) {
        f = sk_schema_get_field(s, i);
        rv = sk_fixrec_set_datetime_timespec(rec, f, &ts);
        if (rv) {
            skAppPrintErr("Cannot set field %s: %s",
                          model[i].name, sk_schema_strerror(rv));
        }
    }
    if (stream) {
        sk_fixstream_write_record(stream, rec, NULL);
    }

    print_times(s, rec);

    sk_fixrec_destroy(rec);

    sk_schema_destroy(s);

    /* close the file */
    if (stream) {
        rv = sk_fixstream_close(stream);
        if (rv) {
            skAppPrintErr("%s", sk_fixstream_strerror(stream));
            sk_fixstream_destroy(&stream);
            exit(EXIT_FAILURE);
        }
        sk_fixstream_destroy(&stream);
    }

    skipfix_information_model_destroy(info_model);

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
