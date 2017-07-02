/*
** Copyright (C) 2003-2017 by Carnegie Mellon University.
**
** @OPENSOURCE_LICENSE_START@
** See license information in ../../LICENSE.txt
** @OPENSOURCE_LICENSE_END@
*/

/*
**  rwflowpack-config.c
**
**    Set up rwflowpack.
*/

#include <silk/silk.h>

RCSIDENT("$SiLK: rwflowpack-config.c 21315f05de74 2017-06-26 20:03:25Z mthomas $");

#include <silk/skdaemon.h>
#include <silk/skipfixcert.h>
#include <silk/skplugin.h>
#include <silk/skpolldir.h>
#include "rwflowpack_priv.h"
#include "stream-cache.h"


/*
 *    MAX FILE HANDLE NOTES
 *
 *    In response to attempts to use 100+ probes that polled
 *    directories which caused us to run out of file handles, we tried
 *    to make some of the code smarter about the number of files
 *    handles we use.
 *
 *    However, currently we only look at polldir numbers, and we do
 *    not consider the number of file handles that we have open to
 *    read from the network.  One issue is we don't know how many that
 *    is until after we start.
 *
 *    We could be smarter and set the number of poll dir handles after
 *    we see how many polldirs we are actually using.
 *
 *    We could use sysconf(_SC_OPEN_MAX) to get the max number of file
 *    handles available and set our values based on that.
 */


/* MACROS AND DATA TYPES */

/* Where to write usage (--help) information */
#define USAGE_FH stdout

/* The maximum number of open output files to support, which is the
 * size of the stream_cache.  This default may be changed with the
 * --file-cache-size switch.  Also specify the minimum size of stream
 * cache. */
#define STREAM_CACHE_SIZE 128
#define STREAM_CACHE_MIN  4

/* These next two values are used when rwflowpack is using probes that
 * poll directories, and they specify fractions of the
 * stream_cache_size.
 *
 * The first is the maximum number of input files to read from
 * simultaneously.  The second is the maximum number of simultaneous
 * directory polls to perform.
 *
 * In addition, specify the absolute minimum for those values. */
#define INPUT_FILEHANDLES_FRACTION   (1.0/8.0)
#define POLLDIR_FILEHANDLES_FRACTION (1.0/16.0)

#define INPUT_FILEHANDLES_MIN      2
#define POLLDIR_FILEHANDLES_MIN    1

/* How often, in seconds, to flush the files in the stream_cache.
 * This default may be changed with the --flush-timeout switch. */
#define FLUSH_TIMEOUT  120

/* Number of seconds to wait between polling the incoming directory or
 * the poll-directory's specified in the sensor.conf file.  This
 * default may be changed with the --polling-interval switch. */
#define POLLING_INTERVAL 15

/* minimum number of bytes to leave free on the data disk.  File
 * distribution will stop when the freespace on the disk reaches or
 * falls below this mark.  This value is parsed by
 * skStringParseHumanUint64(). */
#define DEFAULT_FREESPACE_MINIMUM_BYTES   "1g"

/* maximum percentage of disk space to take */
#define DEFAULT_USEDSPACE_MAXIMUM_PERCENT  ((double)98.00)

/* default maximum file size for flowcap when none-specified */
#define DEFAULT_MAX_FILE_SIZE  "10m"

/* default number of appender threads to run */
#define DEFAULT_APPENDER_COUNT  1

/*
 *  Specify the maximum size (in terms of RECORDS) of the buffer used
 *  to hold records that have been read from the flow-source but not
 *  yet processed.  This value is the number of records as read from
 *  the wire (e.g., PDUs for a NetFlow v5 probe) per PROBE.  The
 *  maximum memory per NetFlow v5 probe will be BUF_REC_COUNT * 1464.
 *  The maximum memory per IPFIX or NetFlow v9 probe will be
 *  BUF_REC_COUNT * 52 (or BUF_REC_COUNT * 88 for IPv6-enabled SiLK).
 *  If records are processed as quickly as they are read, the normal
 *  memory use per probe will be CIRCBUF_CHUNK_MAX_SIZE bytes.
 */
#define DEFAULT_CIRCBUF_SIZE  (1 << 15)

/* EXPORTED VARIABLE DEFINITION */

/* Packer configuration file */
const char *packer_config_file = NULL;

/* IP Set cache */
sk_rbtree_t *ipset_cache = NULL;

/* LOCAL VARIABLES */

/* The index of the first Output Mode */
static const io_mode_t first_output_mode = OUTPUT_LOCAL_STORAGE;

/* Keep in sync with values in io_mode_t enumeration */
static const struct available_modes_st {
    const io_mode_t iomode;
    const char     *name;
    const char     *title;
    const char     *description;
} available_modes[NUM_MODES] = {
    {INPUT_STREAM, "stream", "Stream Input",
     ("\tRead flow data from the network and/or poll directories for files\n"
      "\tcontaining NetFlow v5 PDUs.  The --polling-interval switch applies\n"
      "\tonly when polling directories.\n")},
    {INPUT_SINGLEFILE, "single-file", "Single File Input",
     ("\tProcess a single file containing NetFlow v5 PDUs, IPFIX records,\n"
      "\tor SiLK Flow records and exit.  The\n"
      "\t--sensor-name switch is required unless the sensor configuration\n"
      "\tfile contains a single sensor.\n")},
    {INPUT_FCFILES, "fcfiles", "Flowcap Files Input",
     ("\tContinually poll a directory for files created by flowcap and\n"
      "\tprocess the data those files contain.\n")},
    {INPUT_APPEND, "append-incremental", "Append incremental files",
     ("\tContinually poll a directory for incremental-files created by a\n"
      "\tprevious invocation of rwflowpack.  Append the records in those\n"
      "\tto hourly SiLK Flow files in the data repository.\n")},
    {OUTPUT_LOCAL_STORAGE, "local-storage", "Local-Storage Output",
     ("\tWrite the SiLK Flow records to their final location.\n")},
    {OUTPUT_INCREMENTAL_FILES, "incremental-files", "Incremental-Files Output",
     ("\tWrite the SiLK Flow records to temporary files (called incremental\n"
      "\tfiles) and allow another daemon (such as rwsender or rwflowappend)\n"
      "\tto process the files for final storage.\n")},
    {OUTPUT_FLOWCAP, "flowcap", "Flowcap-Files Output",
     ("\tWrite the SiLK Flow records to temporary files for later processing\n"
      "\tby rwflowpack running in 'fcfiles' input-mode\n")},
    {OUTPUT_ONE_DESTINATION, "one-destination", "One Destination Output",
     ("\tWrite all SiLK Flow records into one file\n")}
};

/*
 * Define an array of input_mode types and function pointers, where
 * the function takes a input_mode_type and fills in the function
 * pointers for that input_mode type.
 */
static const struct input_mode_init_fn_st {
    io_mode_t   mode;
    int       (*init_fn)(input_mode_type_t*);
} input_mode_init_fn[] = {
    {INPUT_STREAM,     &stream_initialize},
    {INPUT_SINGLEFILE, &singlefile_initialize},
    {INPUT_FCFILES,    &fcfiles_initialize},
    {INPUT_APPEND,     &append_initialize}
};

static int dry_run = 0;


/* OPTIONS SETUP */

typedef enum {
    OPT_DRY_RUN
} appOptionsEnum;

static struct option appOptions[] = {
    {"dry-run",             NO_ARG,       0, OPT_DRY_RUN},
    {0,0,0,0}               /* sentinel entry */
};

static const char *appHelp[] = {
    "Verify syntax of configuration file and exit",
    (char *)NULL
};


/* LOCAL FUNCTION PROTOTYPES */

static int appOptionsHandler(clientData cData, int opt_index, char *opt_arg);
static int packconf_load_file(const char *config_file);
static int packing_function_lua(skpc_probe_t *probe);
static int packing_function_flowcap(skpc_probe_t *probe);
static int packing_function_onedest(skpc_probe_t *probe);


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
#define USAGE_MSG                                                          \
    ("<SWITCHES> CONFIG.lua\n"                                             \
     "\tRead flow records generated by NetFlow(v5), IPFIX, or flowcap\n"   \
     "\tfrom a socket or from a file and pack the flow records into\n"     \
     "\thourly flat-files organized in a time-based directory structure.\n")

    FILE *fh = USAGE_FH;
    unsigned int i;

    fprintf(fh, "%s %s", skAppName(), USAGE_MSG);

    fprintf(fh, "\nGeneral switches:\n");
    skOptionsDefaultUsage(fh);
    for (i = 0; appOptions[i].name; ++i) {
        fprintf(fh, "--%s %s. %s\n", appOptions[i].name,
                SK_OPTION_HAS_ARG(appOptions[i]), appHelp[i]);
    }
    sksiteOptionsUsage(fh);
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
    int arg_index;
    int max_fh;

    /* verify same number of options and help strings */
    assert((sizeof(appHelp)/sizeof(char *))
           == (sizeof(appOptions)/sizeof(struct option)));

    /* register the application */
    skAppRegister(argv[0]);
    skAppVerifyFeatures(&features, NULL);
    skOptionsSetUsageCallback(&appUsageLong);

    /* initialize globals */
    stream_cache_size = STREAM_CACHE_SIZE;
    flush_timeout = FLUSH_TIMEOUT;
    ipset_cache = sk_ipset_cache_create();

    /* do not set the comp_method from the environment */
    skCompMethodOptionsNoEnviron();

    /* register the options */
    if (skOptionsRegister(appOptions, &appOptionsHandler, NULL)
        || sksiteOptionsRegister(SK_SITE_FLAG_CONFIG_FILE))
    {
        skAppPrintErr("Unable to register options");
        exit(EXIT_FAILURE);
    }

    /* rwflowpack runs as a daemon */
    if (skdaemonSetup((SKLOG_FEATURE_LEGACY | SKLOG_FEATURE_SYSLOG
                       | SKLOG_FEATURE_CONFIG_FILE),
                      argc, argv))
    {
        exit(EXIT_FAILURE);
    }

    /* setup the probe configuration parser */
    if (skpcSetup()) {
        skAppPrintErr("Unable to setup probe config file parser");
        exit(EXIT_FAILURE);
    }

    /* initialize IPFIX */
    skipfix_initialize(SKIPFIX_INITIALIZE_FLAG_LOG);

    /* register the teardown handler */
    if (atexit(appTeardown) < 0) {
        skAppPrintErr("Unable to register appTeardown() with atexit()");
        appTeardown();
        exit(EXIT_FAILURE);
    }

    /* parse the options */
    arg_index = skOptionsParse(argc, argv);
    if (arg_index < 0) {
        /* options handler has printed error */
        skAppUsage();
    }

    if (argc - arg_index != 1) {
        skAppPrintErr("Expecting the name of the configuration file as"
                      " the single argument");
        skAppUsage();
    }

    packer_config_file = argv[arg_index];
    if (packconf_load_file(packer_config_file)) {
        exit(EXIT_FAILURE);
    }

    /* set input file handles based on stream_cache_size */
    max_fh = (int)((double)stream_cache_size * INPUT_FILEHANDLES_FRACTION);
    if (max_fh < INPUT_FILEHANDLES_MIN) {
        max_fh = INPUT_FILEHANDLES_MIN;
    }
    if (flowpackSetMaximumFileHandles(max_fh)) {
        skAppPrintErr("Cannot set maximum input files to %d", max_fh);
        exit(EXIT_FAILURE);
    }

    /* set polldir file handles based on stream_cache_size */
    max_fh = (int)((double)stream_cache_size * POLLDIR_FILEHANDLES_FRACTION);
    if (max_fh < POLLDIR_FILEHANDLES_MIN) {
        max_fh = POLLDIR_FILEHANDLES_MIN;
    }
    if (skPollDirSetMaximumFileHandles(max_fh)) {
        skAppPrintErr("Cannot set maximum polldirs to %d", max_fh);
        exit(EXIT_FAILURE);
    }

    /* verify the required options for logging */
    if (skdaemonOptionsVerify()) {
        exit(EXIT_FAILURE);
    }

    /* Call the setup function for the input-mode. */
    if (input_mode_type.setup_fn()) {
        exit(EXIT_FAILURE);
    }

    if (INPUT_SINGLEFILE == input_mode) {
        skdaemonDontFork();
    }

    /* set the mask so that the mode is 0644 */
    (void)umask((mode_t)0022);

    if (dry_run) {
        exit(EXIT_SUCCESS);
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
    char        UNUSED(*opt_arg))
{
    switch ((appOptionsEnum)opt_index) {
      case OPT_DRY_RUN:
        dry_run = 1;
        break;
    }

    return 0;
}


int
byteOrderParse(
    const char         *endian_name,
    silk_endian_t      *endian_value)
{
    assert(endian_name);
    assert(endian_value);

    switch (endian_name[0]) {
      case 'a':
        if (0 == strcmp(endian_name, "any")) {
            *endian_value = SILK_ENDIAN_ANY;
            return 0;
        }
        break;
      case 'b':
        if (0 == strcmp(endian_name, "big")) {
            *endian_value = SILK_ENDIAN_BIG;
            return 0;
        }
        break;
      case 'l':
        if (0 == strcmp(endian_name, "little")) {
            *endian_value = SILK_ENDIAN_LITTLE;
            return 0;
        }
        break;
      case 'n':
        if (0 == strcmp(endian_name, "native")) {
            *endian_value = SILK_ENDIAN_NATIVE;
            return 0;
        }
        break;
    }

    return -1;
}



/* ********************************************************************** */
/* ********************************************************************** */
/* ********************************************************************** */
/* ********************************************************************** */


#include <silk/sklua.h>

#define PACKCONF_VARNAME(pv_buf, pv_table, pv_key)                      \
    packconf_make_varname((pv_buf), sizeof(pv_buf), (pv_table), (pv_key))



/**
 *    Fill 'outbuf' with a name suitable for reporting an error.
 *
 *    'table_name' is the name of the current table, and 'key_name' is
 *    the name of the key within that table.  One or both of these
 *    values may be NULL.
 */
static char*
packconf_make_varname(
    char               *outbuf,
    size_t              outbuf_size,
    const char         *table_name,
    const char         *key_name)
{
    if (table_name) {
        if (key_name) {
            snprintf(outbuf, outbuf_size, "%s['%s']", table_name, key_name);
        } else {
            snprintf(outbuf, outbuf_size, "%s", table_name);
        }
    } else if (key_name) {
        snprintf(outbuf, outbuf_size, "%s", key_name);
    } else {
        snprintf(outbuf, outbuf_size, "Value");
    }
    return outbuf;
}


void
packconf_directory_destroy(
    packconf_directory_t   *dir)
{
    if (dir) {
        free((void*)dir->d_archive_directory);
        free((void*)dir->d_error_directory);
        free((void*)dir->d_poll_directory);
        free((void*)dir->d_post_archive_command);
        free(dir);
    }
}

void
packconf_file_destroy(
    packconf_file_t    *file)
{
    if (file) {
        free((void*)file->f_archive_directory);
        free((void*)file->f_error_directory);
        free((void*)file->f_file);
        free((void*)file->f_post_archive_command);
        free(file);
    }
}

void
packconf_network_destroy(
    packconf_network_t *net)
{
    uint32_t j;

    if (net) {
        free((void*)net->n_listen_str);
        if (net->n_listen) {
            skSockaddrArrayDestroy(net->n_listen);
        }
        if (net->n_accept) {
            for (j = 0; j < net->n_accept_count; ++j) {
                skSockaddrArrayDestroy(net->n_accept[j]);
            }
            free(net->n_accept);
        }
        free(net);
        net = NULL;
    }
}

void
packer_fileinfo_destroy(
    packer_fileinfo_t  *finfo)
{
    if (finfo) {
        sk_sidecar_destroy((sk_sidecar_t **)&finfo->sidecar);
        free(finfo);
    }
}


struct packconf_bad_key_data_st {
    const char     *file_name;
    const char     *table_name;
};
typedef struct packconf_bad_key_data_st packconf_bad_key_data_t;

static void
packconf_bad_key_callback(
    const char         *key,
    void               *cb_data)
{
    packconf_bad_key_data_t *data = (packconf_bad_key_data_t*)cb_data;

    if (key) {
        skAppPrintErr("Warning for configuration '%s':"
                      " Unexpected key '%s' found in table '%s'",
                      data->file_name, key, data->table_name);
    } else {
        skAppPrintErr("Warning for configuration '%s':"
                      " Non-alphanumeric key found in table '%s'",
                      data->file_name, data->table_name);
    }
}

static void
packconf_check_table_keys(
    lua_State          *L,
    const char         *config_file,
    const int           t,
    const char         *table,
    const char         *table_keys[])
{
    packconf_bad_key_data_t data;

    data.file_name = config_file;
    data.table_name = table;

    sk_lua_check_table_unknown_keys(L, t, -1, table_keys,
                                    packconf_bad_key_callback, &data);
}


/**
 *    Check whether the value at the top of the Lua stack (index -1)
 *    is nil.  If the value is nil, return 0.
 *
 *    If the value is not nil, print an error message noting that the
 *    value of the key named 'key' in the table named 'table' in the
 *    file 'config_file' is of the incorrect type since an object of
 *    'exp_type' was expected, and return -1.
 */
static int
packconf_warn_not_nil(
    lua_State          *L,
    const char         *config_file,
    const char         *table,
    const char         *key,
    int                 exp_type)
{
    char err_buf[1024];

    if (!lua_isnil(L, -1)) {
        PACKCONF_VARNAME(err_buf, table, key);
        skAppPrintErr("Error in configuration '%s': %s is a %s; %s expected",
                      config_file, err_buf,
                      luaL_typename(L, -1), lua_typename(L, exp_type));
        return -1;
    }
    return 0;
}


/*
 *  status = packconf_do_boolean_field(L, config_file, t, table, key, out_val);
 *  status = packconf_do_double_field(L, config_file, t, table, key, out_val);
 *  status = packconf_do_number_field(L, config_file, t, table, key, out_val);
 *  status = packconf_do_string_field(L, config_file, t, table, key, out_val);
 *  status = packconf_do_subprocess_field(L, config_file, t,table,key,out_val);
 *  status = packconf_do_TYPE_field(L, config_file, t, table, key, out_val);
 *
 *    Get the field 'key' from the table at index 't' in the stack of
 *    the Lua state 'L'.
 *
 *    If the named key does not exist (or is nil), return 0.
 *
 *    If the named key does exist and its value is TYPE, put the value
 *    into the memory referenced by 'out_val' and return 1.
 *
 *    If the named key does exist and is not of the correct type,
 *    print an error (using 'config_file' and 'table' in the error
 *    message) and return -1.
 *
 *    In all cases, the value is popped off the Lua stack, leaving the
 *    stack unchanged from the initial call.
 *
 *    The string test also prints an error and returns -1 if the value
 *    is the empty string or if there is a memory allocation error
 *    copying the string.
 *
 *    The subproces test is an enhancement of the string test in that
 *    it also verifies the string does not contain any invalid
 *    %-conversions.
 *
 *    The number test and double test take two additional parameters
 *    that are the minimum and maximum allowed values for the number.
 *    The test prints and error and returns -1 if the value is outside
 *    that range.
 */
static int
packconf_do_boolean_field(
    lua_State          *L,
    const char         *config_file,
    const int           t,
    const char         *table,
    const char         *key,
    int                *true_false)
{
    int retval = -1;

    lua_getfield(L, t, key);
    if (!lua_isboolean(L, -1)) {
        /* value not a boolean */
        if (packconf_warn_not_nil(L, config_file, table, key, LUA_TBOOLEAN)
            == 0)
        {
            /* value is nil or is not set */
            retval = 0;
        }
        /* else value is neither a boolean nor nil */
    } else {
        *true_false = lua_toboolean(L, -1);
        retval = 1;
    }
    lua_pop(L, 1);
    assert(lua_gettop(L) == t);
    return retval;
}

/* similar to packconf_do_boolean_field(), which see */
static int
packconf_do_double_field(
    lua_State          *L,
    const char         *config_file,
    const int           t,
    const char         *table,
    const char         *key,
    double             *out_value,
    double              min_value,
    double              max_value)
{
    char err_buf[1024];
    lua_Number number;
    lua_Number l_min;
    lua_Number l_max;
    int is_num;
    int retval = -1;

    is_num = 0;
    l_min = (lua_Number)min_value;
    if (0.0 == max_value) {
        max_value = DBL_MAX;
    }
    l_max = (lua_Number)max_value;

    PACKCONF_VARNAME(err_buf, table, key);

    lua_getfield(L, t, key);
    number = lua_tonumberx(L, -1, &is_num);
    if (0 == is_num) {
        if (packconf_warn_not_nil(L, config_file, table, key, LUA_TNUMBER)
            == 0)
        {
            /* value is nil or is not set */
            retval = 0;
        }
        /* else value is neither a number nor nil */
    } else if (number < l_min) {
        skAppPrintErr(("Error in configuration '%s':"
                       " %s '%f' is not valid:"
                       " Value may not be less than %f"),
                      config_file, err_buf, number, min_value);
    } else if (number > l_max) {
        skAppPrintErr(("Error in configuration '%s':"
                       " %s '%f' is not valid:"
                       " Value may not be greater than %f"),
                      config_file, err_buf, number, max_value);
    } else {
        *out_value = (double)number;
        retval = 1;
    }

    lua_pop(L, 1);
    assert(lua_gettop(L) == t);
    return retval;
}

/* similar to packconf_do_boolean_field(), which see */
static int
packconf_do_number_field(
    lua_State          *L,
    const char         *config_file,
    const int           t,
    const char         *table,
    const char         *key,
    uint64_t           *out_value,
    uint64_t            min_value,
    uint64_t            max_value)
{
    char err_buf[1024];
    lua_Number number;
    lua_Number l_min;
    lua_Number l_max;
    int is_num;
    int retval = -1;

    is_num = 0;
    l_min = (lua_Number)min_value;
    if (0 == max_value) {
        max_value = UINT64_MAX;
    }
    l_max = (lua_Number)max_value;

    PACKCONF_VARNAME(err_buf, table, key);

    lua_getfield(L, t, key);
    number = lua_tonumberx(L, -1, &is_num);
    if (0 == is_num) {
        if (packconf_warn_not_nil(L, config_file, table, key, LUA_TNUMBER)
            == 0)
        {
            /* value is nil or is not set */
            retval = 0;
        }
        /* else value is neither a number nor nil */
    } else if (number < l_min) {
        skAppPrintErr(("Error in configuration '%s':"
                       " %s '%f' is not valid:"
                       " Value may not be less than %" PRIu64),
                      config_file, err_buf, number, min_value);
    } else if (number > l_max) {
        skAppPrintErr(("Error in configuration '%s':"
                       " %s '%f' is not valid:"
                       " Value may not be greater than %" PRIu64),
                      config_file, err_buf, number, max_value);
    } else {
        *out_value = (uint64_t)number;
        retval = 1;
    }

    lua_pop(L, 1);
    assert(lua_gettop(L) == t);
    return retval;
}

/* similar to packconf_do_boolean_field(), which see */
static int
packconf_do_string_field(
    lua_State          *L,
    const char         *config_file,
    const int           t,
    const char         *table,
    const char         *key,
    char              **out_value)
{
    char err_buf[1024];
    const char *value;
    int retval = -1;

    lua_getfield(L, t, key);
    if (!lua_isstring(L, -1)) {
        if (packconf_warn_not_nil(L, config_file, table, key, LUA_TSTRING)
            == 0)
        {
            /* value is nil or is not set */
            retval = 0;
        }
        /* else value is neither a string nor nil */
    } else {
        value = lua_tostring(L, -1);
        if ('\0' == value[0]) {
            PACKCONF_VARNAME(err_buf, table, key);
            skAppPrintErr("Error in configuration '%s': %s is the empty string",
                          config_file, err_buf);
        } else {
            *out_value = strdup(value);
            if (*out_value) {
                retval = 1;
            } else {
                skAppPrintOutOfMemory(NULL);
            }
        }
    }

    lua_pop(L, 1);
    assert(lua_gettop(L) == t);
    return retval;
}

/* similar to packconf_do_boolean_field(), which see */
static int
packconf_do_subprocess_field(
    lua_State          *L,
    const char         *config_file,
    const int           t,
    const char         *table,
    const char         *key,
    char              **out_value)
{
    char err_buf[1024];
    char err_string[256];
    char *value;
    size_t pos;
    int rv;

    rv = packconf_do_string_field(L, config_file, t, table, key, &value);
    if (1 != rv) {
        return rv;
    }
    pos = skSubcommandStringCheck(value, "s");
    if (0 == pos) {
        *out_value = value;
        return rv;
    }
    if (value[pos]) {
        snprintf(err_string, sizeof(err_string),
                 " Unknown conversion '%%%c'", value[pos]);
    } else {
        snprintf(err_string, sizeof(err_string),
                 "Single '%%' at end of string");
    }
    skAppPrintErr(("Error in configuration '%s':"
                   " Invalid %s '%s': %s"),
                  config_file, PACKCONF_VARNAME(err_buf, table, key),
                  value, err_string);
    free(value);
    return -1;
}

/* similar to packconf_do_boolean_field(), which see */
static int
packconf_do_directory_field(
    lua_State              *L,
    const char             *config_file,
    const int               t,
    const char             *table,
    const char             *key,
    packconf_directory_t  **out_value)
{
    const char *table_keys[] = {
        "archive_directory", "archive_policy",
        "directory", "error_directory",
        "interval", "post_archive_command", NULL
    };
    const char *subdir_policy[] = { "flat", "y/m/d/h" };
    char dir_table[1024];
    char err_buf[1024];
    const char *dir_key;
    char *value;
    int dir_t;
    uint64_t tmp64;
    packconf_directory_t *dir;
    int rv;
    int retval = -1;

    dir = NULL;

    lua_getfield(L, t, key);
    if (!lua_istable(L, -1)) {
        if (packconf_warn_not_nil(L, config_file, table, key, LUA_TTABLE)
            == 0)
        {
            retval = 0;
        }
        /* else value is neither a table nor nil */
        goto END;
    }

    dir = sk_alloc(packconf_directory_t);
    PACKCONF_VARNAME(dir_table, table, key);
    dir_t = lua_gettop(L);

    /* check table for unrecognized keys */
    packconf_check_table_keys(L, config_file, dir_t, dir_table, table_keys);

    /* get <table>[directory] */
    dir_key = "directory";
    rv = packconf_do_string_field(L, config_file, dir_t, dir_table, dir_key,
                                  &value);
    if (-1 == rv) {
        goto END;
    } else if (!rv) {
        skAppPrintErr("Error in configuration '%s':"
                      " Required entry %s was not specified",
                      config_file,
                      PACKCONF_VARNAME(err_buf, dir_table, dir_key));
        goto END;
    } else {
        PACKCONF_VARNAME(err_buf, dir_table, dir_key);
        if (skOptionsCheckDirectory(value, err_buf)) {
            free(value);
            goto END;
        }
        dir->d_poll_directory = value;
    }

    /* get <table>[error_directory] */
    dir_key = "error_directory";
    rv = packconf_do_string_field(L, config_file, dir_t, dir_table, dir_key,
                                  &value);
    if (-1 == rv) {
        goto END;
    } else if (!rv) {
        skAppPrintErr("Error in configuration '%s':"
                      " Required entry %s was not specified",
                      config_file,
                      PACKCONF_VARNAME(err_buf, dir_table, dir_key));
        goto END;
    } else {
        PACKCONF_VARNAME(err_buf, dir_table, dir_key);
        if (skOptionsCheckDirectory(value, err_buf)) {
            free(value);
            goto END;
        }
        dir->d_error_directory = value;
    }

    /* get <table>[interval] */
    rv = packconf_do_number_field(L, config_file, dir_t, dir_table, "interval",
                                  &tmp64, 1, UINT32_MAX);
    if (-1 == rv) {
        goto END;
    } else if (!rv) {
        dir->d_poll_interval = POLLING_INTERVAL;
    } else {
        dir->d_poll_interval = tmp64;
    }

    /* get <table>[archive_directory] */
    dir_key = "archive_directory";
    rv = packconf_do_string_field(L, config_file, dir_t, dir_table, dir_key,
                                  &value);
    if (-1 == rv) {
        goto END;
    } else if (rv) {
        PACKCONF_VARNAME(err_buf, dir_table, dir_key);
        if (skOptionsCheckDirectory(value, err_buf)) {
            free(value);
            goto END;
        }
        dir->d_archive_directory = value;
    }

    /* get <table>[post_archive_command] */
    dir_key = "post_archive_command";
    rv = packconf_do_subprocess_field(L, config_file, dir_t, dir_table,
                                      dir_key, &value);
    if (-1 == rv) {
        goto END;
    } else if (rv) {
        if (!dir->d_archive_directory) {
            char err_buf2[1024];
            PACKCONF_VARNAME(err_buf, dir_table, dir_key);
            PACKCONF_VARNAME(err_buf2, dir_table, "archive_directory");
            skAppPrintErr("Error in configuration '%s':"
                          " %s requires that %s is specified",
                          config_file, err_buf, err_buf2);
            free(value);
            goto END;
        }
        dir->d_post_archive_command = value;
    }

    /* get <table>[archive_policy] */
    dir_key = "archive_policy";
    rv = packconf_do_string_field(L, config_file, dir_t, dir_table, dir_key,
                                  &value);
    if (-1 == rv) {
        goto END;
    } else if (rv) {
        if (0 == strcmp(value, subdir_policy[0])) {
            dir->d_flat_archive = 1;
        } else if (0 != strcmp(value, subdir_policy[1])) {
            PACKCONF_VARNAME(err_buf, dir_table, dir_key);
            skAppPrintErr("Error in configuration '%s':"
                          " Invalid %s '%s': Must be '%s' or '%s'",
                          config_file, err_buf, value,
                          subdir_policy[0], subdir_policy[1]);
            free(value);
            goto END;
        }
        free(value);
    }

    *out_value = dir;
    retval = 1;

  END:
    lua_pop(L, 1);
    assert(lua_gettop(L) == t);
    if (-1 == retval && dir) {
        free((void*)dir->d_archive_directory);
        free((void*)dir->d_error_directory);
        free((void*)dir->d_poll_directory);
        free((void*)dir->d_post_archive_command);
        free(dir);
    }
    return retval;
}


static int
packconf_do_source_file(
    lua_State          *L,
    const char         *config_file,
    const int           t,
    const char         *table,
    const char         *key,
    packconf_file_t   **out_value)
{
    const char *table_keys[] = {
        "archive_directory", "error_directory", "file",
        "post_archive_command", NULL
    };
    char file_table[1024];
    char err_buf[1024];
    const char *file_key;
    char *value;
    int file_t;
    packconf_file_t *file;
    int rv;
    int retval = -1;

    file = NULL;

    lua_getfield(L, t, key);
    if (!lua_istable(L, -1)) {
        if (packconf_warn_not_nil(L, config_file, table, key, LUA_TTABLE)
            == 0)
        {
            retval = 0;
        }
        /* else value is neither a table nor nil */
        goto END;
    }

    file = sk_alloc(packconf_file_t);
    PACKCONF_VARNAME(file_table, table, key);
    file_t = lua_gettop(L);

    /* check table for unrecognized keys */
    packconf_check_table_keys(L, config_file, file_t, file_table, table_keys);

    /* get <table>[file] */
    file_key = "file";
    rv = packconf_do_string_field(L, config_file, file_t, file_table, file_key,
                                  &value);
    if (-1 == rv) {
        goto END;
    } else if (!rv) {
        skAppPrintErr("Error in configuration '%s':"
                      " Required entry %s was not specified",
                      config_file,
                      PACKCONF_VARNAME(err_buf, file_table, file_key));
        goto END;
    } else {
        PACKCONF_VARNAME(err_buf, file_table, file_key);
        if (!skFileExists(value)) {
            skAppPrintErr("Error in configuration '%s':"
                          " Invalid %s '%s': File does not exist",
                          config_file, err_buf, value);
            free(value);
            goto END;
        }
        file->f_file = value;
    }

    /* get <table>[error_directory] */
    file_key = "error_directory";
    rv = packconf_do_string_field(L, config_file, file_t, file_table, file_key,
                                  &value);
    if (-1 == rv) {
        goto END;
    } else if (rv) {
        PACKCONF_VARNAME(err_buf, file_table, file_key);
        if (skOptionsCheckDirectory(value, err_buf)) {
            free(value);
            goto END;
        }
        file->f_error_directory = value;
    }

    /* get <table>[archive_directory] */
    file_key = "archive_directory";
    rv = packconf_do_string_field(L, config_file, file_t, file_table, file_key,
                                  &value);
    if (-1 == rv) {
        goto END;
    } else if (rv) {
        PACKCONF_VARNAME(err_buf, file_table, file_key);
        if (skOptionsCheckDirectory(value, err_buf)) {
            free(value);
            goto END;
        }
        file->f_archive_directory = value;
    }

    /* get <table>[post_archive_command] */
    file_key = "post_archive_command";
    rv = packconf_do_subprocess_field(L, config_file, file_t, file_table,
                                      file_key, &value);
    if (-1 == rv) {
        goto END;
    } else if (rv) {
        if (!file->f_archive_directory) {
            char err_buf2[1024];
            PACKCONF_VARNAME(err_buf, file_table, file_key);
            PACKCONF_VARNAME(err_buf2, file_table, "archive_directory");
            skAppPrintErr("Error in configuration '%s':"
                          " %s requires that %s is specified",
                          config_file, err_buf, err_buf2);
            free(value);
            goto END;
        }
        file->f_post_archive_command = value;
    }

    *out_value = file;
    retval = 1;

  END:
    lua_pop(L, 1);
    assert(lua_gettop(L) == t);
    if (-1 == retval && file) {
        free((void*)file->f_archive_directory);
        free((void*)file->f_error_directory);
        free((void*)file->f_file);
        free((void*)file->f_post_archive_command);
        free(file);
    }
    return retval;
}


/* similar to packconf_do_boolean_field(), which see */
static int
packconf_do_source_network(
    lua_State              *L,
    const char             *config_file,
    const int               t,
    const char             *table,
    const char             *key,
    packconf_network_t    **out_value)
{
    const char *table_keys[] = {
        "listen", "accept", "protocol", NULL
    };
    char net_table[1024];
    char err_buf[1024];
    const char *net_key;
    const char *const_val;
    char *value;
    int net_t;
    packconf_network_t *net;
    int rv;
    int retval = -1;

    net = NULL;

    lua_getfield(L, t, key);
    if (!lua_istable(L, -1)) {
        if (packconf_warn_not_nil(L, config_file, table, key, LUA_TTABLE)
            == 0)
        {
            retval = 0;
        }
        /* else value is neither a table nor nil */
        goto END;
    }

    net = sk_alloc(packconf_network_t);
    PACKCONF_VARNAME(net_table, table, key);
    net_t = lua_gettop(L);

    /* check table for unrecognized keys */
    packconf_check_table_keys(L, config_file, net_t, net_table, table_keys);

    /* get <table>[listen] */
    net_key = "listen";
    rv = packconf_do_string_field(L, config_file, net_t, net_table, net_key,
                                  &value);
    if (-1 == rv) {
        goto END;
    } else if (!rv) {
        skAppPrintErr("Error in configuration '%s':"
                      " Required entry %s was not specified",
                      config_file,
                      PACKCONF_VARNAME(err_buf, net_table, net_key));
        goto END;
    } else {
        rv = skStringParseHostPortPair(&net->n_listen, value, PORT_REQUIRED);
        if (rv) {
            PACKCONF_VARNAME(err_buf, net_table, net_key);
            skAppPrintErr("Error in configuration '%s':"
                          " Entry %s is not valid '%s': %s",
                          config_file, err_buf, value,
                          skStringParseStrerror(rv));
            free(value);
            goto END;
        }
        net->n_listen_str = value;
    }

    /* get <table>[protocol] */
    net_key = "protocol";
    rv = packconf_do_string_field(L, config_file, net_t, net_table, net_key,
                                  &value);
    if (-1 == rv) {
        goto END;
    } else if (!rv) {
        skAppPrintErr("Error in configuration '%s':"
                      " Required entry %s was not specified",
                      config_file,
                      PACKCONF_VARNAME(err_buf, net_table, net_key));
        goto END;
    } else {
        net->n_protocol = skpcProtocolNameToEnum(value);
        if (net->n_protocol == SKPC_PROTO_UNSET) {
            skAppPrintErr("Error in configuration '%s':"
                          " Entry %s '%s' is not recognized",
                          config_file,
                          PACKCONF_VARNAME(err_buf, net_table, net_key),
                          value);
            free(value);
            goto END;
        }
        free(value);
    }

    /* get <table>[accept] */
    net_key = "accept";
    lua_getfield(L, net_t, net_key);
    if (!lua_istable(L, -1)) {
        /* TODO: Perhaps we should accept a single string as an
         * allowable host, but forcing the user to provide a list is
         * easier to document and check. */
        if (packconf_warn_not_nil(L, config_file, net_table, key,
                                  LUA_TTABLE) != 0)
        {
            /* value is not a table and is not nil */
            lua_pop(L, 1);
            goto END;
        }
        /* else value is nil, and any host may connect */
    } else {
        /*
         *  Following section works on the accept subtable.
         *
         *  We assume this is a sequence, but we only look at the
         *  values, so the keys could be anything.
         *
         *  On error, we need to pop 3 elements from the stack: the
         *  (1)key and (2)value pair in the accept table, and the
         *  (3)accept table itself.
         */
        sk_sockaddr_array_t *sa;
        sk_vector_t *v;
        size_t i;
        const int accept_t = lua_gettop(L);

        v = sk_vector_create(sizeof(sk_sockaddr_array_t*));

        PACKCONF_VARNAME(err_buf, net_table, net_key);

        /* push a dummy first key that lua_next() can pop */
        for (lua_pushnil(L); lua_next(L, accept_t) != 0; lua_pop(L, 1)) {
            /* 'key' is at index -2 and 'value' is at index -1 */
            if (!lua_isstring(L, -1)) {
                if (lua_isnil(L, -1)) {
                    skAppPrintErr("Warning in configuration '%s':"
                                  " Entry %s contains unexpected nil value",
                                  config_file, err_buf);
                } else {
                    skAppPrintErr(("Warning in configuration '%s':"
                                   " Entry %s is invalid. Expected string"
                                   " values but found a %s"),
                                  config_file, err_buf, luaL_typename(L, -1));
                }
                continue;
            }
            const_val = lua_tostring(L, -1);
            rv = skStringParseHostPortPair(&sa, const_val, PORT_PROHIBITED);
            if (rv) {
                PACKCONF_VARNAME(err_buf, net_table, net_key);
                skAppPrintErr("Error in configuration '%s':"
                              " Entry %s is not valid '%s': %s",
                              config_file, err_buf, const_val,
                              skStringParseStrerror(rv));
                for (i = 0; i < sk_vector_get_count(v); ++i) {
                    sk_vector_get_value(v, i, &sa);
                    skSockaddrArrayDestroy(sa);
                }
                sk_vector_destroy(v);
                free(value);
                lua_pop(L, 3);
                goto END;
            }
            sk_vector_append_value(v, &sa);
        }

        net->n_accept = (sk_sockaddr_array_t**)sk_vector_to_array_alloc(v);
        net->n_accept_count = sk_vector_get_count(v);
        sk_vector_destroy(v);
    }
    lua_pop(L, 1);

    *out_value = net;
    retval = 1;

  END:
    lua_pop(L, 1);
    assert(lua_gettop(L) == t);
    if (-1 == retval && net) {
        free((void*)net->n_listen_str);
        if (net->n_listen) {
            skSockaddrArrayDestroy(net->n_listen);
        }
        if (net->n_accept) {
            uint32_t j;
            for (j = 0; j < net->n_accept_count; ++j) {
                skSockaddrArrayDestroy(net->n_accept[j]);
            }
            free(net->n_accept);
        }
        free(net);
        net = NULL;
    }
    return retval;
}


static int
packconf_do_file_info_table(
    lua_State          *L,
    const char         *config_file,
    const int           t,
    const char         *table,
    const char         *key,
    packer_fileinfo_t **out_value,
    int                 always_create)
{
    const char *table_keys[] = {
        "byte_order", "compression_method",
        "record_format", "record_version", "sidecar", NULL
    };
    char pfinfo_table[1024];
    char err_buf[1024];
    const char *pfinfo_key;
    const char *const_val;
    char *value;
    uint64_t tmp64;
    uint32_t tmp32;
    sk_sidecar_t **const_sc;
    sk_sidecar_t *sc;
    int pfinfo_t;
    packer_fileinfo_t *pfinfo;
    int rv;
    int retval = -1;

    pfinfo = sk_alloc(packer_fileinfo_t);
    pfinfo->record_format = FT_RWIPV6ROUTING;
    pfinfo->record_version = SK_RECORD_VERSION_ANY;
    pfinfo->byte_order = SILK_ENDIAN_ANY;
    pfinfo->comp_method = skCompMethodGetDefault();

    lua_getfield(L, t, key);
    if (!lua_istable(L, -1)) {
        if (packconf_warn_not_nil(L, config_file, table, key, LUA_TTABLE)
            == 0)
        {
            retval = 0;
        }
        /* else value is neither a table nor nil */
        goto END;
    }

    PACKCONF_VARNAME(pfinfo_table, table, key);
    pfinfo_t = lua_gettop(L);

    /* check table for unrecognized keys */
    packconf_check_table_keys(L, config_file, pfinfo_t, pfinfo_table,
                              table_keys);

    /* record_format */
    pfinfo_key = "record_format";
    lua_getfield(L, pfinfo_t, pfinfo_key);
    if (!lua_isstring(L, -1)) {
        if (packconf_warn_not_nil(L, config_file, pfinfo_table,
                                  pfinfo_key, LUA_TSTRING))
        {
            lua_pop(L, 1);
            goto END;
        }
    } else {
        const_val = lua_tostring(L, -1);
        pfinfo->record_format = skFileFormatFromName(const_val);
        if (!skFileFormatIsValid(pfinfo->record_format)) {
            rv = skStringParseUint32(&tmp32, const_val, 0, UINT8_MAX);
            if (rv || !skFileFormatIsValid(tmp32)) {
                PACKCONF_VARNAME(err_buf, pfinfo_table, pfinfo_key);
                skAppPrintErr(("Error in configuration '%s':"
                               " %s does not specify a valid record format"),
                               config_file, err_buf);
                lua_pop(L, 1);
                goto END;
            }
            pfinfo->record_format = tmp32;
        }
    }
    lua_pop(L, 1);

    /* file version */
    rv = packconf_do_number_field(L, config_file, pfinfo_t, pfinfo_table,
                                  "record_version", &tmp64, 0, UINT8_MAX);
    if (-1 == rv) {
        goto END;
    } else if (rv) {
        pfinfo->record_version = (sk_file_version_t)tmp64;
    }

    /* sidecar */
    pfinfo_key = "sidecar";
    lua_getfield(L, pfinfo_t, pfinfo_key);
    if (!lua_isuserdata(L, -1)) {
        if (!lua_isnil(L, -1)) {
            PACKCONF_VARNAME(err_buf, pfinfo_table, pfinfo_key);
            skAppPrintErr(
                "Error in configuration '%s': %s is a %s; %s expected",
                config_file, err_buf, luaL_typename(L, -1),
                "silk.sidecar");
            lua_pop(L, 1);
            goto END;
        }
    } else if ((const_sc = sk_lua_tosidecar(L, -1)) == NULL) {
        PACKCONF_VARNAME(err_buf, pfinfo_table, pfinfo_key);
        skAppPrintErr(
            "Error in configuration '%s': %s is a %s; %s expected",
            config_file, err_buf, luaL_typename(L, -1), "silk.sidecar");
        lua_pop(L, 1);
        goto END;
    } else {
        sk_sidecar_copy(&sc, *const_sc);
        pfinfo->sidecar = sc;
    }
    lua_pop(L, 1);


    /* byte_order */
    pfinfo_key = "byte_order";
    rv = packconf_do_string_field(L, config_file, pfinfo_t, pfinfo_table,
                                  pfinfo_key, &value);
    if (-1 == rv) {
        goto END;
    } else if (rv) {
        if (byteOrderParse(value, &pfinfo->byte_order)) {
            PACKCONF_VARNAME(err_buf, pfinfo_table, pfinfo_key);
            skAppPrintErr("Error in configuration '%s':"
                          " %s '%s' is not valid",
                          config_file, err_buf, value);
            free(value);
            goto END;
        }
        free(value);
    }

    /* get options[compression_method] */
    pfinfo_key = "compression_method";
    rv = packconf_do_string_field(L, config_file, pfinfo_t, pfinfo_table,
                                  pfinfo_key, &value);
    if (-1 == rv) {
        goto END;
    } else if (0 == rv) {
        /* set to default value */
        skCompMethodSetFromConfigFile(
            config_file, pfinfo_table, NULL, &pfinfo->comp_method);
    } else {
        PACKCONF_VARNAME(err_buf, pfinfo_table, pfinfo_key);
        if (skCompMethodSetFromConfigFile(
                config_file, err_buf, value, &pfinfo->comp_method))
        {
            free(value);
            goto END;
        }
        free(value);
    }

    retval = 1;

  END:
    lua_pop(L, 1);
    assert(lua_gettop(L) == t);
    if (1 == retval || (0 == retval && always_create)) {
        *out_value = pfinfo;
    } else {
        packer_fileinfo_destroy(pfinfo);
        *out_value = NULL;
    }
    return retval;
}


/*
 *    Parse of all the entries in the input['probes'] table.  The name
 *    of the table is in 'table'.  The table's position on the stack
 *    is 't'.
 *
 *    Each value in this table should itself be a table representing a
 *    single probe.
 */
static int
packconf_do_input_probes(
    lua_State          *L,
    const char         *config_file,
    const int           t,
    const char         *table)
{
    const char *table_keys[] = {
        "log_flags_add", "log_flags_initial", "log_flags_remove",
        "name", "output_file_info", "packing_function",
        "source", "type", "vars", NULL
    };
    const char *fcfile_keys[] = {
        "name", "packing_function", "type", "vars", NULL
    };
#define SOURCE_TYPE_COUNT  3
    const char *source_type_key[SOURCE_TYPE_COUNT+1] = {
        "directory", "listen", "file", NULL
    };
    uint8_t source_type_present[SOURCE_TYPE_COUNT];
    packer_fileinfo_t *file_info;
    const char *key;
    const char *const_val;
    char *value;
    lua_Number number;
    char err_buf[1024];
    char probe_entry[256];
    char probe_table[1024];
    skpc_probetype_t probe_type;
    size_t count;
    size_t i;
    int rv;
    int probe_t;
    skpc_probe_t *probe;
    int retval = -1;

    /* the probes table should be on the stack at 't' */
    assert(lua_istable(L, t));

    probe = NULL;
    file_info = NULL;

    /* push a dummy first key that lua_next() can pop */
    for (lua_pushnil(L); lua_next(L, t) != 0; lua_pop(L, 1)) {
        /* 'key' is at index -2 and 'value' is at index -1; on error,
         * we need to pop these two entries off the stack---in
         * addition to whatever else is present. */

        /* attempt to stringify the 'key' for error reporting, store
         * the result in the local variable 'probe_entry', and create
         * the full path to this probe in 'probe_table' */
        switch (lua_type(L, -2)) {
          case LUA_TNUMBER:
            number = lua_tonumber(L, -2);
            snprintf(probe_entry, sizeof(probe_entry), "%ld", (long)number);
            break;
          case LUA_TSTRING:
            const_val = lua_tostring(L, -2);
            strncpy(probe_entry, const_val, sizeof(probe_entry));
            probe_entry[sizeof(probe_entry)-1] = '\0';
            break;
          default:
            snprintf(probe_entry, sizeof(probe_entry),
                     "<Non-alphanumeric-key>");
            break;
        }
        PACKCONF_VARNAME(probe_table, table, probe_entry);

        if (!lua_istable(L, -1)) {
            packconf_warn_not_nil(L, config_file, table, probe_entry,
                                  LUA_TTABLE);
            continue;
        }

        /* initialize variables for this probe */
        probe_t = lua_gettop(L);
        memset(source_type_present, 0, sizeof(source_type_present));

        if (skpcProbeCreate(&probe)) {
            skAppPrintOutOfMemory(NULL);
            lua_pop(L, 2);
            goto END;
        }

        /* check table for unrecognized keys */
        packconf_check_table_keys(L, config_file, probe_t,
                                  probe_table, table_keys);

        /* get <probe>[name] */
        key = "name";
        rv = packconf_do_string_field(L, config_file, probe_t, probe_table,
                                      key, &value);
        if (-1 == rv) {
            lua_pop(L, 2);
            goto END;
        } else if (!rv) {
            skAppPrintErr("Error in configuration '%s':"
                          " Required entry %s was not specified",
                          config_file,
                          PACKCONF_VARNAME(err_buf, probe_table, key));
            lua_pop(L, 2);
            goto END;
        } else if (skpcProbeSetName(probe, value)) {
            skAppPrintErr("Error in configuration '%s':"
                          " Invalid probe name '%s'",
                          config_file,
                          PACKCONF_VARNAME(err_buf, probe_table, key));
            free(value);
            lua_pop(L, 2);
            goto END;
        }

        /* get <probe>[type] */
        key = "type";
        rv = packconf_do_string_field(L, config_file, probe_t, probe_table,
                                      key, &value);
        if (-1 == rv) {
            lua_pop(L, 2);
            goto END;
        } else if (!rv) {
            skAppPrintErr("Error in configuration '%s':"
                          " Required entry %s was not specified",
                          config_file,
                          PACKCONF_VARNAME(err_buf, probe_table, key));
            lua_pop(L, 2);
            goto END;
        } else {
            probe_type = skpcProbetypeNameToEnum(value);
            if (PROBE_ENUM_INVALID == probe_type) {
                skAppPrintErr("Error in configuration '%s':"
                              " Entry %s '%s' is not recognized",
                              config_file,
                              PACKCONF_VARNAME(err_buf, probe_table, key),
                              value);
                free(value);
                lua_pop(L, 2);
                goto END;
            }
            skpcProbeSetType(probe, probe_type);
            free(value);
        }

        /* get <probe>[vars] */
        key = "vars";
        lua_getfield(L, probe_t, key);
        if (!lua_istable(L, -1)
            && (packconf_warn_not_nil(L, config_file, probe_table, key,
                                      LUA_TTABLE) != 0))
        {
            /* value is not a table and is not nil */
            lua_pop(L, 3);
            goto END;
        }
        lua_pop(L, 1);

        /* get <probe>[packing_function] */
        key = "packing_function";
        lua_getfield(L, probe_t, key);
        if (lua_isfunction(L, -1)) {
            switch (output_mode) {
              case OUTPUT_LOCAL_STORAGE:
              case OUTPUT_INCREMENTAL_FILES:
                skpcProbeSetPackingFunction(probe, packing_function_lua);
                break;
              case OUTPUT_FLOWCAP:
                skpcProbeSetPackingFunction(probe, packing_function_lua);
                break;
              case OUTPUT_ONE_DESTINATION:
                skpcProbeSetPackingFunction(probe, packing_function_lua);
                break;
              default:
                skAbortBadCase(output_mode);
            }
        } else if (packconf_warn_not_nil(L, config_file, probe_table, key,
                                         LUA_TFUNCTION) != 0)
        {
            /* value is not a function and is not nil */
            lua_pop(L, 3);
            goto END;
        } else {
            switch (output_mode) {
              case OUTPUT_FLOWCAP:
                skpcProbeSetPackingFunction(probe, packing_function_flowcap);
                break;
              case OUTPUT_ONE_DESTINATION:
                skpcProbeSetPackingFunction(probe, packing_function_onedest);
                break;
              case OUTPUT_LOCAL_STORAGE:
              case OUTPUT_INCREMENTAL_FILES:
                lua_pop(L, 3);
                skAppPrintErr("Error in configuration '%s':"
                              " Required entry %s was not specified",
                              config_file,
                              PACKCONF_VARNAME(err_buf, probe_table, key));
                goto END;
              default:
                skAbortBadCase(output_mode);
            }
        }
        lua_pop(L, 1);

        /* for FCFILES input mode, only create an ephemeral probe and
         * ignore all other entries in the probe table */
        if (INPUT_FCFILES == input_mode) {
            const char *fc_key;
            size_t j;
            for (i = 0; (key = table_keys[i]) != NULL; ++i) {
                for (j = 0; (fc_key = fcfile_keys[j]) != NULL; ++j) {
                    if (0 == strcmp(fc_key, key)) {
                        /* this key is allowed/expected */
                        break;
                    }
                }
                if (NULL == fc_key) {
                    /* this key is not used */
                    if (LUA_TNIL != lua_getfield(L, probe_t, key)) {
                        skAppPrintErr("Warning in configuration '%s':"
                                      " %s is ignored for '%s' input",
                                      config_file,
                                      PACKCONF_VARNAME(
                                          err_buf, probe_table, key),
                                      available_modes[input_mode].name);
                    }
                    lua_pop(L, 1);
                }
            }
            /* finished with this probe */
            if (skpcProbeVerify(probe, 1)) {
                skAppPrintErr("Unable to verify probe '%s'",
                              skpcProbeGetName(probe));
                skpcProbeDestroy(&probe);
            }
            probe = NULL;
            continue;
        }

        /* get <probe>[source] */
        key = "source";
        lua_getfield(L, probe_t, key);
        if (!lua_istable(L, -1)) {
            if (packconf_warn_not_nil(L, config_file, probe_table, key,
                                      LUA_TTABLE) == 0)
            {
                skAppPrintErr("Error in configuration '%s':"
                              " Required entry %s was not specified",
                              config_file,
                              PACKCONF_VARNAME(err_buf, probe_table, key));
            }
            lua_pop(L, 3);
            goto END;
        }
        /* <probe>[source] is a table whose keys vary depending on the
         * type of source. Check the table for keys that can
         * distinguish the type of source. */
        count = 0;
        for (i = 0; source_type_key[i] != NULL; ++i) {
            /* check for <probe>[source]<source_type_key> then pop it */
            if (lua_getfield(L, -1, source_type_key[i]) != LUA_TNIL) {
                source_type_present[i] = 1;
                ++count;
            }
            lua_pop(L, 1);
        }
        /* pop <probe>[source] */
        lua_pop(L, 1);
        if (count != 1) {
            if (count > 1) {
                skAppPrintErr("Error in configuration '%s':"
                              " Entry %s contains keys for multiple types"
                              " of sources",
                              config_file,
                              PACKCONF_VARNAME(err_buf, probe_table, key));
            } else {
                skAppPrintErr("Error in configuration '%s':"
                              " Entry %s does not contain the expected keys",
                              config_file,
                              PACKCONF_VARNAME(err_buf, probe_table, key));
            }
            lua_pop(L, 2);
            goto END;
        }
        if (source_type_present[0]) {
            packconf_directory_t *dir = NULL;

            /* treat <probe>[source] as a directory field */
            rv = packconf_do_directory_field(L, config_file, probe_t,
                                             probe_table, key, &dir);
            if (-1 == rv) {
                lua_pop(L, 2);
                goto END;
            }
            skpcProbeConfigureCollectorDirectory(probe, dir);
        } else if (source_type_present[1]) {
            packconf_network_t *net = NULL;

            /* treat <probe>[source] as a network field */
            rv = packconf_do_source_network(L, config_file, probe_t,
                                            probe_table, key, &net);
            if (-1 == rv) {
                lua_pop(L, 2);
                goto END;
            }
            skpcProbeConfigureCollectorNetwork(probe, net);
        } else if (source_type_present[2]) {
            packconf_file_t *file = NULL;

            /* treat <probe>[source] as a file field */
            rv = packconf_do_source_file(L, config_file, probe_t,
                                         probe_table, key, &file);
            if (-1 == rv) {
                lua_pop(L, 2);
                goto END;
            }
            skpcProbeConfigureCollectorFile(probe, file);
        } else {
            skAbort();
        }

        /* get <probe>[log_flags_initial] */
        key = "log_flags_initial";
        lua_getfield(L, probe_t, key);
        if (!lua_istable(L, -1)) {
            /* TODO: Perhaps we should accept a single string as a
             * log flag, but forcing the user to provide a list is
             * easier to document and check. */
            if (packconf_warn_not_nil(L, config_file, probe_table, key,
                                      LUA_TTABLE) != 0)
            {
                /* value is not a table and is not nil */
                lua_pop(L, 3);
                goto END;
            }
            /* else value is nil, use default log-flags */
        } else {
            /*
             *  Following section works on the log_flags_initial subtable.
             *
             *  We assume this is a sequence, but we only look at the
             *  values, so the keys could be anything.
             *
             *  On error, we need to pop 5 elements from the stack:
             *  the (1)key and (2)value pair in the log-flags table,
             *  the (3)log-flags table entry, and the (4)key and
             *  (5)value from the probes table.
             */
            const int log_flags_t = lua_gettop(L);

            PACKCONF_VARNAME(err_buf, probe_table, key);

            /* clear the existing log flags */
            skpcProbeClearLogFlags(probe);

            /* push a dummy first key that lua_next() can pop */
            for (lua_pushnil(L); lua_next(L, log_flags_t) != 0; lua_pop(L, 1)){
                /* 'key' is at index -2 and 'value' is at index -1 */
                if (!lua_isstring(L, -1)) {
                    if (lua_isnil(L, -1)) {
                        skAppPrintErr("Warning in configuration '%s':"
                                      " Entry %s contains unexpected nil value",
                                      config_file, err_buf);
                    } else {
                        skAppPrintErr(("Warning in configuration '%s':"
                                       " Entry %s is invalid. Expected string"
                                       " values but found a %s"),
                                      config_file, err_buf,
                                      luaL_typename(L, -1));
                    }
                    continue;
                }
                const_val = lua_tostring(L, -1);
                if (skpcProbeAddLogFlag(probe, const_val)) {
                    skAppPrintErr("Warning in configuration '%s':"
                                  " Entry %s contains unrecognized value %s",
                                  config_file, err_buf, const_val);
                    lua_pop(L, 5);
                    goto END;
                }
            }
        }
        lua_pop(L, 1);

        /* get <probe>[log_flags_add] */
        key = "log_flags_add";
        lua_getfield(L, probe_t, key);
        if (!lua_istable(L, -1)) {
            if (packconf_warn_not_nil(L, config_file, probe_table, key,
                                      LUA_TTABLE) != 0)
            {
                /* value is not a table and is not nil */
                lua_pop(L, 3);
                goto END;
            }
            /* else value is nil; no flags to add */
        } else {
            const int log_flags_t = lua_gettop(L);
            PACKCONF_VARNAME(err_buf, probe_table, key);
            for (lua_pushnil(L); lua_next(L, log_flags_t) != 0; lua_pop(L, 1)){
                /* 'key' is at index -2 and 'value' is at index -1 */
                if (!lua_isstring(L, -1)) {
                    if (lua_isnil(L, -1)) {
                        skAppPrintErr("Warning in configuration '%s':"
                                      " Entry %s contains unexpected nil value",
                                      config_file, err_buf);
                    } else {
                        skAppPrintErr(("Warning in configuration '%s':"
                                       " Entry %s is invalid. Expected string"
                                       " values but found a %s"),
                                      config_file, err_buf,
                                      luaL_typename(L, -1));
                    }
                    continue;
                }
                const_val = lua_tostring(L, -1);
                if (skpcProbeAddLogFlag(probe, const_val)) {
                    skAppPrintErr("Warning in configuration '%s':"
                                  " Entry %s contains unrecognized value %s",
                                  config_file, err_buf, const_val);
                    lua_pop(L, 5);
                    goto END;
                }
            }
        }
        lua_pop(L, 1);

        /* get <probe>[log_flags_remove] */
        key = "log_flags_remove";
        lua_getfield(L, probe_t, key);
        if (!lua_istable(L, -1)) {
            if (packconf_warn_not_nil(L, config_file, probe_table, key,
                                      LUA_TTABLE) != 0)
            {
                /* value is not a table and is not nil */
                lua_pop(L, 3);
                goto END;
            }
            /* else value is nil; no flags to remove */
        } else {
            const int log_flags_t = lua_gettop(L);
            PACKCONF_VARNAME(err_buf, probe_table, key);
            for (lua_pushnil(L); lua_next(L, log_flags_t) != 0; lua_pop(L, 1)){
                /* 'key' is at index -2 and 'value' is at index -1 */
                if (!lua_isstring(L, -1)) {
                    skAppPrintErr(("Warning in configuration '%s':"
                                   " Entry %s is invalid. Expected string"
                                   " values but found a %s"),
                                  config_file, err_buf, luaL_typename(L, -1));
                    continue;
                }
                const_val = lua_tostring(L, -1);
                if (skpcProbeAddLogFlag(probe, const_val)) {
                    skAppPrintErr("Warning in configuration '%s':"
                                  " Entry %s contains unrecognized value %s",
                                  config_file, err_buf, const_val);
                    lua_pop(L, 5);
                    goto END;
                }
            }
        }
        lua_pop(L, 1);

        /* get <probe>[output_file_info] */
        key = "output_file_info";
        rv = packconf_do_file_info_table(L, config_file, probe_t, probe_table,
                                         key, &file_info,
                                         (OUTPUT_FLOWCAP == output_mode));
        if (-1 == rv) {
            goto END;
        } else if (OUTPUT_FLOWCAP != output_mode) {
            if (rv) {
                skAppPrintErr("Warning in configuration '%s':"
                              " %s is ignored for '%s' output",
                              config_file,
                              PACKCONF_VARNAME(err_buf, probe_table, key),
                              available_modes[output_mode].name);
                packer_fileinfo_destroy(file_info);
            }
        } else {
            skpcProbeSetFileInfo(probe, file_info);
        }

        /* finished with this probe */
        if (skpcProbeVerify(probe, 0)) {
            skAppPrintErr("Unable to verify probe '%s'",
                          skpcProbeGetName(probe));
            skpcProbeDestroy(&probe);
        }

        probe = NULL;
    }

    retval = 1;

  END:
    /* table should be only thing on the stack at this point */
    assert(lua_gettop(L) == t);
    if (-1 == retval && probe) {
        skpcProbeDestroy(&probe);
    }
    return retval;
}


#if 0
/*
 *    Parse the value at stack level 't' as a number, and verify that
 *    that value is between 'min_value' and 'max_value' inclusive.  A
 *    'max_value' of 0 corresponds to UINT64_MAX.
 *
 *    If the value is a number and is not outside the limits return 0.
 *    Otherwise, return -1.
 *
 *    This function does not manipulate the Lua stack, so the value
 *    't' may be relative to the top of the stack (for example, -1).
 *
 *    The value 'config_file' is the file being parsed, and it may not
 *    be NULL.  The value is used for reporting errors.  The values
 *    'table' and 'key' are also used for reporting errors, and one or
 *    both of them may be NULL.  If both are specified, the error
 *    message reports an error with "table['key']".  If only 'table'
 *    is specified, the error message reports an error with "table".
 *    Otherwise, a problem with "Value" is reported.
 */
static int
packconf_check_number(
    lua_State          *L,
    const char         *config_file,
    const int           t,
    const char         *table,
    const char         *key,
    uint64_t           *out_value,
    uint64_t            min_value,
    uint64_t            max_value)
{
    char err_buf[1024];
    lua_Number number;
    lua_Number l_min;
    lua_Number l_max;
    int is_num;

    is_num = 0;
    l_min = (lua_Number)min_value;
    if (0 == max_value) {
        max_value = UINT64_MAX;
    }
    l_max = (lua_Number)max_value;

    PACKCONF_VARNAME(err_buf, table, key);

    number = lua_tonumberx(L, t, &is_num);
    if (0 == is_num) {
        skAppPrintErr("Error in configuration '%s':"
                      " %s is not valid: Expecting a number",
                      config_file, err_buf);
        return -1;
    }
    if (number < l_min) {
        skAppPrintErr(("Error in configuration '%s':"
                       " %s '%f' is not valid:"
                       " Value may not be less than %" PRIu64),
                      config_file, err_buf, number, min_value);
        return -1;
    }
    if (number > l_max) {
        skAppPrintErr(("Error in configuration '%s':"
                       " %s '%f' is not valid:"
                       " Value may not be greater than %" PRIu64),
                      config_file, err_buf, number, max_value);
        return -1;
    }
    *out_value = (uint64_t)number;
    return 0;
}
#endif  /* 0 */


static int
packconf_do_toplevel_input_mode(
    lua_State          *L,
    const char         *config_file)
{
    const char table[] = "input";
    const char *key;
    char *value;
    int found_mode;
    size_t i;
    char err_buf[1024];
    const struct input_mode_init_fn_st *init_fn;
    int t;
    int rv;
    int retval = -1;

    /* table is in the stack at index 't' */
    lua_getglobal(L, table);
    t = lua_gettop(L);

    /* does it exist and is it a table? */
    if (lua_isnil(L, t)) {
        skAppPrintErr("Error in configuration '%s':"
                      " Required variable %s was not specified",
                      config_file, table);
        goto END;
    }
    if (!lua_istable(L, t)) {
        skAppPrintErr("Error in configuration '%s':"
                      " Variable '%s' is not a table",
                      config_file, table);
        goto END;
    }

    /* get input[mode] */
    key = "mode";
    rv = packconf_do_string_field(L, config_file, t, table, key, &value);
    if (-1 == rv) {
        goto END;
    } else if (!rv) {
        skAppPrintErr("Error in configuration '%s':"
                      " Required entry %s was not specified",
                      config_file, PACKCONF_VARNAME(err_buf, table, key));
        goto END;
    } else {
        found_mode = 0;
        for (i = 0; i < first_output_mode; ++i) {
            if (0 == strcmp(value, available_modes[i].name)) {
                found_mode = 1;
                input_mode = (io_mode_t)i;
                break;
            }
        }
        if (!found_mode) {
            PACKCONF_VARNAME(err_buf, table, key);
            skAppPrintErr("Error in configuration '%s': %s '%s' is not valid",
                          config_file, err_buf, value);
            free(value);
            goto END;
        }
        free(value);
        /* initialize based on the input_mode */
        for (i = 0, init_fn = input_mode_init_fn;
             i < sizeof(input_mode_init_fn)/sizeof(input_mode_init_fn)[0];
             ++i, ++init_fn)
        {
            if (init_fn->mode == input_mode) {
                if (init_fn->init_fn(&input_mode_type)) {
                    skAppPrintErr("Unable to initialize %s input-mode",
                                  available_modes[input_mode].title);
                    goto END;
                }
                break;
            }
        }
        if (!input_mode_type.setup_fn) {
            skAbort();
        }
    }

    retval = 0;

  END:
    /* table should be only thing on the stack at this point */
    assert(lua_gettop(L) == t);
    /* pop the table */
    lua_pop(L, 1);
    return retval;
}


static int
packconf_do_toplevel_input(
    lua_State          *L,
    const char         *config_file)
{
    const char *table_keys[] = {
        "incoming", "mode", "probes", NULL
    };
    const char table[] = "input";
    const char *key;
    char err_buf[1024];
    int t;
    int rv;
    int retval = -1;

    /* table is in the stack at index 't' */
    lua_getglobal(L, table);
    t = lua_gettop(L);

    /* does it exist and is it a table? */
    if (lua_isnil(L, t)) {
        skAppPrintErr("Error in configuration '%s':"
                      " Required variable %s was not specified",
                      config_file, table);
        goto END;
    }
    if (!lua_istable(L, t)) {
        skAppPrintErr("Error in configuration '%s':"
                      " Variable '%s' is not a table",
                      config_file, table);
        goto END;
    }

    /* check table for unrecognized keys */
    packconf_check_table_keys(L, config_file, t, table, table_keys);

    /* input[mode] is handled elsewhere */

    /* get input[incoming] */
    key = "incoming";
    rv = packconf_do_directory_field(L, config_file, t, table, key,
                                     &incoming_directory);
    if (-1 == rv) {
        goto END;
    } else if (!rv) {
        if (INPUT_FCFILES == input_mode
            || INPUT_APPEND == input_mode)
        {
            skAppPrintErr("Error in configuration '%s':"
                          " Required entry %s was not specified",
                          config_file, PACKCONF_VARNAME(err_buf, table, key));
            goto END;
        }
    } else if (INPUT_FCFILES != input_mode
               && INPUT_APPEND != input_mode)
    {
        skAppPrintErr("Warning in configuration '%s':"
                      " %s is ignored for '%s' input",
                      config_file, PACKCONF_VARNAME(err_buf, table, key),
                      available_modes[input_mode].name);
        packconf_directory_destroy(incoming_directory);
    }

    /* get input[probes] */
    key = "probes";
    lua_getfield(L, t, key);
    if (INPUT_STREAM != input_mode
        && INPUT_SINGLEFILE != input_mode
        && INPUT_FCFILES != input_mode)
    {
        /* not used outside of stream, single-file, and fcfiles input
         * modes.  If fcfiles input_mode, the probes only need a name
         * and type. */
        if (!lua_isnil(L, -1)) {
            skAppPrintErr("Warning in configuration '%s':"
                          " %s is ignored for '%s' input",
                          config_file, PACKCONF_VARNAME(err_buf, table, key),
                          available_modes[input_mode].name);
        }
    } else if (!lua_istable(L, -1)) {
        if (packconf_warn_not_nil(L, config_file, table, key, LUA_TTABLE)
            == 0)
        {
            /* value is nil */
            skAppPrintErr("Error in configuration '%s':"
                          " Required entry %s was not specified",
                          config_file, PACKCONF_VARNAME(err_buf, table, key));
        }
        /* else value is neither a table nor nil */
        lua_pop(L, 1);
        goto END;
    } else {
        PACKCONF_VARNAME(err_buf, table, key);
        rv = packconf_do_input_probes(L, config_file, lua_gettop(L), err_buf);
        if (-1 == rv) {
            lua_pop(L, 1);
            goto END;
        }
    }
    lua_pop(L, 1);

    retval = 0;

  END:
    /* table should be only thing on the stack at this point */
    assert(lua_gettop(L) == t);
    /* pop the table */
    lua_pop(L, 1);
    return retval;
}


static int
packconf_do_toplevel_options(
    lua_State          *L,
    const char         *config_file)
{
    const char *table_keys[] = {
        "file_cache_size", "file_locking", NULL
    };
    const char table[] = "options";
    int rv;
    uint64_t tmp64;
    int true_false;
    int t;
    int retval = -1;

    /* table is in the stack at index 't' */
    lua_getglobal(L, table);
    t = lua_gettop(L);

    /* does it exist and is it a table? */
    if (lua_isnil(L, t)) {
        /* assume default values for these options */
        retval = 0;
        goto END;
    }
    if (!lua_istable(L, t)) {
        skAppPrintErr("Error in configuration '%s':"
                      " Variable '%s' is not a table",
                      config_file, table);
        goto END;
    }

    /* check table for unrecognized keys */
    packconf_check_table_keys(L, config_file, t, table, table_keys);

    /* get options[file_cache_size] */
    rv = packconf_do_number_field(L, config_file, t, table, "file_cache_size",
                                  &tmp64, STREAM_CACHE_MIN, INT16_MAX);
    if (-1 == rv) {
        goto END;
    } else if (rv) {
        stream_cache_size = tmp64;
    }

    /* get options[file_locking] */
    rv = packconf_do_boolean_field(L, config_file, t, table,
                                   "file_locking", &true_false);
    if (-1 == rv) {
        goto END;
    } else if (rv) {
        no_file_locking = !true_false;
    }

    retval = 0;

  END:
    /* table should be only thing on the stack at this point */
    assert(lua_gettop(L) == t);
    /* pop the table */
    lua_pop(L, 1);
    return retval;
}


static int
packconf_do_toplevel_output(
    lua_State          *L,
    const char         *config_file)
{
    /* note: synchronize_flush replaces the "clock-time" switch,
     * though I still do not like the name */
    const char *table_keys[] = {
        "destination_file", "file_info", "flush_interval",
        "usedspace_maximum_percent", "freespace_minimum_bytes",
        "hour_file_command", "max_file_size",
        "mode", "output_directory", "processing",
        "reject_hours_future", "reject_hours_past",
        "repository_writer_threads",
        "root_directory", "synchronize_flush", NULL
    };
    const char table[] = "output";
    const char *key;
    packer_fileinfo_t *file_info;
    char *value;
    uint64_t tmp64;
    double tmp_d = 0;
    char err_buf[1024];
    int rv;
    int found_mode;
    int i;
    packconf_directory_t *processing_dir;
    int t;
    int retval = -1;

    processing_dir = NULL;

    /* table is in the stack at index 't' */
    lua_getglobal(L, table);
    t = lua_gettop(L);

    /* does it exist and is it a table? */
    if (lua_isnil(L, t)) {
        skAppPrintErr("Error in configuration '%s':"
                      " Required variable %s was not specified",
                      config_file, table);
        goto END;
    }
    if (!lua_istable(L, t)) {
        skAppPrintErr("Error in configuration '%s':"
                      " Variable '%s' is not a table",
                      config_file, table);
        goto END;
    }

    /* check the keys of the output table */
    packconf_check_table_keys(L, config_file, t, table, table_keys);

    /* get output[mode] */
    key = "mode";
    rv = packconf_do_string_field(L, config_file, t, table, key, &value);
    if (-1 == rv) {
        goto END;
    } else if (!rv) {
        skAppPrintErr("Error in configuration '%s':"
                      " Required entry %s was not specified",
                      config_file, PACKCONF_VARNAME(err_buf, table, key));
        goto END;
    } else {
        found_mode = 0;
        for (i = first_output_mode; i < NUM_MODES; ++i) {
            if (0 == strcmp(value, available_modes[i].name)) {
                found_mode = 1;
                output_mode = (io_mode_t)i;
                break;
            }
        }
        if (!found_mode) {
            PACKCONF_VARNAME(err_buf, table, key);
            skAppPrintErr("Error in configuration '%s': %s '%s' is not valid",
                          config_file, err_buf, value);
            free(value);
            goto END;
        }
        free(value);
    }

    /* get output[flush_interval] */
    rv = packconf_do_number_field(L, config_file, t, table,
                                  "flush_interval", &tmp64, 1, UINT32_MAX);
    if (-1 == rv) {
        goto END;
    } else if (rv) {
        flush_timeout = tmp64;
    }

    /* get output[synchronize_flush] */
    rv = packconf_do_number_field(L, config_file, t, table,
                                  "synchronize_flush", &tmp64, 1, UINT32_MAX);
    if (-1 == rv) {
        goto END;
    } else if (rv) {
        clock_time = tmp64;
    }

    /* get output[usedspace_maximum_percent] */
    key = "usedspace_maximum_percent";
    rv = packconf_do_double_field(L, config_file, t, table, key,
                                  &tmp_d, 0.0, 99.0);
    if (-1 == rv) {
        goto END;
    } else if (!rv) {
        usedspace_maximum_percent = DEFAULT_USEDSPACE_MAXIMUM_PERCENT;
    } else {
#ifndef SK_HAVE_STATVFS
        skAppPrintErr("Warning in configuration '%s':"
                      " %s is ignored due to lack of OS support",
                      config_file, PACKCONF_VARNAME(err_buf, table, key));
#else
        usedspace_maximum_percent = tmp_d;
#endif
    }

    /* get output[freespace_minimum_bytes] */
    key = "freespace_minimum_bytes";
    rv = packconf_do_string_field(L, config_file, t, table, key, &value);
    if (-1 == rv) {
        goto END;
    } else if (!rv) {
        rv = skStringParseHumanUint64(&tmp64, DEFAULT_FREESPACE_MINIMUM_BYTES,
                                      SK_HUMAN_NORMAL);
        if (rv) {
            skAppPrintErr("Bad default value for %s: '%s': %s",
                          key, DEFAULT_FREESPACE_MINIMUM_BYTES,
                          skStringParseStrerror(rv));
            skAbort();
        }
        freespace_minimum_bytes = (int64_t)tmp64;
    } else {
#ifndef SK_HAVE_STATVFS
        skAppPrintErr("Warning in configuration '%s':"
                      " %s is ignored due to lack of OS support",
                      config_file, PACKCONF_VARNAME(err_buf, table, key));
        free(value);
#else
        rv = skStringParseHumanUint64(&tmp64, value, SK_HUMAN_NORMAL);
        if (rv) {
            skAppPrintErr("Error in configuration '%s': Invalid %s '%s': %s",
                          config_file, PACKCONF_VARNAME(err_buf, table, key),
                          value, skStringParseStrerror(rv));
            free(value);
            goto END;
        }
        freespace_minimum_bytes = (int64_t)tmp64;
#endif  /* SK_HAVE_STATVFS */
    }

    /* get output[max_file_size] */
    key = "max_file_size";
    rv = packconf_do_string_field(L, config_file, t, table, key, &value);
    if (-1 == rv) {
        goto END;
    } else if (!rv) {
        rv = skStringParseHumanUint64(&tmp64, DEFAULT_MAX_FILE_SIZE,
                                     SK_HUMAN_NORMAL);
        if (rv) {
            skAppPrintErr("Bad default value for %s: '%s': %s",
                          key, DEFAULT_MAX_FILE_SIZE,
                          skStringParseStrerror(rv));
            skAbort();
        }
        max_file_size = tmp64;
    } else {
        rv = skStringParseHumanUint64(&tmp64, value, SK_HUMAN_NORMAL);
        if (rv) {
            skAppPrintErr("Error in configuration '%s': Invalid %s '%s': %s",
                          config_file, PACKCONF_VARNAME(err_buf, table, key),
                          value, skStringParseStrerror(rv));
            free(value);
            goto END;
        }
        max_file_size = (int64_t)tmp64;
        free(value);
    }
    alloc_file_size = (uint64_t)(max_file_size
                                 + (double)SKSTREAM_DEFAULT_BLOCKSIZE * 0.15);

    /* get output[hour_file_command] */
    key = "hour_file_command";
    rv = packconf_do_string_field(L, config_file, t, table, key, &value);
    if (-1 == rv) {
        goto END;
    } else if (!rv) {
        /* do nothing; value is optional */
    } else if (OUTPUT_LOCAL_STORAGE != output_mode) {
        skAppPrintErr("Warning in configuration '%s':"
                      " %s is ignored for '%s' output",
                      config_file, PACKCONF_VARNAME(err_buf, table, key),
                      available_modes[output_mode].name);
        free(value);
    } else {
        hour_file_command = value;
    }

    /* get output[output_directory] */
    key = "output_directory";
    rv = packconf_do_string_field(L, config_file, t, table, key, &value);
    if (-1 == rv) {
        goto END;
    } else if (!rv) {
        if (OUTPUT_FLOWCAP == output_mode
            || OUTPUT_INCREMENTAL_FILES == output_mode)
        {
            skAppPrintErr("Error in configuration '%s':"
                          " Required entry %s was not specified",
                          config_file, PACKCONF_VARNAME(err_buf, table, key));
            goto END;
        }
    } else if (OUTPUT_FLOWCAP != output_mode
               && OUTPUT_INCREMENTAL_FILES != output_mode)
    {
        skAppPrintErr("Warning in configuration '%s':"
                      " %s is ignored for '%s' output",
                      config_file, PACKCONF_VARNAME(err_buf, table, key),
                      available_modes[output_mode].name);
        free(value);
    } else {
        PACKCONF_VARNAME(err_buf, table, key);
        if (skOptionsCheckDirectory(value, err_buf)) {
            free(value);
            goto END;
        }
        incremental_directory = value;
        destination_directory = value;
    }

    /* get output[destination_file] */
    key = "destination_file";
    rv = packconf_do_string_field(L, config_file, t, table, key, &value);
    if (-1 == rv) {
        goto END;
    } else if (!rv) {
        if (OUTPUT_ONE_DESTINATION == output_mode) {
            skAppPrintErr("Error in configuration '%s':"
                          " Required entry %s was not specified",
                          config_file, PACKCONF_VARNAME(err_buf, table, key));
            goto END;
        }
    } else if (OUTPUT_ONE_DESTINATION != output_mode) {
        skAppPrintErr("Warning in configuration '%s':"
                      " %s is ignored for '%s' output",
                      config_file, PACKCONF_VARNAME(err_buf, table, key),
                      available_modes[output_mode].name);
        free(value);
    } else {
        one_destination_path = value;
    }

    /* get output[file_info] */
    key = "file_info";
    rv = packconf_do_file_info_table(L, config_file, t, table, key, &file_info,
                                     (OUTPUT_ONE_DESTINATION == output_mode));
    if (-1 == rv) {
        goto END;
    } else if (OUTPUT_ONE_DESTINATION != output_mode) {
        if (rv) {
            /* value is optional; do nothing */
            skAppPrintErr("Warning in configuration '%s':"
                          " %s is ignored for '%s' output",
                          config_file,
                          PACKCONF_VARNAME(err_buf, table, key),
                          available_modes[output_mode].name);
            packer_fileinfo_destroy(file_info);
        }
    } else {
        one_destination_fileinfo = file_info;
    }

    /* get output[processing] */
    key = "processing";
    rv = packconf_do_directory_field(L, config_file, t, table, key,
                                     &processing_dir);
    if (-1 == rv) {
        goto END;
    } else if (!rv) {
        if (OUTPUT_LOCAL_STORAGE == output_mode
            || OUTPUT_INCREMENTAL_FILES == output_mode)
        {
            skAppPrintErr("Error in configuration '%s':"
                          " Required entry %s was not specified",
                          config_file, PACKCONF_VARNAME(err_buf, table, key));
            goto END;
        }
    } else if (OUTPUT_LOCAL_STORAGE != output_mode
               && OUTPUT_INCREMENTAL_FILES != output_mode)
    {
        skAppPrintErr("Warning in configuration '%s':"
                      " %s is ignored for '%s' output",
                      config_file, PACKCONF_VARNAME(err_buf, table, key),
                      available_modes[output_mode].name);
        packconf_directory_destroy(processing_dir);
    } else {
        processing_directory = processing_dir->d_poll_directory;
        processing_dir->d_poll_directory = NULL;
        packconf_directory_destroy(processing_dir);
    }

    /* get output[reject_hours_future] */
    key = "reject_hours_future";
    rv = packconf_do_number_field(L, config_file, t, table, key,
                                  &tmp64, 0, UINT32_MAX);
    if (-1 == rv) {
        goto END;
    } else if (!rv) {
        /* do nothing; value is optional */
    } else if (OUTPUT_LOCAL_STORAGE != output_mode) {
        skAppPrintErr("Warning in configuration '%s':"
                      " %s is ignored for '%s' output",
                      config_file, PACKCONF_VARNAME(err_buf, table, key),
                      available_modes[output_mode].name);
    } else {
        reject_hours_future = tmp64;
        check_time_window = 1;
    }

    /* get output[reject_hours_past] */
    key = "reject_hours_past";
    rv = packconf_do_number_field(L, config_file, t, table, key,
                                  &tmp64, 0, UINT32_MAX);
    if (-1 == rv) {
        goto END;
    } else if (!rv) {
        /* do nothing; value is optional */
    } else if (OUTPUT_LOCAL_STORAGE != output_mode) {
        skAppPrintErr("Warning in configuration '%s':"
                      " %s is ignored for '%s' output",
                      config_file, PACKCONF_VARNAME(err_buf, table, key),
                      available_modes[output_mode].name);
    } else {
        reject_hours_past = tmp64;
        check_time_window = 1;
    }

    /* get output[repository_writer_threads] */
    key = "repository_writer_threads";
    rv = packconf_do_number_field(L, config_file, t, table, key,
                                  &tmp64, 1, UINT16_MAX);
    if (-1 == rv) {
        goto END;
    } else if (!rv) {
        appender_count = DEFAULT_APPENDER_COUNT;
    } else if (OUTPUT_LOCAL_STORAGE != output_mode) {
        skAppPrintErr("Warning in configuration '%s':"
                      " %s is ignored for '%s' output",
                      config_file, PACKCONF_VARNAME(err_buf, table, key),
                      available_modes[output_mode].name);
    } else {
        appender_count = (uint32_t)tmp64;
    }

    /* get output[root_directory] */
    key = "root_directory";
    rv = packconf_do_string_field(L, config_file, t, table, key, &value);
    if (-1 == rv) {
        goto END;
    } else if (!rv) {
        if (OUTPUT_LOCAL_STORAGE == output_mode) {
            skAppPrintErr("Error in configuration '%s':"
                          " Required entry %s was not specified",
                          config_file, PACKCONF_VARNAME(err_buf, table, key));
            goto END;
        }
    } else {
        PACKCONF_VARNAME(err_buf, table, key);
        if (skOptionsCheckDirectory(value, err_buf)) {
            free(value);
            goto END;
        }
        sksiteSetRootDir(value);
        free(value);
    }

    retval = 0;

  END:
    /* table should be only thing on the stack at this point */
    assert(lua_gettop(L) == t);
    /* pop the table */
    lua_pop(L, 1);
    return retval;
}


static int
packconf_load_file(
    const char         *config_file)
{
    lua_State *L = NULL;
    int retval;

    /* Assume error */
    retval = -1;

    assert(config_file);
    if (NULL == config_file || '\0' == config_file[0]) {
        skAppPrintErr("Invalid configuration file name '': File name is empty");
        goto END;
    }
    if ('/' != config_file[0]) {
        skAppPrintErr(("Invalid configuration file name '%s':"
                       " File name must be complate path"),
                      config_file);
        goto END;
    }

    L = sk_lua_newstate();
    sklua_open_pdusource(L);

    if (luaL_loadfile(L, config_file)) {
        skAppPrintErr("Error in configuration '%s': %s",
                      config_file, lua_tostring(L, -1));
        goto END;
    }
    if (lua_pcall(L, 0, 0, 0)) {
        skAppPrintErr("Error in configuration '%s': %s",
                      config_file, lua_tostring(L, -1));
        goto END;
    }

    /* find the [mode] entry in the 'input' table so the input_mode
     * can be initialized; the rest of the input table is handled
     * later. */
    if (packconf_do_toplevel_input_mode(L, config_file)) {
        goto END;
    }

    /* do the "output" first since that may set the root_directory.
     * We want to set the root directory before setting the location
     * of the site config file, which occurs in "options" table.  */
    if (packconf_do_toplevel_output(L, config_file)) {
        goto END;
    }

    /* check for bad input/output combinations */
    if (OUTPUT_FLOWCAP == output_mode && INPUT_STREAM != input_mode) {
        char err_buf_in[1024];
        char err_buf_out[1024];

        PACKCONF_VARNAME(err_buf_in,  "input",  "mode");
        PACKCONF_VARNAME(err_buf_out, "output", "mode");
        skAppPrintErr("Must specify %s = %s when using %s = %s",
                      err_buf_in, available_modes[INPUT_STREAM].title,
                      err_buf_out, available_modes[output_mode].title);
        goto END;
    }

    if (packconf_do_toplevel_options(L, config_file)) {
        goto END;
    }

    /* ensure the site config is available */
    if (sksiteConfigure(1)) {
        goto END;
    }

    if (packconf_do_toplevel_input(L, config_file)) {
        goto END;
    }

    if (sklogParseConfigFile(L, config_file)) {
        goto END;
    }
    if (skdaemonParseConfigFile(L, config_file)) {
        goto END;
    }

    retval = 0;

  END:
    sk_lua_closestate(L);
    return retval;
}




/* ********************************************************************** */
/* ********************************************************************** */
/* ********************************************************************** */
/* ********************************************************************** */

/*
 *  Support for packing Lua
 *
 */

#include <silk/skipset.h>

static const uint8_t packlogic_lua[] = {
#include "packlogic.i"
};


static void
pack_free_lua(
    skpc_probe_t       *probe)
{
    sk_lua_closestate(probe->pack.lua_state);
    probe->pack.lua_state = NULL;
}


static int
lua_read_ipset(
    lua_State *L)
{
    const char *path;
    skipset_t *ipset;
    int rv;

    path = sk_lua_checkstring(L, 1);
    rv = sk_ipset_cache_get_ipset(ipset_cache, &ipset, path);
    if (rv != 0) {
        return luaL_error(L, "Unable to read %s IPset from '%s': %s",
                          path, skIPSetStrerror(rv));
    }
    sk_lua_push_readonly_ipset(L, ipset);
    return 1;
}


/*
 *  status = pack_record_lua(probe, rec);
 *
 *    For the record 'rec' collected from the probe 'probe', set the
 *    flowtype and sensor fields on the record, then call the
 *    write_record() function to output the record.  Return 0 on
 *    success, or -1 on error.
 *
 *    This function has the sigurature defined by
 *    packlogic_pack_record_fn_t, and this is the function returned by
 *    probe->pack_record.
 */
static int
pack_record_lua(
    skpc_probe_t       *probe,
    const rwRec        *const_fwd_rwrec,
    const rwRec        *const_rev_rwrec)
{
    lua_State *L;
    rwRec *fwd_rec;
    rwRec *rev_rec;
    int rv;
    int top;
    int args;

    assert(probe);

    L = probe->pack.lua_state;
    assert(L);

    /* FIXME: We could use lua_getinfo() to get information about the
     * packing function and only provide it with the number of
     * arguments it is expecting; for example, call it once for fwd
     * record and once for rev record if it only expects a single
     * record, and do not bother to create a Lua wrapper for the
     * fixrec. */

    /* FIXME: what is the optimal ordering for the parameters to the
     * packing function?
     *
     * (probe, fwd_rec, rev_rec, fixrec)?    CURRENT APPROACH
     *
     * (probe, fixrec, fwd_rec, rev_rec)?
     *
     * (probe, fwd_rec, fixrec, rev_rec)?
     *
     * (probe, {fwd_rec, rev_rec}, fix_rec)?
     *
     * (probe, <table with key=value pairs>)?
     */

    top = lua_gettop(L);

    /* luaL_loadstring(L, "return debug.traceback();"); */

    lua_pushlightuserdata(L, probe);
    lua_gettable(L, LUA_REGISTRYINDEX);
    lua_rawgeti(L, -1, IDX_PROBE_FUNCTION);
    lua_rawgeti(L, -2, IDX_PROBE_VARS);
    fwd_rec = sk_lua_push_rwrec(L, const_fwd_rwrec);

    rev_rec = NULL;
    switch (skpcProbeGetType(probe)) {
      case PROBE_ENUM_IPFIX:
      case PROBE_ENUM_NETFLOW_V9:
        if (NULL == const_rev_rwrec) {
            lua_pushnil(L);
        } else {
            rev_rec = sk_lua_push_rwrec(L, const_rev_rwrec);
        }
        sk_lua_push_fixrec(L, (sk_fixrec_t *)probe->incoming_rec);
        args = 4;
        break;

      case PROBE_ENUM_NETFLOW_V5:
        assert(probe->incoming_rec);
        sk_lua_push_nfv5(L, (sk_lua_nfv5_t *)probe->incoming_rec);
        args = 3;
        break;

      default:
        /* FIXME: When repacking a SiLK record, should we provide the
         * initial record and "new" version that does not include the
         * sidecar data?  */
        assert(const_rev_rwrec == NULL);
        args = 2;
        break;
    }

    rv = lua_pcall(L, args, 0, 0);
    if (rv != LUA_OK) {
        skAppPrintErr("Lua packing failed for probe '%s': %s",
                      probe->probe_name, lua_tostring(L, -1));
        return -1;
    }
    rwRecReset(fwd_rec);
    if (rev_rec) {
        rwRecReset(rev_rec);
    }
    lua_settop(L, top);

    return 0;
}


/*
 *    A callback function that exists on a probe to set the packing
 *    function for a probe, initialize that function's state, and set
 *    pointers to other callback functions that are used to clean-up
 *    the packing function's state.
 *
 *    This function is registered on the probe by calling
 *    skpcProbeSetPackingFunction().
 *
 *    This function is invoked when skpcProbeInitializePacker() is
 *    called.
 *
 */
static int
packing_function_lua(
    skpc_probe_t       *probe)
{
    int rv;
    int prepare_fn;
    int conf_idx;
    const char *upvalue;
    lua_State *L;

    assert(packer_config_file);

    if (!probe) {
        return -1;
    }

    L = sk_lua_newstate();
    sklua_open_pdusource(L);

    /* Load built-in lua code (compiled contents of packlogic.lua) */
    rv = luaL_loadbufferx(L, (const char *)packlogic_lua,
                          sizeof(packlogic_lua), "packlogic.lua", "b");
    if (rv == LUA_OK) {
        /* Pass to packlogic.lua a version of ipset_read() that shares
         * IPsets among Lua states. */
        lua_pushcfunction(L, lua_read_ipset);
        rv = lua_pcall(L, 1, 1, 0);
    }
    if (rv != LUA_OK) {
        skAppPrintErr("Lua initialization failed: %s",
                      lua_tostring(L, -1));
        sk_lua_closestate(L);
        return -1;
    }

    /* Handle the exported table from packlogic.lua  */
    lua_getfield(L, -1, "prepare_probe");
    prepare_fn = luaL_ref(L, LUA_REGISTRYINDEX);
    lua_pop(L, 1);

    /* Create an empty environment table */
    lua_newtable(L);
    conf_idx = lua_gettop(L);

    /* Add the function write_rwrec() to that environment */
    if (OUTPUT_FLOWCAP == output_mode) {
        /* Include a closure with the probe as the upvalue */
        lua_pushlightuserdata(L, probe);
        lua_pushcclosure(L, flowcap_write_rwrec_lua, 1);
        lua_setfield(L, conf_idx, "write_rwrec");
    } else if (OUTPUT_ONE_DESTINATION == output_mode) {
        lua_pushcfunction(L, onedest_write_rwrec_lua);
        lua_setfield(L, conf_idx, "write_rwrec");
    } else {
        lua_pushcfunction(L, repo_write_rwrec_lua);
        lua_setfield(L, conf_idx, "write_rwrec");
    }

    /* Set a metatable that forwards to _G */
    lua_createtable(L, 0, 1);
    lua_rawgeti(L, LUA_REGISTRYINDEX, LUA_RIDX_GLOBALS);
    lua_setfield(L, -2, "__index");
    lua_setmetatable(L, -2);

    /* Load the config file (as a function) */
    rv = luaL_loadfile(L, packer_config_file);
    if (LUA_OK != rv) {
        skAppPrintErr("Unable to load configuration file '%s': %s",
                      packer_config_file, lua_tostring(L, -1));
        sk_lua_closestate(L);
        return -1;
    }

    /* Set the environment table as the file's _ENV upvalue */
    lua_pushvalue(L, conf_idx);
    upvalue = lua_setupvalue(L, -2, 1);
    if (NULL == upvalue || strcmp(upvalue, "_ENV") != 0) {
        skAppPrintErr(("Programmer error: lua_setupvalue(L, -2, 1) did not"
                       " return \"_ENV\" (got %s)"),
                      (upvalue ? upvalue : "NULL"));
        sk_lua_closestate(L);
        return -1;
    }

    /* Execute the loaded file */
    rv = lua_pcall(L, 0, 0, 0);
    if (LUA_OK != rv) {
        skAppPrintErr("Error in configuration file '%s': %s",
                      packer_config_file, lua_tostring(L, -1));
        sk_lua_closestate(L);
        return -1;
    }

    /* Get the probe */
    lua_rawgeti(L, LUA_REGISTRYINDEX, prepare_fn);
    lua_pushvalue(L, conf_idx);           /* conf environment */
    lua_pushstring(L, probe->probe_name); /* probe name */
    if (lua_pcall(L, 2, 1, 0)) {
        skAppPrintErr("Error preparing probe %s: %s",
                      probe->probe_name,  lua_tostring(L, -1));
        lua_settop(L, 0);
        return -1;
    }
    assert(lua_type(L, -1) == LUA_TTABLE);

    /* Create a table in the registry that is keyed by the probe.  It
     * holds the packing function and the probe variables */
    lua_pushlightuserdata(L, probe);
    lua_createtable(L, PROBE_TABLE_NEXT_IDX - 1, 0);
    {
        /* stash the conf environment */
        lua_getfield(L, -3, "packing_function");
        lua_rawseti(L, -2, IDX_PROBE_FUNCTION);

        /* get the probe variable table */
        lua_getfield(L, -3, "vars");
        lua_rawseti(L, -2, IDX_PROBE_VARS);
    }
    lua_settable(L, LUA_REGISTRYINDEX);

    /* Clear the stack */
    lua_settop(L, 0);

    /* Set the packer to use Lua */
    probe->pack.lua_state = L;
    probe->pack.pack_record = pack_record_lua;
    probe->pack.free_state = pack_free_lua;

    /* Do any configuration specific to the output-mode */
    if (OUTPUT_FLOWCAP == output_mode) {
        return flowcap_initialize_packer(probe, L);
    }
    if (OUTPUT_ONE_DESTINATION == output_mode) {
        return onedest_initialize_packer(probe, L);
    }

    return 0;
}


/*
 *    A callback function that exists on a probe to set the packing
 *    function for a probe, initialize that function's state, and set
 *    pointers to other callback functions that are used to clean-up
 *    the packing function's state.
 *
 *    This function is used when rwflowpack is running in
 *    OUTPUT_FLOWCAP mode and the user has not provided a Lua function
 *    to write the records.
 *
 *    This function is registered on the probe by calling
 *    skpcProbeSetPackingFunction().
 *
 *    This function is invoked when skpcProbeInitializePacker() is
 *    called.
 */
static int
packing_function_flowcap(
    skpc_probe_t       *probe)
{
    return flowcap_initialize_packer(probe, NULL);
}


/*
 *    A callback function that exists on a probe to set the packing
 *    function for a probe, initialize that function's state, and set
 *    pointers to other callback functions that are used to clean-up
 *    the packing function's state.
 *
 *    This function is used when rwflowpack is running in
 *    OUTPUT_ONE_DESTINATION mode and the user has not provided a Lua
 *    function to write the records.
 *
 *    This function is registered on the probe by calling
 *    skpcProbeSetPackingFunction().
 *
 *    This function is invoked when skpcProbeInitializePacker() is
 *    called.
 */
static int
packing_function_onedest(
    skpc_probe_t       *probe)
{
    return onedest_initialize_packer(probe, NULL);
}


/*
** Local Variables:
** mode:c
** indent-tabs-mode:nil
** c-basic-offset:4
** End:
*/
