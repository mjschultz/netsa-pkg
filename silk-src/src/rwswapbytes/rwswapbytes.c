/*
** Copyright (C) 2003-2016 by Carnegie Mellon University.
**
** @OPENSOURCE_LICENSE_START@
** See license information in ../../LICENSE.txt
** @OPENSOURCE_LICENSE_END@
*/

/*
 * rwswapbytes
 *
 * Read any rw file (rwpacked file, rwfilter output, etc) and output a
 * file in the specified byte order.
 *
 */

#include <silk/silk.h>

RCSIDENT("$SiLK: rwswapbytes.c 314c5852c1b4 2016-06-03 21:41:11Z mthomas $");

#include <silk/rwrec.h>
#include <silk/sksite.h>
#include <silk/skstream.h>
#include <silk/utils.h>


/* MACROS AND TYPEDEFS */

/* File handle for --help output */
#define USAGE_FH stdout

typedef enum rwswapOptions {
    RWSW_BIG = 128, RWSW_LITTLE, RWSW_NATIVE, RWSW_SWAP
} rwswapOptions_t;

#if SK_LITTLE_ENDIAN
#  define RWSW_NATIVE_FORMAT "little"
#else
#  define RWSW_NATIVE_FORMAT "big"
#endif


/* LOCAL VARIABLES */

static rwswapOptions_t out_endian;
static const char *in_path;
static const char *out_path;


/* OPTIONS */

static struct option appOptions[] = {
    {"big-endian",       NO_ARG,       0, RWSW_BIG},
    {"little-endian",    NO_ARG,       0, RWSW_LITTLE},
    {"native-endian",    NO_ARG,       0, RWSW_NATIVE},
    {"swap-endian",      NO_ARG,       0, RWSW_SWAP},
    {0,0,0,0}            /* sentinel entry */
};

static const char *appHelp[] = {
    /* add help strings here for the applications options */
    "Write output in big-endian format (network byte-order)",
    "Write output in little-endian format",
    ("Write output in this machine's native format [" RWSW_NATIVE_FORMAT "]"),
    "Unconditionally swap the byte-order of the input",
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
#define USAGE_MSG                                                       \
    ("[SWITCHES] <ENDIAN_SWITCH> <INPUT_FILE> <OUTPUT_FILE>\n"          \
     "\tChange the byte-order of <INPUT_FILE> as specified by "         \
     "<ENDIAN_SWITCH>\n"                                                \
     "\tand write result to <OUTPUT_FILE>.  You may use \"stdin\" for\n" \
     "\t<INPUT_FILE> and \"stdout\" for <OUTPUT_FILE>.\n")

    FILE *fh = USAGE_FH;
    unsigned int i;

    fprintf(fh, "%s %s", skAppName(), USAGE_MSG);
    fprintf(fh, "\nSWITCHES:\n");
    skOptionsDefaultUsage(fh);
    skOptionsNotesUsage(fh);
    fprintf(fh, "\nENDIAN_SWITCH:\n");
    for (i = 0; appOptions[i].name; ++i) {
        fprintf(fh, "--%s: %s\n", appOptions[i].name, appHelp[i]);
    }
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
    static int teardownFlag = 0;

    if (teardownFlag) {
        return;
    }
    teardownFlag = 1;

    skOptionsNotesTeardown();
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
    int arg_index;

    /* verify same number of options and help strings */
    assert((sizeof(appHelp)/sizeof(char *)) ==
           (sizeof(appOptions)/sizeof(struct option)));

    /* register the application */
    skAppRegister(argv[0]);
    skAppVerifyFeatures(&features, NULL);
    skOptionsSetUsageCallback(&appUsageLong);

    /* register the options */
    if (skOptionsRegister(appOptions, &appOptionsHandler, NULL)
        || skOptionsNotesRegister(NULL))
    {
        skAppPrintErr("Unable to register options");
        exit(EXIT_FAILURE);
    }

    /* register the teardown handler */
    if (atexit(appTeardown) < 0) {
        skAppPrintErr("Unable to register appTeardown() with atexit()");
        appTeardown();
        exit(EXIT_FAILURE);
    }

    /* parse options */
    arg_index = skOptionsParse(argc, argv);
    if (arg_index < 0) {
        skAppUsage();           /* never returns */
    }

    /* try to load site config file; if it fails, we will not be able
     * to resolve flowtype and sensor from input file names */
    sksiteConfigure(0);

    /* Check that a swapping option was given */
    if (0 == out_endian) {
        skAppPrintErr("You must specify the output byte order.");
        skAppUsage();
    }

    /* Check that we have input */
    if (arg_index >= argc) {
        skAppPrintErr("Expecting input file name");
        skAppUsage();           /* never returns */
    }
    in_path = argv[arg_index];
    ++arg_index;

    /* check that we have an output location */
    if (arg_index >= argc) {
        skAppPrintErr("Expecting output file name");
        skAppUsage();           /* never returns */
    }
    out_path = argv[arg_index];
    ++arg_index;

    /* check for extra options */
    if (argc != arg_index) {
        skAppPrintErr("Got extra options");
        skAppUsage();           /* never returns */
    }

    return;                     /* OK */
}


/*
 *  status = appOptionsHandler(cData, opt_index, opt_arg);
 *
 *    This function is passed to skOptionsRegister(); it will be called
 *    by skOptionsParse() for each user-specified switch that the
 *    application has registered; it should handle the switch as
 *    required---typically by setting global variables---and return 1
 *    if the switch processing failed or 0 if it succeeded.  Returning
 *    a non-zero from from the handler causes skOptionsParse() to return
 *    a negative value.
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
    char        UNUSED(*opt_arg))
{
    switch (opt_index) {
#if !SK_LITTLE_ENDIAN
      case RWSW_NATIVE:
#endif
      case RWSW_BIG:
        if ((0 != out_endian) && (RWSW_BIG != out_endian)) {
            skAppPrintErr("Conflicting endian options given");
            return 1;
        }
        out_endian = RWSW_BIG;
        break;

#if SK_LITTLE_ENDIAN
      case RWSW_NATIVE:
#endif
      case RWSW_LITTLE:
        if ((0 != out_endian) && (RWSW_LITTLE != out_endian)) {
            skAppPrintErr("Conflicting endian options given");
            return 1;
        }
        out_endian = RWSW_LITTLE;
        break;

      case RWSW_SWAP:
        if ((0 != out_endian) && (RWSW_SWAP != out_endian)) {
            skAppPrintErr("Conflicting endian options given");
            return 1;
        }
        out_endian = RWSW_SWAP;
        break;

      default:
        skAbortBadCase(opt_index);
    }

    return 0;                   /* OK */
}


/*
 *  is_ok = rwswap_file(input_file, output_file, endian);
 *
 *    Byte swap the file named 'input_file' and write it to
 *    'output_file' using the 'endian' to determine the byte-order of
 *    'output_file'.
 */
static int
rwswap_file(
    const char         *in_file,
    const char         *out_file,
    rwswapOptions_t     endian)
{
    silk_endian_t byte_order;
    skstream_t *in_stream;
    skstream_t *out_stream;
    sk_file_header_t *in_hdr;
    sk_file_header_t *out_hdr;
    rwRec rwrec;
    int in_rv = SKSTREAM_OK;
    int rv = 0;

    /* Open input file */
    in_rv = skStreamOpenSilkFlow(&in_stream, in_file, SK_IO_READ);
    if (in_rv) {
        skStreamPrintLastErr(in_stream, in_rv, &skAppPrintErr);
        skStreamDestroy(&in_stream);
        return 1;
    }

    in_hdr = skStreamGetSilkHeader(in_stream);

    switch (endian) {
      case RWSW_BIG:
        byte_order = SILK_ENDIAN_BIG;
        break;
      case RWSW_LITTLE:
        byte_order = SILK_ENDIAN_LITTLE;
        break;
      case RWSW_NATIVE:
        byte_order = SILK_ENDIAN_NATIVE;
        break;
      case RWSW_SWAP:
        switch (skHeaderGetByteOrder(in_hdr)) {
          case SILK_ENDIAN_BIG:
            byte_order = SILK_ENDIAN_LITTLE;
            break;
          case SILK_ENDIAN_LITTLE:
            byte_order = SILK_ENDIAN_BIG;
            break;
          default:
            skAbortBadCase(skHeaderGetByteOrder(in_hdr));
        }
        break;
      default:
        skAbortBadCase(endian);
    }

    /* Open the output file and copy the headers from the source file,
     * but modify the byte order */
    rv = skStreamCreate(&out_stream, SK_IO_WRITE, SK_CONTENT_SILK_FLOW);
    if (rv == SKSTREAM_OK) {
        rv = skStreamBind(out_stream, out_file);
        out_hdr = skStreamGetSilkHeader(out_stream);
    }
    if (rv == SKSTREAM_OK) {
        rv = skHeaderCopy(out_hdr, in_hdr,
                          (SKHDR_CP_ALL & ~SKHDR_CP_ENDIAN));
    }
    if (rv == SKSTREAM_OK) {
        rv = skHeaderSetByteOrder(out_hdr, byte_order);
    }
    if (rv == SKSTREAM_OK) {
        rv = skOptionsNotesAddToStream(out_stream);
    }
    if (rv == SKSTREAM_OK) {
        rv = skStreamOpen(out_stream);
    }
    if (rv == SKSTREAM_OK) {
        rv = skStreamWriteSilkHeader(out_stream);
    }
    if (rv != SKSTREAM_OK) {
        goto END;
    }

    /* read and write the data until we encounter an error */
    while ((in_rv = skStreamReadRecord(in_stream, &rwrec)) == SKSTREAM_OK) {
        rv = skStreamWriteRecord(out_stream, &rwrec);
        if (SKSTREAM_ERROR_IS_FATAL(rv)) {
            goto END;
        }
    }

    /* input error */
    if (in_rv != SKSTREAM_ERR_EOF) {
        skStreamPrintLastErr(in_stream, in_rv, &skAppPrintErr);
    }

    if (rv == SKSTREAM_OK) {
        rv = skStreamClose(out_stream);
    }

  END:
    if (rv) {
        skStreamPrintLastErr(out_stream, rv, &skAppPrintErr);
    }
    if (out_stream) {
        skStreamDestroy(&out_stream);
    }
    if (in_stream) {
        skStreamDestroy(&in_stream);
    }

    return rv;
}


int main(int argc, char **argv)
{
    int rv;

    appSetup(argc, argv);

    rv = rwswap_file(in_path, out_path, out_endian);

    /* done */
    return rv;
}


/*
** Local Variables:
** mode:c
** indent-tabs-mode:nil
** c-basic-offset:4
** End:
*/
