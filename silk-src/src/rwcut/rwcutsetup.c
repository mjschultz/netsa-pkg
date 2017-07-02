/*
** Copyright (C) 2001-2017 by Carnegie Mellon University.
**
** @OPENSOURCE_LICENSE_START@
** See license information in ../../LICENSE.txt
** @OPENSOURCE_LICENSE_END@
*/

/*
** rwcutsetup.c
**      utility routines in support of rwcut.
** Suresh Konda
**
*/

#include <silk/silk.h>

RCSIDENT("$SiLK: rwcutsetup.c d1637517606d 2017-06-23 16:51:31Z mthomas $");

#include <silk/silkpython.h>
#include <silk/skcountry.h>
#include <silk/skdllist.h>
#include <silk/skplugin.h>
#include <silk/skprefixmap.h>
#include <silk/sksidecar.h>
#include <silk/skstringmap.h>
#include <silk/skvector.h>
#include "rwcut.h"


/* TYPEDEFS AND MACROS */

/* where to write --help output */
#define USAGE_FH stdout

/* The last field printed by default. */
#define RWCUT_LAST_DEFAULT_FIELD  RWREC_FIELD_SID

/* A stringmap entry whose ID has this bit set is from a plugin */
#define PLUGIN_FIELD_BIT    0x80000000

/* A stringmap entry whose ID has this bit set is from a sidecar field
 * that appears in the input files */
#define SIDECAR_FIELD_BIT   0x40000000

/* A stringmap entry whose ID has this bit set is from a sidecar field
 * defined by a Lus function */
#define SC_LUA_FIELD_BIT    0x20000000

/* User options */
typedef struct cut_opt_flags_st {
    unsigned no_titles          :1;
    unsigned no_final_delimiter :1;
    unsigned no_columns         :1;
    unsigned integer_sensors    :1;
    unsigned integer_tcp_flags  :1;
    unsigned dry_run            :1;
} cut_opt_flags_t;


/* LOCAL VARIABLES */

/* Lua initialization code; this is binary code compiled from
 * rwcut.lua */
static const uint8_t rwcut_lua[] = {
#include "rwcut.i"
};


/* Lua references into the Lua registry of various functions defined
 * in rwcut.lua */
static struct reg_ref_st {
    int     load_lua_file;
    int     activate_field;
    int     get_sidecar;
    int     count_functions;
    int     apply_sidecar;
    int     invoke_teardown;
} reg_ref = {
    LUA_NOREF, LUA_NOREF, LUA_NOREF, LUA_NOREF, LUA_NOREF, LUA_NOREF
};

/* start and end record number */
static uint64_t start_rec_num = 0;
static uint64_t end_rec_num = 0;
/* the variables num_recs, skip_recs, and tail_recs are globals */

/* whether to print the fields help */
static int help_fields = 0;

/* name of program to run to page output */
static char *pager = NULL;

/* user's options */
static cut_opt_flags_t cut_opts;

/* delimiter between columns */
static char delimiter;

/* how to print IP addresses */
static uint32_t ip_format = SKIPADDR_CANONICAL;

/* how to print timestamps */
static uint32_t time_flags = 0;

/* flags when registering --timestamp-format */
static const uint32_t time_register_flags = 0;

/* the text the user entered for the --fields switch */
static char *fields_arg = NULL;

/* whether the --all-fields switch was given */
static int all_fields = 0;

/* available fields */
static sk_stringmap_t *key_field_map;

/* sidecar holding all defined sidecar elements */
static sk_sidecar_t *sidecar;

/* number of sidecar functions defined in --lua-file */
static long num_sidecar_adds = 0;

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

/* List of plugins to attempt to open at startup */
static const char *app_plugin_names[] = {
    NULL /* sentinel */
};

/* Plug-ins that are in use */
static sk_vector_t *active_plugins = NULL;

/* Number of active plugins */
static size_t num_plugins = 0;


/* OPTIONS SETUP */

typedef enum {
    /* Keep this list in sync with appOptions[] */
    OPT_HELP_FIELDS,
    OPT_FIELDS,
    OPT_ALL_FIELDS,
    OPT_LUA_FILE,
    OPT_NUM_RECS,
    OPT_START_REC_NUM,
    OPT_END_REC_NUM,
    OPT_TAIL_RECS,
    OPT_DRY_RUN,
    OPT_PLUGIN,
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
    {"help-fields",         NO_ARG,       0, OPT_HELP_FIELDS},
    {"fields",              REQUIRED_ARG, 0, OPT_FIELDS},
    {"all-fields",          NO_ARG,       0, OPT_ALL_FIELDS},
    {"lua-file",            REQUIRED_ARG, 0, OPT_LUA_FILE},
    {"num-recs",            REQUIRED_ARG, 0, OPT_NUM_RECS},
    {"start-rec-num",       REQUIRED_ARG, 0, OPT_START_REC_NUM},
    {"end-rec-num",         REQUIRED_ARG, 0, OPT_END_REC_NUM},
    {"tail-recs",           REQUIRED_ARG, 0, OPT_TAIL_RECS},
    {"dry-run",             NO_ARG,       0, OPT_DRY_RUN},
    {"plugin",              REQUIRED_ARG, 0, OPT_PLUGIN},
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
    "Describe each field and exit. Def. no",
    NULL, /* generated dynamically */
    ("Print all known fields to the output. Conflicts with\n"
     "\tthe --fields switch"),
    ("Load the named Lua file during set-up.  Switch may be\n"
     "\trepeated to load multiple files. Def. None"),
    "Print no more than this number of records. Def. Unlimited",
    ("Start printing with this record number, where 1 is the\n"
     "\tfirst record.  Def. 1.  Conflicts with --tail-recs"),
    ("End printing with this record number; must be greater\n"
     "\tthan --start-rec-num.  Def. Final record. Conflicts with --tail-recs"),
    ("Start printing this number of records from the end of the\n"
     "\tinput. Def. None. Conflicts with --start-rec-num and --end-rec-num"),
    "Parse options and print column titles only. Def. No",
    ("Load given plug-in to add fields. Switch may be repeated to\n"
     "\tload multiple plug-ins. Def. None"),
    "Print sensor as an integer. Def. Sensor name",
    "Print TCP Flags as an integer. Def. No",
    "Do not print column headers. Def. Print titles.",
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
static void usageFields(FILE *fh);
static void helpFields(FILE *fh);
static lua_State *appLuaCreateState(void);
static int  createStringmaps(void);
static int  selectFieldsDefault(void);
static int  selectFieldsAll(void);
static int  parseFields(const char* opt_arg);
static int  appAddPluginField(const sk_stringmap_entry_t *map_entry);


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
    ("[SWITCHES] [FILES]\n"                                                   \
     "\tPrint SiLK Flow records in a |-delimited, columnar, human-readable\n" \
     "\tformat.  Use --fields to select columns to print. When no files are\n"\
     "\tgiven on the command line, flows are read from the standard input.\n")

    FILE *fh = USAGE_FH;
    int i;

    fprintf(fh, "%s %s", skAppName(), USAGE_MSG);
    fprintf(fh, "\nSWITCHES:\n");
    skOptionsDefaultUsage(fh);

    for (i = 0; appOptions[i].name; ++i) {
        fprintf(fh, "--%s %s. ", appOptions[i].name,
                SK_OPTION_HAS_ARG(appOptions[i]));
        switch (appOptions[i].val) {
          case OPT_FIELDS:
            /* Dynamically build the help */
            usageFields(fh);
            break;
          case OPT_PLUGIN:
            /* Simple static help text from the appHelp array */
            fprintf(fh, "%s\n", appHelp[i]);
            /* Print the timestamp format usage */
            skOptionsTimestampFormatUsage(fh);
            /* Print the IP address format usage */
            skOptionsIPFormatUsage(fh);
            break;
          default:
            /* Simple static help text from the appHelp array */
            fprintf(fh, "%s\n", appHelp[i]);
            break;
        }
    }

    skOptionsCtxOptionsUsage(optctx, fh);
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

    /* Plugin teardown */
    skPluginRunCleanup(SKPLUGIN_APP_CUT);
    skPluginTeardown();

    /* close copy input stream */
    skOptionsCtxCopyStreamClose(optctx, skAppPrintErr);

    /* close the output file or process */
    if (output.of_name) {
        skFileptrClose(&output, &skAppPrintErr);
    }

    sk_vector_destroy(active_plugins);

    /* destroy output */
    sk_formatter_destroy(fmtr);

    /* destroy field map */
    if (key_field_map != NULL) {
        skStringMapDestroy(key_field_map);
        key_field_map = NULL;
    }

    if (L) {
        /* invoke the teardown functions registered in Lua */
        lua_rawgeti(L, LUA_REGISTRYINDEX, reg_ref.invoke_teardown);
        rv = lua_pcall(L, 0, 1, 0);
        if (rv != LUA_OK) {
            skAppPrintErr("%s", lua_tostring(L, -1));
            lua_pop(L, 1);
        } else if (lua_type(L, -1) == LUA_TNIL) {
            lua_pop(L, 1);
        } else {
            /* FIXME: go through entries in list and print any error
             * messages */
            lua_pop(L, 1);
        }
    }

    sk_sidecar_destroy(&sidecar);

    free(tail_buf);
    tail_buf = NULL;

    sk_lua_closestate(L);

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
void
appSetup(
    int                 argc,
    char              **argv)
{
    SILK_FEATURES_DEFINE_STRUCT(features);
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

    /* initialize globals */
    memset(&cut_opts, 0, sizeof(cut_opts));
    delimiter = '|';

    memset(&output, 0, sizeof(output));
    output.of_fp = stdout;

    optctx_flags = (SK_OPTIONS_CTX_INPUT_SILK_FLOW | SK_OPTIONS_CTX_ALLOW_STDIN
                    | SK_OPTIONS_CTX_XARGS | SK_OPTIONS_CTX_PRINT_FILENAMES
                    | SK_OPTIONS_CTX_COPY_INPUT | SK_OPTIONS_CTX_IPV6_POLICY);

    /* Initialize plugin library */
    skPluginSetup(1, SKPLUGIN_APP_CUT);

    /* register the options */
    if (skOptionsCtxCreate(&optctx, optctx_flags)
        || skOptionsCtxOptionsRegister(optctx)
        || skOptionsRegister(appOptions, &appOptionsHandler, NULL)
        || sksiteOptionsRegister(SK_SITE_FLAG_CONFIG_FILE)
        || skOptionsTimestampFormatRegister(&time_flags, time_register_flags)
        || skOptionsIPFormatRegister(&ip_format))
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

    /* try to load hard-coded plugins */
    for (j = 0; app_static_plugins[j].name; ++j) {
        skPluginAddAsPlugin(app_static_plugins[j].name,
                            app_static_plugins[j].setup_fn);
    }
    for (j = 0; app_plugin_names[j]; ++j) {
        skPluginLoadPlugin(app_plugin_names[j], 0);
    }

    L = appLuaCreateState();
    sk_sidecar_create(&sidecar);

    /* parse options */
    rv = skOptionsCtxOptionsParse(optctx, argc, argv);
    if (rv < 0) {
        skAppUsage();         /* never returns */
    }

    /* create flow iterator to read the records from the stream */
    flowiter = skOptionsCtxCreateFlowIterator(optctx);
    sk_flow_iter_set_max_readers(flowiter, 1);

    if (help_fields) {
        helpFields(USAGE_FH);
        exit(EXIT_SUCCESS);
    }

    /* Not having site config is allowed */
    sksiteConfigure(0);

    /* Create the --fields */
    if (createStringmaps()) {
        exit(EXIT_FAILURE);
    }

    /* Create the formatter */
    fmtr = sk_formatter_create();

    /* Create vector to hold list of plugins */
    active_plugins = sk_vector_create(sizeof(skplugin_field_t *));

    /* Parse the --fields or --all-fields argument, or use the default
     * fields */
    if (fields_arg != NULL) {
        if (parseFields(fields_arg)) {
            exit(EXIT_FAILURE);
        }
    } else if (all_fields) {
        if (selectFieldsAll()) {
            exit(EXIT_FAILURE);
        }
    } else if (selectFieldsDefault()) {
        skAppPrintErr("Cannot set default output fields");
        exit(EXIT_FAILURE);
    }

    num_plugins = sk_vector_get_count(active_plugins);
    if (0 == num_plugins) {
        sk_vector_destroy(active_plugins);
        active_plugins = NULL;
    }

    /* check limits; main loop uses 'num_recs' with either 'skip_recs'
     * or 'tail_recs' */
    if (tail_recs) {
        if (start_rec_num || end_rec_num) {
            skAppPrintErr("May not use --%s when --%s or --%s is specified",
                          appOptions[OPT_TAIL_RECS].name,
                          appOptions[OPT_START_REC_NUM].name,
                          appOptions[OPT_END_REC_NUM].name);
            exit(EXIT_FAILURE);
        }
        if (num_recs >= tail_recs) {
            /* cannot print more than 'tail_recs' records */
            num_recs = 0;
        }
    }
    if (start_rec_num) {
        skip_recs = start_rec_num - 1;
    }
    if (end_rec_num) {
        if (end_rec_num < start_rec_num) {
            skAppPrintErr(("The %s is less than the %s: "
                           "%" PRIu64 " < %" PRIu64),
                          appOptions[OPT_END_REC_NUM].name,
                          appOptions[OPT_START_REC_NUM].name,
                          end_rec_num, start_rec_num);
            exit(EXIT_FAILURE);
        }
        if (start_rec_num) {
            /* unconditionally set num_recs to their difference */
            num_recs = end_rec_num - skip_recs;
        } else if ((num_recs > 0) && (num_recs < end_rec_num)) {
            skip_recs = end_rec_num - num_recs;
        } else {
            num_recs = end_rec_num;
        }
    }

    /* make certain stdout is not being used for multiple outputs */
    if (skOptionsCtxCopyStreamIsStdout(optctx)) {
        if ((NULL == output.of_name)
            || (0 == strcmp(output.of_name, "-"))
            || (0 == strcmp(output.of_name, "stdout")))
        {
            skAppPrintErr("May not use stdout for multiple output streams");
            exit(EXIT_FAILURE);
        }
    }

    /* set properties on the formatter */
    sk_formatter_set_delimeter(fmtr, delimiter);
    if (skOptionsCtxGetIPv6Policy(optctx) < SK_IPV6POLICY_MIX) {
        sk_formatter_set_assume_ipv4_ips(fmtr);
    }
    sk_formatter_set_default_ipaddr_format(fmtr, (skipaddr_flags_t)ip_format);
    sk_formatter_set_default_timestamp_format(fmtr, time_flags);

    if (cut_opts.no_columns) {
        sk_formatter_set_no_columns(fmtr);
    }
    if (cut_opts.no_final_delimiter) {
        sk_formatter_set_no_final_delimeter(fmtr);
    }
    sk_formatter_finalize(fmtr);

    /* allocate the buffer for 'tail_recs' */
    if (tail_recs) {
        tail_buf = (rwRec*)malloc(tail_recs * sizeof(rwRec));
        if (NULL == tail_buf) {
            skAppPrintErr("Unable to create buffer for %" PRIu64 " records",
                          tail_recs);
            exit(EXIT_FAILURE);
        }
        rwRecInitializeArray(tail_buf, L, tail_recs);
    }

    /* open the --output-path.  the 'of_name' member is NULL if user
     * didn't get an output-path.  only invoke the pager when an
     * explicit --output-path was not given. */
    if (output.of_name) {
        rv = skFileptrOpen(&output, SK_IO_WRITE);
        if (rv) {
            skAppPrintErr("Cannot open '%s': %s",
                          output.of_name, skFileptrStrerror(rv));
            exit(EXIT_FAILURE);
        }
    } else {
        /* Invoke the pager */
        rv = skFileptrOpenPager(&output, pager);
        if (rv && rv != SK_FILEPTR_PAGER_IGNORED) {
            skAppPrintErr("Unable to invoke pager");
        }
    }

    /* if dry-run, print the column titles and exit */
    if (cut_opts.dry_run) {
        printTitle();
        appTeardown();
        exit(EXIT_SUCCESS);
    }

    /* open the --copy-input stream */
    if ((rv = skOptionsCtxOpenStreams(optctx, &skAppPrintErr))) {
        exit(EXIT_FAILURE);
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
    int rv;

    switch ((appOptionsEnum)opt_index) {
      case OPT_HELP_FIELDS:
        help_fields = 1;
        break;

      case OPT_FIELDS:
        /* only one of --fields or --all-fields is allowed */
        if (fields_arg) {
            skAppPrintErr("Invalid %s: Switch used multiple times",
                          appOptions[opt_index].name);
            return 1;
        }
        if (all_fields) {
            skAppPrintErr("Invalid %s: The --%s switch was already given",
                          appOptions[opt_index].name,
                          appOptions[OPT_ALL_FIELDS].name);
            return 1;
        }
        fields_arg = opt_arg;
        break;

      case OPT_ALL_FIELDS:
        if (fields_arg) {
            skAppPrintErr("Invalid %s: The --%s switch was already given",
                          appOptions[opt_index].name,
                          appOptions[OPT_FIELDS].name);
            return 1;
        }
        all_fields = 1;
        break;

      case OPT_LUA_FILE:
        /* get the 'load_lua_file' function from from the registry
         * push the argument, and run the function */
        lua_rawgeti(L, LUA_REGISTRYINDEX, reg_ref.load_lua_file);
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

      case OPT_NUM_RECS:
        rv = skStringParseUint64(&num_recs, opt_arg, 0, 0);
        if (rv) {
            goto PARSE_ERROR;
        }
        break;

      case OPT_START_REC_NUM:
        rv = skStringParseUint64(&start_rec_num, opt_arg, 1, 0);
        if (rv) {
            goto PARSE_ERROR;
        }
        break;

      case OPT_END_REC_NUM:
        rv = skStringParseUint64(&end_rec_num, opt_arg, 0, 0);
        if (rv) {
            goto PARSE_ERROR;
        }
        break;

      case OPT_TAIL_RECS:
        rv = skStringParseUint64(&tail_recs, opt_arg, 1, 0);
        if (rv) {
            goto PARSE_ERROR;
        }
        break;

      case OPT_NO_TITLES:
        cut_opts.no_titles = 1;
        break;

      case OPT_NO_COLUMNS:
        cut_opts.no_columns = 1;
        break;

      case OPT_NO_FINAL_DELIMITER:
        cut_opts.no_final_delimiter = 1;
        break;

      case OPT_COLUMN_SEPARATOR:
        delimiter = opt_arg[0];
        break;

      case OPT_DELIMITED:
        cut_opts.no_columns = 1;
        cut_opts.no_final_delimiter = 1;
        if (opt_arg) {
            delimiter = opt_arg[0];
        }
        break;

      case OPT_PLUGIN:
        if (skPluginLoadPlugin(opt_arg, 1) != 0) {
            skAppPrintErr("Unable to load %s as a plugin", opt_arg);
            return 1;
        }
        break;

      case OPT_INTEGER_SENSORS:
        cut_opts.integer_sensors = 1;
        break;

      case OPT_INTEGER_TCP_FLAGS:
        cut_opts.integer_tcp_flags = 1;
        break;

      case OPT_DRY_RUN:
        cut_opts.dry_run = 1;
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

    return 0;                     /* OK */

  PARSE_ERROR:
    skAppPrintErr("Invalid %s '%s': %s",
                  appOptions[opt_index].name, opt_arg,
                  skStringParseStrerror(rv));
    return 1;
}


/*
 *    A helper function for appLuaCreateState().
 *
 *    This function expects the table of functions exported by
 *    rwcut.lua to be at the top of the stack.  This function finds
 *    the function named 'function_name', inserts it into the Lua
 *    registry, and sets the reference of 'storage_location' to that
 *    Lua reference.
 *
 *    In practice, the storage_location is the member of the reg_ref
 *    structure that matches the string in 'function_name'.
 */
static void
appLuaAddFunctionToRegistry(
    lua_State          *S,
    const char         *function_name,
    int                *storage_location)
{
    assert(LUA_TTABLE == lua_type(S, -1));

    lua_getfield(S, -1, function_name);
    assert(LUA_TFUNCTION == lua_type(S, -1));
    *storage_location = luaL_ref(S, LUA_REGISTRYINDEX);
}

/*
 *    Create a Lua state and load the (compiled) contents of
 *    "rwcut.lua" into that state.  Set some functions defined in
 *    rwcut.lua as Lua globals, store others in the Lua registry and
 *    store their locations in the reg_ref global structure.
 */
static lua_State *
appLuaCreateState(
    void)
{
#define ADD_REGISTRY_FUNC(l_state, l_name)                              \
    appLuaAddFunctionToRegistry(l_state, #l_name, &( reg_ref. l_name ))

    /* functions defined in the export table to make global so they
     * may be called by code in --lua-file */
    const char *global_fns[] = {
        "register_field",
        "register_teardown",
        "add_sidecar_field",
        NULL
    };
    lua_State *S;
    size_t i;
    int rv;

    /* initialize Lua */
    S = sk_lua_newstate();

    /* load and run the the initialization code in rwcut.lua.  The
     * return value is a table of functions. */
    rv = luaL_loadbufferx(S, (const char*)rwcut_lua,
                          sizeof(rwcut_lua), "rwcut.lua", "b");
    if (LUA_OK == rv) {
        rv = lua_pcall(S, 0, 1, 0);
    }
    if (rv != LUA_OK) {
        skAppPrintErr("Lua initialization failed: %s", lua_tostring(S, -1));
        exit(EXIT_FAILURE);
    }
    assert(LUA_TTABLE == lua_type(S, -1));

    /* add functions from the export table to the global namespace */
    for (i = 0; global_fns[i] != NULL; ++i) {
        lua_getfield(S, -1, global_fns[i]);
        assert(LUA_TFUNCTION == lua_type(S, -1));
        lua_setglobal(S, global_fns[i]);
    }

    /* add functions from the export table to the Lua registry and
     * store the inedexes in the reg_ref structure */
    ADD_REGISTRY_FUNC(S, load_lua_file);
    ADD_REGISTRY_FUNC(S, activate_field);
    ADD_REGISTRY_FUNC(S, get_sidecar);
    ADD_REGISTRY_FUNC(S, count_functions);
    ADD_REGISTRY_FUNC(S, apply_sidecar);
    ADD_REGISTRY_FUNC(S, invoke_teardown);

    /* Done with the table of functions. */
    lua_pop(S, 1);
    assert(0 == lua_gettop(S));

    return S;
}


/*
 *    Update the formatter to print the standard SiLK field 'id'.
 */
static void
addSilkField(
    uint32_t            id)
{
    sk_formatter_field_t *field;

    field = sk_formatter_add_silk_field(fmtr, (rwrec_field_id_t)id);
    if (NULL == field) {
        skAppPrintErr("Cannot add field %" PRIu32 " to output", id);
        exit(EXIT_FAILURE);
    }
    switch ((rwrec_field_id_t)id) {
      case RWREC_FIELD_FLAGS:
      case RWREC_FIELD_INIT_FLAGS:
      case RWREC_FIELD_REST_FLAGS:
        if (cut_opts.integer_tcp_flags) {
            sk_formatter_field_set_number_format(fmtr, field, 10);
        } else if (!cut_opts.no_columns) {
            sk_formatter_field_set_space_padded(fmtr, field);
        }
        break;

      case RWREC_FIELD_TCP_STATE:
        if (!cut_opts.no_columns) {
            sk_formatter_field_set_space_padded(fmtr, field);
        }
        break;

      case RWREC_FIELD_SID:
        if (cut_opts.integer_sensors) {
            sk_formatter_field_set_number_format(fmtr, field, 10);
        }
        break;

      default:
        break;
    }
}

/*
 *  status = selectFieldsDefault();
 *
 *    Set the global formatter to print rwcut's default columns.
 *    Return 0 on success or -1 on memory error.
 */
static int
selectFieldsDefault(
    void)
{
    uint32_t i;

    /* set up the default fields for rwcut */
    for (i = 0; i <= RWCUT_LAST_DEFAULT_FIELD; ++i) {
        addSilkField(i);
    }
    return 0;
}


/*
 *  status = selectFieldsAll();
 *
 *    Set the global formatter to print all known fields---both
 *    built-in and from plug-ins.  Return 0 on success or -1 on memory
 *    error.
 */
static int
selectFieldsAll(
    void)
{
    int rv = -1;
    uint32_t i;
    FILE *old_errs = NULL;
    sk_bitmap_t *field_seen = NULL;
    sk_dll_iter_t node;
    sk_stringmap_entry_t *sm_entry;

    /* create a bitmap to keep track of the fields we've added */
    if (skBitmapCreate(&field_seen, 65536)) {
        goto END;
    }

    /* add all built-in fields to the formatter */
    for (i = 0; i < RWREC_FIELD_ID_COUNT; ++i) {
        addSilkField(i);
    }

    /* disable error output to avoid seeing warnings from plug-ins */
    old_errs = skAppSetErrStream(NULL);

    /* add the fields from every plug-in */
    skDLLAssignIter(&node, key_field_map);
    while (skDLLIterForward(&node, (void **)&sm_entry) == 0) {
        if ((NULL != sm_entry->userdata)
            && !skBitmapGetBit(field_seen, sm_entry->id))
        {
            /* ignore errors */
            (void)appAddPluginField(sm_entry);
            skBitmapSetBit(field_seen, sm_entry->id);
        }
    }

    /* re-enable errors */
    skAppSetErrStream(old_errs);

    /* successful */
    rv = 0;

  END:
    if (field_seen) {
        skBitmapDestroy(&field_seen);
    }

    return rv;
}


/*
 *  status = parseFields(fields_string);
 *
 *    Parse the user's option for the --fields switch and add the
 *    fields to the formatter.  Return 0 on success; -1 on failure.
 */
static int
parseFields(
    const char         *field_string)
{
    const sk_sidecar_elem_t *sc_elem;
    sk_stringmap_iter_t *sm_iter = NULL;
    sk_stringmap_entry_t *sm_entry;
    char *errmsg;
    int rv = -1;

    if (field_string == NULL || field_string[0] == '\0') {
        skAppPrintErr("Missing --fields value");
        return -1;
    }

    /* parse the field-list */
    if (skStringMapParse(key_field_map, field_string, SKSTRINGMAP_DUPES_KEEP,
                         &sm_iter, &errmsg))
    {
        skAppPrintErr("Invalid %s: %s",
                      appOptions[OPT_FIELDS].name, errmsg);
        goto END;
    }

    while (skStringMapIterNext(sm_iter, &sm_entry, NULL) == SK_ITERATOR_OK) {
        if (sm_entry->id & PLUGIN_FIELD_BIT) {
            /* field comes from a plug-in */
            if (appAddPluginField(sm_entry)) {
                skAppPrintErr("Cannot add field %s from plugin",
                              sm_entry->name);
                goto END;
            }
        } else if (sm_entry->id & (SIDECAR_FIELD_BIT | SC_LUA_FIELD_BIT)) {
            /* field comes from a sidecar */
            sc_elem = (sk_sidecar_elem_t *)sm_entry->userdata;
            if (sm_entry->id & SC_LUA_FIELD_BIT) {
                /* field comes from a sidecar added by --lua-file */
                lua_rawgeti(L, LUA_REGISTRYINDEX, reg_ref.activate_field);
                lua_pushstring(L, sm_entry->name);
                rv = lua_pcall(L, 1, 0, 0);
                if (rv != LUA_OK) {
                    skAppPrintErr(
                        "Unable to activate field %s defined in Lua: %s",
                        sm_entry->name, lua_tostring(L, -1));
                    lua_pop(L, 1);
                    assert(0 == lua_gettop(L));
                    goto END;
                }
            }
            if (!sk_formatter_add_field(
                    fmtr, sm_entry->name, 1+strlen(sm_entry->name),
                    sk_sidecar_elem_get_data_type(sc_elem),
                    sk_sidecar_elem_get_ipfix_ident(sc_elem)))
            {
                skAppPrintErr("Cannot add field %s to stream",
                              sm_entry->name);
                goto END;
            }
        } else {
            assert(NULL == sm_entry->userdata);
            /* field is built-in */
            addSilkField(sm_entry->id);
        }
    }

    /* determine the number of sidecar fields defined in --lua-file;
     * the count is not really important---we only need to know
     * whether to call the function that adds the sidecar fields. */
    lua_rawgeti(L, LUA_REGISTRYINDEX, reg_ref.count_functions);
    rv = lua_pcall(L, 0, 1, 0);
    if (rv != LUA_OK) {
        skAppPrintErr("Unable to get number of function: %s",
                      lua_tostring(L, -1));
        lua_pop(L, 1);
        assert(0 == lua_gettop(L));
        goto END;
    }
    num_sidecar_adds = lua_tointeger(L, -1);
    lua_pop(L, 1);
    assert(0 == lua_gettop(L));

    /* successful */
    rv = 0;

  END:
    skStringMapIterDestroy(sm_iter);
    return rv;
}


/*
 *  usageFields(fh);
 *
 *    Print the usage (help) message for --fields to the 'fh' file pointer
 */
static void
usageFields(
    FILE               *fh)
{
    uint32_t i;

    /* Create the string map for --fields */
    createStringmaps();

    fprintf(fh, ("Print these fields in the output. Specify fields as a\n"
                 "\tcomma-separated list of names, IDs, and/or ID-ranges.\n"));

    skStringMapPrintUsage(key_field_map, fh, 4);

    /* Print default fields */
    i = 0;
    fprintf(fh, "\tDef. %s", skStringMapGetFirstName(key_field_map, i));
    for (++i; i <= RWCUT_LAST_DEFAULT_FIELD; ++i) {
        fprintf(fh, ",%s", skStringMapGetFirstName(key_field_map, i));
    }
    fprintf(fh, "\n");
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
 *  ok = createStringmaps();
 *
 *    Create the string-map to assist in parsing the --fields switch.
 */
static int
createStringmaps(
    void)
{
    skplugin_field_iter_t       pi_iter;
    skplugin_err_t              pi_err;
    skplugin_field_t           *pi_field;
    sk_stringmap_status_t       sm_err;
    sk_stringmap_entry_t        sm_entry;
    const char                **field_names;
    const char                **name;
    uint32_t                    max_id;
    sk_sidecar_t              **sc;
    sk_sidecar_iter_t           sc_iter;
    const sk_sidecar_elem_t    *sc_elem;
    char                        buf[PATH_MAX];
    size_t                      buflen;

    /* initialize string-map of field identifiers using the standard
     * rwRec fields */
    if (skStringMapCreate(&key_field_map)
        || skRwrecAppendFieldsToStringMap(key_field_map))
    {
        skAppPrintErr("Unable to setup fields stringmap");
        return -1;
    }
    max_id = RWREC_FIELD_ID_COUNT - 1;

    /* add sidecar fields defined in the input files */
    if (flowiter) {
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
    pi_err = skPluginFieldIteratorBind(&pi_iter, SKPLUGIN_APP_CUT, 1);
    if (pi_err != SKPLUGIN_OK) {
        assert(pi_err == SKPLUGIN_OK);
        skAppPrintErr("Unable to bind plugin field iterator");
        return -1;
    }
    while (skPluginFieldIteratorNext(&pi_iter, &pi_field)) {
        skPluginFieldName(pi_field, &field_names);
        ++max_id;

        /* Add fields to the key_field_map */
        for (name = field_names; *name; name++) {
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

    /* add sidecar fields defined by --lua-file */
    lua_rawgeti(L, LUA_REGISTRYINDEX, reg_ref.get_sidecar);
    lua_call(L, 0, 1);
    switch (lua_type(L, -1)) {
      case LUA_TNIL:
        lua_pop(L, 1);
        break;
      case LUA_TSTRING:
        skAppPrintErr("Error creating sidecar from registered fields: %s",
                      lua_tostring(L, -1));
        lua_pop(L, 1);
        return -1;
      case LUA_TUSERDATA:
        sc = sk_lua_tosidecar(L, -1);
        assert(sc && *sc);
        sk_sidecar_iter_bind(*sc, &sc_iter);
        while (sk_sidecar_iter_next(&sc_iter, &sc_elem) == SK_ITERATOR_OK) {
            buflen = sizeof(buf);
            sk_sidecar_elem_get_name(sc_elem, buf, &buflen);
            ++max_id;
            sm_entry.name = buf;
            sm_entry.id = SC_LUA_FIELD_BIT | max_id;
            sm_entry.userdata = sc_elem;
            sm_entry.description = NULL;
            sm_err = skStringMapAddEntries(key_field_map, 1, &sm_entry);
            if (sm_err) {
                skAppPrintErr("Cannot add field '%s' from sidecar: %s",
                              buf, skStringMapStrerror(sm_err));
            }
        }
        lua_pop(L, 1);
        break;
      default:
        skAbortBadCase(lua_type(L, -1));
    }
    assert(0 == lua_gettop(L));

    return 0;
}


#if 0
static void
appPluginGetTitle(
    char               *text_buf,
    size_t              text_buf_size,
    void               *cb_data)
{
    skplugin_field_t *pi_field = (skplugin_field_t*)cb_data;
    const char *title;

    skPluginFieldTitle(pi_field, &title);
    strncpy(text_buf, title, text_buf_size);
    text_buf[text_buf_size-1] = '\0';
}


static int
appPluginGetValue(
    const rwRec        *rwrec,
    char               *text_buf,
    size_t              text_buf_size,
    void               *cb_data)
{
    skplugin_field_t *pi_field = (skplugin_field_t*)cb_data;
    skplugin_err_t pi_err;

    pi_err = skPluginFieldRunRecToTextFn(pi_field, text_buf, text_buf_size,
                                         rwrec, NULL);
    if (pi_err != SKPLUGIN_OK) {
        const char **name;
        skPluginFieldName(pi_field, &name);
        skAppPrintErr(("Plugin-based field %s failed converting to text "
                       "with error code %d"), name[0], pi_err);
        exit(EXIT_FAILURE);
    }
    return 0;
}
#endif  /* 0 */


/*
 *  status = appAddPluginField(sm_entry);
 *
 *    Add callbacks to the global 'fmtr' to print a field that
 *    comes from a plug-in.
 *
 *    Returns 0 on success, or -1 for a memory allocation error or if
 *    the plug-in that provides the field numbered 'field_id' cannot
 *    be found.
 */
static int
appAddPluginField(
    const sk_stringmap_entry_t *sm_entry)
{
    sk_formatter_field_t *fmtr_field;
    const char **field_names;
    skplugin_field_t *pi_field;
    skplugin_err_t pi_err;
    const char *title;
    size_t text_width;

    pi_field = (skplugin_field_t*)sm_entry->userdata;
    assert(pi_field);

    /* activate the plugin (so cleanup knows about it) */
    pi_err = skPluginFieldActivate(pi_field);
    if (pi_err != SKPLUGIN_OK) {
        return -1;
    }
    /* initialize this field */
    pi_err = skPluginFieldRunInitialize(pi_field);
    if (pi_err != SKPLUGIN_OK) {
        return -1;
    }

    sk_vector_append_value(active_plugins, &pi_field);

    /* get the names and the title */
    skPluginFieldName(pi_field, &field_names);
    skPluginFieldTitle(pi_field, &title);

    /* get the text width for this field */
    pi_err = skPluginFieldGetLenText(pi_field, &text_width);
    if (pi_err != SKPLUGIN_OK) {
        return -1;
    }
    if (0 == text_width) {
        skAppPrintErr("Plug-in field '%s' has a textual width of 0", title);
        return -1;
    }

    fmtr_field = (sk_formatter_add_field(
                      fmtr, field_names[0], 1+strlen(field_names[0]),
                      SK_SIDECAR_STRING, 0));
    sk_formatter_field_set_min_width(fmtr, fmtr_field, text_width);
    sk_formatter_field_set_title(fmtr, fmtr_field, title);
    return 0;
}


void
printTitle(
    void)
{
    char *fmtr_buf = NULL;
    size_t len;

    if (!cut_opts.no_titles) {
        len = sk_formatter_fill_title_buffer(fmtr, &fmtr_buf);
        if (!fwrite(fmtr_buf, len, 1, output.of_fp)) {
            skAppPrintErr("Could not write titles");
            exit(EXIT_FAILURE);
        }
    }
}


/*
 *    If 'in_rwrec' and 'out_rwrec' point to the same location, this
 *    function modifies 'out_rwrec' if it determines there is work to
 *    do.  If the parameters point to different locations and there is
 *    work to do, a copy of the record in 'in_rwrec' is made, stored
 *    at 'out_rwrec' prior to modifying 'out_rwrec'.
 *
 *    If there are no plug-in fields or there are plug-in fields but
 *    the record does not have a Lua state object, make out_rwrec
 *    contain the contents of in_rwrec, and return 1.
 *
 *    Create a sidecar (Lua table) on 'out_rwrec' if none exists, add
 *    the plug-in fields to the Lua table as sidecar data, and return
 *    0.
 */
void
addPluginFields(
    rwRec              *rwrec)
{
    skplugin_field_t *pi_field;
    skplugin_err_t pi_err;
    const char **field_names;
    char text_buf[4096];
    size_t i;
    int rv;
    int ref;

    assert(rwrec->lua_state == L);
    if (0 == num_plugins && 0 == num_sidecar_adds) {
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
        sk_vector_get_value(active_plugins, i, &pi_field);

        skPluginFieldName(pi_field, &field_names);

        pi_err = (skPluginFieldRunRecToTextFn(
                      pi_field, text_buf, sizeof(text_buf), rwrec, NULL));
        if (pi_err != SKPLUGIN_OK) {
            skAppPrintErr(("Plugin-based field %s failed converting to text "
                           "with error code %d"), field_names[0], pi_err);
            exit(EXIT_FAILURE);
        }

        lua_pushstring(L, text_buf);
        lua_setfield(L, -2, field_names[0]);
    }

    assert(LUA_TTABLE == lua_type(L, -1));

    if (ref == LUA_NOREF) {
        /* copied table is at the top of the stack; get a reference to
         * it and remove it */
        rwRecSetSidecar(rwrec, luaL_ref(L, LUA_REGISTRYINDEX));
    } else {
        lua_pop(L, 1);
    }

    if (num_sidecar_adds) {
        rwRec *lua_rec;
        lua_rawgeti(L, LUA_REGISTRYINDEX, reg_ref.apply_sidecar);
        lua_rec = sk_lua_push_rwrec(L, NULL);
        rwRecCopy(lua_rec, rwrec, SK_RWREC_COPY_FIXED);
        lua_rec->sidecar = rwRecGetSidecar(rwrec);
        rv = lua_pcall(L, 1, 1, 0);
        if (LUA_OK != rv) {
            skAppPrintErr("%s", lua_tostring(L, -1));
            lua_pop(L, 1);
            assert(0 == lua_gettop(L));
            exit(EXIT_FAILURE);
        }
        lua_rec->sidecar = LUA_NOREF;
        lua_pop(L, 1);
    }

    assert(0 == lua_gettop(L));
}


/*
** Local Variables:
** mode:c
** indent-tabs-mode:nil
** c-basic-offset:4
** End:
*/
