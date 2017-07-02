/*
** Copyright (C) 2001-2017 by Carnegie Mellon University.
**
** @OPENSOURCE_LICENSE_START@
** See license information in ../../LICENSE.txt
** @OPENSOURCE_LICENSE_END@
*/

/*
**  rwcat.c
**
**  12/13/2002
**
**  Suresh L. Konda
**
**  Stream out a bunch of input files--given on the command
**  line--and/or stdin to stdout or given file Path
**
*/

#include <silk/silk.h>

RCSIDENT("$SiLK: rwcat.c d1637517606d 2017-06-23 16:51:31Z mthomas $");

#include <silk/rwrec.h>
#include <silk/skflowiter.h>
#include <silk/sksite.h>
#include <silk/skstream.h>
#include <silk/utils.h>


/* TYPEDEFS AND DEFINES */

/* file handle for --help output */
#define USAGE_FH stdout

/* file handle to use for --print-filenames output */
#define FILENAMES_FH stderr

/* LOCAL VARIABLES */

/* handles input streams */
static sk_options_ctx_t *optctx = NULL;
static sk_flow_iter_t *flowiter = NULL;

/* output stream */
static skstream_t *out_stream = NULL;

/* number of records in output stream */
static uint64_t out_count = 0;

/* where --print-filenames data is being written */
static FILE *print_filenames = NULL;

/* Lua state */
static lua_State *L;

/* sidecar holding all defined sidecar elements */
static sk_sidecar_t *sidecar = NULL;

/* the compression method to use when writing the file.
 * skCompMethodOptionsRegister() will set this to the default or
 * to the value the user specifies. */
static sk_compmethod_t comp_method;

/* byte order of the files we generate; default is to write files in
 * the native byte order */
static silk_endian_t byte_order = SILK_ENDIAN_ANY;

/* how to handle IPv6 flows. the --ipv4-output switch will set this to
 * SK_IPV6POLICY_ASV4 */
static sk_ipv6policy_t ipv6_policy = SK_IPV6POLICY_MIX;

/* Do not copy sidecar data from source records. Default is FALSE
 * (meaning sidecar data is copied); set to TRUE via --no-sidecar */
static int no_sidecar = 0;


/* OPTIONS SETUP */

typedef enum {
    OPT_OUTPUT_PATH, OPT_BYTE_ORDER, OPT_IPV4_OUTPUT, OPT_NO_SIDECAR
} appOptionsEnum;

static struct option appOptions[] = {
    {"output-path",     REQUIRED_ARG, 0, OPT_OUTPUT_PATH},
    {"byte-order",      REQUIRED_ARG, 0, OPT_BYTE_ORDER},
    {"ipv4-output",     NO_ARG,       0, OPT_IPV4_OUTPUT},
    {"no-sidecar",      NO_ARG,       0, OPT_NO_SIDECAR},
    {0,0,0,0}           /* sentinel entry */
};

static const char *appHelp[] = {
    "Write the output to this stream or file. Def. stdout",
    ("Write the output in this byte order. Def. 'native'.\n"
     "\tChoices: 'native', 'little', 'big'"),
    ("Force the output to contain only IPv4 addresses. Def. no"),
    ("Remove sidecar fields from the input. Def. no"),
    (char *)NULL
};



/* LOCAL FUNCTION PROTOTYPES */

static int  appOptionsHandler(clientData cData, int opt_index, char *opt_arg);
static int  byteOrderParse(const char *endian_string);
static void close_callback(sk_flow_iter_t *fiter, skstream_t *s, void *data);


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
    ("[SWITCHES] [FILES] \n"                                            \
     "\tReads SiLK Flow records from the FILES named on the comamnd\n"  \
     "\tline, or from the standard input when no FILES are provided,\n" \
     "\tand writes the SiLK records to the specified output file or\n"  \
     "\tto the standard output if it is not connected to a terminal.\n")

    FILE *fh = USAGE_FH;

    skAppStandardUsage(fh, USAGE_MSG, appOptions, appHelp);
    skOptionsCtxOptionsUsage(optctx, fh);
    skCompMethodOptionsUsage(fh);
    skOptionsNotesUsage(fh);
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
    static uint8_t teardownFlag = 0;
    int rv;

    if (teardownFlag) {
        return;
    }
    teardownFlag = 1;

    /* close output file */
    if (out_stream) {
        rv = skStreamClose(out_stream);
        if (rv && rv != SKSTREAM_ERR_NOT_OPEN) {
            skStreamPrintLastErr(out_stream, rv, &skAppPrintErr);
        }
        skStreamDestroy(&out_stream);
    }

    sk_sidecar_destroy(&sidecar);
    sk_lua_closestate(L);

    skOptionsNotesTeardown();
    sk_flow_iter_destroy(&flowiter);
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
    sk_file_header_t *out_hdr = NULL;
    int rv;

    /* verify same number of options and help strings */
    assert((sizeof(appHelp)/sizeof(char *)) ==
           (sizeof(appOptions)/sizeof(struct option)));

    /* register the application */
    skAppRegister(argv[0]);
    skAppVerifyFeatures(&features, NULL);
    skOptionsSetUsageCallback(&appUsageLong);

    optctx_flags = (SK_OPTIONS_CTX_INPUT_SILK_FLOW | SK_OPTIONS_CTX_ALLOW_STDIN
                    | SK_OPTIONS_CTX_XARGS | SK_OPTIONS_CTX_PRINT_FILENAMES);

    /* register the options */
    if (skOptionsCtxCreate(&optctx, optctx_flags)
        || skOptionsCtxOptionsRegister(optctx)
        || skOptionsRegister(appOptions, &appOptionsHandler, NULL)
        || skOptionsNotesRegister(NULL)
        || skCompMethodOptionsRegister(&comp_method)
        || sksiteOptionsRegister(SK_SITE_FLAG_CONFIG_FILE))
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

    /* create an empty sidecar description and a Lua state */
    sk_sidecar_create(&sidecar);
    L = sk_lua_newstate();

    /* parse options */
    rv = skOptionsCtxOptionsParse(optctx, argc, argv);
    if (rv < 0) {
        skAppUsage();           /* never returns */
    }

    /* try to load site config file; if it fails, we will not be able
     * to resolve flowtype and sensor from input file names */
    sksiteConfigure(0);

    /* create flow iterator to read the records from the stream */
    flowiter = skOptionsCtxCreateFlowIterator(optctx);
    sk_flow_iter_set_ipv6_policy(flowiter, ipv6_policy);

    /* get the file handle used for printing filenames so that the
     * number of records read from each file can be written there */
    print_filenames = skOptionsCtxGetPrintFilenames(optctx);
    if (print_filenames) {
        sk_flow_iter_set_max_readers(flowiter, 1);
        sk_flow_iter_set_stream_event_cb(
            flowiter, SK_FLOW_ITER_CB_EVENT_POST_CLOSE, &close_callback, NULL);
    }

    /* create output stream to stdout if no --output-path was given */
    if (out_stream == NULL) {
        if ((rv = skStreamCreate(&out_stream,SK_IO_WRITE,SK_CONTENT_SILK_FLOW))
            || (rv = skStreamBind(out_stream, "-")))
        {
            skStreamPrintLastErr(out_stream, rv, &skAppPrintErr);
            skStreamDestroy(&out_stream);
            exit(EXIT_FAILURE);
        }
    }

    /* get the output file's header */
    out_hdr = skStreamGetSilkHeader(out_stream);

    /* read the headers from all input streams and look for sidecar
     * data */
    if (!no_sidecar) {
        if (sk_flow_iter_fill_sidecar(flowiter, sidecar)) {
            skAppPrintErr("Error reading file header");
            exit(EXIT_FAILURE);
        }
    }
    if (0 == sk_sidecar_count_elements(sidecar)) {
        sk_sidecar_destroy(&sidecar);

        /* set the file version based on whether sidecar data is
         * present.  Using version 16 by default allows the "make
         * check" tests to pass. */
        rv = skHeaderSetFileVersion(out_hdr, 16);
        if (rv) {
            skStreamPrintLastErr(out_stream, rv, &skAppPrintErr);
            skStreamDestroy(&out_stream);
            exit(EXIT_FAILURE);
        }
    } else {
        /* add sidecar description to stream */
        rv = skStreamSetSidecar(out_stream, sidecar);
        if (rv) {
            skStreamPrintLastErr(out_stream, rv, &skAppPrintErr);
            skStreamDestroy(&out_stream);
            exit(EXIT_FAILURE);
        }
    }

    /* write an RWGENERIC file if we know there will be no IPv6 flows */
    if (ipv6_policy < SK_IPV6POLICY_MIX) {
        rv = skHeaderSetFileFormat(out_hdr, FT_RWGENERIC);
        if (rv) {
            skStreamPrintLastErr(out_stream, rv, &skAppPrintErr);
            skStreamDestroy(&out_stream);
            exit(EXIT_FAILURE);
        }
    }

    /* set the header, add the notes (if given), open output, and
     * write header */
    if ((rv = skHeaderSetCompressionMethod(out_hdr, comp_method))
        || (rv = skHeaderSetByteOrder(out_hdr, byte_order))
        || (rv = skOptionsNotesAddToStream(out_stream))
        || (rv = skStreamOpen(out_stream))
        || (rv = skStreamWriteSilkHeader(out_stream)))
    {
        skStreamPrintLastErr(out_stream, rv, &skAppPrintErr);
        skStreamDestroy(&out_stream);
        exit(EXIT_FAILURE);
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
    char               *opt_arg)
{
    int rv;

    switch ((appOptionsEnum)opt_index) {
      case OPT_OUTPUT_PATH:
        if (out_stream != NULL) {
            skAppPrintErr("Invalid %s: Switch used multiple times",
                          appOptions[opt_index].name);
            return 1;
        }
        if ((rv = skStreamCreate(&out_stream,SK_IO_WRITE,SK_CONTENT_SILK_FLOW))
            || (rv = skStreamBind(out_stream, opt_arg)))
        {
            skStreamPrintLastErr(out_stream, rv, &skAppPrintErr);
            skStreamDestroy(&out_stream);
            return 1;
        }
        break;

      case OPT_BYTE_ORDER:
        if (byteOrderParse(opt_arg)) {
            return 1;
        }
        break;

      case OPT_IPV4_OUTPUT:
        ipv6_policy = SK_IPV6POLICY_ASV4;
        break;

      case OPT_NO_SIDECAR:
        no_sidecar = 1;
        break;
    }

    return 0;                     /* OK */
}


/*
 *  ok = byteOrderParse(argument)
 *
 *    Parse the argument to the --byte-order switch and set the global
 *    variable 'byte_order'.
 */
static int
byteOrderParse(
    const char         *endian_string)
{
    static int option_seen = 0;
    int i;
    size_t len;
    /* Options for byte-order switch */
    struct {
        const char     *name;
        silk_endian_t   value;
    } byte_order_opts[] = {
        {"native", SILK_ENDIAN_NATIVE},
        {"little", SILK_ENDIAN_LITTLE},
        {"big",    SILK_ENDIAN_BIG},
        {NULL,     SILK_ENDIAN_ANY} /* sentinel */
    };

    /* only process option one time */
    if (option_seen != 0) {
        skAppPrintErr("Invalid %s: Switch used multiple times",
                      appOptions[OPT_BYTE_ORDER].name);
        return 1;
    }
    option_seen = 1;

    len = strlen(endian_string);
    if (len == 0) {
        skAppPrintErr("Invalid %s: Empty string",
                      appOptions[OPT_BYTE_ORDER].name);
        return 1;
    }

    /* parse user's input */
    for (i = 0; byte_order_opts[i].name; ++i) {
        if ((len <= strlen(byte_order_opts[i].name))
            && (0 == strncmp(byte_order_opts[i].name, endian_string, len)))
        {
            byte_order = byte_order_opts[i].value;
            option_seen = 2;
            break;
        }
    }

    if (option_seen != 2) {
        skAppPrintErr("Invalid %s '%s': Unrecognized value",
                      appOptions[OPT_BYTE_ORDER].name, endian_string);
        return 1;
    }

    if (byte_order == SILK_ENDIAN_NATIVE) {
#if SK_LITTLE_ENDIAN
        byte_order = SILK_ENDIAN_LITTLE;
#else
        byte_order = SILK_ENDIAN_BIG;
#endif
    }

    return 0;
}


/*
 *    Report the number of records that were read from the input
 *    stream 'stream' and that have been added to the global output
 *    stream 'out_stream' since the previous input stream was closed.
 *
 *    This is a callback function registered with the global
 *    sk_flow_iter_t 'flowiter' and it may be invoked by
 *    sk_flow_iter_get_next_rec().
 */
static void
close_callback(
    sk_flow_iter_t  UNUSED(*f_iter),
    skstream_t             *stream,
    void            UNUSED(*data))
{
    uint64_t new_count;

    new_count = skStreamGetRecordCount(out_stream);

    fprintf(print_filenames, ("Read %" PRIu64 " Wrote %" PRIu64 "\n"),
            skStreamGetRecordCount(stream), new_count - out_count);
    out_count = new_count;
}



int main(int argc, char **argv)
{
    rwRec rwrec;
    ssize_t rv_in;
    ssize_t rv = SKSTREAM_OK;

    appSetup(argc, argv);

    rwRecInitialize(&rwrec, L);

    /* process input */
    while ((rv_in = sk_flow_iter_get_next_rec(flowiter, &rwrec)) == 0) {
        rv = skStreamWriteRecord(out_stream, &rwrec);
        if (SKSTREAM_OK != rv) {
            skStreamPrintLastErr(out_stream, rv, &skAppPrintErr);
            if (SKSTREAM_ERROR_IS_FATAL(rv)) {
                exit(EXIT_FAILURE);
            }
        }
    }
    if (rv_in != SKSTREAM_ERR_EOF) {
        exit(EXIT_FAILURE);
    }

    return 0;
}


/*
** Local Variables:
** mode:c
** indent-tabs-mode:nil
** c-basic-offset:4
** End:
*/
