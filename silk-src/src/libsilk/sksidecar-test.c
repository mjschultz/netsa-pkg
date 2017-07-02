/*
** Copyright (C) 2015-2017 by Carnegie Mellon University.
**
** @OPENSOURCE_LICENSE_START@
** See license information in ../../LICENSE.txt
** @OPENSOURCE_LICENSE_END@
*/

/*
 *  Add your description here
 *  Include the author's name and date (month and year is good enough).
 *
 */

#include <silk/silk.h>

RCSIDENT("$SiLK: sksidecar-test.c efd886457770 2017-06-21 18:43:23Z mthomas $");

#include <silk/skipfixcert.h>
#include <silk/sksidecar.h>
#include <silk/utils.h>


/* LOCAL DEFINES AND TYPEDEFS */

#define USAGE_FH stderr


/* FUNCTION DEFINITIONS */

/*
 *  appUsageLong();
 *
 *    Print complete usage information to USAGE_FH.  Pass this
 *    function to skOptionsSetUsageCallback(); skOptionsParse() will
 *    call this funciton and then exit the program when the --help
 *    option is given.
 */
static void
appUsageLong(
    void)
{
#define USAGE_MSG                                                       \
    ("[SWITCHES]\n"                                                     \
    "\tSmall application to test creating and reading a sidecar header.\n")

    FILE *fh = USAGE_FH;

    skAppStandardUsage(fh, USAGE_MSG, NULL, NULL);
}


int
main(
    int                 argc,
    char              **argv)
{
    SILK_FEATURES_DEFINE_STRUCT(features);
    typedef struct field_st {
        const char              name[64];
        size_t                  name_len;
        sk_sidecar_type_t       data_type;
        sk_sidecar_type_t       list_elem;
        sk_field_ident_t        ipfix_ident;
    } field_t;
    sk_sidecar_t *sc;
    sk_sidecar_iter_t iter;
    const sk_sidecar_elem_t *e;
    uint8_t printable[24];
    uint8_t buf[PATH_MAX];
    char name[PATH_MAX];
    size_t buflen;
    size_t namelen;
    size_t i;

    /* list of fields */
    const field_t field[] = {
        {"uint8_t",                 0,  SK_SIDECAR_UINT8,
         SK_SIDECAR_UNKNOWN,    0},
        {"sourceTransportPort",     0,  SK_SIDECAR_UINT16,
         SK_SIDECAR_UNKNOWN,    7},
        {"ingressInterface",        0,  SK_SIDECAR_UINT32,
         SK_SIDECAR_UNKNOWN,   10},
        {"now",                     0,  SK_SIDECAR_DATETIME,
         SK_SIDECAR_UNKNOWN,    0},
        {"sourceIPv4Address",       0,  SK_SIDECAR_ADDR_IP4,
         SK_SIDECAR_UNKNOWN,    8},
        {"destinationIPv6Address",  0,  SK_SIDECAR_ADDR_IP6,
         SK_SIDECAR_UNKNOWN,   28},
        {"silkSensor",              0,  SK_SIDECAR_UINT16,
         SK_SIDECAR_UNKNOWN,   SK_FIELD_IDENT_CREATE(IPFIX_CERT_PEN, 31)},
        {"packetTotalCount",        0,  SK_SIDECAR_UINT64,
         SK_SIDECAR_UNKNOWN,   86},
        {"random string",           0,  SK_SIDECAR_STRING,
         SK_SIDECAR_UNKNOWN,    0},
        {"destinationMacAddress",   0,  SK_SIDECAR_BINARY,
         SK_SIDECAR_UNKNOWN,   80},
        {"hashDigestOutput",        0,  SK_SIDECAR_BOOLEAN,
         SK_SIDECAR_UNKNOWN,  333},
        {"empty",                   0,  SK_SIDECAR_EMPTY,
         SK_SIDECAR_UNKNOWN,    0},
        {"absoluteError",           0,  SK_SIDECAR_DOUBLE,
         SK_SIDECAR_UNKNOWN,  320},

        /* a list */
        {"smtpTo",                  0,  SK_SIDECAR_LIST,
         SK_SIDECAR_STRING,    SK_FIELD_IDENT_CREATE(IPFIX_CERT_PEN, 164)},

        /* a table */
        {"tcpInfo",                 0,  SK_SIDECAR_TABLE,
         SK_SIDECAR_UNKNOWN,    0},
        {"tcpInfo\0tcpSequenceNumber",          26, SK_SIDECAR_UINT32,
         SK_SIDECAR_UNKNOWN,  184},
        {"tcpInfo\0initialTCPFlags",            24, SK_SIDECAR_UINT8,
         SK_SIDECAR_UNKNOWN,   SK_FIELD_IDENT_CREATE(6871, 14)},
        {"tcpInfo\0unionTCPFlags",              22, SK_SIDECAR_UINT8,
         SK_SIDECAR_UNKNOWN,   SK_FIELD_IDENT_CREATE(6871, 15)},
        {"tcpInfo\0reverseInitialTCPFlags",     31, SK_SIDECAR_UINT8,
         SK_SIDECAR_UNKNOWN,   SK_FIELD_IDENT_CREATE(6871, 16398)},
        {"tcpInfo\0reverseUnionTCPFlags",       29, SK_SIDECAR_UINT8,
         SK_SIDECAR_UNKNOWN,   SK_FIELD_IDENT_CREATE(6871, 16399)},
        {"tcpInfo\0reverseTcpSequenceNumber",   33, SK_SIDECAR_UINT32,
         SK_SIDECAR_UNKNOWN,   SK_FIELD_IDENT_CREATE(29305, 184)},

        {"", 0, SK_SIDECAR_UNKNOWN, SK_SIDECAR_UNKNOWN, 0} /* sentinel */
    };

    skAppRegister(argv[0]);
    skAppVerifyFeatures(&features, NULL);
    skOptionsSetUsageCallback(&appUsageLong);

    if (skOptionsParse(argc, argv) != argc) {
        skAppUsage();
    }

    /* Create sidecar object and add fields */
    sk_sidecar_create(&sc);
    for (i = 0; field[i].name[0]; ++i) {
#if 0
        /* DEBUGGGING CODE */
        if (0 == field[i].name_len) {
            fprintf(stderr, "%" SK_PRIuZ "  '%s'\n", i, field[i].name);
        } else {
            size_t sz, len;
            sz = 0;
            do {
                len = strlen(field[i].name + sz);
                fprintf(stderr, ("DEBUG: %" SK_PRIuZ "  %" SK_PRIuZ
                                 " '%s'  %" SK_PRIuZ "\n"),
                        i, sz, field[i].name+sz, len);
                strncpy(name + sz, field[i].name + sz, sizeof(name)-sz);
                sz += 1 + len;
            } while (sz < field[i].name_len);
            fprintf(stderr, "DEBUG: %" SK_PRIuZ "  %" SK_PRIuZ "\n", i, sz);
            fprintf(stderr, "%" SK_PRIuZ "  '", i);
            fwrite(name, 1, sz, stderr);
            fprintf(stderr, "'  %" SK_PRIuZ "  %" SK_PRIuZ "\n",
                    sz, field[i].name_len);
        }
#endif  /* 0 */

        if (SK_SIDECAR_LIST != field[i].data_type) {
            sk_sidecar_append(sc, field[i].name, field[i].name_len,
                              field[i].data_type, field[i].ipfix_ident);
        } else {
            sk_sidecar_append_list(sc, field[i].name, field[i].name_len,
                                   field[i].list_elem, field[i].ipfix_ident);
        }
    }

    /* Serialize it into 'buf' */
    buflen = sizeof(buf);
    if (sk_sidecar_serialize_self(sc, buf, &buflen)) {
        skAppPrintErr("Error in serialize");
        return EXIT_FAILURE;
    }

    /* Destroy it */
    sk_sidecar_destroy(&sc);

    /* Print serialized buffer in "hexdump" type format */
    for (i = 0; i < buflen; ++i) {
        if (0 == (i & 0x0f)) {
            /* divisible by 16; start a new line (if not first line),
             * print the current position, print value */
            if (i > 0) {
                printf("  |%s|\n", printable);
            }
            memset(printable, 0, sizeof(printable));
            printf("%08lx  %02x", (unsigned long)i, buf[i]);
        } else if (0 == (i & 0x07)) {
            /* divisible by 8; add an extra space and print value */
            printf("  %02x", buf[i]);
        } else {
            printf(" %02x", buf[i]);
        }
        printable[(i & 0x0f)] = (isalnum((int)buf[i]) ? buf[i] : '.');
    }
    /* print newline (if we have output) and length of buffer */
    if (i > 0) {
        /* if divisible by 16, we want 16, not 0 */
        int pos = 1 + ((i - 1) & 0x0f);
        printable[pos] = '|';
        /* allow 3 spaces for each value not printed, plus one extra
         * space if 8 or fewer values were printed */
        printf("%*s  |%s\n", (int)(3 * (16 - pos) + (pos <= 8)),
               "", printable);
    }
    printf("%08lx\n", (unsigned long)buflen);


    /* Create anothe sidecar object */
    sk_sidecar_create(&sc);

    /* Read serialized object into this sidecar */
    if (sk_sidecar_deserialize_self(sc, buf, &buflen)) {
        skAppPrintErr("Error in deserialize");
        return EXIT_FAILURE;
    }

    /* Print the object and compare against original fields */
    printf("Deserialized object holds %" SK_PRIuZ " elements\n",
           sk_sidecar_count_elements(sc));

    sk_sidecar_iter_bind(sc, &iter);

    for (i = 0; SK_ITERATOR_OK == sk_sidecar_iter_next(&iter, &e); ++i) {
        if ('\0' == field[i].name[0]) {
            skAppPrintErr("Out of fields before iterator ended");
            return EXIT_FAILURE;
        }
        namelen = sizeof(name);
        printf("Entry %2" SK_PRIuZ "  name %s, type %s%s, IPFIX ident %s\n",
               i,
               ((0 == strcmp(sk_sidecar_elem_get_name(e, name, &namelen),
                             field[i].name))
                ? "ok" : "mismatch"),
               ((sk_sidecar_elem_get_data_type(e) == field[i].data_type)
                ? "ok" : "mismatch"),
               ((SK_SIDECAR_LIST != sk_sidecar_elem_get_data_type(e)
                 && SK_SIDECAR_UNKNOWN==sk_sidecar_elem_get_list_elem_type(e))
                ? ""
                : ((SK_SIDECAR_LIST == sk_sidecar_elem_get_data_type(e)
                    && (sk_sidecar_elem_get_list_elem_type(e)
                        == field[i].list_elem))
                   ? ",ok"
                   : ",mismatch")),
               ((sk_sidecar_elem_get_ipfix_ident(e) == field[i].ipfix_ident)
                ? "ok" : "mismatch"));
    }

    if ('\0' != field[i].name[0]) {
        skAppPrintErr("Iterator ended before fields");
        return EXIT_FAILURE;
    }

    /* Destroy it */
    sk_sidecar_destroy(&sc);

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
