/*
** Copyright (C) 2016-2020 by Carnegie Mellon University.
**
** @OPENSOURCE_LICENSE_START@
** See license information in ../../LICENSE.txt
** @OPENSOURCE_LICENSE_END@
*/

/*
 *  rwaggbagcat.c
 *
 *    Print an Aggregate Bag file as text.
 *
 *  Mark Thomas
 *  December 2016
 */

#include <silk/silk.h>

RCSIDENT("$SiLK: rwaggbagcat.c ef14e54179be 2020-04-14 21:57:45Z mthomas $");

#include <silk/silk_files.h>
#include <silk/skaggbag.h>
#include <silk/skcountry.h>
#include <silk/sksite.h>
#include <silk/utils.h>


/* TYPEDEFS AND DEFINES */

/* file handle for --help usage message */
#define USAGE_FH stdout


/* LOCAL VARIABLES */

/* how to print IP addresses */
static uint32_t ip_format = SKIPADDR_CANONICAL;

/* how to print timestamps */
static uint32_t timestamp_format = 0;

/* flags when registering --timestamp-format */
static const uint32_t time_register_flags =
    (SK_OPTION_TIMESTAMP_NEVER_MSEC);

/* the output stream.  stdout, PAGER, or value set by --output-path */
static sk_fileptr_t output;

/* name of program to run to page output, set by --pager or PAGER */
static char *pager;

/* the width of each column in the output */
static int width[UINT8_MAX];

/* separator between output columns */
static char column_separator = '|';

/* output features set by the specified switch */
static struct app_flags_st {
    /* --no-columns */
    unsigned no_columns         :1;
    /* --no-titles */
    unsigned no_titles          :1;
    /* --no-final-delimiter */
    unsigned no_final_delimiter :1;
    /* --integer-sensors */
    unsigned integer_sensors    :1;
    /* --integer-tcp-flags */
    unsigned integer_tcp_flags  :1;
} app_flags;

/* input checker */
static sk_options_ctx_t *optctx = NULL;


/* OPTIONS */

typedef enum {
    OPT_INTEGER_SENSORS,
    OPT_INTEGER_TCP_FLAGS,
    OPT_NO_TITLES,
    OPT_NO_COLUMNS,
    OPT_COLUMN_SEPARATOR,
    OPT_NO_FINAL_DELIMITER,
    OPT_DELIMITED,
    OPT_OUTPUT_PATH,
    OPT_PAGER
} appOptionsEnum;


static struct option appOptions[] = {
    {"integer-sensors",     NO_ARG,       0, OPT_INTEGER_SENSORS},
    {"integer-tcp-flags",   NO_ARG,       0, OPT_INTEGER_TCP_FLAGS},
    {"no-titles",           NO_ARG,       0, OPT_NO_TITLES},
    {"no-columns",          NO_ARG,       0, OPT_NO_COLUMNS},
    {"column-separator",    REQUIRED_ARG, 0, OPT_COLUMN_SEPARATOR},
    {"no-final-delimiter",  NO_ARG,       0, OPT_NO_FINAL_DELIMITER},
    {"delimited",           OPTIONAL_ARG, 0, OPT_DELIMITED},
    {"output-path",         REQUIRED_ARG, 0, OPT_OUTPUT_PATH},
    {"pager",               REQUIRED_ARG, 0, OPT_PAGER},
    {0,0,0,0}               /* sentinel entry */
};

static const char *appHelp[] = {
    "Print sensor as an integer. Def. Sensor name",
    "Print TCP Flags as an integer. Def. No",
    "Do not print column titles. Def. Print titles",
    "Disable fixed-width columnar output. Def. Columnar",
    "Use specified character between columns. Def. '|'",
    "Suppress column delimiter at end of line. Def. No",
    "Shortcut for --no-columns --no-final-del --column-sep=CHAR",
    "Write the output to this stream or file. Def. stdout",
    "Invoke this program to page output. Def. $SILK_PAGER or $PAGER",
    (char *)NULL
};



/* LOCAL FUNCTION PROTOTYPES */

static int  appOptionsHandler(clientData cData, int opt_index, char *opt_arg);


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
    FILE *fh = USAGE_FH;
    unsigned int i;

#define USAGE_MSG                                                           \
    ("[SWITCHES] [AGGBAG_FILES]\n"                                          \
     "\tPrint binary Aggregate Bag files as text to the standard output,\n" \
     "\tthe pager, or the --output-path. When multiple files are given,\n"  \
     "\tthe files are processed sequentially: they are not merged.\n")

    /* Create the string maps for --fields and --values */
    /*createStringmaps();*/

    fprintf(fh, "%s %s", skAppName(), USAGE_MSG);

    fprintf(fh, "\nSWITCHES:\n");
    skOptionsDefaultUsage(fh);
    skOptionsTimestampFormatUsage(fh);
    skOptionsIPFormatUsage(fh);
    for (i = 0; appOptions[i].name; ++i) {
        fprintf(fh, "--%s %s. %s\n", appOptions[i].name,
                SK_OPTION_HAS_ARG(appOptions[i]), appHelp[i]);
    }

    skOptionsCtxOptionsUsage(optctx, fh);
    sksiteOptionsUsage(fh);
}


/*
 *  appTeardown()
 *
 *    Teardown all modules, close all files, and tidy up all
 *    application state.
 *
 *    This function is idempotent.
 */
static void
appTeardown(
    void)
{
    static int teardown_flag = 0;

    if (teardown_flag) {
        return;
    }
    teardown_flag = 1;

    /* close output */
    if (output.of_name) {
        skFileptrClose(&output, &skAppPrintErr);
    }

    skOptionsCtxDestroy(&optctx);
    skAppUnregister();
}


/*
 *  appSetup(argc, argv);
 *
 *    Perform all the setup for this application include setting up
 *    required modules, parsing options, etc.  This function should be
 *    passed the same arguments that were passed into main().
 *
 *    Returns to the caller if all setup succeeds.  If anything fails,
 *    this function will cause the application to exit with a FAILURE
 *    exit status.
 */
static void
appSetup(
    int                 argc,
    char              **argv)
{
    SILK_FEATURES_DEFINE_STRUCT(features);
    unsigned int optctx_flags;
    int rv;

    /* verify same number of options and help strings */
    assert((sizeof(appHelp)/sizeof(char *)) ==
           (sizeof(appOptions)/sizeof(struct option)));

    /* register the application */
    skAppRegister(argv[0]);
    skAppVerifyFeatures(&features, NULL);
    skOptionsSetUsageCallback(&appUsageLong);

    /* initialize globals */
    memset(&app_flags, 0, sizeof(app_flags));
    memset(&output, 0, sizeof(output));
    output.of_fp = stdout;

    optctx_flags = (SK_OPTIONS_CTX_INPUT_BINARY | SK_OPTIONS_CTX_ALLOW_STDIN);

    /* register the options */
    if (skOptionsCtxCreate(&optctx, optctx_flags)
        || skOptionsCtxOptionsRegister(optctx)
        || skOptionsRegister(appOptions, &appOptionsHandler, NULL)
        || skOptionsTimestampFormatRegister(
            &timestamp_format, time_register_flags)
        || skOptionsIPFormatRegister(&ip_format, 0)
        || sksiteOptionsRegister(SK_SITE_FLAG_CONFIG_FILE))
    {
        skAppPrintErr("Unable to register options");
        exit(EXIT_FAILURE);
    }

    /* register the teardown handler */
    if (atexit(appTeardown) < 0) {
        skAppPrintErr("Unable to register appTeardown() with atexit()");
        exit(EXIT_FAILURE);
    }

    /* parse options */
    rv = skOptionsCtxOptionsParse(optctx, argc, argv);
    if (rv < 0) {
        skAppUsage();           /* never returns */
    }

    /* try to load site config file */
    sksiteConfigure(0);

    /* open the --output-path.  the 'of_name' member is NULL if user
     * did not specify an output-path. */
    if (output.of_name) {
        rv = skFileptrOpen(&output, SK_IO_WRITE);
        if (rv) {
            skAppPrintErr("Unable to open %s '%s': %s",
                          appOptions[OPT_OUTPUT_PATH].name,
                          output.of_name, skFileptrStrerror(rv));
            exit(EXIT_FAILURE);
        }
    }

    return;
}


/*
 *  status = appOptionsHandler(cData, opt_index, opt_arg);
 *
 *    Called by skOptionsParse(), this handles a user-specified switch
 *    that the application has registered, typically by setting global
 *    variables.  Returns 1 if the switch processing failed or 0 if it
 *    succeeded.  Returning a non-zero from from the handler causes
 *    skOptionsParse() to return a negative value.
 *
 *    The clientData in 'cData' is typically ignored; 'opt_index' is
 *    the index number that was specified as the last value for each
 *    struct option in appOptions[]; 'opt_arg' is the user's argument
 *    to the switch for options that have a REQUIRED_ARG or an
 *    OPTIONAL_ARG.
 */
static int
appOptionsHandler(
    clientData   UNUSED(cData),
    int                 opt_index,
    char               *opt_arg)
{
    switch ((appOptionsEnum)opt_index) {
      case OPT_INTEGER_SENSORS:
        app_flags.integer_sensors = 1;
        break;

      case OPT_INTEGER_TCP_FLAGS:
        app_flags.integer_tcp_flags = 1;
        break;

      case OPT_NO_TITLES:
        app_flags.no_titles = 1;
        break;

      case OPT_NO_COLUMNS:
        app_flags.no_columns = 1;
        break;

      case OPT_NO_FINAL_DELIMITER:
        app_flags.no_final_delimiter = 1;
        break;

      case OPT_COLUMN_SEPARATOR:
        column_separator = opt_arg[0];
        break;

      case OPT_DELIMITED:
        app_flags.no_columns = 1;
        app_flags.no_final_delimiter = 1;
        if (opt_arg) {
            column_separator = opt_arg[0];
        }
        break;

      case OPT_OUTPUT_PATH:
        if (output.of_name) {
            skAppPrintErr("Invalid %s: Switch used multiple times",
                          appOptions[opt_index].name);
            return 1;
        }
        output.of_name = opt_arg;
        break;

      case OPT_PAGER:
        pager = opt_arg;
        break;
    }

    return 0;
}


/*
 *    Return the file handle to use for output.  This invokes the
 *    pager if necessary.
 */
static FILE *
getOutputHandle(
    void)
{
    int rv;

    /* only invoke the pager when the user has not specified the
     * output-path, even if output-path is stdout */
    if (NULL == output.of_name) {
        rv = skFileptrOpenPager(&output, pager);
        if (rv && rv != SK_FILEPTR_PAGER_IGNORED) {
            skAppPrintErr("Unable to invoke pager");
        }
    }
    return output.of_fp;
}


/*
 *    Determine the widths of the output columns.
 */
static void
determineWidths(
    const sk_aggbag_t  *ab)
{
    sk_aggbag_field_t field;
    sk_aggbag_aggregate_t agg;
    unsigned int col;

    if (app_flags.no_columns) {
        return;
    }

    col = 0;

    skAggBagInitializeKey(ab, &agg, &field);
    do {
        switch (skAggBagFieldIterGetType(&field)) {
          case SKAGGBAG_FIELD_SIPv4:
          case SKAGGBAG_FIELD_DIPv4:
          case SKAGGBAG_FIELD_NHIPv4:
          case SKAGGBAG_FIELD_ANY_IPv4:
            width[col] = skipaddrStringMaxlen(0, ip_format);
            break;
          case SKAGGBAG_FIELD_SIPv6:
          case SKAGGBAG_FIELD_DIPv6:
          case SKAGGBAG_FIELD_NHIPv6:
          case SKAGGBAG_FIELD_ANY_IPv6:
            width[col] = skipaddrStringMaxlen(1, ip_format);
            break;
          case SKAGGBAG_FIELD_SPORT:
          case SKAGGBAG_FIELD_DPORT:
          case SKAGGBAG_FIELD_ANY_PORT:
          case SKAGGBAG_FIELD_ELAPSED:
          case SKAGGBAG_FIELD_APPLICATION:
          case SKAGGBAG_FIELD_INPUT:
          case SKAGGBAG_FIELD_OUTPUT:
          case SKAGGBAG_FIELD_ANY_SNMP:
            width[col] = 5;
            break;
          case SKAGGBAG_FIELD_PROTO:
          case SKAGGBAG_FIELD_ICMP_TYPE:
          case SKAGGBAG_FIELD_ICMP_CODE:
            width[col] = 3;
            break;
          case SKAGGBAG_FIELD_PACKETS:
          case SKAGGBAG_FIELD_BYTES:
          case SKAGGBAG_FIELD_CUSTOM_KEY:
            width[col] = 10;
            break;
          case SKAGGBAG_FIELD_STARTTIME:
          case SKAGGBAG_FIELD_ENDTIME:
          case SKAGGBAG_FIELD_ANY_TIME:
            if (timestamp_format & SKTIMESTAMP_EPOCH) {
                width[col] = 10;
            } else {
                width[col] = 19;
            }
            break;
          case SKAGGBAG_FIELD_FLAGS:
          case SKAGGBAG_FIELD_INIT_FLAGS:
          case SKAGGBAG_FIELD_REST_FLAGS:
            if (app_flags.integer_tcp_flags) {
                width[col] = 3;
            } else {
                width[col] = 8;
            }
            break;
          case SKAGGBAG_FIELD_TCP_STATE:
            width[col] = 8;
            break;
          case SKAGGBAG_FIELD_SID:
            if (app_flags.integer_sensors) {
                width[col] = 5;
            } else {
                width[col] = sksiteSensorGetMaxNameStrLen();
            }
            break;
          case SKAGGBAG_FIELD_FTYPE_CLASS:
            width[col] = sksiteClassGetMaxNameStrLen();
            break;
          case SKAGGBAG_FIELD_FTYPE_TYPE:
            width[col] = (uint8_t)sksiteFlowtypeGetMaxTypeStrLen();
            break;
          case SKAGGBAG_FIELD_SIP_COUNTRY:
          case SKAGGBAG_FIELD_DIP_COUNTRY:
          case SKAGGBAG_FIELD_ANY_COUNTRY:
            width[col] = 2;
            break;
          default:
            break;
        }
        ++col;
    } while (skAggBagFieldIterNext(&field) == SK_ITERATOR_OK);

    skAggBagInitializeCounter(ab, &agg, &field);
    do {
        switch (skAggBagFieldIterGetType(&field)) {
          case SKAGGBAG_FIELD_RECORDS:
            width[col] = 10;
            break;
          case SKAGGBAG_FIELD_SUM_BYTES:
            width[col] = 20;
            break;
          case SKAGGBAG_FIELD_SUM_PACKETS:
            width[col] = 15;
            break;
          case SKAGGBAG_FIELD_SUM_ELAPSED:
            width[col] = 10;
            break;
          case SKAGGBAG_FIELD_CUSTOM_COUNTER:
            width[col] = 20;
            break;
          default:
            break;
        }
        ++col;
    } while (skAggBagFieldIterNext(&field) == SK_ITERATOR_OK);
}


/*
 *    Print the column titles.  Does nothing if the user has requested
 *    --no-titles.
 */
static void
printTitles(
    const sk_aggbag_t  *ab,
    FILE               *fh)
{
    sk_aggbag_field_t field;
    sk_aggbag_aggregate_t agg;
    sk_aggbag_type_t t;
    const char *name;
    unsigned int col;
    unsigned int kc;
    char delim[] = {'\0', '\0'};

    if (app_flags.no_titles) {
        return;
    }

    col = 0;

    for (kc = 0; kc < 2; ++kc) {
        if (0 == kc) {
            skAggBagInitializeKey(ab, &agg, &field);
        } else {
            skAggBagInitializeCounter(ab, &agg, &field);
        }
        do {
            t = skAggBagFieldIterGetType(&field);
            name = skAggBagFieldTypeGetName(t);
            if (app_flags.no_columns) {
                fprintf(fh, "%s%s", delim, name);
            } else {
                fprintf(fh, "%s%*.*s", delim, width[col], width[col], name);
            }
            if (0 == col) {
                delim[0] = column_separator;
            }
            ++col;
        } while (skAggBagFieldIterNext(&field) == SK_ITERATOR_OK);
    }

    if (app_flags.no_final_delimiter) {
        delim[0] = '\0';
    }
    fprintf(fh, "%s\n", delim);
}


static void
printAggBag(
    const sk_aggbag_t  *ab,
    FILE               *fh)
{
    sk_aggbag_iter_t iter = SK_AGGBAG_ITER_INITIALIZER;
    sk_aggbag_iter_t *it = &iter;
    char buf[1024];
    uint64_t number;
    skipaddr_t ip;
    char delim[] = {'\0', '\0'};
    unsigned int col;

    skAggBagIteratorBind(it, ab);

    while (skAggBagIteratorNext(it) == SK_ITERATOR_OK) {
        col = 0;
        do {
            switch (skAggBagFieldIterGetType(&it->key_field_iter)) {
              case SKAGGBAG_FIELD_SIPv6:
              case SKAGGBAG_FIELD_SIPv4:
              case SKAGGBAG_FIELD_DIPv6:
              case SKAGGBAG_FIELD_DIPv4:
              case SKAGGBAG_FIELD_NHIPv6:
              case SKAGGBAG_FIELD_NHIPv4:
              case SKAGGBAG_FIELD_ANY_IPv6:
              case SKAGGBAG_FIELD_ANY_IPv4:
                skAggBagAggregateGetIPAddress(
                    &it->key, &it->key_field_iter, &ip);
                fprintf(fh, "%s%*s", delim, width[col],
                        skipaddrString(buf, &ip, ip_format));
                break;
              case SKAGGBAG_FIELD_SPORT:
              case SKAGGBAG_FIELD_DPORT:
              case SKAGGBAG_FIELD_PROTO:
              case SKAGGBAG_FIELD_PACKETS:
              case SKAGGBAG_FIELD_BYTES:
              case SKAGGBAG_FIELD_ELAPSED:
              case SKAGGBAG_FIELD_INPUT:
              case SKAGGBAG_FIELD_OUTPUT:
              case SKAGGBAG_FIELD_APPLICATION:
              case SKAGGBAG_FIELD_ICMP_TYPE:
              case SKAGGBAG_FIELD_ICMP_CODE:
              case SKAGGBAG_FIELD_ANY_PORT:
              case SKAGGBAG_FIELD_ANY_SNMP:
              case SKAGGBAG_FIELD_CUSTOM_KEY:
              case SKAGGBAG_FIELD_CUSTOM_COUNTER:
                skAggBagAggregateGetUnsigned(
                    &it->key, &it->key_field_iter, &number);
                fprintf(fh, "%s%*" PRIu64, delim, width[col], number);
                break;
              case SKAGGBAG_FIELD_STARTTIME:
              case SKAGGBAG_FIELD_ENDTIME:
              case SKAGGBAG_FIELD_ANY_TIME:
                skAggBagAggregateGetUnsigned(
                    &it->key, &it->key_field_iter, &number);
                fprintf(fh, "%s%*s", delim, width[col],
                        sktimestamp_r(buf, sktimeCreate(number, 0),
                                      timestamp_format));
                break;
              case SKAGGBAG_FIELD_FLAGS:
              case SKAGGBAG_FIELD_INIT_FLAGS:
              case SKAGGBAG_FIELD_REST_FLAGS:
                skAggBagAggregateGetUnsigned(
                    &it->key, &it->key_field_iter, &number);
                fprintf(fh, "%s%*s", delim, width[col],
                        skTCPFlagsString(number, buf, 0));
                break;
              case SKAGGBAG_FIELD_TCP_STATE:
                skAggBagAggregateGetUnsigned(
                    &it->key, &it->key_field_iter, &number);
                fprintf(fh, "%s%*s", delim, width[col],
                        skTCPStateString(number, buf, 0));
                break;
              case SKAGGBAG_FIELD_SID:
                skAggBagAggregateGetUnsigned(
                    &it->key, &it->key_field_iter, &number);
                sksiteSensorGetName(buf, sizeof(buf), number);
                fprintf(fh, "%s%*s", delim, width[col], buf);
                break;
              case SKAGGBAG_FIELD_FTYPE_CLASS:
                skAggBagAggregateGetUnsigned(
                    &it->key, &it->key_field_iter, &number);
                sksiteClassGetName(buf, sizeof(buf), number);
                fprintf(fh, "%s%*s", delim, width[col], buf);
                break;
              case SKAGGBAG_FIELD_FTYPE_TYPE:
                skAggBagAggregateGetUnsigned(
                    &it->key, &it->key_field_iter, &number);
                sksiteFlowtypeGetType(buf, sizeof(buf), number);
                fprintf(fh, "%s%*s", delim, width[col], buf);
                break;
              case SKAGGBAG_FIELD_SIP_COUNTRY:
              case SKAGGBAG_FIELD_DIP_COUNTRY:
              case SKAGGBAG_FIELD_ANY_COUNTRY:
                skAggBagAggregateGetUnsigned(
                    &it->key, &it->key_field_iter, &number);
                skCountryCodeToName(number, buf, sizeof(buf));
                fprintf(fh, "%s%*s", delim, width[col], buf);
                break;
              default:
                break;
            }
            if (0 == col) {
                delim[0] = column_separator;
            }
            ++col;
        } while (skAggBagFieldIterNext(&it->key_field_iter) == SK_ITERATOR_OK);

        do {
            skAggBagAggregateGetUnsigned(
                &it->counter, &it->counter_field_iter, &number);
            fprintf(fh, "%s%*" PRIu64, delim, width[col], number);
            ++col;
        } while (skAggBagFieldIterNext(&it->counter_field_iter)
                 == SK_ITERATOR_OK);

        if (app_flags.no_final_delimiter) {
            fprintf(fh, "\n");
        } else {
            fprintf(fh, "%c\n", column_separator);
        }
        delim[0] = '\0';
    }

    skAggBagIteratorFree(it);
}


int main(int argc, char **argv)
{
    char *filename;
    FILE *fh;
    sk_aggbag_t *ab;
    ssize_t rv;
    int err;

    /* Global setup */
    appSetup(argc, argv);

    fh = NULL;
    while ((rv = skOptionsCtxNextArgument(optctx, &filename)) == 0) {
        err = skAggBagLoad(&ab, filename);
        if (err != SKAGGBAG_OK) {
            skAppPrintErr("Error reading aggbag from input stream '%s': %s",
                          filename, skAggBagStrerror(err));
            exit(EXIT_FAILURE);
        }

        if (NULL == fh) {
            fh = getOutputHandle();
        }
        determineWidths(ab);
        printTitles(ab, fh);
        printAggBag(ab, fh);

        skAggBagDestroy(&ab);
    }

    /* Done, do cleanup */
    appTeardown();

    return 0;
}


/*
** Local Variables:
** mode:c
** indent-tabs-mode:nil
** c-basic-offset:4
** End:
*/
