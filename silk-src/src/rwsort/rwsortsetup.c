/*
** Copyright (C) 2001-2017 by Carnegie Mellon University.
**
** @OPENSOURCE_LICENSE_START@
** See license information in ../../LICENSE.txt
** @OPENSOURCE_LICENSE_END@
*/

/*
**  rwsort reads SiLK Flow Records from the standard input or from
**  named files and sorts them on one or more user-specified fields.
**
**  See rwsort.c for implementation details.
**
*/

#include <silk/silk.h>

RCSIDENT("$SiLK: rwsortsetup.c d1637517606d 2017-06-23 16:51:31Z mthomas $");

#include <silk/silkpython.h>
#include <silk/skcountry.h>
#include <silk/skprefixmap.h>
#include <silk/sksidecar.h>
#include <silk/sksite.h>
#include <silk/skstringmap.h>
#include "rwsort.h"


/* LOCAL DEFINES AND TYPEDEFS */

/* where to send --help output */
#define USAGE_FH stdout

/* Where to write filenames if --print-file specified */
#define PRINT_FILENAMES_FH  stderr


/* LOCAL VARIABLES */

/* whether to print the fields' help */
static int help_fields = 0;

/* the text the user entered for the --fields switch */
static char *fields_arg = NULL;

/* available key fields; skRwrecAppendFieldsToStringMap() fills this */
static sk_stringmap_t *key_field_map = NULL;

/* fields used as part of the key that come from plug-ins; this is an
 * array of pointers into the key_fields[] array */
static key_field_t **active_plugins = NULL;

/* the number of active plug-in fields */
static uint32_t num_plugins = 0;

/* handle input streams */
static sk_options_ctx_t *optctx = NULL;

/* whether to print names of files as they are opened; 0 == no */
static int print_filenames = 0;

/* sidecar holding all defined sidecar elements */
static sk_sidecar_t *sidecar = NULL;

/* non-zero if we are shutting down due to a signal; controls whether
 * errors are printed in appTeardown(). */
static int caught_signal = 0;

/* the compression method to use when writing the file.
 * skCompMethodOptionsRegister() will set this to the default or
 * to the value the user specifies. */
static sk_compmethod_t comp_method;

/* fields that get defined just like plugins */
static const struct app_static_plugins_st {
    const char         *name;
    skplugin_setup_fn_t setup_fn;
} app_static_plugins[] = {
    {"addrtype",        skAddressTypesAddFields},
    {"ccfilter",        skCountryAddFields},
    {"pmapfilter",      skPrefixMapAddFields},
#if SK_ENABLE_PYTHON
    {"silkpython",      skSilkPythonAddFields},
#endif
    {NULL, NULL}        /* sentinel */
};

/* names of plug-ins to attempt to load at startup */
static const char *app_plugin_names[] = {
    NULL /* sentinel */
};

/* temporary directory */
static const char *temp_directory = NULL;


/* OPTIONS */

typedef enum {
    OPT_HELP_FIELDS,
    OPT_FIELDS,
    OPT_REVERSE,
    OPT_PRINT_FILENAMES,
    OPT_OUTPUT_PATH,
    OPT_PLUGIN,
    OPT_PRESORTED_INPUT,
    OPT_SORT_BUFFER_SIZE
} appOptionsEnum;

static struct option appOptions[] = {
    {"help-fields",         NO_ARG,       0, OPT_HELP_FIELDS},
    {"fields",              REQUIRED_ARG, 0, OPT_FIELDS},
    {"reverse",             NO_ARG,       0, OPT_REVERSE},
    {"print-filenames",     NO_ARG,       0, OPT_PRINT_FILENAMES},
    {"output-path",         REQUIRED_ARG, 0, OPT_OUTPUT_PATH},
    {"plugin",              REQUIRED_ARG, 0, OPT_PLUGIN},
    {"presorted-input",     NO_ARG,       0, OPT_PRESORTED_INPUT},
    {"sort-buffer-size",    REQUIRED_ARG, 0, OPT_SORT_BUFFER_SIZE},
    {0,0,0,0}               /* sentinel entry */
};

static const char *appHelp[] = {
    "Describe each possible field and exit. Def. no",
    ("Use these fields as the sorting key. Specify fields as a\n"
     "\tcomma-separated list of names, IDs, and/or ID-ranges"),
    "Reverse the sort order. Def. No",
    "Print names of input files as they are opened. Def. No",
    ("Write sorted output to this stream or file. Def. stdout"),
    ("Load given plug-in to add fields. Switch may be repeated to\n"
     "\tload multiple plug-ins. Def. None"),
    ("Assume input has been presorted using\n"
     "\trwsort invoked with the exact same --fields value. Def. No"),
    NULL, /* generated dynamically */
    (char *)NULL
};



/* LOCAL FUNCTION PROTOTYPES */

static int  appOptionsHandler(clientData cData, int opt_index, char *opt_arg);
static void appHandleSignal(int sig);
static int  parseFields(const char *fields_string);
static void helpFields(FILE *fh);
static int  createStringmaps(void);
static void
copy_file_header_callback(
    sk_flow_iter_t     *f_iter,
    skstream_t         *stream,
    void               *data);



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
#define USAGE_MSG                                                             \
    ("--fields=<FIELDS> [SWITCHES] [FILES]\n"                                 \
     "\tRead SiLK Flow records, sort them by the specified FIELD(S), and\n"   \
     "\twrite the records to the named output path or to the standard\n"      \
     "\toutput.  When no FILES are given on command line, flows are read\n"   \
     "\tfrom the standard input.\n")

    FILE *fh = USAGE_FH;
    int i;

    /* Create the string map for --fields */
    createStringmaps();

    fprintf(fh, "%s %s", skAppName(), USAGE_MSG);
    fprintf(fh, "\nSWITCHES:\n");
    skOptionsDefaultUsage(fh);

    for (i = 0; appOptions[i].name; i++ ) {
        fprintf(fh, "--%s %s. ", appOptions[i].name,
                SK_OPTION_HAS_ARG(appOptions[i]));
        switch (i) {
          case OPT_FIELDS:
            /* Dynamically build the help */
            fprintf(fh, "%s\n", appHelp[i]);
            skStringMapPrintUsage(key_field_map, fh, 4);
            break;
          case OPT_SORT_BUFFER_SIZE:
            fprintf(fh,
                    ("Attempt to allocate this much memory for the sort\n"
                     "\tbuffer, in bytes."
                     "  Append k, m, g, for kilo-, mega-, giga-bytes,\n"
                     "\trespectively. Range: %" SK_PRIuZ "-%" SK_PRIuZ
                     ". Def. " DEFAULT_SORT_BUFFER_SIZE "\n"),
                    MINIMUM_SORT_BUFFER_SIZE, MAXIMUM_SORT_BUFFER_SIZE);
            break;
          default:
            /* Simple help text from the appHelp array */
            assert(appHelp[i]);
            fprintf(fh, "%s\n", appHelp[i]);
            break;
        }
    }

    skOptionsCtxOptionsUsage(optctx, fh);
    skOptionsTempDirUsage(fh);
    skOptionsNotesUsage(fh);
    skCompMethodOptionsUsage(fh);
    sksiteOptionsUsage(fh);
    skPluginOptionsUsage(fh);
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
    key_field_t *key;
    uint32_t i;
    int rv;

    if (teardownFlag) {
        return;
    }
    teardownFlag = 1;

    /* close and destroy output */
    if (out_stream) {
        rv = skStreamDestroy(&out_stream);
        if (rv && !caught_signal) {
            /* only print error when not in signal handler */
            skStreamPrintLastErr(out_stream, rv, &skAppPrintErr);
        }
        out_stream = NULL;
    }

    /* remove any temporary files */
    skTempFileTeardown(&tmpctx);

    /* plug-in teardown */
    skPluginRunCleanup(SKPLUGIN_APP_SORT);
    skPluginTeardown();

    /* free variables */
    if (key_fields != NULL) {
        for (i = 0, key = key_fields; i < num_fields; ++i, ++key) {
            free(key->kf_name);
        }
        free(key_fields);
    }
    skStringMapDestroy(key_field_map);
    key_field_map = NULL;

    sk_sidecar_destroy(&sidecar);
    sk_sidecar_destroy(&out_sidecar);

    sk_lua_closestate(L);
    L = NULL;

    free(active_plugins);

    skOptionsNotesTeardown();
    sk_flow_iter_destroy(&flowiter);
    skOptionsCtxDestroy(&optctx);
    skAppUnregister();
}


/*
 *  appExit(status)
 *
 *  Exit the application with the given status.
 */
void
appExit(
    int                 status)
{
    appTeardown();
    exit(status);
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
void
appSetup(
    int                 argc,
    char              **argv)
{
    SILK_FEATURES_DEFINE_STRUCT(features);
    uint64_t tmp64;
    unsigned int optctx_flags;
    int rv;
    int j;

    /* verify same number of options and help strings */
    assert((sizeof(appHelp)/sizeof(char *)) ==
           (sizeof(appOptions)/sizeof(struct option)));

    /* register the application */
    skAppRegister(argv[0]);
    skAppVerifyFeatures(&features, NULL);
    skOptionsSetUsageCallback(&appUsageLong);

    /* Initialize variables */
    rv = skStringParseHumanUint64(&tmp64, DEFAULT_SORT_BUFFER_SIZE,
                                  SK_HUMAN_NORMAL);
    assert(0 == rv);
    sort_buffer_size = tmp64;

    optctx_flags = (SK_OPTIONS_CTX_INPUT_SILK_FLOW | SK_OPTIONS_CTX_ALLOW_STDIN
                    | SK_OPTIONS_CTX_XARGS);

    /* Initialize plugin library */
    skPluginSetup(1, SKPLUGIN_APP_SORT);

    /* register the options */
    if (skOptionsCtxCreate(&optctx, optctx_flags)
        || skOptionsCtxOptionsRegister(optctx)
        || skOptionsRegister(appOptions, &appOptionsHandler, NULL)
        || skOptionsTempDirRegister(&temp_directory)
        || skOptionsNotesRegister(NULL)
        || skCompMethodOptionsRegister(&comp_method)
        || sksiteOptionsRegister(SK_SITE_FLAG_CONFIG_FILE))
    {
        skAppPrintErr("Unable to register options");
        appExit(EXIT_FAILURE);
    }

    /* register the teardown handler */
    if (atexit(appTeardown) < 0) {
        skAppPrintErr("Unable to register appTeardown() with atexit()");
        appExit(EXIT_FAILURE);
    }

    sk_sidecar_create(&sidecar);
    sk_sidecar_create(&out_sidecar);
    L = sk_lua_newstate();

    /* try to load hard-coded plugins */
    for (j = 0; app_static_plugins[j].name; ++j) {
        skPluginAddAsPlugin(app_static_plugins[j].name,
                            app_static_plugins[j].setup_fn);
    }
    for (j = 0; app_plugin_names[j]; ++j) {
        skPluginLoadPlugin(app_plugin_names[j], 0);
    }

    /* parse options */
    rv = skOptionsCtxOptionsParse(optctx, argc, argv);
    if (rv < 0) {
        skAppUsage();           /* never returns */
    }

    /* create flow iterator to read the records from the stream */
    flowiter = skOptionsCtxCreateFlowIterator(optctx);
    /* copy header information from inputs to output */
    sk_flow_iter_set_stream_event_cb(
        flowiter, SK_FLOW_ITER_CB_EVENT_PRE_READ,
        &copy_file_header_callback, NULL);

    if (help_fields) {
        helpFields(USAGE_FH);
        exit(EXIT_SUCCESS);
    }

    /* try to load site config file; if it fails, we will not be able
     * to resolve flowtype and sensor from input file names */
    sksiteConfigure(0);

    /* create the --fields */
    if (createStringmaps()) {
        appExit(EXIT_FAILURE);
    }

    /* Parse the fields argument */
    if (fields_arg != NULL) {
        if (parseFields(fields_arg)) {
            exit(EXIT_FAILURE);
        }
    }

    /* Make sure the user specified at least one field */
    if (num_fields == 0) {
        skAppPrintErr("The sorting key (--%s switch) was not given",
                      appOptions[OPT_FIELDS].name);
        skAppUsage();           /* never returns */
    }

    /* verify that the temp directory is valid */
    if (skTempFileInitialize(&tmpctx, temp_directory, NULL, &skAppPrintErr)) {
        appExit(EXIT_FAILURE);
    }

    /* Check for an output stream; or default to stdout  */
    if (out_stream == NULL) {
        if ((rv = skStreamCreate(&out_stream,SK_IO_WRITE,SK_CONTENT_SILK_FLOW))
            || (rv = skStreamBind(out_stream, "-")))
        {
            skStreamPrintLastErr(out_stream, rv, NULL);
            skStreamDestroy(&out_stream);
            appExit(EXIT_FAILURE);
        }
    }

    /* set the compmethod on the header */
    rv = skHeaderSetCompressionMethod(skStreamGetSilkHeader(out_stream),
                                      comp_method);
    if (rv) {
        skAppPrintErr("Error setting header on %s: %s",
                      skStreamGetPathname(out_stream), skHeaderStrerror(rv));
        appExit(EXIT_FAILURE);
    }

    /* open output */
    rv = skStreamOpen(out_stream);
    if (rv) {
        skStreamPrintLastErr(out_stream, rv, NULL);
        skAppPrintErr("Could not open output file.  Exiting.");
        appExit(EXIT_FAILURE);
    }

    /* set signal handler to clean up temp files on SIGINT, SIGTERM, etc */
    if (skAppSetSignalHandler(&appHandleSignal)) {
        appExit(EXIT_FAILURE);
    }

    return;                       /* OK */
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
    uint64_t tmp64;
    int rv;

    switch ((appOptionsEnum)opt_index) {
      case OPT_HELP_FIELDS:
        help_fields = 1;
        break;

      case OPT_FIELDS:
        assert(opt_arg);
        if (fields_arg) {
            skAppPrintErr("Invalid %s: Switch used multiple times",
                          appOptions[opt_index].name);
            return 1;
        }
        fields_arg = opt_arg;
        break;

      case OPT_REVERSE:
        reverse = 1;
        break;

      case OPT_PRINT_FILENAMES:
        print_filenames = 1;
        break;

      case OPT_OUTPUT_PATH:
        /* check for switch given multiple times */
        if (out_stream) {
            skAppPrintErr("Invalid %s: Switch used multiple times",
                          appOptions[opt_index].name);
            skStreamDestroy(&out_stream);
            return 1;
        }
        if ((rv = skStreamCreate(&out_stream,SK_IO_WRITE,SK_CONTENT_SILK_FLOW))
            || (rv = skStreamBind(out_stream, opt_arg)))
        {
            skStreamPrintLastErr(out_stream, rv, NULL);
            return 1;
        }
        break;

      case OPT_PLUGIN:
        if (skPluginLoadPlugin(opt_arg, 1) != 0) {
            skAppPrintErr("Unable to load %s as a plugin", opt_arg);
            return 1;
        }
        break;

      case OPT_PRESORTED_INPUT:
        presorted_input = 1;
        break;

      case OPT_SORT_BUFFER_SIZE:
        rv = skStringParseHumanUint64(&tmp64, opt_arg, SK_HUMAN_NORMAL);
        if (rv) {
            goto PARSE_ERROR;
        }
        if ((tmp64 < MINIMUM_SORT_BUFFER_SIZE)
            || (tmp64 >= MAXIMUM_SORT_BUFFER_SIZE))
        {
            skAppPrintErr(
                ("The --%s value must be between %" SK_PRIuZ " and %" SK_PRIuZ),
                appOptions[opt_index].name,
                MINIMUM_SORT_BUFFER_SIZE, MAXIMUM_SORT_BUFFER_SIZE);
            return 1;
        }
        sort_buffer_size = tmp64;
        break;
    }

    return 0;                     /* OK */

  PARSE_ERROR:
    skAppPrintErr("Invalid %s '%s': %s",
                  appOptions[opt_index].name, opt_arg,
                  skStringParseStrerror(rv));
    return 1;
}


/*
 *  appHandleSignal(signal_value)
 *
 *    Call appExit() to exit the program.  If signal_value is SIGPIPE,
 *    close cleanly; otherwise print a message that we've caught the
 *    signal and exit with EXIT_FAILURE.
 */
static void
appHandleSignal(
    int                 sig)
{
    caught_signal = 1;

    if (sig == SIGPIPE) {
        /* we get SIGPIPE if something downstream, like rwcut, exits
         * early, so don't bother to print a warning, and exit
         * successfully */
        appExit(EXIT_SUCCESS);
    } else {
        skAppPrintErr("Caught signal..cleaning up and exiting");
        appExit(EXIT_FAILURE);
    }
}


/*
 *  status = parseFields(fields_string);
 *
 *    Parse the user's option for the --fields switch and fill in the
 *    global key_fields[].  Return 0 on success; -1 on failure.
 */
static int
parseFields(
    const char         *field_string)
{
    sk_stringmap_iter_t *sm_iter = NULL;
    sk_stringmap_entry_t *sm_entry;
    key_field_t *key;
    char *errmsg;
    uint32_t i;
    int rv = -1;

    /* have we been here before? */
    if (num_fields > 0) {
        skAppPrintErr("Invalid %s: Switch used multiple times",
                      appOptions[OPT_FIELDS].name);
        goto END;
    }

    /* parse the input */
    if (skStringMapParse(key_field_map, field_string, SKSTRINGMAP_DUPES_ERROR,
                         &sm_iter, &errmsg))
    {
        skAppPrintErr("Invalid %s: %s", appOptions[OPT_FIELDS].name, errmsg);
        goto END;
    }

    num_fields = skStringMapIterCountMatches(sm_iter);
    key_fields = sk_alloc_array(key_field_t, num_fields);

    for (key = key_fields;
         skStringMapIterNext(sm_iter, &sm_entry, NULL) == SK_ITERATOR_OK;
         ++key)
    {
        assert(key < key_fields + num_fields);
        key->kf_id = sm_entry->id;
        if (key->kf_id & SIDECAR_FIELD_BIT) {
            /* field comes from a sidecar */
            const sk_sidecar_elem_t *sc_elem;

            sc_elem = (sk_sidecar_elem_t *)sm_entry->userdata;
            assert(sc_elem);

            key->kf_name = sk_alloc_strdup(sm_entry->name);
            key->kf_type = sk_sidecar_elem_get_data_type(sc_elem);

        } else if (key->kf_id & PLUGIN_FIELD_BIT) {
            /* field comes from a plug-in */
            const char **field_names;
            skplugin_field_t *pi_field;
            skplugin_err_t pi_err;
            const char *title;
            size_t bin_width;

            pi_field = (skplugin_field_t*)sm_entry->userdata;
            assert(pi_field);

            key->kf_pi_handle = pi_field;

            /* Activate the plugin (so cleanup knows about it) */
            skPluginFieldActivate(pi_field);
            /* Initialize this field */
            pi_err = skPluginFieldRunInitialize(pi_field);
            if (pi_err != SKPLUGIN_OK) {
                skAppPrintErr("Cannot add field %s from plugin",
                              sm_entry->name);
                goto END;
            }

            /* get the names and the title */
            skPluginFieldName(pi_field, &field_names);
            skPluginFieldTitle(pi_field, &title);
            key->kf_name = sk_alloc_strdup(field_names[0]);

            /* get the bin width for this field */
            pi_err = skPluginFieldGetLenBin(pi_field, &bin_width);
            if (pi_err != SKPLUGIN_OK) {
                skAppPrintErr("Cannot add field %s from plugin:"
                              " Unable to get bin length",
                              sm_entry->name);
                goto END;
            }
            if (0 == bin_width) {
                skAppPrintErr("Cannot add field %s from plugin:"
                              " Field has a binary width of 0",
                              sm_entry->name);
                goto END;
            }
            key->kf_width = bin_width;
            ++num_plugins;

        } else {
            assert(NULL == sm_entry->userdata);
            /* field is built-in */
        }
    }
    num_fields = (key - key_fields);

    /* create an array for quick access to plug-in fields */
    if (num_plugins) {
        uint32_t j = 0;

        active_plugins = sk_alloc_array(key_field_t *, num_plugins);
        for (i = 0, key = key_fields; i < num_fields; ++i, ++key) {
            if (key->kf_id & PLUGIN_FIELD_BIT) {
                assert(j < num_plugins);
                active_plugins[j] = key;
                ++j;
            }
        }
        assert(j == num_plugins);
    }

    /* success */
    rv = 0;

  END:
    if (sm_iter) {
        skStringMapIterDestroy(sm_iter);
    }
    return rv;
}


/*
 *  helpFields(fh);
 *
 *    Print a description of each field to the 'fh' file pointer
 */
static void
helpFields(
    FILE               *fh)
{
    if (createStringmaps()) {
        exit(EXIT_FAILURE);
    }

    fprintf(fh,
            ("The following names may be used in the --%s switch. Names are\n"
             "case-insensitive and may be abbreviated to the shortest"
             " unique prefix.\n"),
            appOptions[OPT_FIELDS].name);

    skStringMapPrintDetailedUsage(key_field_map, fh);
}


/*
 *    Copy the annotation and command line entries from the header of
 *    the input stream 'stream' to the global output stream
 *    'out_stream'.
 *
 *    This is a callback function registered with the global
 *    sk_flow_iter_t 'flowiter' and it may be invoked by
 *    sk_flow_iter_get_next_rec().
 */
static void
copy_file_header_callback(
    sk_flow_iter_t     *f_iter,
    skstream_t         *stream,
    void               *data)
{
    const sk_sidecar_t *in_sidecar;
    const sk_sidecar_elem_t *elem;
    sk_sidecar_iter_t iter;
    ssize_t rv;

    (void)f_iter;
    (void)data;

    if ((rv = skHeaderCopyEntries(skStreamGetSilkHeader(out_stream),
                                  skStreamGetSilkHeader(stream),
                                  SK_HENTRY_INVOCATION_ID))
        || (rv = skHeaderCopyEntries(skStreamGetSilkHeader(out_stream),
                                     skStreamGetSilkHeader(stream),
                                     SK_HENTRY_ANNOTATION_ID)))
    {
        skStreamPrintLastErr(out_stream, rv, &skAppPrintErr);
    }

    in_sidecar = skStreamGetSidecar(stream);
    if (NULL == in_sidecar) {
        return;
    }
    sk_sidecar_iter_bind(in_sidecar, &iter);
    while (sk_sidecar_iter_next(&iter, &elem) == SK_ITERATOR_OK) {
        rv = sk_sidecar_add_elem(out_sidecar, elem, NULL);
        if (rv) {
            if (SK_SIDECAR_E_DUPLICATE == rv) {
                /* FIXME: ignore for now */
            } else {
                skAppPrintErr("Cannot add field from sidecar: %" SK_PRIdZ, rv);
            }
        }
    }

    if (print_filenames) {
        fprintf(PRINT_FILENAMES_FH, "%s\n", skStreamGetPathname(stream));
    }
}


/*
 *  ok = createStringmaps();
 *
 *    Create the string-map to assist in parsing the --fields switch.
 */
static int
createStringmaps(
    void)
{
    skplugin_field_iter_t  pi_iter;
    skplugin_err_t         pi_err;
    skplugin_field_t      *pi_field;
    sk_stringmap_status_t  sm_err;
    sk_stringmap_entry_t   sm_entry;
    const char           **field_names;
    const char           **name;
    uint32_t               max_id;

    /* initialize string-map of field identifiers */
    if (skStringMapCreate(&key_field_map)
        || skRwrecAppendFieldsToStringMap(key_field_map))
    {
        skAppPrintErr("Unable to setup fields stringmap");
        appExit(EXIT_FAILURE);
    }
    max_id = RWREC_FIELD_ID_COUNT - 1;

    if (flowiter) {
        sk_sidecar_iter_t sc_iter;
        const sk_sidecar_elem_t *sc_elem;
        char buf[PATH_MAX];
        size_t buflen;

        if (sk_flow_iter_fill_sidecar(flowiter, sidecar)) {
            skAppPrintErr("Error reading file header");
            return -1;
        }
        sk_sidecar_iter_bind(sidecar, &sc_iter);
        while (sk_sidecar_iter_next(&sc_iter, &sc_elem) == SK_ITERATOR_OK) {
            buflen = sizeof(buf);
            sk_sidecar_elem_get_name(sc_elem, buf, &buflen);
            ++max_id;
            sm_entry.name = buf;
            sm_entry.id = SIDECAR_FIELD_BIT | max_id;
            sm_entry.userdata = sc_elem;
            sm_entry.description = NULL;
            sm_err = skStringMapAddEntries(key_field_map, 1, &sm_entry);
            if (sm_err) {
                skAppPrintErr("Cannot add field '%s' from sidecar: %s",
                              buf, skStringMapStrerror(sm_err));
            }
        }
    }

    /* add --fields from plug-ins */
    pi_err = skPluginFieldIteratorBind(&pi_iter, SKPLUGIN_APP_SORT, 1);
    if (pi_err != SKPLUGIN_OK) {
        assert(pi_err == SKPLUGIN_OK);
        skAppPrintErr("Unable to bind plugin field iterator");
        return -1;
    }

    while (skPluginFieldIteratorNext(&pi_iter, &pi_field)) {
        skPluginFieldName(pi_field, &field_names);
        ++max_id;

        /* Add the field to the key_field_map */
        for (name = field_names; *name; ++name) {
            memset(&sm_entry, 0, sizeof(sm_entry));
            sm_entry.name = *name;
            sm_entry.id = PLUGIN_FIELD_BIT | max_id;
            sm_entry.userdata = pi_field;
            skPluginFieldDescription(pi_field, &sm_entry.description);
            sm_err = skStringMapAddEntries(key_field_map, 1, &sm_entry);
            if (sm_err != SKSTRINGMAP_OK) {
                const char *plugin_name;
                skPluginFieldGetPluginName(pi_field, &plugin_name);
                skAppPrintErr(("Plug-in cannot add field named '%s': %s."
                               " Plug-in file: %s"),
                              *name, skStringMapStrerror(sm_err),plugin_name);
                return -1;
            }
        }
    }

    return 0;
}


/*
 *    Copy 'src_rec' into 'dst_rec' and add plug-in fields.
 *
 *    If 'src_rec' has sidecar data, add a sidecar table to 'dst_rec'
 *    and copy the entries from 'src_rec' to 'dst_rec'.
 *
 *    If plug-in fields exist, add the plug-in's data into sidecar
 *    fields on 'dst_rec'.
 */
void
addPluginFields(
    rwRec              *rwrec)
{
    skplugin_err_t pi_err;
    key_field_t *key;
    uint8_t data_buf[1 << 14];
    size_t i;
    int ref;

    assert(rwrec->lua_state == L);
    if (0 == num_plugins) {
        return;
    }

    ref = rwRecGetSidecar(rwrec);
    if (ref == LUA_NOREF) {
        /* create a table to use as sidecar on the record */
        lua_newtable(L);
    } else if (lua_rawgeti(L, LUA_REGISTRYINDEX, ref) != LUA_TTABLE) {
        skAppPrintErr("Sidecar is not a table");
        skAbort();
    }

    /* call the plug-ins */
    for (i = 0; i < num_plugins; ++i) {
        key = active_plugins[i];
        pi_err = (skPluginFieldRunRecToBinFn(
                      key->kf_pi_handle, data_buf, rwrec, NULL));
        if (pi_err != SKPLUGIN_OK) {
            skAppPrintErr(("Plugin-based field %s failed converting to binary "
                           "with error code %d"), key->kf_name, pi_err);
            exit(EXIT_FAILURE);
        }
        lua_pushlstring(L, (char *)data_buf, key->kf_width);
        lua_setfield(L, -2, key->kf_name);
    }

    assert(LUA_TTABLE == lua_type(L, -1));

    /* The copied table is at the top of the stack; get a reference to
     * it and remove it */
    if (ref == LUA_NOREF) {
        rwRecSetSidecar(rwrec, luaL_ref(L, LUA_REGISTRYINDEX));
    } else {
        lua_pop(L, 1);
    }
}


/*
 *  status = fillRecordAndKey(stream, node);
 *
 *    Reads a flow record from 'stream', computes the key based on the
 *    global key_fields[] settings, and fills in the parameter 'buf'
 *    with the record and then the key.  Return 1 if a record was
 *    read, or 0 if it was not.
 */
int
fillRecordAndKey(
    skstream_t         *stream,
    rwRec              *rwrec)
{
    int rv;

    rv = skStreamReadRecord(stream, rwrec);
    if (rv) {
        /* end of file or error getting record */
        if (SKSTREAM_ERR_EOF != rv) {
            skStreamPrintLastErr(stream, rv, &skAppPrintErr);
        }
        return 0;
    }
    addPluginFields(rwrec);
    return 1;
}


/*
** Local Variables:
** mode:c
** indent-tabs-mode:nil
** c-basic-offset:4
** End:
*/
