/*
** Copyright (C) 2001-2017 by Carnegie Mellon University.
**
** @OPENSOURCE_LICENSE_START@
** See license information in ../../LICENSE.txt
** @OPENSOURCE_LICENSE_END@
*/

/*
**  rwfilterutils.c
**
**  utility routines for rwfilter.c
**
**  Suresh L. Konda
**
*/

#include <silk/silk.h>

RCSIDENT("$SiLK: rwfiltersetup.c efd886457770 2017-06-21 18:43:23Z mthomas $");

#include <silk/sklua.h>
#include <silk/silkpython.h>
#include <silk/skprefixmap.h>
#include "rwfilter.h"

/* TYPEDEFS AND DEFINES */



/* LOCAL VARIABLES */

/* Lua initialization code; this is binary code compiled from
 * rwfilter.lua */
static const uint8_t rwfilter_lua[] = {
#include "rwfilter.i"
};

/* Lua registry index to the table of functions */
static int ref_export = LUA_NOREF;

/* Lua registry index to the filter function */
static int ref_run_filter = LUA_NOREF;

/* the number of --lua-file arguments */
static unsigned int count_lua_file = 0;

/* the number of --lua-expression arguments */
static unsigned int count_lua_expression = 0;

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
    {"pmapfilter",      skPrefixMapAddFields},
#if SK_ENABLE_PYTHON
    {"silkpython",      skSilkPythonAddFields},
#endif
    {NULL, NULL}        /* sentinel */
};

/* names of plug-ins to attempt to load at startup */
static const char *app_plugin_names[] = {
    SK_PLUGIN_ADD_SUFFIX("ipafilter"),
    NULL /* sentinel */
};



/* OPTION SETUP */

/* these values used to index into following arrays.  need to keep
 * everything in sync */
typedef enum {
    OPT_DRY_RUN,
#if SK_RWFILTER_THREADED
    OPT_THREADS,
#endif
    OPT_MAX_PASS_RECORDS, OPT_MAX_FAIL_RECORDS,
    OPT_PLUGIN, OPT_LUA_FILE,

    OPT_PASS_DEST, OPT_FAIL_DEST, OPT_ALL_DEST,
    OPT_PRINT_STAT, OPT_PRINT_VOLUME,

    OPT_LUA_EXPRESSION
} appOptionsEnum;

static struct option appOptions[] = {
    {"dry-run",                 NO_ARG,       0, OPT_DRY_RUN},
#if SK_RWFILTER_THREADED
    {"threads",                 REQUIRED_ARG, 0, OPT_THREADS},
#endif
    {"max-pass-records",        REQUIRED_ARG, 0, OPT_MAX_PASS_RECORDS},
    {"max-fail-records",        REQUIRED_ARG, 0, OPT_MAX_FAIL_RECORDS},
    {"plugin",                  REQUIRED_ARG, 0, OPT_PLUGIN},
    {"lua-file",                REQUIRED_ARG, 0, OPT_LUA_FILE},

    {"pass-destination",        REQUIRED_ARG, 0, OPT_PASS_DEST},
    {"fail-destination",        REQUIRED_ARG, 0, OPT_FAIL_DEST},
    {"all-destination",         REQUIRED_ARG, 0, OPT_ALL_DEST},
    {"print-statistics",        OPTIONAL_ARG, 0, OPT_PRINT_STAT},
    {"print-volume-statistics", OPTIONAL_ARG, 0, OPT_PRINT_VOLUME},

    {"lua-expression",          REQUIRED_ARG, 0, OPT_LUA_EXPRESSION},
    {0,0,0,0}                   /* sentinel entry */
};

static const char *appHelp[] = {
    "Parse command line switches but do not process records",
#if SK_RWFILTER_THREADED
    "Use this number of threads. Def $SILK_RWFILTER_THREADS or 1",
#endif
    ("Write at most this many records to\n"
     "\tthe pass-destination; 0 for all.  Def. 0"),
    ("Write at most this many records to\n"
     "\tthe fail-destination; 0 for all.  Def. 0"),
    ("Augment processing with the specified plug-in.\n"
     "\tSwitch may be repeated to load multiple plug-ins. No default"),
    ("Load the named Lua file during set-up.  Switch may be\n"
     "\trepeated to load multiple files. No default"),
    ("Destination for records which pass the filter(s):\n"
     "\tpathname or 'stdout'. If pathname, it must not exist. No default"),
    ("Destination for records which fail the filter(s):\n"
     "\tpathname or 'stdout'. If pathname, it must not exist. No default"),
    ("Destination for all records regardless of pass/fail:\n"
     "\tpathname or 'stdout'. If pathname, it must not exist. No default"),
    ("Print a count of total flows read to named file.\n"
     "\tIf no pathname provided, use stderr. No default"),
    ("Print count of flows/packets/bytes read\n"
     "\tto named file. If no pathname provided, use stderr. No default"),
    ("Use the return value of given Lua expression as the\n"
     "\tpass/fail determiner (flow record is called \"rec\").  Repeatable."),
    (char *)NULL
};


/* LOCAL FUNCTION PROTOTYPES */

static int  appOptionsHandler(clientData cData, int opt_index, char *opt_arg);
static int  filterCheckOutputs(void);
static int  filterOpenOutputs(void);
static checktype_t filterPluginCheck(const rwRec *rec);
static checktype_t filterLuaFiltersCheck(const rwRec *rec);
static int  filterSetCheckers(void);
static int  filterLuaFiltersInitialize(void);
static int  filterLuaFiltersFinalize(void);
#if 0
static void
filterCopyFileHeaderCallback(
    sk_flow_iter_t     *f_iter,
    skstream_t         *stream,
    void               *data);
#endif  /* 0 */


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
#define USAGE_MSG                                                           \
    ("<app-opts> <partition-opts> {<selection-opts> | <inputFiles>}\n"      \
     "\tPartitions SiLK Flow records into one or more 'pass' and/or\n"      \
     "\t'fail' output streams.  The source of the SiLK records can\n"       \
     "\tbe stdin, a named pipe, files listed on the command line, or\n"     \
     "\tfiles selected from the data-store via the selection switches.\n"   \
     "\tThere is no default input or output; these must be specified.\n")

    FILE *fh = USAGE_FH;
    int i;


    fprintf(fh, "%s %s", skAppName(), USAGE_MSG);

    fprintf(fh, "\nGENERAL SWITCHES:\n\n");
    skOptionsDefaultUsage(fh);

    /* print general options (everything before --pass-destination) */
    for (i = 0; appOptions[i].name && appOptions[i].val < OPT_PASS_DEST; ++i) {
        fprintf(fh, "--%s %s. %s\n", appOptions[i].name,
                SK_OPTION_HAS_ARG(appOptions[i]), appHelp[i]);
    }

    skOptionsNotesUsage(fh);
    skCompMethodOptionsUsage(fh);

    /* print output options (everything before --lua-expression) */
    fprintf(fh, ("\nOUTPUT SWITCHES."
                 " At least one output switch is required:\n\n"));
    for ( ; appOptions[i].name && appOptions[i].val < OPT_LUA_EXPRESSION; ++i){
        fprintf(fh, "--%s %s. %s\n", appOptions[i].name,
                SK_OPTION_HAS_ARG(appOptions[i]), appHelp[i]);
    }

    /* print input option (everything before --lua-expression) */
    fprintf(fh, ("\nINPUT SWITCH."
                 " Exactly one type of input is required:"
                 " this input switch; one or\n"
                 "\tmore arguments specifying filenames, named pipes,"
                 " or '-' or 'stdin'\n"
                 "\tfor standard input; one or more"
                 " FILE SELECTION SWITCHES (next group):\n\n"));
    skOptionsCtxOptionsUsage(optctx, fh);

    /* partitioning switches */
    filterUsage(fh);
    tupleUsage(fh);
    /* print local partitioning option (remaining options) */
    for ( ; appOptions[i].name; ++i) {
        fprintf(fh, "--%s %s. %s\n", appOptions[i].name,
                SK_OPTION_HAS_ARG(appOptions[i]), appHelp[i]);
    }

    /* switches from plug-ins */
    skPluginOptionsUsage(fh);
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
    unsigned int optctx_flags;
    int output_count;
    int rv;
    int j;

    /* verify same number of options and help strings */
    assert((sizeof(appHelp)/sizeof(char*)) ==
           (sizeof(appOptions)/sizeof(struct option)));

    /* register the application */
    /* skAppRegister(argv[0]);  -- we do this in main() */
    skOptionsSetUsageCallback(&appUsageLong);

    /* initialize variables */
    memset(&dest_type, 0, sizeof(dest_type));

    optctx_flags = (SK_OPTIONS_CTX_INPUT_SILK_FLOW | SK_OPTIONS_CTX_XARGS
                    | SK_OPTIONS_CTX_PRINT_FILENAMES | SK_OPTIONS_CTX_FGLOB);

    /* load filter module */
    if (filterSetup()) {
        skAppPrintErr("Unable to setup filter module");
        exit(EXIT_FAILURE);
    }
    /* load tuple module */
    if (tupleSetup()) {
        skAppPrintErr("Unable to setup tuple module");
        exit(EXIT_FAILURE);
    }

    /* Initialize plugin library */
    skPluginSetup(1, SKPLUGIN_APP_FILTER);

    L = filterLuaCreateState();

    /* register the options */
    if (skOptionsCtxCreate(&optctx, optctx_flags)
        || skOptionsCtxOptionsRegister(optctx)
        || skOptionsRegister(appOptions, &appOptionsHandler, NULL)
        || skOptionsNotesRegister(NULL)
        || skCompMethodOptionsRegister(&comp_method))
    {
        skAppPrintErr("Unable to register options");
        exit(EXIT_FAILURE);
    }


    /* specify the function that plug-ins should use for opening any
     * input files (e.g., a python script) that they require */
    skPluginSetOpenInputFunction(&filterOpenInputData);

    /* try to load hard-coded plugins */
    for (j = 0; app_static_plugins[j].name; ++j) {
        skPluginAddAsPlugin(app_static_plugins[j].name,
                            app_static_plugins[j].setup_fn);
    }
    for (j = 0; app_plugin_names[j]; ++j) {
        skPluginLoadPlugin(app_plugin_names[j], 0);
    }

#if SK_RWFILTER_THREADED
    {
        /* check the thread count envar */
        char *env;
        uint32_t tc;

        env = getenv(RWFILTER_THREADS_ENVAR);
        if (env && env[0]) {
            if (skStringParseUint32(&tc, env, 0, 0) == 0) {
                thread_count = tc;
            } else {
                thread_count = 1;
            }
        }
    }
#endif  /* SK_RWFILTER_THREADED */

    /* register the teardown handler */
    if (atexit(appTeardown) < 0) {
        skAppPrintErr("Unable to register appTeardown() with atexit()");
        appTeardown();
        exit(EXIT_FAILURE);
    }

    /* parse options */
    rv = skOptionsCtxOptionsParse(optctx, argc, argv);
    if (rv < 0) {
        skAppUsage();         /* never returns */
    }

    /* Try to load site config file; if it fails, we will not be able
     * to resolve flowtype and sensor from input file names.  If fglob
     * is active, it will require the configuration file. */
    sksiteConfigure(0);

    /* Can only use Lua in a single thread */
    if ((thread_count > 1) && (count_lua_file || count_lua_expression)) {
        skAppPrintErr("May not use multiple threads with --%s or %s",
                      appOptions[OPT_LUA_FILE].name,
                      appOptions[OPT_LUA_EXPRESSION].name);
        exit(EXIT_FAILURE);
    }

    if (thread_count == 1) {
        /* Call the initialization functions defined in Lua */
        if (filterLuaFiltersInitialize()) {
            exit(EXIT_FAILURE);
        }
    }

    /* initialize the plug-ins */
    if (skPluginRunInititialize(SKPLUGIN_APP_FILTER) != SKPLUGIN_OK) {
        exit(EXIT_FAILURE);
    }

#if SK_RWFILTER_THREADED
    /* do not use threading when the plug-in doesn't support
     * it */
    if ((thread_count > 1) && !skPluginIsThreadSafe()) {
        thread_count = 1;
    }
#endif  /* SK_RWFILTER_THREADED */

    /* check that the user asked for some output */
    output_count = filterCheckOutputs();
    if (output_count < 0) {
        /* fatal error. msg already printed */
        exit(EXIT_FAILURE);
    }
    if (output_count == 0) {
        skAppPrintErr("No output(s) specified");
        skAppUsage();
    }

    /* Check whether we have a filtering rule--either built in or from
     * a plugin.  If we don't, complain unless the --all-dest switch
     * was given */
    checker_count = filterSetCheckers();
    if (checker_count < 0) {
        /* fatal error */
        exit(EXIT_FAILURE);
    }
    if (checker_count == 0) {
        if (dest_type[DEST_PASS].dest_list) {
            skAppPrintErr("Must specify partitioning rules when using --%s",
                          appOptions[OPT_PASS_DEST].name);
            skAppUsage();
        }
        if (dest_type[DEST_FAIL].dest_list) {
            skAppPrintErr("Must specify partitioning rules when using --%s",
                          appOptions[OPT_FAIL_DEST].name);
            skAppUsage();
        }
        if (NULL == dest_type[DEST_ALL].dest_list) {
            skAppPrintErr(("Must specify partitioning rules when using --%s"
                           " without --%s"),
                          (print_volume_stats
                           ? appOptions[OPT_PRINT_VOLUME].name
                           : appOptions[OPT_PRINT_STAT].name),
                          appOptions[OPT_ALL_DEST].name);
            skAppUsage();
        }
    }


    /* if this is a dry-run, there is nothing else to do */
    if (dryrun_fp) {
        return;
    }

    /* create flow iterator to read the records from the inputs */
    flowiter = skOptionsCtxCreateFlowIterator(optctx);

    /* ignore errors opening files */
    sk_flow_iter_set_stream_error_cb(flowiter, SK_FLOW_ITER_CB_ERROR_OPEN,
                                     sk_flow_iter_ignore_error_open_cb, NULL);

    /* open the output streams */
    if (filterOpenOutputs()) {
        /* fatal error. msg already printed */
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
    destination_t *dest;
    destination_t **end;
    dest_type_t *d_type;
    int dest_id;
    int rv;

    switch ((appOptionsEnum)opt_index) {
      case OPT_PASS_DEST:
      case OPT_FAIL_DEST:
      case OPT_ALL_DEST:
        /* an output stream */
        dest_id = opt_index - OPT_PASS_DEST;
        assert(dest_id >= 0 && dest_id < DESTINATION_TYPES);

        dest = (destination_t*)calloc(1, sizeof(destination_t));
        if (dest == NULL) {
            skAppPrintOutOfMemory(NULL);
            return 1;
        }
        if ((rv = skStreamCreate(&dest->stream, SK_IO_WRITE,
                                 SK_CONTENT_SILK_FLOW))
            || (rv = skStreamBind(dest->stream, opt_arg)))
        {
            skStreamPrintLastErr(dest->stream, rv, &skAppPrintErr);
            skStreamDestroy(&dest->stream);
            free(dest);
            return 1;
        }

        d_type = &dest_type[dest_id];
        ++d_type->count;
        end = &d_type->dest_list;
        while (*end != NULL) {
            end = &((*end)->next);
        }
        *end = dest;
        break;

#if SK_RWFILTER_THREADED
      case OPT_THREADS:
        rv = skStringParseUint32(&thread_count, opt_arg, 1, 0);
        if (rv) {
            goto PARSE_ERROR;
        }
        break;
#endif  /* SK_RWFILTER_THREADED */

      case OPT_PLUGIN:
        if (skPluginLoadPlugin(opt_arg, 1)) {
            skAppPrintErr("Fatal error loading plugin '%s'", opt_arg);
            return 1;
        }
        break;

      case OPT_DRY_RUN:
        dryrun_fp = DRY_RUN_FH;
        break;

      case OPT_PRINT_VOLUME:
        print_volume_stats = 1;
        /* FALLTHROUGH */
      case OPT_PRINT_STAT:
        if (print_stat) {
            skAppPrintErr("May only specified one of --%s or --%s",
                          appOptions[OPT_PRINT_STAT].name,
                          appOptions[OPT_PRINT_VOLUME].name);
            return 1;
        }
        if ((rv = skStreamCreate(&print_stat, SK_IO_WRITE, SK_CONTENT_TEXT))
            || (rv = skStreamBind(print_stat, (opt_arg ? opt_arg : "stderr"))))
        {
            skStreamPrintLastErr(print_stat, rv, &skAppPrintErr);
            skStreamDestroy(&print_stat);
            skAppPrintErr("Invalid %s '%s'",
                          appOptions[opt_index].name, opt_arg);
            return 1;
        }
        break;

      case OPT_MAX_PASS_RECORDS:
        rv = skStringParseUint64(&(dest_type[DEST_PASS].max_records),
                                 opt_arg, 0, 0);
        if (rv) {
            goto PARSE_ERROR;
        }
        break;

      case OPT_MAX_FAIL_RECORDS:
        rv = skStringParseUint64(&(dest_type[DEST_FAIL].max_records),
                                 opt_arg, 0, 0);
        if (rv) {
            goto PARSE_ERROR;
        }
        break;

      case OPT_LUA_FILE:
        /* get the 'export' table from the registry, get the
         * 'load_lua_file' function from 'export', remove 'export'
         * from the stack, push the argument, and run the function */
        ++count_lua_file;
        lua_rawgeti(L, LUA_REGISTRYINDEX, ref_export);
        lua_getfield(L, -1, "load_lua_file");
        lua_remove(L, -2);
        lua_pushstring(L, opt_arg);
        rv = lua_pcall(L, 1, 0, 0);
        if (rv != LUA_OK) {
            skAppPrintErr("%s", lua_tostring(L, -1));
            lua_pop(L, 1);
            assert(0 == lua_gettop(L));
            return 1;
        }
        assert(0 == lua_gettop(L));
        break;

      case OPT_LUA_EXPRESSION:
        ++count_lua_expression;
        lua_rawgeti(L, LUA_REGISTRYINDEX, ref_export);
        lua_getfield(L, -1, "parse_lua_expression");
        lua_remove(L, -2);
        lua_pushstring(L, opt_arg);
        rv = lua_pcall(L, 1, 0, 0);
        if (rv != LUA_OK) {
            skAppPrintErr("Invalid %s", lua_tostring(L, -1));
            lua_pop(L, 1);
            assert(0 == lua_gettop(L));
            return 1;
        }
        assert(0 == lua_gettop(L));
        break;
    } /* switch */

    return 0;                     /* OK */

  PARSE_ERROR:
    skAppPrintErr("Invalid %s '%s': %s",
                  appOptions[opt_index].name, opt_arg,
                  skStringParseStrerror(rv));
    return 1;
}


/*
 *  appTeardown()
 *
 *    Teardown all modules, close all files, and tidy up all
 *    application state.
 *
 *    This function is idempotent.
 */
void
appTeardown(
    void)
{
    static uint8_t teardownFlag = 0;
    int rv;

    if (teardownFlag) {
        return;
    }
    teardownFlag = 1;

    skPluginRunCleanup(SKPLUGIN_APP_FILTER);
    skPluginTeardown();

    tupleTeardown();
    filterTeardown();
    skOptionsNotesTeardown();

    if (L) {
        if (thread_count == 1) {
            filterLuaFiltersFinalize();
        }
    }

    closeAllDests();

    sk_lua_closestate(L);
    L = NULL;

    /* close stats */
    if (print_stat) {
        rv = skStreamClose(print_stat);
        switch (rv) {
          case SKSTREAM_OK:
          case SKSTREAM_ERR_NOT_OPEN:
          case SKSTREAM_ERR_CLOSED:
            break;
          default:
            skStreamPrintLastErr(print_stat, rv, &skAppPrintErr);
            skAppPrintErr("Error closing --%s stream '%s'",
                          (print_volume_stats
                           ? appOptions[OPT_PRINT_VOLUME].name
                           : appOptions[OPT_PRINT_STAT].name),
                          skStreamGetPathname(print_stat));
            break;
        }
        skStreamDestroy(&print_stat);
    }

    skOptionsCtxDestroy(&optctx);
    sk_flow_iter_destroy(&flowiter);


    skAppUnregister();
}


/*
 *  ignoreSigPipe();
 *
 *    Ignore SIGPIPE.
 */
void
filterIgnoreSigPipe(
    void)
{
    /* install a signal handler to ignore SIGPIPE */
    struct sigaction act;

    act.sa_handler = SIG_IGN;
    sigemptyset(&act.sa_mask);
    act.sa_flags = 0;
    if (sigaction(SIGPIPE, &act, NULL) < 0) {
        skAppPrintErr("Cannot register handler for SIGPIPE");
    }
}


/*
 *    Create a Lua state and load the (compiled) contents of
 *    "rwfilter.lua" into that state.
 */
lua_State *
filterLuaCreateState(
    void)
{
    lua_State *S;
    int rv;

    /* initialize Lua */
    S = sk_lua_newstate();

    /* load and run the the initialization code in rwfilter.lua.  The
     * return value is a table of functions. */
    rv = luaL_loadbufferx(S, (const char*)rwfilter_lua,
                          sizeof(rwfilter_lua), "rwfilter.lua", "b");
    if (LUA_OK == rv) {
        rv = lua_pcall(S, 0, 1, 0);
    }
    if (rv != LUA_OK) {
        skAppPrintErr("Lua initialization failed: %s", lua_tostring(S, -1));
        exit(EXIT_FAILURE);
    }
    assert(LUA_TTABLE == lua_type(S, -1));

    /* Get the filtering function from that table and store it in the
     * registry. */
    lua_getfield(S, -1, "run_filter");
    assert(LUA_TFUNCTION == lua_type(S, -1));
    ref_run_filter = luaL_ref(S, LUA_REGISTRYINDEX);

    /* Get the 'register_filter' function from that table and store it
     * in the global namespace so the --lua-file may access it. */
    lua_getfield(S, -1, "register_filter");
    assert(LUA_TFUNCTION == lua_type(S, -1));
    lua_setglobal(S, "register_filter");

    /* Store the table of functions in the registry. */
    ref_export = luaL_ref(S, LUA_REGISTRYINDEX);
    assert(0 == lua_gettop(S));

    return S;
}


#if 0
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
filterCopyFileHeaderCallback(
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
        rv = sk_sidecar_add_elem(sidecar, elem, NULL);
        if (rv) {
            if (SK_SIDECAR_E_DUPLICATE == rv) {
                /* FIXME: ignore for now */
            } else {
                skAppPrintErr("Cannot add field from sidecar: %" SK_PRIdZ, rv);
            }
        }
    }

/*
**    if (print_filenames) {
**        fprintf(PRINT_FILENAMES_FH, "%s\n", skStreamGetPathname(stream));
**    }
*/
}
#endif  /* 0 */


/*
 *  stream_count = filterCheckOutputs()
 *
 *    Count the number of output streams, and do basic checks for the
 *    outputs, such as making certain only one stream uses stdout.
 *
 *    Returns the number of output streams requested, or -1 on error.
 */
static int
filterCheckOutputs(
    void)
{
    destination_t *dest;
    int count = 0;
    int stdout_used = 0;
    int dest_id;

    /* Basic checks: Only allow one output stream to use 'stdout'.
     * Make certain no one uses 'stderr'. */
    for (dest_id = 0; dest_id < DESTINATION_TYPES; ++dest_id) {
        for (dest = dest_type[dest_id].dest_list;
             dest != NULL;
             dest = dest->next)
        {
            ++count;
            if (strcmp("stderr", skStreamGetPathname(dest->stream)) == 0) {
                skAppPrintErr(("Invalid %s '%s': Will not write"
                               " binary data to the standard error"),
                              appOptions[dest_id+OPT_PASS_DEST].name,
                              skStreamGetPathname(dest->stream));
                return -1;
            }
            if ((strcmp("stdout", skStreamGetPathname(dest->stream)) == 0)
                || (strcmp("-", skStreamGetPathname(dest->stream)) == 0))
            {
                if (stdout_used) {
                    skAppPrintErr("Invalid %s '%s':"
                                  " The standard output is already allocated",
                                  appOptions[dest_id+OPT_PASS_DEST].name,
                                  skStreamGetPathname(dest->stream));
                    return -1;
                }
                stdout_used = 1;
            }
        }
    }

    /* Check the STATISTICS stream: increment the output count and
     * check whether stdout is already used if the stats are going to
     * stdout. */
    if (print_stat) {
        ++count;
        if (stdout_used
            && ((strcmp(skStreamGetPathname(print_stat), "stdout") == 0)
                || (strcmp(skStreamGetPathname(print_stat), "-") == 0)))
        {
            skAppPrintErr(("Invalid %s '%s':"
                           " The standard output is already allocated"),
                          (print_volume_stats
                           ? appOptions[OPT_PRINT_VOLUME].name
                           : appOptions[OPT_PRINT_STAT].name),
                          skStreamGetPathname(print_stat));
            return -1;
        }
    }

    return count;
}


/*
 *  status = filterOpenOutputs()
 *
 *    Open all output streams.  Return 0 on success, or -1 on failure.
 */
static int
filterOpenOutputs(
    void)
{
    destination_t *dest = NULL;
    int dest_id;
    int rv = SKSTREAM_OK;

    /* open the STATISTICS stream */
    if (print_stat) {
        rv = skStreamOpen(print_stat);
        if (rv) {
            skStreamPrintLastErr(print_stat, rv, &skAppPrintErr);
            return -1;
        }
    }



    /* Open all the SiLK Flow output streams */
    for (dest_id = 0; dest_id < DESTINATION_TYPES; ++dest_id) {

        for (dest = dest_type[dest_id].dest_list;
             dest != NULL;
             dest = dest->next)
        {
            /* set compression level */
            rv = skStreamSetCompressionMethod(dest->stream, comp_method);
            if (rv) {
                goto DEST_ERROR;
            }
            rv = skStreamOpen(dest->stream);
            if (rv) {
                goto DEST_ERROR;
            }
        }
    }

    return 0;

  DEST_ERROR:
    skStreamPrintLastErr(dest->stream, rv, &skAppPrintErr);
    closeAllDests();
    return -1;
}


/*
 *  count = filterSetCheckers();
 *
 *    Set the array of function pointers to the pass/fail checking
 *    routines, and return the number of pointers that were set.  If a
 *    check-routine is a plug-in, call the plug-in's initialize()
 *    routine.  If the initialize() routine fails, return -1.  A
 *    return code of 0 means no filtering rules were specified.
 */
static int
filterSetCheckers(
    void)
{
    int count = 0;
    int rv;

    if (filterGetCheckCount() > 0) {
        checker[count] = filterCheck;
        ++count;
    }

    rv = tupleGetCheckCount();
    if (rv == -1) {
        return -1;
    }
    if (rv) {
        checker[count] = tupleCheck;
        ++count;
    }

    lua_rawgeti(L, LUA_REGISTRYINDEX, ref_export);
    lua_getfield(L, -1, "count_filters");
    lua_call(L, 0, 1);
    rv = lua_tointeger(L, -1);
    lua_pop(L, 2);
    assert(0 == lua_gettop(L));
    assert(rv >= 0);
    if (rv > 0) {
        assert(1 == thread_count);
        checker[count] = &filterLuaFiltersCheck;
        ++count;
    }

    if (skPluginFiltersRegistered()) {
        checker[count] = &filterPluginCheck;
        ++count;
    }

    return count;
}


/*
 *  result = filterPluginCheck(rec);
 *
 *    Runs plugin rwfilter functions, and converts the result to an
 *    RWF enum.
 */
static checktype_t
filterPluginCheck(
    const rwRec        *rec)
{
    skplugin_err_t err = skPluginRunFilterFn(rec, NULL);
    switch (err) {
      case SKPLUGIN_FILTER_PASS:
        return RWF_PASS;
      case SKPLUGIN_FILTER_PASS_NOW:
        return RWF_PASS_NOW;
      case SKPLUGIN_FILTER_IGNORE:
        return RWF_IGNORE;
      case SKPLUGIN_FILTER_FAIL:
        return RWF_FAIL;
      default:
        skAppPrintErr("Plugin-based filter failed with error code %d", err);
        exit(EXIT_FAILURE);
    }
}


/*
 *    Helper function for filterLuaFiltersInitialize() and
 *    filterLuaFiltersFinalize().
 */
static int
filterLuaFiltersRun(
    const char         *func_name)
{
    int rv;

    assert(1 == thread_count);

    lua_rawgeti(L, LUA_REGISTRYINDEX, ref_export);
    assert(LUA_TTABLE == lua_type(L, -1));
    lua_getfield(L, -1, func_name);
    lua_remove(L, -2);
    rv = lua_pcall(L, 0, 0, 0);
    if (LUA_OK != rv) {
        skAppPrintErr("%s", lua_tostring(L, -1));
        lua_pop(L, 1);
    }
    assert(0 == lua_gettop(L));
    return ((LUA_OK == rv) - 1);
}

static int
filterLuaFiltersInitialize(
    void)
{
    return filterLuaFiltersRun("run_initialize");
}

static int
filterLuaFiltersFinalize(
    void)
{
    return filterLuaFiltersRun("run_finalize");
}

/*
 *  result = filterLuaCheck(rec);
 *
 *    Run Lua-based filter functions.
 */
static checktype_t
filterLuaFiltersCheck(
    const rwRec        *const_rec)
{
    static int printed_error = 0;
    checktype_t ct;
    int rv;

    assert(1 == thread_count);

    lua_rawgeti(L, LUA_REGISTRYINDEX, ref_run_filter);
    sk_lua_push_rwrec(L, const_rec);
    rv = lua_pcall(L, 1, 1, 0);
    if (LUA_OK == rv) {
        ct = lua_toboolean(L, -1) ? RWF_PASS : RWF_FAIL;
        lua_pop(L, 1);
        assert(0 == lua_gettop(L));
        return ct;
    }
    if (!printed_error) {
        printed_error = 1;
        skAppPrintErr("Lua-based filter failed with error %s",
                      lua_tostring(L, -1));
    }
    lua_pop(L, 1);
    assert(0 == lua_gettop(L));
    return RWF_IGNORE;
}

/*
 *  status = filterOpenInputData(&stream, content_type, filename);
 *
 *    This is the function that plug-ins should use for opening any
 *    input files they use.  The function opens 'filename', a file
 *    having the specified 'content_type', and fills 'stream' with a
 *    handle to the file.  Returns 0 if the file was successfully
 *    opened, and -1 if the file could not be opened.
 */
int
filterOpenInputData(
    skstream_t        **stream,
    skcontent_t         content_type,
    const char         *filename)
{
    skstream_t *s;
    int rv;


    if ((rv = skStreamCreate(&s, SK_IO_READ, content_type))
        || (rv = skStreamBind(s, filename))
        || (rv = skStreamOpen(s)))
    {
        skStreamPrintLastErr(s, rv, &skAppPrintErr);
        skStreamDestroy(&s);
        return -1;
    }
    *stream = s;
    return 0;
}


/*
** Local Variables:
** mode:c
** indent-tabs-mode:nil
** c-basic-offset:4
** End:
*/
