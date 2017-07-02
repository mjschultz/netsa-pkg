/*
** Copyright (C) 2001-2017 by Carnegie Mellon University.
**
** @OPENSOURCE_LICENSE_START@
** See license information in ../../LICENSE.txt
** @OPENSOURCE_LICENSE_END@
*/

/*
**  rwstatssetup.c
**
**  Application setup for rwstats.  See rwstats.c for a description.
*/

#include <silk/silk.h>

RCSIDENT("$SiLK: rwstatssetup.c d1637517606d 2017-06-23 16:51:31Z mthomas $");

#include <silk/silkpython.h>
#include <silk/skcountry.h>
#include <silk/sklua.h>
#include <silk/skplugin.h>
#include <silk/skprefixmap.h>
#include <silk/sksidecar.h>
#include <silk/sksite.h>
#include <silk/skstringmap.h>
#include <silk/skvector.h>
#include "rwstats.h"


/* TYPEDEFS AND DEFINES */

/* file handle for --help usage message */
#define USAGE_FH stdout

/* suffix for distinct fields */
#define DISTINCT_SUFFIX  "-Distinct"

/* default sTime bin size to use when --bin-time is requested */
#define DEFAULT_TIME_BIN  60

/* When this bit is set in a sk_stringmap_entry_t, the entry comes
 * from a plug-in. */
#define PLUGIN_FIELD_BIT    0x80000000

/* When this bit is set in a sk_stringmap_entry_t, the entry comes
 * from a sidecar field defined in the input. */
#define SIDECAR_FIELD_BIT   0x40000000

/* When this bit is set in a sk_stringmap_entry_t, the entry comes
 * from a sidecar field defined via --lua-file. */
#define SC_LUA_FIELD_BIT    0x20000000


/* type of field being defined */
typedef enum field_type_en {
    FIELD_TYPE_KEY, FIELD_TYPE_VALUE, FIELD_TYPE_DISTINCT
} field_type_t;


#define PARSE_KEY_ELAPSED   (1 << 0)
#define PARSE_KEY_STIME     (1 << 1)
#define PARSE_KEY_ETIME     (1 << 2)
#define PARSE_KEY_ALL_TIMES (PARSE_KEY_ELAPSED|PARSE_KEY_STIME|PARSE_KEY_ETIME)

/*
 *  These macros extract part of a field-list buffer to get a value,
 *  and then set that value on 'rec' by calling 'func'
 */
#define KEY_TO_REC(type, func, rec, field_buffer, field_list, field)    \
    {                                                                   \
        type k2r_val;                                                   \
        skFieldListExtractFromBuffer(field_list, field_buffer,          \
                                     field, (uint8_t*)&k2r_val);        \
        func((rec), k2r_val);                                           \
    }

#define KEY_TO_REC_08(func, rec, field_buffer, field_list, field)       \
    KEY_TO_REC(uint8_t, func, rec, field_buffer, field_list, field)

#define KEY_TO_REC_16(func, rec, field_buffer, field_list, field)       \
    KEY_TO_REC(uint16_t, func, rec, field_buffer, field_list, field)

#define KEY_TO_REC_32(func, rec, field_buffer, field_list, field)       \
    KEY_TO_REC(uint32_t, func, rec, field_buffer, field_list, field)

#define KEY_TO_REC_64(func, rec, field_buffer, field_list, field)       \
    KEY_TO_REC(uint64_t, func, rec, field_buffer, field_list, field)



/* EXPORTED VARIABLES */

/* export variables commmon to both rwstats and rwuniq */
sk_unique_t *uniq;
sk_sort_unique_t *ps_uniq;

sk_fieldlist_t *key_fields;
sk_fieldlist_t *value_fields;
sk_fieldlist_t *distinct_fields;

/* the input */
sk_flow_iter_t *flowiter;

/* output formattter */
sk_formatter_t *fmtr;

/* the real output */
sk_fileptr_t output;

/* flags set by the user options */
app_flags_t app_flags;

sk_sidecar_t *sidecar = NULL;

lua_State *L = NULL;


/* rwstats-variables */

/* user limit for this stat: N if top N or bottom N, threshold, or
 * percentage */
rwstats_limit_t limit;

/* number of records read */
uint64_t record_count = 0;

/* Summation of whatever value (bytes, packets, flows) we are using.
 * When counting flows, this will be equal to record_count. */
uint64_t value_total = 0;

/* non-zero when --overall-stats or --detail-proto-stats is given */
int proto_stats = 0;

/* these must be exported so the proto-stats output may access them */

/* delimiter between output columns */
char delimiter = '|';


/* LOCAL VARIABLES */

/* Lua initialization code; this is binary code compiled from
 * rwstats.lua */
static const uint8_t rwstats_lua[] = {
#include "rwstats.i"
};

/* Information about each potential "value" field the user can choose
 * to compute and display.  Ensure these appear in same order as in
 * the OPT_BYTES...OPT_DIP_DISTINCT values in appOptionsEnum. */
static builtin_field_t builtin_values[] = {
    /* title, min-threshold, max-threshold, text_len, id,
     * application, is_distinct, is_in_all_counts, is_switched_on,
     * description */
    {"Bytes",          0, UINT64_MAX, 20, SK_FIELD_SUM_BYTES,
     STATSUNIQ_PROGRAM_BOTH,    0, 1, 0,
     "Sum of bytes for all flows in the group"},
    {"Packets",        0, UINT64_MAX, 15, SK_FIELD_SUM_PACKETS,
     STATSUNIQ_PROGRAM_BOTH,    0, 1, 0,
     "Sum of packets for all flows in the group"},
    {"Records",        0, UINT64_MAX, 10, SK_FIELD_RECORDS,
     STATSUNIQ_PROGRAM_BOTH,    0, 1, 0,
     "Number of flow records in the group"},
    {"sTime-Earliest", 0, UINT64_MAX, 19, SK_FIELD_MIN_STARTTIME,
     STATSUNIQ_PROGRAM_UNIQ,    0, 1, 0,
     "Minimum starting time for flows in the group"},
    {"eTime-Latest",   0, UINT64_MAX, 19, SK_FIELD_MAX_ENDTIME,
     STATSUNIQ_PROGRAM_UNIQ,    0, 1, 0,
     "Maximum ending time for flows in the group"},
    {"sIP-Distinct",   0, UINT64_MAX, 10, SK_FIELD_SIPv6,
     STATSUNIQ_PROGRAM_BOTH,    1, 0, 0,
     "Number of distinct source IPs in the group"},
    {"dIP-Distinct",   0, UINT64_MAX, 10, SK_FIELD_DIPv6,
     STATSUNIQ_PROGRAM_BOTH,    1, 0, 0,
     "Number of distinct source IPs in the group"},
    {"Distinct",       0, UINT64_MAX, 10, SK_FIELD_CALLER,
     STATSUNIQ_PROGRAM_BOTH,    1, 0, 0,
     "You must append a colon and a key field to count the number of"
     " distinct values seen for that field in the group"}
};

static const size_t num_builtin_values = (sizeof(builtin_values)/
                                          sizeof(builtin_field_t));

/* create aliases for exisitng value fields.  the struct contains the
 * name of the alias and an ID to match in the builtin_values[]
 * array */
static const struct builtin_value_aliases_st {
    const char     *ba_name;
    sk_fieldid_t    ba_id;
} builtin_value_aliases[] = {
    {"Flows",   SK_FIELD_RECORDS},
    {NULL,      (sk_fieldid_t)0}
};

/* whether to print the fields' help */
static int help_fields = 0;

/* key fields used when parsing the user's --fields switch */
static sk_stringmap_t *key_field_map = NULL;

/* available aggregate value fields */
static sk_stringmap_t *value_field_map = NULL;

/* the text the user entered for the --fields switch */
static const char *fields_arg = NULL;

/* the text the user entered for the --values switch */
static const char *values_arg = NULL;

/* name of program to run to page output */
static char *pager;

/* temporary directory */
static const char *temp_directory = NULL;

/* how to handle IPv6 flows */
static sk_ipv6policy_t ipv6_policy = SK_IPV6POLICY_MIX;

/* which of elapsed, sTime, and eTime will be part of the key. uses
 * the PARSE_KEY_* values above */
static unsigned int time_fields_key = 0;

/* whether dPort is part of the key */
static unsigned int dport_key = 0;

/* width for percentage columns */
static const int col_width_percent = 10;

/* cumulative percentage value; updated by by row_percent_to_ascii(),
 * read by cumul_percent_to_ascii(). */
static double cumul_pct = 0.0;

/* how to print IP addresses */
static uint32_t ip_format = SKIPADDR_CANONICAL;

/* how to print timestamps */
static uint32_t time_flags = 0;

/* flags when registering --timestamp-format */
static const uint32_t time_register_flags = SK_OPTION_TIMESTAMP_NEVER_MSEC;

/* the floor of the sTime and/or eTime.  set by --bin-time switch */
static sktime_t time_bin_size = 0;

/* reference into the Lua registry to a table that maps from string to
 * references and from references back to that string.  Used when a
 * string appears as part of a key */
static int str_to_ref = LUA_NOREF;

/* a reference in the str_to_ref table for the empty string; used when
 * the requested sidecar field is not on a record */
static int str_to_ref_nil = LUA_NOREF;

/* input checker */
static sk_options_ctx_t *optctx = NULL;

/* sidecar fields */
static sk_vector_t *sc_field_vec = NULL;

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

/* plug-ins to attempt to load at startup */
static const char *app_plugin_names[] = {
    NULL /* sentinel */
};

/* non-zero if we are shutting down due to a signal; controls whether
 * errors are printed in appTeardown(). */
static int caught_signal = 0;

/* Lua references into the Lua registry of various functions defined
 * in rwstats.lua */
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

/* the number of sidecar functions defined in --lua-file */
static long num_sidecar_adds = 0;


/* OPTIONS */

/* statsuniq_option_t holds a struct option, its help text, and a flag
 * to indicate whether the option is for rwstats, rwuniq, or both */
struct statsuniq_option_st {
    statsuniq_program_t use_opt;
    struct option       opt;
    const char         *help;
};
typedef struct statsuniq_option_st statsuniq_option_t;

typedef enum {
    OPT_OVERALL_STATS, OPT_DETAIL_PROTO_STATS,

    OPT_HELP_FIELDS,
    OPT_FIELDS, OPT_VALUES, OPT_LUA_FILE, OPT_PLUGIN,

    /* keep these in same order as stat_stat_type_t */
    OPT_COUNT, OPT_THRESHOLD, OPT_PERCENTAGE,

    OPT_TOP, OPT_BOTTOM,

    OPT_ALL_COUNTS,
    /* OPT_BYTES...OPT_DIP_DISTINCT must be contiguous and appear in
     * same order as in builtin_values[] */
    OPT_BYTES,
    OPT_PACKETS,
    OPT_FLOWS,
    OPT_STIME,
    OPT_ETIME,
    OPT_SIP_DISTINCT,
    OPT_DIP_DISTINCT,

    OPT_PRESORTED_INPUT,

    OPT_NO_PERCENTS,

    OPT_SORT_OUTPUT,

    OPT_BIN_TIME,
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

static statsuniq_option_t appOptions[] = {
    {STATSUNIQ_PROGRAM_STATS,
     {"overall-stats",       NO_ARG,       0, OPT_OVERALL_STATS},
     ("Print minima, maxima, quartiles, and interval-count\n"
      "\tstatistics for bytes, pkts, bytes/pkt across all flows.  Def. No")},
    {STATSUNIQ_PROGRAM_STATS,
     {"detail-proto-stats",  REQUIRED_ARG, 0, OPT_DETAIL_PROTO_STATS},
     ("Print above statistics for each of the specified\n"
      "\tprotocols.  List protocols or ranges separated by commas. Def. No")},

    {STATSUNIQ_PROGRAM_BOTH,
     {"help-fields",         NO_ARG,       0, OPT_HELP_FIELDS},
     ("Describe each possible field and value and exit. Def. no")},

    {STATSUNIQ_PROGRAM_BOTH,
     {"fields",              REQUIRED_ARG, 0, OPT_FIELDS},
     (("Use these fields as the grouping key. Specify fields as a\n"
       "\tcomma-separated list of names, IDs, and/or ID-ranges"))},
    {STATSUNIQ_PROGRAM_BOTH,
     {"values",              REQUIRED_ARG, 0, OPT_VALUES},
     ("Compute these values for each group. Def. records.\n"
      "\tSpecify values as a comma-separated list of names")},
    {STATSUNIQ_PROGRAM_BOTH,
     {"lua-file",            REQUIRED_ARG, 0, OPT_LUA_FILE},
     ("Load the named Lua file during set-up.  Switch may be\n"
      "\trepeated to load multiple files. Def. None")},
    {STATSUNIQ_PROGRAM_BOTH,
     {"plugin",              REQUIRED_ARG, 0, OPT_PLUGIN},
     ("Load given plug-in to add fields and/or values. Switch may\n"
      "\tbe repeated to load multiple plug-ins. Def. None")},

    {STATSUNIQ_PROGRAM_STATS,
     {"count",               REQUIRED_ARG, 0, OPT_COUNT},
     ("Print the specified number of bins")},
    {STATSUNIQ_PROGRAM_STATS,
     {"threshold",           REQUIRED_ARG, 0, OPT_THRESHOLD},
     ("Print bins where the primary value is greater-/less-than\n"
      "\tthis threshold. Not valid for primary values from plug-ins.")},
    {STATSUNIQ_PROGRAM_STATS,
     {"percentage",          REQUIRED_ARG, 0, OPT_PERCENTAGE},
     ("Print bins where the primary value is greater-/less-than\n"
      "\tthis percentage of the total across all flows. Only allowed when the\n"
      "\tprimary value field is Bytes, Packets, or Records.")},

    {STATSUNIQ_PROGRAM_STATS,
     {"top",                 NO_ARG,       0, OPT_TOP},
     ("Print the top N keys and their values. Def. Yes")},
    {STATSUNIQ_PROGRAM_STATS,
     {"bottom",              NO_ARG,       0, OPT_BOTTOM},
     ("Print the bottom N keys and their values. Def. No")},

    {STATSUNIQ_PROGRAM_UNIQ,
     {"all-counts",          NO_ARG,       0, OPT_ALL_COUNTS},
     ("Enable the next five switches--count everything.  If no\n"
      "\tcount is specified, flows are counted.  Def. No")},
    {STATSUNIQ_PROGRAM_UNIQ,
     {"bytes",               OPTIONAL_ARG, 0, OPT_BYTES},
     ("Sum bytes in each bin; optionally choose to print\n"
      "\tbins whose total is in given range;"
      " range is MIN or MIN-MAX. Def. No")},
    {STATSUNIQ_PROGRAM_UNIQ,
     {"packets",             OPTIONAL_ARG, 0, OPT_PACKETS},
     ("Sum packets in each bin; optionally choose to print\n"
      "\tbins whose total is in given range;"
      " range is MIN or MIN-MAX. Def. No")},
    {STATSUNIQ_PROGRAM_UNIQ,
     {"flows",               OPTIONAL_ARG, 0, OPT_FLOWS},
     ("Count flow records in each bin; optionally choose to print\n"
      "\tbins whose count is in given range;"
      " range is MIN or MIN-MAX. Def. No")},
    {STATSUNIQ_PROGRAM_UNIQ,
     {"stime",               NO_ARG,       0, OPT_STIME},
     ("Print earliest time flow was seen in each bin. Def. No")},
    {STATSUNIQ_PROGRAM_UNIQ,
     {"etime",               NO_ARG,       0, OPT_ETIME},
     ("Print latest time flow was seen  in each bin. Def. No")},
    {STATSUNIQ_PROGRAM_UNIQ,
     {"sip-distinct",        OPTIONAL_ARG, 0, OPT_SIP_DISTINCT},
     (("Count distinct sIPs in each bin; optionally choose to\n"
       "\tprint bins whose count is in range;"
       " range is MIN or MIN-MAX. Def. No"))},
    {STATSUNIQ_PROGRAM_UNIQ,
     {"dip-distinct",        OPTIONAL_ARG, 0, OPT_DIP_DISTINCT},
     (("Count distinct dIPs in each bin; optionally choose to\n"
       "\tprint bins whose count is in range;"
       " range is MIN or MIN-MAX. Def. No"))},

    {STATSUNIQ_PROGRAM_BOTH,
     {"presorted-input",     NO_ARG,       0, OPT_PRESORTED_INPUT},
     ("Assume input has been presorted using\n"
      "\trwsort invoked with the exact same --fields value. Def. No")},

    {STATSUNIQ_PROGRAM_STATS,
     {"no-percents",         NO_ARG,       0, OPT_NO_PERCENTS},
     ("Do not print the percentage columns. Def. Print percents")},

    {STATSUNIQ_PROGRAM_UNIQ,
     {"sort-output",         NO_ARG,       0, OPT_SORT_OUTPUT},
     ("Present the output in sorted order. Def. No")},

    {STATSUNIQ_PROGRAM_BOTH,
     {"bin-time",            OPTIONAL_ARG, 0, OPT_BIN_TIME},
     ("When using 'sTime' or 'eTime' as a key, adjust time(s) to\n"
      "\tappear in N-second bins (floor of time is used). Def. No, ")},
    {STATSUNIQ_PROGRAM_BOTH,
     {"integer-sensors",     NO_ARG,       0, OPT_INTEGER_SENSORS},
     ("Print sensor as an integer. Def. Sensor name")},
    {STATSUNIQ_PROGRAM_BOTH,
     {"integer-tcp-flags",   NO_ARG,       0, OPT_INTEGER_TCP_FLAGS},
     ("Print TCP Flags as an integer. Def. No")},
    {STATSUNIQ_PROGRAM_BOTH,
     {"no-titles",           NO_ARG,       0, OPT_NO_TITLES},
     ("Do not print column titles. Def. Print titles")},
    {STATSUNIQ_PROGRAM_BOTH,
     {"no-columns",          NO_ARG,       0, OPT_NO_COLUMNS},
     ("Disable fixed-width columnar output. Def. Columnar")},
    {STATSUNIQ_PROGRAM_BOTH,
     {"column-separator",    REQUIRED_ARG, 0, OPT_COLUMN_SEPARATOR},
     ("Use specified character between columns. Def. '|'")},
    {STATSUNIQ_PROGRAM_BOTH,
     {"no-final-delimiter",  NO_ARG,       0, OPT_NO_FINAL_DELIMITER},
     ("Suppress column delimiter at end of line. Def. No")},
    {STATSUNIQ_PROGRAM_BOTH,
     {"delimited",           OPTIONAL_ARG, 0, OPT_DELIMITED},
     ("Shortcut for --no-columns --no-final-del --column-sep=CHAR")},
    {STATSUNIQ_PROGRAM_BOTH,
     {"output-path",         REQUIRED_ARG, 0, OPT_OUTPUT_PATH},
     ("Write the output to this stream or file. Def. stdout")},
    {STATSUNIQ_PROGRAM_BOTH,
     {"pager",               REQUIRED_ARG, 0, OPT_PAGER},
     ("Invoke this program to page output. Def. $SILK_PAGER or $PAGER")},

    {STATSUNIQ_PROGRAM_BOTH,    /* sentinel entry */
     {0,0,0,0},
     (char *)NULL}
};

/* a number greater than the number of options; used to define an
 * array */
#define STATSUNIQ_NUM_OPTIONS  40


/* LOCAL FUNCTION PROTOTYPES */

static int  appOptionsHandler(clientData cData, int opt_index, char *opt_arg);
static void appHandleSignal(int sig);

static void helpFields(FILE *fh);

static lua_State *appLuaCreateState(void);
static int  createStringmaps(void);
static int  parseKeyFields(const char *field_string);
static int  parseValueFields(const char *field_string);
static int
appAddPlugin(
    sk_stringmap_entry_t   *sm_entry,
    field_type_t            field_type);
static int
appAddSidecar(
    sk_stringmap_entry_t   *sm_entry,
    field_type_t            field_type);
static int
isFieldDuplicate(
    const sk_fieldlist_t   *flist,
    sk_fieldid_t            fid,
    const void             *fcontext);
static int  readRecord(skstream_t *stream, rwRec *rwrec);



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

    /* use two macros to avoid CPP limit in c89 */
#define USAGE_MSG_STATS1                                                      \
    ("<SWITCHES> [FILES]\n"                                                   \
     "\tSummarize SiLK Flow records by the specified field(s) into bins.\n"   \
     "\tFor each bin, compute the specified value(s), then display the\n"     \
     "\tresults as a Top-N or Bottom-N list based on the primary value.\n"    \
     "\tThe N may be a fixed value; some values allow the N to be a\n"        \
     "\tthreshold value or to be based on a percentage of the input.\n")
#define USAGE_MSG_STATS2                                                      \
    ("\tAlternatively, provide statistics for each of bytes, packets, and\n"  \
     "\tbytes-per-packet giving minima, maxima, quartile, and interval\n"     \
     "\tflow-counts across all flows or across user-specified protocols.\n"   \
     "\tWhen no files are given on command line, flows are read from STDIN.\n")

#define USAGE_MSG_UNIQ                                                        \
    ("--fields=N [SWITCHES] [FILES]\n"                                        \
     "\tSummarize SiLK Flow records into user-defined keyed bins specified\n" \
     "\twith the --fields switch.  For each keyed bin, print byte, packet,\n" \
     "\tand/or flow counts and/or the time window when key was active.\n"     \
     "\tWhen no files are given on command line, flows are read from STDIN.\n")

    /* Create the string maps for --fields and --values */
    createStringmaps();

    if (STATSUNIQ_PROGRAM_STATS == this_program) {
        fprintf(fh, "%s %s%s", skAppName(), USAGE_MSG_STATS1,USAGE_MSG_STATS2);
    } else {
        fprintf(fh, "%s %s", skAppName(), USAGE_MSG_UNIQ);
        fprintf(fh, "\nSWITCHES:\n");
        skOptionsDefaultUsage(fh);
    }

    for (i = 0; appOptions[i].opt.name; ++i) {
        if (this_program & appOptions[i].use_opt) {
            /* Print a header before some options */
            switch (appOptions[i].opt.val) {
              case OPT_OVERALL_STATS:
                fprintf(fh, "\nPROTOCOL STATISTICS SWITCHES:\n");
                break;
              case OPT_FIELDS:
                if (STATSUNIQ_PROGRAM_STATS == this_program) {
                    fprintf(fh, "\nTOP-N/BOTTOM-N SWITCHES:\n");
                }
                break;
              case OPT_COUNT:
                fprintf(fh, ("\nHow to determine the N for Top-/Bottom-N;"
                             " must specify one:\n"));
                break;
              case OPT_TOP:
                fprintf(fh, ("\nWhether to compute Top- or Bottom-N;"
                             " may specify one (top is default):\n"));
                break;
              case OPT_PRESORTED_INPUT:
                if (STATSUNIQ_PROGRAM_STATS == this_program) {
                    fprintf(fh, "\nMISCELLANEOUS SWITCHES:\n");
                    skOptionsDefaultUsage(fh);
                }
                break;
              case OPT_BIN_TIME:
                skOptionsCtxOptionsUsage(optctx, fh);
                break;
              case OPT_INTEGER_SENSORS:
                skOptionsTimestampFormatUsage(fh);
                skOptionsIPFormatUsage(fh);
                break;
              default:
                break;
            }

            fprintf(fh, "--%s %s. ", appOptions[i].opt.name,
                    SK_OPTION_HAS_ARG(appOptions[i].opt));
            switch ((appOptionsEnum)appOptions[i].opt.val) {
              case OPT_FIELDS:
                /* Dynamically build the help */
                fprintf(fh, "%s\n", appOptions[i].help);
                skStringMapPrintUsage(key_field_map, fh, 4);
                break;
              case OPT_VALUES:
                fprintf(fh, "%s\n", appOptions[i].help);
                skStringMapPrintUsage(value_field_map, fh, 4);
                break;
              case OPT_BIN_TIME:
                fprintf(fh, "%s%d\n", appOptions[i].help, DEFAULT_TIME_BIN);
                break;
              default:
                /* Simple help text */
                fprintf(fh, "%s\n", appOptions[i].help);
                break;
            }
        }
    }

    skOptionsTempDirUsage(fh);
    sksiteOptionsUsage(fh);
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
    static struct option app_options[STATSUNIQ_NUM_OPTIONS];
    SILK_FEATURES_DEFINE_STRUCT(features);
    unsigned int optctx_flags;
    unsigned int i;
    unsigned int app_options_count;
    int rv;
    int j;

    assert((sizeof(appOptions)/sizeof(appOptions[0])) < STATSUNIQ_NUM_OPTIONS);

    /* register the application */
    skAppRegister(argv[0]);
    skAppVerifyFeatures(&features, NULL);
    skOptionsSetUsageCallback(&appUsageLong);

    /* initialize globals */
    memset(&app_flags, 0, sizeof(app_flags));
    memset(&output, 0, sizeof(output));
    output.of_fp = stdout;
    memset(&limit, 0, sizeof(limit));
    limit.direction = RWSTATS_DIR_TOP;
    limit.type = RWSTATS_ALL;

    optctx_flags = (SK_OPTIONS_CTX_INPUT_SILK_FLOW | SK_OPTIONS_CTX_ALLOW_STDIN
                    | SK_OPTIONS_CTX_XARGS | SK_OPTIONS_CTX_PRINT_FILENAMES
                    | SK_OPTIONS_CTX_COPY_INPUT | SK_OPTIONS_CTX_IPV6_POLICY);

    /* initialize plugin library */
    if (STATSUNIQ_PROGRAM_STATS == this_program) {
        skPluginSetup(2, SKPLUGIN_APP_STATS_FIELD, SKPLUGIN_APP_STATS_VALUE);
    } else {
        skPluginSetup(2, SKPLUGIN_APP_UNIQ_FIELD, SKPLUGIN_APP_UNIQ_VALUE);
    }

    /* skOptionsRegister() requires an array of struct option */
    app_options_count = 0;
    for (i = 0; appOptions[i].opt.name; ++i) {
        if (this_program & appOptions[i].use_opt) {
            assert(app_options_count < STATSUNIQ_NUM_OPTIONS);
            app_options[app_options_count] = appOptions[i].opt;
            ++app_options_count;
        }
    }

    /* register the options */
    if (skOptionsCtxCreate(&optctx, optctx_flags)
        || skOptionsCtxOptionsRegister(optctx)
        || skOptionsRegister(app_options, &appOptionsHandler, NULL)
        || skOptionsTempDirRegister(&temp_directory)
        || skOptionsTimestampFormatRegister(&time_flags, time_register_flags)
        || skOptionsIPFormatRegister(&ip_format)
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
    L = appLuaCreateState();

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

    /* create flow iterator to read the records */
    flowiter = skOptionsCtxCreateFlowIterator(optctx);

    if (help_fields) {
        helpFields(USAGE_FH);
        exit(EXIT_SUCCESS);
    }

    ipv6_policy = skOptionsCtxGetIPv6Policy(optctx);

    /* try to load site config file; if it fails, we will not be able
     * to resolve flowtype and sensor from input file names, but we
     * should not consider it a complete failure */
    sksiteConfigure(0);

    if (STATSUNIQ_PROGRAM_STATS == this_program) {
        if (proto_stats) {
            /* skip much of the following */
            goto CHECK_OUTPUT;
        }

        /* verify that we have an N for our top-N */
        if (!limit.seen) {
            /* remove this block if we want printing all bins to be
             * the default behavior of rwstats */
            skAppPrintErr(("No stopping condition was entered.\n"
                           "\tChoose one of --%s, --%s, or --%s"),
                          appOptions[OPT_COUNT].opt.name,
                          appOptions[OPT_THRESHOLD].opt.name,
                          appOptions[OPT_PERCENTAGE].opt.name);
            skAppUsage();
        }
    }

    /* set up the key_field_map and value_field_map */
    if (createStringmaps()) {
        appExit(EXIT_FAILURE);
    }

    /* make sure the user specified the --fields switch */
    if (fields_arg == NULL || fields_arg[0] == '\0') {
        skAppPrintErr("The --%s switch is required",
                      appOptions[OPT_FIELDS].opt.name);
        skAppUsage();         /* never returns */
    }

    /* create the formatter */
    fmtr = sk_formatter_create();

    /* parse the --fields and --values switches */
    if (parseKeyFields(fields_arg)) {
        appExit(EXIT_FAILURE);
    }
    if (parseValueFields(values_arg)) {
        appExit(EXIT_FAILURE);
    }

    /* determine the number of sidecar fields defined in --lua-file;
     * the count is not really important---we only need to know
     * whether to call the function that adds the sidecar fields. */
    lua_rawgeti(L, LUA_REGISTRYINDEX, reg_ref.count_functions);
    rv = lua_pcall(L, 0, 1, 0);
    if (rv != LUA_OK) {
        skAppPrintErr("Unable to get number of added functions: %s",
                      lua_tostring(L, -1));
        lua_pop(L, 1);
        assert(0 == lua_gettop(L));
        appExit(EXIT_FAILURE);
    }
    num_sidecar_adds = lua_tointeger(L, -1);
    lua_pop(L, 1);
    assert(0 == lua_gettop(L));

    /* set properties on the formatter */
    sk_formatter_set_delimeter(fmtr, delimiter);
    if (ipv6_policy < SK_IPV6POLICY_MIX) {
        sk_formatter_set_assume_ipv4_ips(fmtr);
    }
    sk_formatter_set_default_ipaddr_format(fmtr, (skipaddr_flags_t)ip_format);
    sk_formatter_set_default_timestamp_format(fmtr, time_flags);

    if (app_flags.no_columns) {
        sk_formatter_set_no_columns(fmtr);
    }
    if (app_flags.no_final_delimiter) {
        sk_formatter_set_no_final_delimeter(fmtr);
    }
    sk_formatter_finalize(fmtr);

    /* create and initialize the uniq object */
    if (app_flags.presorted_input) {
        /* cannot use the --percentage limit when using
         * --presorted-input */
        if (RWSTATS_PERCENTAGE == limit.type) {
            skAppPrintErr(("The --%s limit is not supported"
                           " when --%s is active"),
                          appOptions[OPT_PERCENTAGE].opt.name,
                          appOptions[OPT_PRESORTED_INPUT].opt.name);
            appExit(EXIT_FAILURE);
        }

        if (skPresortedUniqueCreate(&ps_uniq)) {
            appExit(EXIT_FAILURE);
        }

        skPresortedUniqueSetTempDirectory(ps_uniq, temp_directory);

        if (skPresortedUniqueSetFields(
                ps_uniq, key_fields, distinct_fields, value_fields))
        {
            skAppPrintErr("Unable to set fields");
            appExit(EXIT_FAILURE);
        }
        skPresortedUniqueSetFlowIterator(ps_uniq, flowiter);
        skPresortedUniqueSetReadFn(ps_uniq, readRecord);

    } else {
        if (skUniqueCreate(&uniq)) {
            appExit(EXIT_FAILURE);
        }
        if (app_flags.sort_output) {
            assert(STATSUNIQ_PROGRAM_UNIQ == this_program);
            skUniqueSetSortedOutput(uniq);
        }

        skUniqueSetTempDirectory(uniq, temp_directory);

        if (skUniqueSetFields(uniq, key_fields, distinct_fields, value_fields)
            || skUniquePrepareForInput(uniq))
        {
            skAppPrintErr("Unable to set fields");
            appExit(EXIT_FAILURE);
        }
    }

  CHECK_OUTPUT:
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

    /* open the --output-path.  the 'of_name' member is NULL if user
     * didn't get an output-path. */
    if (output.of_name) {
        rv = skFileptrOpen(&output, SK_IO_WRITE);
        if (rv) {
            skAppPrintErr("Unable to open %s '%s': %s",
                          appOptions[OPT_OUTPUT_PATH].opt.name,
                          output.of_name, skFileptrStrerror(rv));
            appExit(EXIT_FAILURE);
        }
    }

    /* open the --copy-input destination */
    if (skOptionsCtxOpenStreams(optctx, &skAppPrintErr)) {
        appExit(EXIT_FAILURE);
    }

    /* set signal handler to clean up temp files on SIGINT, SIGTERM, etc */
    if (skAppSetSignalHandler(&appHandleSignal)) {
        appExit(EXIT_FAILURE);
    }

    return;                       /* OK */
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
    static int teardownFlag = 0;
    int rv;

    if (teardownFlag) {
        return;
    }
    teardownFlag = 1;

    skUniqueDestroy(&uniq);
    skPresortedUniqueDestroy(&ps_uniq);

    /* destroy field lists */
    skFieldListDestroy(&key_fields);
    skFieldListDestroy(&distinct_fields);
    skFieldListDestroy(&value_fields);

    /* plugin teardown */
    skPluginRunCleanup(SKPLUGIN_FN_ANY);
    skPluginTeardown();

    /* destroy output */
    sk_formatter_destroy(fmtr);
    fmtr = NULL;

    /* close output */
    if (output.of_name) {
        skFileptrClose(&output, &skAppPrintErr);
    }
    /* close the --copy-input */
    skOptionsCtxCopyStreamClose(optctx, skAppPrintErr);

    sk_flow_iter_destroy(&flowiter);

    /* destroy string maps for keys and values */
    skStringMapDestroy(key_field_map);
    key_field_map = NULL;
    skStringMapDestroy(value_field_map);
    value_field_map = NULL;

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

    if (sc_field_vec) {
        sidecar_field_t *sc_field;
        size_t n;

        n = sk_vector_get_count(sc_field_vec);
        while (n > 0) {
            --n;
            sk_vector_get_value(sc_field_vec, n, &sc_field);
            free(sc_field->scf_name);
            free(sc_field);
        }
        sk_vector_destroy(sc_field_vec);
    }

    sk_sidecar_destroy(&sidecar);
    sk_lua_closestate(L);

    skOptionsCtxDestroy(&optctx);
    skAppUnregister();
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
    static int saw_direction = 0;
    uint32_t val32;
    size_t i;
    int rv;

    switch ((appOptionsEnum)opt_index) {
      case OPT_HELP_FIELDS:
        help_fields = 1;
        break;

      case OPT_FIELDS:
        if (fields_arg) {
            skAppPrintErr("Invalid %s: Switch used multiple times",
                          appOptions[opt_index].opt.name);
            return 1;
        }
        fields_arg = opt_arg;
        break;

      case OPT_VALUES:
        if (values_arg) {
            skAppPrintErr("Invalid %s: Switch used multiple times",
                          appOptions[opt_index].opt.name);
            return 1;
        }
        values_arg = opt_arg;
        break;

      case OPT_TOP:
      case OPT_BOTTOM:
        if (saw_direction) {
            skAppPrintErr("May only specify one of --%s or --%s.",
                          appOptions[OPT_TOP].opt.name,
                          appOptions[OPT_BOTTOM].opt.name);
            return 1;
        }
        saw_direction = 1;
        if (OPT_TOP == opt_index) {
            limit.direction = RWSTATS_DIR_TOP;
        } else {
            limit.direction = RWSTATS_DIR_BOTTOM;
        }
        break;

      case OPT_COUNT:
      case OPT_THRESHOLD:
      case OPT_PERCENTAGE:
        if (limit.seen != 0) {
            skAppPrintErr("May only specify one of --%s, --%s, or --%s.",
                          appOptions[OPT_COUNT].opt.name,
                          appOptions[OPT_THRESHOLD].opt.name,
                          appOptions[OPT_PERCENTAGE].opt.name);
            return 1;
        }
        limit.type = ((rwstats_limit_type_t)
                      (RWSTATS_COUNT + (opt_index - OPT_COUNT)));
        if (OPT_PERCENTAGE == opt_index) {
            rv = skStringParseDouble(&limit.value[limit.type].d, opt_arg,
                                     0.0, 100.0);
        } else {
            rv = skStringParseUint64(&limit.value[limit.type].u64, opt_arg,
                                     0, 0);
        }
        if (rv) {
            goto PARSE_ERROR;
        }
        if (OPT_COUNT == opt_index && 0 == limit.value[limit.type].u64) {
            limit.type = RWSTATS_ALL;
        }
        limit.seen = 1;
        break;

      case OPT_OVERALL_STATS:
        /* combined stats for all protocols */
        proto_stats = 1;
        break;

      case OPT_DETAIL_PROTO_STATS:
        /* detailed stats for specific proto */
        if (0 != protoStatsParse(opt_arg)) {
            return 1;
        }
        proto_stats = 1;
        break;

      case OPT_ALL_COUNTS:
        for (i = 0; i < num_builtin_values; ++i) {
            if (builtin_values[i].bf_all_counts) {
                builtin_values[i].bf_switched_on = 1;
            }
        }
        break;

      case OPT_BYTES:
      case OPT_PACKETS:
      case OPT_FLOWS:
      case OPT_STIME:
      case OPT_ETIME:
      case OPT_SIP_DISTINCT:
      case OPT_DIP_DISTINCT:
        i = opt_index - OPT_BYTES;
        builtin_values[i].bf_switched_on = 1;
        if (opt_arg) {
            rv = skStringParseRange64(&builtin_values[i].bf_min,
                                      &builtin_values[i].bf_max,
                                      opt_arg, 0, 0,
                                      SKUTILS_RANGE_SINGLE_OPEN);
            if (rv) {
                goto PARSE_ERROR;
            }
            /* treat a single value as having no max, not as a range
             * of a single value */
            if ((builtin_values[i].bf_min == builtin_values[i].bf_max)
                && !strchr(opt_arg, '-'))
            {
                builtin_values[i].bf_max = UINT64_MAX;
            }
            app_flags.check_limits = 1;
        }
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

      case OPT_PLUGIN:
        if (skPluginLoadPlugin(opt_arg, 1) != 0) {
            skAppPrintErr("Unable to load %s as a plugin", opt_arg);
            return 1;
        }
        break;

      case OPT_BIN_TIME:
        if (opt_arg == NULL || opt_arg[0] == '\0') {
            /* no time given; use default */
            time_bin_size = sktimeCreate(DEFAULT_TIME_BIN, 0);
        } else {
            /* parse user's time */
            rv = skStringParseUint32(&val32, opt_arg, 1, 0);
            if (rv) {
                goto PARSE_ERROR;
            }
            time_bin_size = sktimeCreate(val32, 0);
        }
        break;

      case OPT_PRESORTED_INPUT:
        app_flags.presorted_input = 1;
        break;

      case OPT_NO_PERCENTS:
        app_flags.no_percents = 1;
        break;

      case OPT_SORT_OUTPUT:
        app_flags.sort_output = 1;
        break;

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
        delimiter = opt_arg[0];
        break;

      case OPT_DELIMITED:
        app_flags.no_columns = 1;
        app_flags.no_final_delimiter = 1;
        if (opt_arg) {
            delimiter = opt_arg[0];
        }
        break;

      case OPT_OUTPUT_PATH:
        if (output.of_name) {
            skAppPrintErr("Invalid %s: Switch used multiple times",
                          appOptions[opt_index].opt.name);
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
                  appOptions[opt_index].opt.name, opt_arg,
                  skStringParseStrerror(rv));
    return 1;
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
            appOptions[OPT_FIELDS].opt.name);
    skStringMapPrintDetailedUsage(key_field_map, fh);

    fprintf(fh,
            ("\n"
             "The following names may be used in the --%s switch. Names are\n"
             "case-insensitive and may be abbreviated to the shortest"
             " unique prefix.\n"),
            appOptions[OPT_VALUES].opt.name);
    skStringMapPrintDetailedUsage(value_field_map, fh);
}


/*
 *    A helper function for appLuaCreateState().
 *
 *    This function expects the table of functions exported by
 *    rwstats.lua to be at the top of the stack.  This function finds
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
 *    "rwstats.lua" into that state.  Set some functions defined in
 *    rwstats.lua as Lua globals, store others in the Lua registry and
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

    /* load and run the the initialization code in rwstats.lua.  The
     * return value is a table of functions. */
    rv = luaL_loadbufferx(S, (const char*)rwstats_lua,
                          sizeof(rwstats_lua), "rwstats.lua", "b");
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
 *  value_to_ascii(rwrec, buf, bufsize, field_entry, extra);
 *
 *    Invoked by sk_formatter_record_to_string_extra() to get the
 *    value for an aggregate value field.  This function is called for
 *    built-in aggregate values as well as plug-in defined values.
 *
 *    Fill 'buf' with the value for the column represented by the
 *    aggregate value field list entry 'field_entry'.  'rwrec' is
 *    ignored; 'extra' is an array[3] that contains the buffers for
 *    the key, aggregate value, and distinct field-lists.  This
 *    function should write no more than 'bufsize' characters to
 *    'buf'.
 */
static int
value_to_ascii(
    const rwRec UNUSED(*rwrec),
    char               *text_buf,
    size_t              text_buf_size,
    void               *v_fl_entry,
    void               *v_outbuf)
{
    sk_fieldentry_t *fl_entry = (sk_fieldentry_t*)v_fl_entry;
    uint64_t val64;
    uint32_t val32;
    uint8_t bin_buf[HASHLIB_MAX_VALUE_WIDTH];

    switch (skFieldListEntryGetId(fl_entry)) {
      case SK_FIELD_SUM_BYTES:
      case SK_FIELD_SUM_PACKETS:
        skFieldListExtractFromBuffer(value_fields, ((uint8_t**)v_outbuf)[1],
                                     fl_entry, (uint8_t*)&val64);
        return snprintf(text_buf, text_buf_size, ("%" PRIu64), val64);

      case SK_FIELD_RECORDS:
      case SK_FIELD_SUM_ELAPSED:
        skFieldListExtractFromBuffer(value_fields, ((uint8_t**)v_outbuf)[1],
                                     fl_entry, (uint8_t*)&val32);
        return snprintf(text_buf, text_buf_size, ("%" PRIu32), val32);

      case SK_FIELD_MIN_STARTTIME:
      case SK_FIELD_MAX_ENDTIME:
        skFieldListExtractFromBuffer(value_fields, ((uint8_t**)v_outbuf)[1],
                                     fl_entry, (uint8_t*)&val32);
        assert(text_buf_size > SKTIMESTAMP_STRLEN);
        sktimestamp_r(text_buf, sktimeCreate(val32, 0), time_flags);
        return (int)strlen(text_buf);

      case SK_FIELD_CALLER:
        /* get the binary value from the field-list */
        skFieldListExtractFromBuffer(value_fields, ((uint8_t**)v_outbuf)[1],
                                     fl_entry, bin_buf);
        /* call the plug-in to convert from binary to text */
        skPluginFieldRunBinToTextFn(
            (skplugin_field_t*)skFieldListEntryGetContext(fl_entry),
            text_buf, text_buf_size, bin_buf);
        return (int)strlen(text_buf);

      default:
        skAbortBadCase(skFieldListEntryGetId(fl_entry));
    }

    return 0;                   /* NOTREACHED */
}


/*
 *  distinct_to_ascii(rwrec, buf, bufsize, field_entry, extra);
 *
 *    Invoked by sk_formatter_record_to_string_extra() to get the
 *    value for a distinct field.  This function is called for
 *    built-in distinct fields as well as those from a plug-in.
 *
 *    Fill 'buf' with the value for the column represented by the
 *    distinct field list entry 'field_entry'.  'rwrec' is ignored;
 *    'extra' is an array[3] that contains the buffers for the key,
 *    aggregate value, and distinct field-lists.  This function should
 *    write no more than 'bufsize' characters to 'buf'.
 */
static int
distinct_to_ascii(
    const rwRec UNUSED(*rwrec),
    char               *text_buf,
    size_t              text_buf_size,
    void               *v_fl_entry,
    void               *v_outbuf)
{
    sk_fieldentry_t *fl_entry = (sk_fieldentry_t*)v_fl_entry;
    size_t len;
    union value_un {
        uint8_t   ar[HASHLIB_MAX_VALUE_WIDTH];
        uint64_t  u64;
        uint32_t  u32;
        uint16_t  u16;
        uint8_t   u8;
    } value;

    len = skFieldListEntryGetBinOctets(fl_entry);
    switch (len) {
      case 1:
        skFieldListExtractFromBuffer(distinct_fields, ((uint8_t**)v_outbuf)[2],
                                     fl_entry, &value.u8);
        return snprintf(text_buf, text_buf_size, ("%" PRIu8), value.u8);
      case 2:
        skFieldListExtractFromBuffer(distinct_fields, ((uint8_t**)v_outbuf)[2],
                                     fl_entry, (uint8_t*)&value.u16);
        return snprintf(text_buf, text_buf_size, ("%" PRIu16), value.u16);
      case 4:
        skFieldListExtractFromBuffer(distinct_fields, ((uint8_t**)v_outbuf)[2],
                                     fl_entry, (uint8_t*)&value.u32);
        return snprintf(text_buf, text_buf_size, ("%" PRIu32), value.u32);
      case 8:
        skFieldListExtractFromBuffer(distinct_fields, ((uint8_t**)v_outbuf)[2],
                                     fl_entry, (uint8_t*)&value.u64);
        return snprintf(text_buf, text_buf_size, ("%" PRIu64), value.u64);

      case 3:
      case 5:
      case 6:
      case 7:
        value.u64 = 0;
#if SK_BIG_ENDIAN
        skFieldListExtractFromBuffer(distinct_fields, ((uint8_t**)v_outbuf)[2],
                                     fl_entry, &value.ar[8 - len]);
#else
        skFieldListExtractFromBuffer(distinct_fields, ((uint8_t**)v_outbuf)[2],
                                     fl_entry, &value.ar[0]);
#endif  /* #else of #if SK_BIG_ENDIAN */
        return snprintf(text_buf, text_buf_size, ("%" PRIu64), value.u64);

      default:
        skFieldListExtractFromBuffer(distinct_fields, ((uint8_t**)v_outbuf)[2],
                                     fl_entry, value.ar);
        return snprintf(text_buf, text_buf_size, ("%" PRIu64), value.u64);
    }
    return 0;                   /* NOTREACHED */
}


/*
 *  row_percent_to_ascii(rwrec, buf, bufsize, field_entry, outbuf);
 *
 *    Invoked by sk_formatter_record_to_string_extra() to fill 'buf'
 *    with the value for the percentage column.  Uses the 'limit'
 *    global to get the field entry.
 *
 *    This function also updates the global cumulative percentage
 *    value 'cumul_pct'.
 */
static int
row_percent_to_ascii(
    const rwRec UNUSED(*rwrec),
    char               *text_buf,
    size_t              text_buf_size,
    void        UNUSED(*v_fl_entry),
    void               *v_outbuf)
{
    double percent;
    uint64_t val64;
    uint32_t val32;

    switch (limit.fl_id) {
      case SK_FIELD_RECORDS:
        skFieldListExtractFromBuffer(value_fields, ((uint8_t**)v_outbuf)[1],
                                     limit.fl_entry, (uint8_t*)&val32);
        percent = 100.0 * (double)val32 / value_total;
        cumul_pct += percent;
        return snprintf(text_buf, text_buf_size, "%.6f", percent);

      case SK_FIELD_SUM_BYTES:
      case SK_FIELD_SUM_PACKETS:
        skFieldListExtractFromBuffer(value_fields, ((uint8_t**)v_outbuf)[1],
                                     limit.fl_entry, (uint8_t*)&val64);
        percent = 100.0 * (double)val64 / value_total;
        cumul_pct += percent;
        return snprintf(text_buf, text_buf_size, "%.6f", percent);;

      default:
        return snprintf(text_buf, text_buf_size, "?");
    }
    return 0;                   /* NOTREACHED */
}


/*
 *  cumul_percent_to_ascii(rwrec, buf, bufsize, field_entry, outbuf);
 *
 *    Invoked by sk_formatter_record_to_string_extra() to fill 'buf'
 *    with the value for the cumulative percentage column.  Uses the
 *    global cumulative percentage value 'cumul_pct'.
 */
static int
cumul_percent_to_ascii(
    const rwRec UNUSED(*rwrec),
    char               *text_buf,
    size_t              text_buf_size,
    void        UNUSED(*v_fl_entry),
    void        UNUSED(*v_outbuf))
{
    switch (limit.fl_id) {
      case SK_FIELD_RECORDS:
      case SK_FIELD_SUM_BYTES:
      case SK_FIELD_SUM_PACKETS:
        return snprintf(text_buf, text_buf_size, "%.6f", cumul_pct);

      default:
        return snprintf(text_buf, text_buf_size, "?");
    }
    return 0;                   /* NOTREACHED */
}


/*
 *    Invoked by sk_formatter_record_to_string_extra() to get the
 *    value for a key field that is defined by a plug-in.
 *
 *    Fill 'text_buf' with a textual representation of the key for the
 *    column represented by the plug-in associated with the
 *    field-entry 'v_fl_entry'.  'rwrec' is ignored; 'v_outbuf' is an
 *    array[3] that contains the binary buffers for the key, aggregate
 *    value, and distinct field-lists.  (This function only uses the
 *    key portion of the array.)  This function should write no more
 *    than 'text_buf_size' characters to 'buf'.  Return the string
 *    length of 'text_buf'.
 */
static int
plugin_key_to_ascii(
    const rwRec UNUSED(*rwrec),
    char               *text_buf,
    size_t              text_buf_size,
    void               *v_fl_entry,
    void               *v_outbuf)
{
    sk_fieldentry_t *fl_entry = (sk_fieldentry_t*)v_fl_entry;
    uint8_t bin_buf[HASHLIB_MAX_KEY_WIDTH];

    /* get the binary value from the field-list */
    skFieldListExtractFromBuffer(key_fields, ((uint8_t**)v_outbuf)[0],
                                 fl_entry, bin_buf);

    /* call the plug-in to convert from binary to text */
    skPluginFieldRunBinToTextFn(
        (skplugin_field_t*)skFieldListEntryGetContext(fl_entry),
        text_buf, text_buf_size, bin_buf);

    return strlen(text_buf);
}

/*
 *    Fill 'out_buf' with the binary value of the plug-in field
 *    represented by 'v_pi_field' on the record 'rwrec'.
 *
 *    Invoked as a callback by the skunique object when inserting
 *    values from a record and building the key.
 *
 *    The size of the buffer 'out_buf' was specified when the field
 *    was added to the field-list.
 */
static void
plugin_rec_to_bin(
    const rwRec        *rwrec,
    uint8_t            *out_buf,
    void               *v_pi_field)
{
    skPluginFieldRunRecToBinFn((skplugin_field_t*)v_pi_field,
                               out_buf, rwrec, NULL);
}

/*
 *    Given a binary value in 'in_out_buf', add-to/merge-with it the
 *    binary value of the plug-in field represented by 'v_pi_field' on
 *    the record 'rwrec'.
 *
 *    Invoked as a callback by the skunique object when updating a
 *    value field with a value from a record.
 *
 *    The size of 'in_out_buf' was specified when the field was added
 *    to the field-list.
 */
static void
plugin_add_rec_to_bin(
    const rwRec        *rwrec,
    uint8_t            *in_out_buf,
    void               *v_pi_field)
{
    skPluginFieldRunAddRecToBinFn((skplugin_field_t*)v_pi_field,
                                  in_out_buf, rwrec, NULL);
}

/*
 *    Compare the value in 'buf1' with the value in 'buf2' for the
 *    plug-in field 'v_pi_field'.
 *
 *    Invoked as a callback by the skunique object when comparing
 *    portions of a key field (used by rwuniq --sort) or when
 *    comparing aggregate values (used by rwstats).
 *
 *    The size of 'buf1' and 'buf2' were specified when the field was
 *    added to the field-list.
 */
static int
plugin_bin_compare(
    const uint8_t      *buf1,
    const uint8_t      *buf2,
    void               *v_pi_field)
{
    int val = 0;
    skPluginFieldRunBinCompareFn((skplugin_field_t*)v_pi_field,
                                 &val, buf1, buf2);
    return val;
}

/*
 *    Given a binary value in 'in_out_buf' and other in 'in_buf' for
 *    the plug-in field represented by 'v_pi_field', add or merge
 *    these two values and store the result in 'in_out_buf'.
 *
 *    Invoked as a callback by the skunique object when aggregate
 *    values from multiple temporary files need to be merged into a
 *    single value.
 *
 *    The size of 'in_out_buf' and 'in_buf' were specified when the
 *    field was added to the field-list.
 */
static void
plugin_bin_merge(
    uint8_t            *in_out_buf,
    const uint8_t      *in_buf,
    void               *v_pi_field)
{
    skPluginFieldRunBinMergeFn((skplugin_field_t*)v_pi_field,
                               in_out_buf, in_buf);
}


/*
 *    Invoked by sk_formatter_record_to_string_extra() to get the
 *    value for a key field that is defined by a sidecar.
 *
 *    Fill 'text_buf' with a textual representation of the key for the
 *    column represented by the sidecar associated with the
 *    field-entry 'v_fl_entry'.  'rwrec' is ignored; 'v_outbuf' is an
 *    array[3] that contains the binary buffers for the key, aggregate
 *    value, and distinct field-lists.  (This function only uses the
 *    key portion of the array.)  This function should write no more
 *    than 'text_buf_size' characters to 'buf'.  Return the string
 *    length of 'text_buf'.
 */
static int
sidecar_key_to_ascii(
    const rwRec UNUSED(*rwrec),
    char               *text_buf,
    size_t              text_buf_size,
    void               *v_fl_entry,
    void               *v_outbuf)
{
    sk_fieldentry_t *fl_entry;
    const sidecar_field_t *sc_field;
    union bin_buf_un {
        uint8_t         u8;
        uint16_t        u16;
        uint32_t        u32;
        uint64_t        u64;
        double          d;
        sktime_t        t;
        uint8_t         ipv6[16];
        int             ref;
        uint8_t         arr[HASHLIB_MAX_KEY_WIDTH];
    } bin_buf;
    int rv;

    fl_entry = (sk_fieldentry_t *)v_fl_entry;
    sc_field = (sidecar_field_t *)skFieldListEntryGetContext(fl_entry);

    /* get the binary value from the field-list */
    skFieldListExtractFromBuffer(key_fields, ((uint8_t**)v_outbuf)[0],
                                 fl_entry, bin_buf.arr);

    /* FIXME: UGH! UGH! UGH!  Why oh why is the code from the
     * formatter repeated here?!  Why can't we just use the support
     * for printing sidecar fields that already exists in the
     * formatter? */

    /* convert from binary to text */
    switch (sc_field->scf_type) {
      case SK_SIDECAR_UINT8:
      case SK_SIDECAR_BOOLEAN:
        {
            return snprintf(text_buf, text_buf_size,
                            "%" PRIu8, bin_buf.u8);
        }
      case SK_SIDECAR_UINT16:
        {
            return snprintf(text_buf, text_buf_size,
                            "%" PRIu16, bin_buf.u16);
        }
      case SK_SIDECAR_UINT32:
        {
            return snprintf(text_buf, text_buf_size,
                            "%" PRIu32, bin_buf.u32);
        }
      case SK_SIDECAR_UINT64:
        {
            return snprintf(text_buf, text_buf_size,
                            "%" PRIu64, bin_buf.u64);
        }
      case SK_SIDECAR_DOUBLE:
        {
            return snprintf(text_buf, text_buf_size,
                            "%f", bin_buf.d);
        }
      case SK_SIDECAR_ADDR_IP4:
        {
            skipaddr_t ipaddr;
            skipaddrSetV4(&ipaddr, &bin_buf.u32);
            skipaddrString(text_buf, &ipaddr, 0);
            return strlen(text_buf);
        }
      case SK_SIDECAR_ADDR_IP6:
        {
            skipaddr_t ipaddr;
            skipaddrSetV6(&ipaddr, bin_buf.ipv6);
            skipaddrString(text_buf, &ipaddr, 0);
            return strlen(text_buf);
        }
      case SK_SIDECAR_DATETIME:
        {
            sktimestamp_r(text_buf, bin_buf.t, 0);
            return strlen(text_buf);
        }
      case SK_SIDECAR_STRING:
        {
            lua_rawgeti(L, LUA_REGISTRYINDEX, str_to_ref);
            if (LUA_TSTRING != lua_rawgeti(L, -1, bin_buf.ref)){
                lua_pop(L, 2);
                break;
            }
            rv = snprintf(text_buf, text_buf_size, "%s", lua_tostring(L, -1));
            lua_pop(L, 2);
            return rv;
        }
      case SK_SIDECAR_BINARY:
        {
            const char *str;
            size_t len;
            size_t i;
            char *tb;
            size_t t;

            lua_rawgeti(L, LUA_REGISTRYINDEX, str_to_ref);
            if (LUA_TSTRING != lua_rawgeti(L, -1, bin_buf.ref)){
                lua_pop(L, 2);
                break;
            }
            str = lua_tolstring(L, -1, &len);
            text_buf[0] = '\0';
            tb = text_buf;
            t = text_buf_size;
            for (i = 0; i < len && t > 1; ++i) {
                if (str[i] == '\\') {
                    if (t < 2) { break; }
                    *tb++ = '\\';
                    *tb++ = '\\';
                    t += 2;
                } else if (isprint((int)str[i])) {
                    *tb++ = str[i];
                } else if (isspace((int)str[i])) {
                    if (t < 2) { break; }
                    switch (str[i]) {
                      case '\t':
                        *tb++ = '\\';
                        *tb++ = 't';
                        break;
                      case '\n':
                        *tb++ = '\\';
                        *tb++ = 'n';
                        break;
                      case '\v':
                        *tb++ = '\\';
                        *tb++ = 'v';
                        break;
                      case '\f':
                        *tb++ = '\\';
                        *tb++ = 'f';
                        break;
                      case '\r':
                        *tb++ = '\\';
                        *tb++ = 'r';
                        break;
                      default:
                        skAbortBadCase(str[i]);
                    }
                    t += 2;
                } else {
                    /* ignore what we have put into the buffer and
                     * print as fully encoded */
                    tb = text_buf;
                    t = text_buf_size;
                    i = len;
                    while (i < len && t > 2) {
                        snprintf(tb, 3, "%02x", (uint8_t)str[i]);
                        ++i;
                        t -= 2;
                        tb += 2;
                    }
                    break;
                }
            }
            rv = tb - text_buf;
            lua_pop(L, 2);
            return rv;
        }
        break;
      case SK_SIDECAR_EMPTY:
        break;
      case SK_SIDECAR_LIST:
      case SK_SIDECAR_TABLE:
        break;
      case SK_SIDECAR_UNKNOWN:
        break;
    }

    text_buf[0] = '\0';
    return strlen(text_buf);
}


/*
 *    Fill 'out_buf' with the binary value of the sidecar field
 *    represented by 'v_sc_field' on the record 'rwrec'.
 *
 *    Invoked as a callback by the skunique object when inserting
 *    values from a record and building the key.
 *
 *    The size of the buffer 'out_buf' was specified when the field
 *    was added to the field-list.
 */
static void
sidecar_rec_to_bin(
    const rwRec        *rwrec,
    uint8_t            *out_buf,
    void               *v_sc_field)
{
    const sidecar_field_t *sc_field;
    int64_t sc_idx;
    int top;

    sc_field = (sidecar_field_t *)v_sc_field;
    top = lua_gettop(L);
    lua_rawgeti(L, LUA_REGISTRYINDEX, str_to_ref);

    /* get the sidecar table for the record, and then the field from
     * the table */
    if (((sc_idx = rwRecGetSidecar(rwrec)) == LUA_NOREF)
        || (lua_rawgeti(L, LUA_REGISTRYINDEX, sc_idx) != LUA_TTABLE)
        || (lua_getfield(L, -1, sc_field->scf_name) == LUA_TNIL))
    {
        switch (sc_field->scf_type) {
          case SK_SIDECAR_STRING:
          case SK_SIDECAR_BINARY:
#ifndef SK_HAVE_ALIGNED_ACCESS_REQUIRED
            *(int *)out_buf = str_to_ref_nil;
#else
            memcpy(out_buf, &str_to_ref_nil, sizeof(str_to_ref_nil));
#endif
            break;
          default:
            memset(out_buf, 0, sc_field->scf_binoct);
            break;
        }
        lua_settop(L, top);
        return;
    }
    switch (sc_field->scf_type) {
      case SK_SIDECAR_UINT8:
        {
            uint8_t n = lua_tointeger(L, -1);
            *out_buf = n;
            break;
        }
      case SK_SIDECAR_UINT16:
        {
            uint16_t n = lua_tointeger(L, -1);
#ifndef SK_HAVE_ALIGNED_ACCESS_REQUIRED
            *(uint16_t *)out_buf = n;
#else
            memcpy(out_buf, &n, sizeof(n));
#endif
            break;
        }
      case SK_SIDECAR_UINT32:
        {
            uint32_t n = lua_tointeger(L, -1);
#ifndef SK_HAVE_ALIGNED_ACCESS_REQUIRED
            *(uint32_t *)out_buf = n;
#else
            memcpy(out_buf, &n, sizeof(n));
#endif
            break;
        }
      case SK_SIDECAR_UINT64:
        {
            uint64_t n = lua_tointeger(L, -1);
#ifndef SK_HAVE_ALIGNED_ACCESS_REQUIRED
            *(uint64_t *)out_buf = n;
#else
            memcpy(out_buf, &n, sizeof(n));
#endif
            break;
        }
      case SK_SIDECAR_DOUBLE:
        {
            double d = lua_tonumber(L, -1);
#ifndef SK_HAVE_ALIGNED_ACCESS_REQUIRED
            *(double *)out_buf = d;
#else
            memcpy(out_buf, &d, sizeof(d));
#endif
            break;
        }
      case SK_SIDECAR_ADDR_IP4:
        {
            const skipaddr_t *ipaddr = sk_lua_toipaddr(L, -1);
            uint32_t ip4;
            ip4 = skipaddrGetV4(ipaddr);
#ifndef SK_HAVE_ALIGNED_ACCESS_REQUIRED
            *(uint32_t *)out_buf = ip4;
#else
            memcpy(out_buf, &ip4, sizeof(ip4));
#endif
            break;
        }
      case SK_SIDECAR_ADDR_IP6:
        {
            const skipaddr_t *ipaddr = sk_lua_toipaddr(L, -1);
            skipaddrGetV6(ipaddr, out_buf);
            break;
        }
      case SK_SIDECAR_DATETIME:
        {
            sktime_t *t = sk_lua_todatetime(L, -1);
#ifndef SK_HAVE_ALIGNED_ACCESS_REQUIRED
            *(sktime_t *)out_buf = *t;
#else
            memcpy(out_buf, &t, sizeof(*t));
#endif
            break;
        }
      case SK_SIDECAR_BOOLEAN:
        {
            *out_buf = lua_toboolean(L, -1);
            break;
        }
      case SK_SIDECAR_EMPTY:
        {
            memset(out_buf, 0, sc_field->scf_binoct);
            break;
        }
      case SK_SIDECAR_STRING:
      case SK_SIDECAR_BINARY:
        {
            /* each unique string is stored in the str_to_ref table
             * twice, once as a str->ref and again as ref->str */
            int ref;

            /* push a copy of the string and then check for it in the
             * string to ref table */
            lua_pushvalue(L, -1);
            if (LUA_TNUMBER == lua_gettable(L, top + 1)) {
                ref = lua_tointeger(L, -1);
            } else {
                /* pop nil from the stack, push the string again and
                 * create a ref->str entry in the str_to_ref table */
                lua_pop(L, 1);
                lua_pushvalue(L, -1);
                ref = luaL_ref(L, top + 1);
                /* add str->ref to the mapping table (str is at top of
                 * stack)*/
                lua_pushinteger(L, ref);
                lua_settable(L, top + 1);
            }
#ifndef SK_HAVE_ALIGNED_ACCESS_REQUIRED
            *(int *)out_buf = ref;
#else
            memcpy(out_buf, &ref, sizeof(ref));
#endif
            break;
        }
      case SK_SIDECAR_LIST:
      case SK_SIDECAR_TABLE:
        memset(out_buf, 0, sc_field->scf_binoct);
        break;
      case SK_SIDECAR_UNKNOWN:
        break;
    }

    lua_settop(L, top);
}


/*
 *    Given a binary value in 'in_out_buf', add-to/merge-with it the
 *    binary value of the sidecar field represented by 'v_sc_field' on
 *    the record 'rwrec'.
 *
 *    Invoked as a callback by the skunique object when updating a
 *    value field with a value from a record.
 *
 *    The size of 'in_out_buf' was specified when the field was added
 *    to the field-list.
 */
static void
sidecar_add_rec_to_bin(
    const rwRec        *rwrec,
    uint8_t            *in_out_buf,
    void               *v_sc_field)
{
    const sidecar_field_t *sc_field;
    int64_t sc_idx;
    int top;

    sc_field = (sidecar_field_t *)v_sc_field;
    top = lua_gettop(L);

    /* get the sidecar table for the record, and then the field from
     * the table */
    if (((sc_idx = rwRecGetSidecar(rwrec)) == LUA_NOREF)
        || (lua_rawgeti(L, LUA_REGISTRYINDEX, sc_idx) != LUA_TTABLE)
        || (lua_getfield(L, -1, sc_field->scf_name) == LUA_TNIL))
    {
        lua_settop(L, top);
        return;
    }
    switch (sc_field->scf_type) {
      case SK_SIDECAR_UINT8:
        {
            uint8_t n = lua_tointeger(L, -1);
            *in_out_buf += n;
            break;
        }
      case SK_SIDECAR_UINT16:
        {
            uint16_t n = lua_tointeger(L, -1);
#ifndef SK_HAVE_ALIGNED_ACCESS_REQUIRED
            *(uint16_t *)in_out_buf += n;
#else
            uint16_t sum;
            memcpy(&sum, in_out_buf, sizeof(sum));
            sum += n;
            memcpy(in_out_buf, &sum, sizeof(sum));
#endif
            break;
        }
      case SK_SIDECAR_UINT32:
        {
            uint32_t n = lua_tointeger(L, -1);
#ifndef SK_HAVE_ALIGNED_ACCESS_REQUIRED
            *(uint32_t *)in_out_buf += n;
#else
            uint32_t sum;
            memcpy(&sum, in_out_buf, sizeof(sum));
            sum += n;
            memcpy(in_out_buf, &sum, sizeof(sum));
#endif
            break;
        }
      case SK_SIDECAR_UINT64:
        {
            uint64_t n = lua_tointeger(L, -1);
#ifndef SK_HAVE_ALIGNED_ACCESS_REQUIRED
            *(uint64_t *)in_out_buf += n;
#else
            uint64_t sum;
            memcpy(&sum, in_out_buf, sizeof(sum));
            sum += n;
            memcpy(in_out_buf, &sum, sizeof(sum));
#endif
            break;
        }
      case SK_SIDECAR_DOUBLE:
        {
            double d = lua_tonumber(L, -1);
#ifndef SK_HAVE_ALIGNED_ACCESS_REQUIRED
            *(double *)in_out_buf += d;
#else
            double sum;
            memcpy(&sum, in_out_buf, sizeof(sum));
            sum += d;
            memcpy(in_out_buf, &sum, sizeof(sum));
#endif
            break;
        }
      case SK_SIDECAR_ADDR_IP4:
      case SK_SIDECAR_ADDR_IP6:
      case SK_SIDECAR_DATETIME:
        /* what does it mean to merge these? */
        break;
      case SK_SIDECAR_BOOLEAN:
        {
            if (*in_out_buf) {
                *in_out_buf = lua_toboolean(L, -1);
            }
            break;
        }
      case SK_SIDECAR_STRING:
      case SK_SIDECAR_BINARY:
        /* what does it mean to merge these? */
        break;
      case SK_SIDECAR_EMPTY:
        break;
      case SK_SIDECAR_LIST:
      case SK_SIDECAR_TABLE:
        break;
      case SK_SIDECAR_UNKNOWN:
        break;
    }

    lua_settop(L, top);
}

/*
 *    Compare the value in 'buf1' with the value in 'buf2' for the
 *    sidecar field 'v_sc_field'.
 *
 *    Invoked as a callback by the skunique object when comparing
 *    portions of a key field (used by rwuniq --sort) or when
 *    comparing aggregate values (used by rwstats).
 *
 *    The size of 'buf1' and 'buf2' were specified when the field was
 *    added to the field-list.
 */
static int
sidecar_bin_compare(
    const uint8_t      *buf1,
    const uint8_t      *buf2,
    void               *v_sc_field)
{
    const sidecar_field_t *sc_field;

    sc_field = (sidecar_field_t *)v_sc_field;
    switch (sc_field->scf_type) {
      case SK_SIDECAR_UINT8:
      case SK_SIDECAR_BOOLEAN:
        {
            if (*buf1 < *buf2) {
                return -1;
            }
            return *buf1 > *buf2;
        }
      case SK_SIDECAR_UINT16:
        {
#ifndef SK_HAVE_ALIGNED_ACCESS_REQUIRED
            if (*(uint16_t *)buf1 < *(uint16_t *)buf2) {
                return -1;
            }
            return *(uint16_t *)buf1 > *(uint16_t *)buf2;
#else
            uint16_t n_1, n_2;
            memcpy(&n_1, buf1, sizeof(n_1));
            memcpy(&n_2, buf2, sizeof(n_2));
            if (n_1 < n_2) {
                return -1;
            }
            return n_1 > n_2;
#endif
        }
      case SK_SIDECAR_UINT32:
      case SK_SIDECAR_ADDR_IP4:
        {
#ifndef SK_HAVE_ALIGNED_ACCESS_REQUIRED
            if (*(uint32_t *)buf1 < *(uint32_t *)buf2) {
                return -1;
            }
            return *(uint32_t *)buf1 > *(uint32_t *)buf2;
#else
            uint32_t n_1, n_2;
            memcpy(&n_1, buf1, sizeof(n_1));
            memcpy(&n_2, buf2, sizeof(n_2));
            if (n_1 < n_2) {
                return -1;
            }
            return n_1 > n_2;
#endif
        }
      case SK_SIDECAR_UINT64:
        {
#ifndef SK_HAVE_ALIGNED_ACCESS_REQUIRED
            if (*(uint64_t *)buf1 < *(uint64_t *)buf2) {
                return -1;
            }
            return *(uint64_t *)buf1 > *(uint64_t *)buf2;
#else
            uint64_t n_1, n_2;
            memcpy(&n_1, buf1, sizeof(n_1));
            memcpy(&n_2, buf2, sizeof(n_2));
            if (n_1 < n_2) {
                return -1;
            }
            return n_1 > n_2;
#endif
        }
      case SK_SIDECAR_DOUBLE:
        {
#ifndef SK_HAVE_ALIGNED_ACCESS_REQUIRED
            if (*(double *)buf1 < *(double *)buf2) {
                return -1;
            }
            return *(double *)buf1 > *(double *)buf2;
#else
            double d_1, d_2;
            memcpy(&d_1, buf1, sizeof(d_1));
            memcpy(&d_2, buf2, sizeof(d_2));
            if (d_1 < d_2) {
                return -1;
            }
            return d_1 > d_2;
#endif
        }
      case SK_SIDECAR_ADDR_IP6:
        {
#if 1
            return memcmp(buf1, buf2, 16);
#else
            skipaddr_t ipaddr_1, ipaddr_2;
            skipaddrSetV6(&ipaddr_1, buf1);
            skipaddrSetV6(&ipaddr_2, buf2);
            return skipaddrCompare(&ipaddr_1, &ipaddr_2);
#endif
        }
      case SK_SIDECAR_DATETIME:
        {
#ifndef SK_HAVE_ALIGNED_ACCESS_REQUIRED
            return *(sktime_t *)buf1 - *(sktime_t *)buf2;
#else
            sktime_t t_1, t_2;
            memcpy(&t_1, buf1, sizeof(t_1));
            memcpy(&t_2, buf2, sizeof(t_2));
            return t_1 - t_2;
#endif
        }
      case SK_SIDECAR_EMPTY:
        break;
      case SK_SIDECAR_STRING:
      case SK_SIDECAR_BINARY:
        {
            const char *str1, *str2;
            int ref1, ref2;
            int rv;

#ifndef SK_HAVE_ALIGNED_ACCESS_REQUIRED
            ref1 = *(int *)buf1;
            ref2 = *(int *)buf2;
#else
            memcpy(&ref1, buf1, sizeof(ref1));
            memcpy(&ref2, buf2, sizeof(ref2));
#endif
            if (ref1 == ref2) {
                return 0;
            }
            lua_rawgeti(L, LUA_REGISTRYINDEX, str_to_ref);
            lua_rawgeti(L, -1, ref1);
            lua_rawgeti(L, -2, ref2);
            str1 = lua_tostring(L, -2);
            str2 = lua_tostring(L, -1);
            rv = strcmp(str1, str2);
            lua_pop(L, 3);
            return rv;
        }
      case SK_SIDECAR_LIST:
      case SK_SIDECAR_TABLE:
        break;
      case SK_SIDECAR_UNKNOWN:
        break;
    }

    return 0;
}


/*
 *    Given a binary value in 'in_out_buf' and other in 'in_buf' for
 *    the sidecar field represented by 'v_sc_field', add or merge
 *    these two values and store the result in 'in_out_buf'.
 *
 *    Invoked as a callback by the skunique object when aggregate
 *    values from multiple temporary files need to be merged into a
 *    single value.
 *
 *    The size of 'in_out_buf' and 'in_buf' were specified when the
 *    field was added to the field-list.
 */
static void
sidecar_bin_merge(
    uint8_t            *in_out_buf,
    const uint8_t      *in_buf,
    void               *v_sc_field)
{
    const sidecar_field_t *sc_field;

    sc_field = (sidecar_field_t *)v_sc_field;
    switch (sc_field->scf_type) {
      case SK_SIDECAR_UINT8:
        {
            *in_out_buf += *in_buf;
            return;
        }
      case SK_SIDECAR_UINT16:
        {
#ifndef SK_HAVE_ALIGNED_ACCESS_REQUIRED
            *(uint16_t *)in_out_buf += *(uint16_t *)in_buf;
#else
            uint16_t sum, n;
            memcpy(&sum, in_out_buf, sizeof(sum));
            memcpy(&n, in_buf, sizeof(n));
            sum += n;
            memcpy(in_out_buf, &sum, sizeof(n));
#endif
            return;
        }
      case SK_SIDECAR_UINT32:
        {
#ifndef SK_HAVE_ALIGNED_ACCESS_REQUIRED
            *(uint32_t *)in_out_buf += *(uint32_t *)in_buf;
#else
            uint32_t sum, n;
            memcpy(&sum, in_out_buf, sizeof(sum));
            memcpy(&n, in_buf, sizeof(n));
            sum += n;
            memcpy(in_out_buf, &sum, sizeof(n));
#endif
            return;
        }
      case SK_SIDECAR_UINT64:
        {
#ifndef SK_HAVE_ALIGNED_ACCESS_REQUIRED
            *(uint64_t *)in_out_buf += *(uint64_t *)in_buf;
#else
            uint64_t sum, n;
            memcpy(&sum, in_out_buf, sizeof(sum));
            memcpy(&n, in_buf, sizeof(n));
            sum += n;
            memcpy(in_out_buf, &sum, sizeof(n));
#endif
            return;
        }
      case SK_SIDECAR_DOUBLE:
        {
#ifndef SK_HAVE_ALIGNED_ACCESS_REQUIRED
            *(double *)in_out_buf += *(double *)in_buf;
#else
            double sum, n;
            memcpy(&sum, in_out_buf, sizeof(sum));
            memcpy(&n, in_buf, sizeof(n));
            sum += n;
            memcpy(in_out_buf, &sum, sizeof(n));
#endif
            return;
        }
      case SK_SIDECAR_ADDR_IP4:
      case SK_SIDECAR_ADDR_IP6:
      case SK_SIDECAR_DATETIME:
        /* what does it mean to merge these? */
        break;
      case SK_SIDECAR_BOOLEAN:
        {
            if (*in_out_buf) {
                *in_out_buf = *in_buf;
            }
            return;
        }
      case SK_SIDECAR_STRING:
      case SK_SIDECAR_BINARY:
        /* FIXME */
        break;
      case SK_SIDECAR_EMPTY:
        break;
      case SK_SIDECAR_LIST:
      case SK_SIDECAR_TABLE:
        break;
      case SK_SIDECAR_UNKNOWN:
        break;
    }
}


/*
 *  ok = createStringmaps();
 *
 *    Create the string-maps to assist in parsing the --fields and
 *    --values switches.
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
    size_t                      i;
    size_t                      j;

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

    /* add --fields from the plug-ins */
    if (STATSUNIQ_PROGRAM_STATS == this_program) {
        pi_err = (skPluginFieldIteratorBind(
                      &pi_iter, SKPLUGIN_APP_STATS_FIELD, 1));
    } else {
        pi_err = (skPluginFieldIteratorBind(
                      &pi_iter, SKPLUGIN_APP_UNIQ_FIELD, 1));
    }
    if (pi_err != SKPLUGIN_OK) {
        assert(pi_err == SKPLUGIN_OK);
        skAppPrintErr("Unable to bind plugin field iterator");
        return -1;
    }
    while (skPluginFieldIteratorNext(&pi_iter, &pi_field)) {
        skPluginFieldName(pi_field, &field_names);
        ++max_id;

        /* Add keys to the key_field_map */
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

    max_id = 0;

    /* create the string-map for value field identifiers */
    if (skStringMapCreate(&value_field_map)) {
        skAppPrintErr("Unable to create map for values");
        return -1;
    }

    /* add the built-in names */
    for (i = 0; i < num_builtin_values; ++i) {
        if (this_program & builtin_values[i].bf_app) {
            memset(&sm_entry, 0, sizeof(sk_stringmap_entry_t));
            sm_entry.name = builtin_values[i].bf_title;
            sm_entry.id = i;
            sm_entry.description = builtin_values[i].bf_description;
            sm_err = skStringMapAddEntries(value_field_map, 1, &sm_entry);
            if (sm_err) {
                skAppPrintErr("Unable to add value field named '%s': %s",
                              sm_entry.name, skStringMapStrerror(sm_err));
                return -1;
            }
            if (sm_entry.id > max_id) {
                max_id = sm_entry.id;
            }
        }
    }

    /* add aliases for built-in fields */
    for (j = 0; builtin_value_aliases[j].ba_name; ++j) {
        for (i = 0; i < num_builtin_values; ++i) {
            if (builtin_value_aliases[j].ba_id == builtin_values[i].bf_id) {
                memset(&sm_entry, 0, sizeof(sk_stringmap_entry_t));
                sm_entry.name = builtin_value_aliases[j].ba_name;
                sm_entry.id = i;
                sm_err = skStringMapAddEntries(value_field_map, 1, &sm_entry);
                if (sm_err) {
                    skAppPrintErr("Unable to add value field named '%s': %s",
                                  sm_entry.name, skStringMapStrerror(sm_err));
                    return -1;
                }
                break;
            }
        }
        if (i == num_builtin_values) {
            skAppPrintErr("No field found with id %d",
                          builtin_value_aliases[j].ba_id);
            return -1;
        }
    }

    /* add the value fields from the plugins */
    if (STATSUNIQ_PROGRAM_STATS == this_program) {
        pi_err = (skPluginFieldIteratorBind(
                      &pi_iter, SKPLUGIN_APP_STATS_VALUE, 1));
    } else {
        pi_err = (skPluginFieldIteratorBind(
                      &pi_iter, SKPLUGIN_APP_UNIQ_VALUE, 1));
    }
    if (pi_err != SKPLUGIN_OK) {
        assert(pi_err == SKPLUGIN_OK);
        skAppPrintErr("Unable to bind plugin field iterator");
        return -1;
    }
    while (skPluginFieldIteratorNext(&pi_iter, &pi_field)) {
        skPluginFieldName(pi_field, &field_names);
        ++max_id;

        /* Add value names to the field_map */
        for (name = field_names; *name; ++name) {
            memset(&sm_entry, 0, sizeof(sm_entry));
            sm_entry.name = *name;
            sm_entry.id = PLUGIN_FIELD_BIT | max_id;
            sm_entry.userdata = pi_field;
            skPluginFieldDescription(pi_field, &sm_entry.description);
            sm_err = skStringMapAddEntries(value_field_map, 1, &sm_entry);
            if (sm_err != SKSTRINGMAP_OK) {
                const char *plugin_name;
                skPluginFieldGetPluginName(pi_field, &plugin_name);
                skAppPrintErr(("Plug-in cannot add value named '%s': %s."
                               " Plug-in file: %s"),
                              *name, skStringMapStrerror(sm_err),plugin_name);
                return -1;
            }
        }
    }

    return 0;
}


/*
 *  status = parseKeyFields(field_string);
 *
 *    Parse the string that represents the key fields the user wishes
 *    to bin by, create and fill in the global sk_fieldlist_t
 *    'key_fields', and add columns to the formatter.  Return 0 on
 *    success or non-zero on error.
 */
static int
parseKeyFields(
    const char         *field_string)
{
    sk_stringmap_iter_t *sm_iter = NULL;
    sk_stringmap_entry_t *sm_entry = NULL;
    sk_formatter_field_t *fmtr_field;
    sk_fieldentry_t *fl_entry;
    int sm_entry_id;

    /* keep track of which time field we see last; uses the
     * RWREC_FIELD_* values from rwrec.h */
    rwrec_field_id_t final_time_field = (rwrec_field_id_t)0;
    unsigned int time_fields;

    /* return value; assume failure */
    int rv = -1;

    /* error message generated when parsing fields */
    char *errmsg;

    /* parse the --fields argument */
    if (skStringMapParse(key_field_map, field_string, SKSTRINGMAP_DUPES_ERROR,
                         &sm_iter, &errmsg))
    {
        skAppPrintErr("Invalid %s: %s",
                      appOptions[OPT_FIELDS].opt.name, errmsg);
        goto END;
    }

    /* create the field-list */
    if (skFieldListCreate(&key_fields)) {
        skAppPrintErr("Unable to create key field list");
        goto END;
    }

    /* check for dport in the key and see which time fields are
     * requested */
    time_fields = 0;
    while (skStringMapIterNext(sm_iter, &sm_entry, NULL) == SK_ITERATOR_OK) {
        switch (sm_entry->id) {
          case RWREC_FIELD_DPORT:
            dport_key = 1;
            break;
          case RWREC_FIELD_STIME:
            time_fields |= PARSE_KEY_STIME;
            final_time_field = (rwrec_field_id_t)sm_entry->id;
            break;
          case RWREC_FIELD_ELAPSED:
            time_fields |= PARSE_KEY_ELAPSED;
            final_time_field = (rwrec_field_id_t)sm_entry->id;
            break;
          case RWREC_FIELD_ETIME:
            time_fields |= PARSE_KEY_ETIME;
            final_time_field = (rwrec_field_id_t)sm_entry->id;
            break;
          default:
            break;
        }
    }

    /* set 'time_fields_key' to the time fields that will be in the
     * key.  since only two of the three time fields are independent,
     * when all three are requested only the first two fields are put
     * into the key. */
    time_fields_key = time_fields;
    if (PARSE_KEY_ALL_TIMES == time_fields_key) {
        switch (final_time_field) {
          case RWREC_FIELD_STIME:
            time_fields_key &= ~PARSE_KEY_STIME;
            break;
          case RWREC_FIELD_ELAPSED:
            time_fields_key &= ~PARSE_KEY_ELAPSED;
            break;
          case RWREC_FIELD_ETIME:
            time_fields_key &= ~PARSE_KEY_ETIME;
            break;
          default:
            skAbortBadCase(final_time_field);
        }
    }

    /* when binning by time was requested, see if time fields make sense */
    if (time_bin_size != 0) {
        switch (time_fields) {
          case 0:
          case PARSE_KEY_ELAPSED:
            if (FILEIsATty(stderr)) {
                skAppPrintErr("Warning: Neither sTime nor eTime appear in"
                              " --%s; %s switch ignored",
                              appOptions[OPT_FIELDS].opt.name,
                              appOptions[OPT_BIN_TIME].opt.name);
            }
            time_bin_size = 0;
            break;
          case PARSE_KEY_ALL_TIMES:
            /* must adjust elapsed to be eTime-sTime */
            if (FILEIsATty(stderr)) {
                skAppPrintErr("Warning: Modifying duration field "
                              "to be difference of eTime and sTime");
            }
            break;
        }
    }

    /* warn when using --presorted-input and multiple time fields are
     * present or when the time field is not the final field */
    if (app_flags.presorted_input && FILEIsATty(stderr)) {
        switch (time_fields) {
          case 0:
            /* no time fields present */
            break;
          case PARSE_KEY_ELAPSED:
          case PARSE_KEY_STIME:
          case PARSE_KEY_ETIME:
            /* one field is present.  see if it is last.  note that
             * 'sm_entry' is still pointed at the final entry */
            switch (sm_entry->id) {
              case RWREC_FIELD_STIME:
              case RWREC_FIELD_ELAPSED:
              case RWREC_FIELD_ETIME:
                /* one field is present and it is last */
                break;
              default:
                /* one field is present but it is not last */
                skAppPrintErr(("Warning: Suggest putting '%s' last in --%s"
                               " when using --%s due to millisecond"
                               " truncation"),
                              ((PARSE_KEY_ELAPSED == time_fields)
                               ? "elapsed"
                               : ((PARSE_KEY_STIME == time_fields)
                                  ? "sTime"
                                  : "eTime")),
                              appOptions[OPT_FIELDS].opt.name,
                              appOptions[OPT_PRESORTED_INPUT].opt.name);
                break;
            }
            break;
          default:
            /* multiple time fields present */
            skAppPrintErr(("Warning: Using multiple time-related key"
                           " fields with\n\t--%s may lead to unexpected"
                           " results due to millisecond truncation"),
                          appOptions[OPT_PRESORTED_INPUT].opt.name);
            break;
        }
    }

    skStringMapIterReset(sm_iter);

    /* add the key fields to the field-list and to the formatter. */
    while (skStringMapIterNext(sm_iter, &sm_entry, NULL) == SK_ITERATOR_OK) {
        if (PLUGIN_FIELD_BIT & sm_entry->id) {
            assert(sm_entry->userdata);
            if (appAddPlugin(sm_entry, FIELD_TYPE_KEY)) {
                skAppPrintErr("Cannot add key field '%s' from plugin",
                              sm_entry->name);
                goto END;
            }
            continue;
        }
        if ((SIDECAR_FIELD_BIT | SC_LUA_FIELD_BIT) & sm_entry->id) {
            assert(sm_entry->userdata);
            if (appAddSidecar(sm_entry, FIELD_TYPE_KEY)) {
                skAppPrintErr("Cannot add key field '%s' from sidecar",
                              sm_entry->name);
                goto END;
            }
            continue;
        }
        assert(sm_entry->id < RWREC_FIELD_ID_COUNT);

        fmtr_field = (sk_formatter_add_silk_field(
                          fmtr, (rwrec_field_id_t)sm_entry->id));
        if (NULL == fmtr_field) {
            skAppPrintErr("Cannot add key field %" PRIu32 " to output",
                          sm_entry->id);
            goto END;
        }
        switch ((rwrec_field_id_t)sm_entry->id) {
          case RWREC_FIELD_FLAGS:
          case RWREC_FIELD_INIT_FLAGS:
          case RWREC_FIELD_REST_FLAGS:
            if (app_flags.integer_tcp_flags) {
                sk_formatter_field_set_number_format(fmtr, fmtr_field, 10);
            } else if (!app_flags.no_columns) {
                sk_formatter_field_set_space_padded(fmtr, fmtr_field);
            }
            break;
          case RWREC_FIELD_TCP_STATE:
            if (!app_flags.no_columns) {
                sk_formatter_field_set_space_padded(fmtr, fmtr_field);
            }
            break;
          case RWREC_FIELD_SID:
            if (app_flags.integer_sensors) {
                sk_formatter_field_set_number_format(fmtr, fmtr_field, 10);
            }
            break;
          default:
            break;
        }

        if (PARSE_KEY_ALL_TIMES == time_fields
            && (rwrec_field_id_t)sm_entry->id == final_time_field)
        {
            /* when all time fields were requested, do not include the
             * final one that was seen as part of the key */
            continue;
        }
        sm_entry_id = (int)sm_entry->id;
        switch ((rwrec_field_id_t)sm_entry_id) {
          case RWREC_FIELD_SIP:
            sm_entry_id = ((ipv6_policy < SK_IPV6POLICY_MIX)
                           ? SK_FIELD_SIPv4 : SK_FIELD_SIPv6);
            break;
          case RWREC_FIELD_DIP:
            sm_entry_id = ((ipv6_policy < SK_IPV6POLICY_MIX)
                           ? SK_FIELD_DIPv4 : SK_FIELD_DIPv6);
            break;
          case RWREC_FIELD_NHIP:
            sm_entry_id = ((ipv6_policy < SK_IPV6POLICY_MIX)
                           ? SK_FIELD_NHIPv4 : SK_FIELD_NHIPv6);
            break;
          default:
            break;
        }
        fl_entry = skFieldListAddKnownField(key_fields, sm_entry_id, NULL);
        if (NULL == fl_entry) {
            skAppPrintErr("Cannot add key field '%s' to field list",
                          sm_entry->name);
            goto END;
        }
    }

    /* successful */
    rv = 0;

  END:
    if (rv != 0) {
        /* something went wrong.  clean up */
        if (key_fields) {
            skFieldListDestroy(&key_fields);
            key_fields = NULL;
        }
    }
    /* do standard clean-up */
    if (sm_iter != NULL) {
        skStringMapIterDestroy(sm_iter);
    }
    return rv;
}


/*
 *  ok = parseValueFields(field_string);
 *
 *    Parse the string that represents the aggregate value and
 *    distinct fields the user wishes to compute, create and fill in
 *    the global sk_fieldlist_t 'value_fields' and 'distinct_fields',
 *    and add columns to the formatter.  Return 0 on success or
 *    non-zero on error.
 *
 *    Returns 0 on success, or non-zero on error.
 */
static int
parseValueFields(
    const char         *value_string)
{
    char text_buf[PATH_MAX];
    sk_stringmap_iter_t *sm_iter = NULL;
    sk_stringmap_entry_t *sm_entry;
    sk_stringmap_status_t sm_err;
    sk_stringmap_id_t sm_entry_id;
    sk_formatter_field_t *fmtr_field;
    const char *sm_attr;
    size_t i;

    /* to create a new --values switch */
    char *buf = NULL;
    size_t buf_size;

    /* return value; assume failure */
    int rv = -1;

    /* error message generated when parsing fields */
    char *errmsg;

    builtin_field_t *bf;
    sk_fieldentry_t *fl_entry;

    if (STATSUNIQ_PROGRAM_UNIQ == this_program) {
        /* in rwuniq, set limit to a garbage value so it is ignored */
        limit.fl_entry = (sk_fieldentry_t *)&limit;
    }

    if (ipv6_policy < SK_IPV6POLICY_MIX) {
        /* change the field id of the distinct fields */
        for (i = 0, bf = builtin_values; i < num_builtin_values; ++i, ++bf) {
            switch (bf->bf_id) {
              case SK_FIELD_SIPv6:
                bf->bf_id = SK_FIELD_SIPv4;
                break;
              case SK_FIELD_DIPv6:
                bf->bf_id = SK_FIELD_DIPv4;
                break;
              default:
                break;
            }
        }
    }

    if (time_flags & SKTIMESTAMP_EPOCH) {
        /* Reduce width of the textual columns for the MIN_STARTTIME
         * and MAX_ENDTIME fields. */
        for (i = 0, bf = builtin_values; i < num_builtin_values; ++i, ++bf) {
            if ((bf->bf_id == SK_FIELD_MIN_STARTTIME)
                || (bf->bf_id == SK_FIELD_MAX_ENDTIME))
            {
                bf->bf_text_len = 10;
            }
        }
    }

    /*
     *    In rwuniq, handling the old style --bytes,--packets,etc
     *    switches and the new --values switch is a bit of a pain.
     *
     *    First, parse --values if it is provided.  If any --values
     *    fields are also specified as stand-alone switches (e.g.,
     *    --bytes), turn off the stand-alone switch.
     *
     *    If any stand-alone switch is still on, create a new --values
     *    switch that includes the names of the stand-alone switches.
     *    Or, if no --values and no stand-alone switches are given,
     *    fall-back to the default and count flow records.
     */
    if (value_string) {
        if (skStringMapParseWithAttributes(value_field_map, value_string,
                                           SKSTRINGMAP_DUPES_KEEP, &sm_iter,
                                           &errmsg))
        {
            skAppPrintErr("Invalid %s: %s",
                          appOptions[OPT_VALUES].opt.name, errmsg);
            goto END;
        }

        /* turn off the --bytes,--packets,etc switches if they also appear
         * in the --values switch */
        while (skStringMapIterNext(sm_iter, &sm_entry, NULL)==SK_ITERATOR_OK) {
            if (sm_entry->id < num_builtin_values) {
                builtin_values[sm_entry->id].bf_switched_on = 0;
            }
        }

        skStringMapIterDestroy(sm_iter);
        sm_iter = NULL;
    }
    /* determine whether any of the --bytes,--packets,etc switches are
     * still marked as active */
    buf_size = 0;
    for (i = 0, bf = builtin_values; i < num_builtin_values; ++i, ++bf) {
        if (bf->bf_switched_on) {
            buf_size += 2 + strlen(bf->bf_title);
        }
    }

    if (buf_size) {
        /* switches are active; create new --values switch */
        if (NULL == value_string) {
            buf = (char*)malloc(buf_size);
            if (!buf) {
                skAppPrintOutOfMemory(NULL);
                goto END;
            }
            buf[0] = '\0';
        } else {
            buf_size += 1 + strlen(value_string);
            buf = (char*)malloc(buf_size);
            if (!buf) {
                skAppPrintOutOfMemory(NULL);
                goto END;
            }
            strncpy(buf, value_string, buf_size);
        }
        for (i = 0, bf = builtin_values; i < num_builtin_values; ++i, ++bf) {
            if (bf->bf_switched_on) {
                strncat(buf, ",", 2);
                strncat(buf, bf->bf_title, buf_size - 1 - strlen(buf));
            }
        }
        value_string = buf;

    } else if (!value_string) {
        /* no --values switch and no --bytes,--packets,etc switches,
         * so count flow records */
        for (i = 0, bf = builtin_values; i < num_builtin_values; ++i, ++bf) {
            if (SK_FIELD_RECORDS == bf->bf_id) {
                value_string = bf->bf_title;
                break;
            }
        }
    }

    /* parse the --values field list */
    if (skStringMapParseWithAttributes(value_field_map, value_string,
                                       SKSTRINGMAP_DUPES_KEEP, &sm_iter,
                                       &errmsg))
    {
        skAppPrintErr("Invalid %s: %s",
                      appOptions[OPT_VALUES].opt.name, errmsg);
        goto END;
    }

    /* create the field-lists */
    if (skFieldListCreate(&value_fields)) {
        skAppPrintErr("Unable to create value field list");
        goto END;
    }
    if (skFieldListCreate(&distinct_fields)) {
        skAppPrintErr("Unable to create distinct field list");
        goto END;
    }

    /* loop over the selected values */
    while (skStringMapIterNext(sm_iter, &sm_entry, &sm_attr)==SK_ITERATOR_OK) {
        if (PLUGIN_FIELD_BIT & sm_entry->id) {
            assert(sm_entry->userdata);
            /* this is a values field that comes from a plug-in */
            if (sm_attr[0]) {
                skAppPrintErr("Invalid %s: Extra text after field name ':%s'",
                              appOptions[OPT_VALUES].opt.name, sm_attr);
                goto END;
            }
            if (isFieldDuplicate(value_fields, SK_FIELD_CALLER,
                                 sm_entry->userdata))
            {
                skAppPrintErr("Invalid %s: Duplicate name '%s'",
                              appOptions[OPT_VALUES].opt.name, sm_entry->name);
                goto END;
            }
            if (appAddPlugin(sm_entry, FIELD_TYPE_VALUE)) {
                skAppPrintErr("Cannot add value field '%s' from plugin",
                              sm_entry->name);
                goto END;
            }
            continue;
        }
        if ((SIDECAR_FIELD_BIT | SC_LUA_FIELD_BIT) & sm_entry->id) {
            skAbort();
        }
        /* else, field is built-in */
        assert(sm_entry->id < num_builtin_values);
        bf = &builtin_values[sm_entry->id];
        if (SK_FIELD_CALLER != bf->bf_id) {
            sk_formatter_field_extra_t format_fn
                = (bf->bf_is_distinct) ? &distinct_to_ascii : &value_to_ascii;
            sk_fieldlist_t *list
                = (bf->bf_is_distinct) ? distinct_fields : value_fields;

            /* this built-in field must have no attribute */
            if (sm_attr[0]) {
                skAppPrintErr("Invalid %s: Unrecognized field '%s:%s'",
                              appOptions[OPT_VALUES].opt.name,
                              bf->bf_title, sm_attr);
                goto END;
            }
            if (isFieldDuplicate(list, bf->bf_id, NULL)) {
                skAppPrintErr("Invalid %s: Duplicate name '%s'",
                              appOptions[OPT_VALUES].opt.name, bf->bf_title);
                goto END;
            }
            fl_entry = skFieldListAddKnownField(list, bf->bf_id, bf);
            if (NULL == fl_entry) {
                skAppPrintErr("Cannot add field '%s' to %s field list",
                              sm_entry->name,
                              ((bf->bf_is_distinct) ? "distinct" : "value"));
                goto END;
            }
            fmtr_field = (sk_formatter_add_extra_field(
                              fmtr, format_fn, fl_entry, bf->bf_text_len));
            if (NULL == fmtr_field) {
                skAppPrintErr("Cannot add field '%s' to formatter",
                              sm_entry->name);
                goto END;
            }
            sk_formatter_field_set_title(fmtr, fmtr_field, bf->bf_title);
            if (NULL == limit.fl_entry) {
                limit.fl_entry = fl_entry;
                limit.fl_id = (sk_fieldid_t)skFieldListEntryGetId(fl_entry);
                limit.distinct = bf->bf_is_distinct;
                strncpy(limit.title, bf->bf_title, sizeof(limit.title));
                limit.title[sizeof(limit.title)-1] = '\0';
            }
        } else {
            /* got a distinct:KEY field */
            if (!sm_attr[0]) {
                skAppPrintErr(("Invalid %s:"
                               " The distinct value requires a field"),
                              appOptions[OPT_VALUES].opt.name);
                goto END;
            }
            /* need to parse KEY as a key field */
            sm_err = skStringMapGetByName(key_field_map, sm_attr, &sm_entry);
            if (sm_err) {
                if (strchr(sm_attr, ',')) {
                    skAppPrintErr(("Invalid %s:"
                                   " May only distinct over a single field"),
                                  appOptions[OPT_VALUES].opt.name);
                } else {
                    skAppPrintErr("Invalid %s: Bad distinct field '%s': %s",
                                  appOptions[OPT_VALUES].opt.name, sm_attr,
                                  skStringMapStrerror(sm_err));
                }
                goto END;
            }
            if (PLUGIN_FIELD_BIT & sm_entry->id) {
                assert(sm_entry->userdata);
                /* distinct:KEY where KEY is from a plug-in */
                if (isFieldDuplicate(distinct_fields, SK_FIELD_CALLER,
                                     sm_entry->userdata))
                {
                    skAppPrintErr("Invalid %s: Duplicate distinct '%s'",
                                  appOptions[OPT_VALUES].opt.name,
                                  sm_entry->name);
                    goto END;
                }
                if (appAddPlugin(sm_entry, FIELD_TYPE_DISTINCT)) {
                    skAppPrintErr("Cannot add distinct field '%s' from plugin",
                                  sm_entry->name);
                    goto END;
                }
                continue;
            }
            if ((SIDECAR_FIELD_BIT | SC_LUA_FIELD_BIT) & sm_entry->id) {
                assert(sm_entry->userdata);
                /* distinct:KEY where KEY is from sidecar data */
                if (isFieldDuplicate(distinct_fields, SK_FIELD_CALLER,
                                     sm_entry->userdata))
                {
                    skAppPrintErr("Invalid %s: Duplicate distinct '%s'",
                                  appOptions[OPT_VALUES].opt.name,
                                  sm_entry->name);
                    goto END;
                }
                if (appAddSidecar(sm_entry, FIELD_TYPE_DISTINCT)) {
                    skAppPrintErr("Cannot add distinct field '%s' from sidecar",
                                  sm_entry->name);
                    goto END;
                }
                continue;
            }
            /* distinct:KEY where KEY is a standard rwRec field */
            if (isFieldDuplicate(distinct_fields, (sk_fieldid_t)sm_entry->id,
                                 NULL))
            {
                skAppPrintErr("Invalid %s: Duplicate distinct '%s'",
                              appOptions[OPT_VALUES].opt.name, sm_entry->name);
                goto END;
            }
            sm_entry_id = sm_entry->id;
            switch ((rwrec_field_id_t)sm_entry_id) {
              case RWREC_FIELD_SIP:
                sm_entry_id = ((ipv6_policy < SK_IPV6POLICY_MIX)
                               ? SK_FIELD_SIPv4 : SK_FIELD_SIPv6);
                break;
              case RWREC_FIELD_DIP:
                sm_entry_id = ((ipv6_policy < SK_IPV6POLICY_MIX)
                               ? SK_FIELD_DIPv4 : SK_FIELD_DIPv6);
                break;
              case RWREC_FIELD_NHIP:
                sm_entry_id = ((ipv6_policy < SK_IPV6POLICY_MIX)
                               ? SK_FIELD_NHIPv4 : SK_FIELD_NHIPv6);
                break;
              default:
                break;
            }
            fl_entry = (skFieldListAddKnownField(
                            distinct_fields, sm_entry_id, NULL));
            if (NULL == fl_entry) {
                skAppPrintErr("Cannot add field '%s' to distinct field list",
                              sm_entry->name);
                goto END;
            }
            fmtr_field = (sk_formatter_add_extra_field(
                              fmtr, &distinct_to_ascii, fl_entry,
                              bf->bf_text_len));
            if (NULL == fmtr_field) {
                skAppPrintErr("Cannot add value field '%s' to stream",
                              sm_entry->name);
                goto END;
            }
            snprintf(text_buf, sizeof(text_buf), ("%s" DISTINCT_SUFFIX),
                     sm_entry->name);
            sk_formatter_field_set_title(fmtr, fmtr_field, text_buf);

            if (NULL == limit.fl_entry) {
                limit.fl_entry = fl_entry;
                limit.fl_id = (sk_fieldid_t)skFieldListEntryGetId(fl_entry);
                limit.distinct = bf->bf_is_distinct;
                strncpy(limit.title, text_buf, sizeof(limit.title));
                limit.title[sizeof(limit.title)-1] = '\0';
            }
        }
    }

    /* Handle the limit and percentages used by rwstats */
    if (STATSUNIQ_PROGRAM_STATS == this_program) {
        /* for rwstats, the first value determines order of output
         * rows; get the first entry specified in --values to know
         * whether it is a value_fields or a distinct_fields */
#ifndef NDEBUG
        skStringMapIterReset(sm_iter);
        assert(skStringMapIterNext(sm_iter, &sm_entry, &sm_attr)
               == SK_ITERATOR_OK);
#endif
        skStringMapIterReset(sm_iter);
        skStringMapIterNext(sm_iter, &sm_entry, &sm_attr);

        if (PLUGIN_FIELD_BIT & sm_entry->id) {
            assert(sm_entry->userdata);
            /* this is a values field that comes from a plug-in */
            if (RWSTATS_PERCENTAGE == limit.type
                || RWSTATS_THRESHOLD == limit.type)
            {
                skAppPrintErr(("Only the --%s limit is supported when the"
                               " primary values field is from a plug-in"),
                              appOptions[OPT_COUNT].opt.name);
                goto END;
            }
        } else if (SIDECAR_FIELD_BIT & sm_entry->id) {
            assert(sm_entry->userdata);
            /* this is a values field that comes from a sidecar */
            if (RWSTATS_PERCENTAGE == limit.type
                || RWSTATS_THRESHOLD == limit.type)
            {
                skAppPrintErr(("Only the --%s limit is supported when the"
                               " primary values field is from the sidecar"),
                              appOptions[OPT_COUNT].opt.name);
                goto END;
            }
        } else {
            if (limit.distinct &&  RWSTATS_PERCENTAGE == limit.type) {
                skAppPrintErr(("The --%s limit is not supported when the"
                               " primary values field is a distinct count"),
                              appOptions[OPT_PERCENTAGE].opt.name);
                goto END;
            }
        }

        /* Add the percentage fields */
        if (!app_flags.no_percents) {
            /* column that contains the percentage for this row */
            fmtr_field = (sk_formatter_add_extra_field(
                              fmtr, &row_percent_to_ascii, NULL,
                              col_width_percent));
            if (NULL == fmtr_field) {
                skAppPrintErr("Cannot add percentage field to stream");
                goto END;
            }
            snprintf(text_buf, sizeof(text_buf), "%%%s", limit.title);
            sk_formatter_field_set_title(fmtr, fmtr_field, text_buf);

            /* column that contains the cumulutive percentage */
            fmtr_field = (sk_formatter_add_extra_field(
                              fmtr, &cumul_percent_to_ascii, NULL,
                              col_width_percent));
            if (NULL == fmtr_field) {
                skAppPrintErr("Cannot add cumulutive percentage to stream");
                goto END;
            }
            sk_formatter_field_set_title(fmtr, fmtr_field, "cumul_%");
        }
    }

    rv = 0;

  END:
    /* do standard clean-up */
    free(buf);
    skStringMapIterDestroy(sm_iter);
    if (rv != 0) {
        /* something went wrong. do additional clean-up */
        if (value_fields) {
            skFieldListDestroy(&value_fields);
            value_fields = NULL;
        }
        if (distinct_fields) {
            skFieldListDestroy(&distinct_fields);
            distinct_fields = NULL;
        }
    }

    return rv;
}


/*
 *  status = appAddPlugin(plugin_field, field_type);
 *
 *    Given a key, an aggregate value, or distinct(key) field defined
 *    in a plug-in, activate that field and get the information from
 *    the field that the application requires.  field_type indicates
 *    whether the field represents a key, an aggregate value, or a
 *    distinct field.
 *
 *    The function adds the field to the approprirate sk_fieldlist_t
 *    ('key_fields', 'value_fields', 'distinct_fields') and to the
 *    formatter.
 */
static int
appAddPlugin(
    sk_stringmap_entry_t   *sm_entry,
    field_type_t            field_type)
{
    char text_buf[PATH_MAX];
    uint8_t bin_buf[HASHLIB_MAX_VALUE_WIDTH];
    const char **field_names;
    skplugin_field_t *pi_field;
    sk_fieldlist_entrydata_t regdata;
    sk_fieldentry_t *fl_entry;
    sk_formatter_field_t *fmtr_field;
    const char *title;
    size_t text_width;
    skplugin_err_t pi_err;

    pi_field = (skplugin_field_t *)sm_entry->userdata;
    assert(pi_field);

    /* set the regdata for the sk_fieldlist_t */
    memset(&regdata, 0, sizeof(regdata));
    regdata.bin_compare = plugin_bin_compare;
    regdata.add_rec_to_bin = plugin_add_rec_to_bin;
    regdata.bin_merge = plugin_bin_merge;
    /* regdata.bin_output; */

    /* activate the field (so cleanup knows about it) */
    pi_err = skPluginFieldActivate(pi_field);
    if (pi_err != SKPLUGIN_OK) {
        return -1;
    }
    /* initialize this field */
    pi_err = skPluginFieldRunInitialize(pi_field);
    if (pi_err != SKPLUGIN_OK) {
        return -1;
    }

    /* get the names and the title */
    skPluginFieldName(pi_field, &field_names);
    skPluginFieldTitle(pi_field, &title);

    /* get the required textual width of the column */
    pi_err = skPluginFieldGetLenText(pi_field, &text_width);
    if (pi_err != SKPLUGIN_OK) {
        return -1;
    }
    if (0 == text_width) {
        skAppPrintErr("Plug-in field '%s' has a textual width of 0", title);
        return -1;
    }

    /* get the bin width for this field */
    pi_err = skPluginFieldGetLenBin(pi_field, &regdata.bin_octets);
    if (pi_err != SKPLUGIN_OK) {
        return -1;
    }
    if (0 == regdata.bin_octets) {
        skAppPrintErr("Plug-in field '%s' has a binary width of 0", title);
        return -1;
    }
    if (regdata.bin_octets > HASHLIB_MAX_VALUE_WIDTH) {
        return -1;
    }

    memset(bin_buf, 0, sizeof(bin_buf));
    pi_err = skPluginFieldGetInitialValue(pi_field, bin_buf);
    if (pi_err != SKPLUGIN_OK) {
        return -1;
    }
    regdata.initial_value = bin_buf;

    switch (field_type) {
      case FIELD_TYPE_KEY:
        regdata.rec_to_bin = plugin_rec_to_bin;
        fl_entry = skFieldListAddField(key_fields, &regdata, (void*)pi_field);
        break;
      case FIELD_TYPE_VALUE:
        fl_entry = skFieldListAddField(value_fields, &regdata,(void*)pi_field);
        break;
      case FIELD_TYPE_DISTINCT:
        regdata.rec_to_bin = plugin_rec_to_bin;
        fl_entry = skFieldListAddField(distinct_fields, &regdata,
                                       (void*)pi_field);
        break;
      default:
        skAbortBadCase(field_type);
    }
    if (NULL == fl_entry) {
        skAppPrintErr("Unable to add field to field list");
        return -1;
    }

    switch (field_type) {
      case FIELD_TYPE_KEY:
        fmtr_field = (sk_formatter_add_extra_field(
                          fmtr, plugin_key_to_ascii, fl_entry, text_width));
        snprintf(text_buf, sizeof(text_buf), "%s", title);
        break;
      case FIELD_TYPE_VALUE:
        fmtr_field = (sk_formatter_add_extra_field(
                          fmtr, value_to_ascii, fl_entry, text_width));
        snprintf(text_buf, sizeof(text_buf), "%s", title);
        break;
      case FIELD_TYPE_DISTINCT:
        fmtr_field = (sk_formatter_add_extra_field(
                          fmtr, distinct_to_ascii, fl_entry, text_width));
        snprintf(text_buf, sizeof(text_buf), ("%s" DISTINCT_SUFFIX), title);
        break;
      default:
        skAbortBadCase(field_type);
    }

    if (NULL == fmtr_field) {
        return -1;
    }
    sk_formatter_field_set_title(fmtr, fmtr_field, text_buf);

    if (NULL == limit.fl_entry && FIELD_TYPE_KEY != field_type) {
        limit.pi_field = pi_field;
        limit.fl_entry = fl_entry;
        limit.fl_id = (sk_fieldid_t)skFieldListEntryGetId(fl_entry);
        limit.distinct = (FIELD_TYPE_DISTINCT == field_type);
        strncpy(limit.title, text_buf, sizeof(limit.title));
        limit.title[sizeof(limit.title)-1] = '\0';
    }

    return 0;
}


static int
appAddSidecar(
    sk_stringmap_entry_t   *sm_entry,
    field_type_t            field_type)
{
    char text_buf[PATH_MAX];
    uint8_t bin_buf[HASHLIB_MAX_VALUE_WIDTH];
    sidecar_field_t *sc_field;
    sk_fieldlist_entrydata_t regdata;
    sk_fieldentry_t *fl_entry;
    sk_formatter_field_t *fmtr_field;
    size_t text_width;
    const sk_sidecar_elem_t *sc_elem;
    int rv;

    sc_elem = (sk_sidecar_elem_t *)sm_entry->userdata;
    assert(sc_elem);

    if (SC_LUA_FIELD_BIT & sm_entry->id) {
        /* field comes from a sidecar added by --lua-file; we need to
         * activate the field */
        lua_rawgeti(L, LUA_REGISTRYINDEX, reg_ref.activate_field);
        lua_pushstring(L, sm_entry->name);
        rv = lua_pcall(L, 1, 0, 0);
        if (rv != LUA_OK) {
            skAppPrintErr("Unable to activate field %s defined in Lua: %s",
                          sm_entry->name, lua_tostring(L, -1));
            lua_pop(L, 1);
            assert(0 == lua_gettop(L));
            return -1;
        }
        assert(0 == lua_gettop(L));
    }

    if (!sc_field_vec) {
        sc_field_vec = sk_vector_create(sizeof(sidecar_field_t *));
    }

    sc_field = sk_alloc(sidecar_field_t);
    sk_vector_append_value(sc_field_vec, &sc_field);

    sc_field->scf_type = sk_sidecar_elem_get_data_type(sc_elem);
    text_width = sizeof(text_buf);
    sk_sidecar_elem_get_name(sc_elem, text_buf, &text_width);
    sc_field->scf_name = sk_alloc_strdup(text_buf);

    memset(bin_buf, 0, sizeof(bin_buf));

    /* set the regdata for the sk_fieldlist_t */
    memset(&regdata, 0, sizeof(regdata));
    regdata.bin_compare = sidecar_bin_compare;
    regdata.add_rec_to_bin = sidecar_add_rec_to_bin;
    regdata.bin_merge = sidecar_bin_merge;
    regdata.initial_value = bin_buf;

    switch (sk_sidecar_elem_get_data_type(sc_elem)) {
      case SK_SIDECAR_UINT8:
        regdata.bin_octets = sc_field->scf_binoct = sizeof(uint8_t);
        text_width = 3;
        break;
      case SK_SIDECAR_UINT16:
        regdata.bin_octets = sc_field->scf_binoct = sizeof(uint16_t);
        text_width = 5;
        break;
      case SK_SIDECAR_UINT32:
        regdata.bin_octets = sc_field->scf_binoct = sizeof(uint32_t);
        text_width = 10;
        break;
      case SK_SIDECAR_UINT64:
        regdata.bin_octets = sc_field->scf_binoct = sizeof(uint64_t);
        text_width = 20;
        break;
      case SK_SIDECAR_DOUBLE:
        regdata.bin_octets = sc_field->scf_binoct = sizeof(double);
        text_width = 20;
        break;
      case SK_SIDECAR_ADDR_IP4:
        regdata.bin_octets = sc_field->scf_binoct = sizeof(uint32_t);
        text_width = 15;
        break;
      case SK_SIDECAR_ADDR_IP6:
        regdata.bin_octets = sc_field->scf_binoct = sizeof(uint64_t);
        text_width = 39;
        break;
      case SK_SIDECAR_DATETIME:
        regdata.bin_octets = sc_field->scf_binoct = sizeof(uint64_t);
        text_width = SKTIMESTAMP_STRLEN;
        break;
      case SK_SIDECAR_BOOLEAN:
        regdata.bin_octets = sc_field->scf_binoct = sizeof(uint8_t);
        text_width = 1;
        bin_buf[0] = 1;
        break;
      case SK_SIDECAR_EMPTY:
        /* use a size of 1 since I think some code expects non-zero
         * width */
        regdata.bin_octets = sc_field->scf_binoct = sizeof(uint8_t);
        text_width = 0;
        break;
      case SK_SIDECAR_STRING:
      case SK_SIDECAR_BINARY:
        /* a reference to the string in the str_to_ref table */
        regdata.bin_octets = sc_field->scf_binoct = sizeof(int);
        text_width = 40;
        if (LUA_NOREF == str_to_ref) {
            /* create the table and add an entry for the empty string
             * to use when the sidecar field is not present */
            lua_newtable(L);
            lua_pushliteral(L, "");
            lua_pushvalue(L, -1);
            /* mapping from ref -> "" */
            str_to_ref_nil = luaL_ref(L, -3);
            lua_pushinteger(L, str_to_ref_nil);
            /* mapping from "" -> ref */
            lua_settable(L, -3);
            /* put this table in the Lua registry */
            str_to_ref = luaL_ref(L, LUA_REGISTRYINDEX);
        }
        /* set initial value to str_to_ref_nil */
        memcpy(bin_buf, &str_to_ref_nil, sizeof(str_to_ref_nil));
        break;
      case SK_SIDECAR_LIST:
      case SK_SIDECAR_TABLE:
        break;
      case SK_SIDECAR_UNKNOWN:
        break;
    }

    switch (field_type) {
      case FIELD_TYPE_KEY:
        regdata.rec_to_bin = sidecar_rec_to_bin;
        fl_entry = skFieldListAddField(key_fields, &regdata, sc_field);
        break;
      case FIELD_TYPE_VALUE:
        fl_entry = skFieldListAddField(value_fields, &regdata, sc_field);
        break;
      case FIELD_TYPE_DISTINCT:
        regdata.rec_to_bin = sidecar_rec_to_bin;
        fl_entry = skFieldListAddField(distinct_fields, &regdata, sc_field);
        break;
      default:
        skAbortBadCase(field_type);
    }
    if (NULL == fl_entry) {
        skAppPrintErr("Unable to add field to field list");
        return -1;
    }

    switch (field_type) {
      case FIELD_TYPE_KEY:
        fmtr_field = (sk_formatter_add_extra_field(
                          fmtr, sidecar_key_to_ascii, fl_entry, text_width));
        snprintf(text_buf, sizeof(text_buf), "%s", sc_field->scf_name);
        break;
      case FIELD_TYPE_VALUE:
        fmtr_field = (sk_formatter_add_extra_field(
                          fmtr, value_to_ascii, fl_entry, text_width));
        snprintf(text_buf, sizeof(text_buf), "%s", sc_field->scf_name);
        break;
      case FIELD_TYPE_DISTINCT:
        fmtr_field = (sk_formatter_add_extra_field(
                          fmtr, distinct_to_ascii, fl_entry, text_width));
        snprintf(text_buf, sizeof(text_buf), ("%s" DISTINCT_SUFFIX),
                 sc_field->scf_name);
        break;
      default:
        skAbortBadCase(field_type);
    }

    if (NULL == fmtr_field) {
        return -1;
    }
    sk_formatter_field_set_title(fmtr, fmtr_field, text_buf);

    if (NULL == limit.fl_entry && FIELD_TYPE_KEY != field_type) {
        limit.fl_entry = fl_entry;
        limit.fl_id = (sk_fieldid_t)skFieldListEntryGetId(fl_entry);
        limit.distinct = (FIELD_TYPE_DISTINCT == field_type);
        strncpy(limit.title, text_buf, sizeof(limit.title));
        limit.title[sizeof(limit.title)-1] = '\0';
    }

    return 0;
}


/*
 *  is_duplicate = isFieldDuplicate(flist, fid, fcontext);
 *
 *    Return 1 if the field-id 'fid' appears in the field-list
 *    'flist'.  If 'fid' is SK_FIELD_CALLER, return 1 when a field in
 *    'flist' has the id SK_FIELD_CALLER and its context object points
 *    to 'fcontext'.  Return 0 otherwise.
 *
 *    In this function, IPv4 and IPv6 fields are considered
 *    equivalent; that is, you cannot have both SK_FIELD_SIPv4 and
 *    SK_FIELD_SIPv6, and multiple SK_FIELD_CALLER fields are allowed.
 */
static int
isFieldDuplicate(
    const sk_fieldlist_t   *flist,
    sk_fieldid_t            fid,
    const void             *fcontext)
{
    sk_fieldlist_iterator_t fl_iter;
    sk_fieldentry_t *fl_entry;

    skFieldListIteratorBind(flist, &fl_iter);
    switch (fid) {
      case SK_FIELD_SIPv4:
      case SK_FIELD_SIPv6:
        while ((fl_entry = skFieldListIteratorNext(&fl_iter)) != NULL) {
            switch (skFieldListEntryGetId(fl_entry)) {
              case SK_FIELD_SIPv4:
              case SK_FIELD_SIPv6:
                return 1;
              default:
                break;
            }
        }
        break;

      case SK_FIELD_DIPv4:
      case SK_FIELD_DIPv6:
        while ((fl_entry = skFieldListIteratorNext(&fl_iter)) != NULL) {
            switch (skFieldListEntryGetId(fl_entry)) {
              case SK_FIELD_DIPv4:
              case SK_FIELD_DIPv6:
                return 1;
              default:
                break;
            }
        }
        break;

      case SK_FIELD_NHIPv4:
      case SK_FIELD_NHIPv6:
        while ((fl_entry = skFieldListIteratorNext(&fl_iter)) != NULL) {
            switch (skFieldListEntryGetId(fl_entry)) {
              case SK_FIELD_NHIPv4:
              case SK_FIELD_NHIPv6:
                return 1;
              default:
                break;
            }
        }
        break;

      case SK_FIELD_CALLER:
        while ((fl_entry = skFieldListIteratorNext(&fl_iter)) != NULL) {
            if ((skFieldListEntryGetId(fl_entry) == (uint32_t)fid)
                && (skFieldListEntryGetContext(fl_entry) == fcontext))
            {
                return 1;
            }
        }
        break;

      default:
        while ((fl_entry = skFieldListIteratorNext(&fl_iter)) != NULL) {
            if (skFieldListEntryGetId(fl_entry) == (uint32_t)fid) {
                return 1;
            }
        }
        break;
    }
    return 0;
}


static void
adjustTimeFields(
    rwRec              *rwrec)
{
    sktime_t sTime;
    sktime_t sTime_mod;
    uint32_t elapsed;

    switch (time_fields_key) {
      case PARSE_KEY_STIME:
      case (PARSE_KEY_STIME | PARSE_KEY_ELAPSED):
        /* adjust start time */
        sTime = rwRecGetStartTime(rwrec);
        sTime_mod = sTime % time_bin_size;
        rwRecSetStartTime(rwrec, (sTime - sTime_mod));
        break;
      case (PARSE_KEY_STIME | PARSE_KEY_ETIME):
        /* adjust sTime and elapsed/duration */
        sTime = rwRecGetStartTime(rwrec);
        sTime_mod = sTime % time_bin_size;
        rwRecSetStartTime(rwrec, (sTime - sTime_mod));
        /*
         * the following sets elapsed to:
         * ((eTime - (eTime % bin_size)) - (sTime - (sTime % bin_size)))
         */
        elapsed = rwRecGetElapsed(rwrec);
        elapsed = (elapsed + sTime_mod - ((sTime + elapsed) % time_bin_size));
        rwRecSetElapsed(rwrec, elapsed);
        break;
      case PARSE_KEY_ETIME:
      case (PARSE_KEY_ETIME | PARSE_KEY_ELAPSED):
        /* want to set eTime to (eTime - (eTime % bin_size)), but
         * eTime is computed as (sTime + elapsed) */
        sTime = rwRecGetStartTime(rwrec);
        elapsed = rwRecGetElapsed(rwrec);
        rwRecSetStartTime(
            rwrec, (sTime - ((sTime + elapsed) % time_bin_size)));
        break;
      case 0:
      case PARSE_KEY_ELAPSED:
      default:
        skAbortBadCase(time_fields_key);
    }
}


/*
 *    Invoke the function in rwstats.lua that adds sidecar fields
 *    defined in the user's --lua-file argument(s) to the 'rwrec'.
 */
static void
addSidecarFields(
    rwRec              *rwrec)
{
    rwRec *lua_rec;
    int rv;

    /* push the apply_sidecar() function from rwstats.lua */
    lua_rawgeti(L, LUA_REGISTRYINDEX, reg_ref.apply_sidecar);

    /* create a Lua copy of the rwrec, and have the two records point
     * to the same sidecar table */
    lua_rec = sk_lua_push_rwrec(L, NULL);
    rwRecCopy(lua_rec, rwrec, SK_RWREC_COPY_FIXED);
    lua_rec->sidecar = rwRecGetSidecar(rwrec);

    /* if no sidecar table exists, add one to both records */
    if (LUA_NOREF == lua_rec->sidecar) {
        lua_newtable(L);
        lua_rec->sidecar = luaL_ref(L, LUA_REGISTRYINDEX);
        rwRecSetSidecar(rwrec, lua_rec->sidecar);
    }

    /* call the function on the lua record */
    rv = lua_pcall(L, 1, 1, 0);
    if (LUA_OK != rv) {
        skAppPrintErr("%s", lua_tostring(L, -1));
        lua_pop(L, 1);
        assert(0 == lua_gettop(L));
        exit(EXIT_FAILURE);
    }

    /* remove sidecar table from the lua record so it does not get
     * garbage collected, and pop the lua record */
    lua_rec->sidecar = LUA_NOREF;
    lua_pop(L, 1);

    assert(0 == lua_gettop(L));
}


/*
 *  status = readRecord(stream, rwrec);
 *
 *    Fill 'rwrec' with a SiLK Flow record read from 'stream'.
 *
 *    Return the status of reading the record.
 */
static int
readRecord(
    skstream_t         *stream,
    rwRec              *rwrec)
{
    int rv;

    rv = skStreamReadRecord(stream, rwrec);
    if (SKSTREAM_OK != rv) {
        return rv;
    }
    if (num_sidecar_adds) {
        addSidecarFields(rwrec);
    }
    if (time_bin_size) {
        adjustTimeFields(rwrec);
    }
    ++record_count;
    switch (limit.fl_id) {
      case SK_FIELD_RECORDS:
        ++value_total;
        break;
      case SK_FIELD_SUM_BYTES:
        value_total += rwRecGetBytes(rwrec);
        break;
      case SK_FIELD_SUM_PACKETS:
        value_total += rwRecGetPkts(rwrec);
        break;
      default:
        break;
    }

    return rv;
}


/*
 *    Use the flow iterator to read all records from the input
 *    stream(s) and insert each record into the skunique data
 *    structure.
 */
int
readAllRecords(
    void)
{
    rwRec rwrec;
    int rv;

    rwRecInitialize(&rwrec, L);

    if (STATSUNIQ_PROGRAM_UNIQ == this_program) {
        while ((rv = sk_flow_iter_get_next_rec(flowiter, &rwrec)) == 0) {
            if (num_sidecar_adds) {
                addSidecarFields(&rwrec);
            }
            if (time_bin_size) {
                adjustTimeFields(&rwrec);
            }
            if (0 != skUniqueAddRecord(uniq, &rwrec)) {
                appExit(EXIT_FAILURE);
            }
        }
    } else {
        while ((rv = sk_flow_iter_get_next_rec(flowiter, &rwrec)) == 0) {
            if (num_sidecar_adds) {
                addSidecarFields(&rwrec);
            }
            if (time_bin_size) {
                adjustTimeFields(&rwrec);
            }
            if (0 != skUniqueAddRecord(uniq, &rwrec)) {
                appExit(EXIT_FAILURE);
            }
            ++record_count;
            switch (limit.fl_id) {
              case SK_FIELD_RECORDS:
                ++value_total;
                break;
              case SK_FIELD_SUM_BYTES:
                value_total += rwRecGetBytes(&rwrec);
                break;
              case SK_FIELD_SUM_PACKETS:
                value_total += rwRecGetPkts(&rwrec);
                break;
              default:
                break;
            }
        }
    }

    rwRecReset(&rwrec);

    return rv;
}

/*
 *  setOutputHandle();
 *
 *    Enable the pager if using it.
 */
void
setOutputHandle(
    void)
{
    int rv;

    /* only invoke the pager when the user has not specified the
     * output-path, even if output-path is stdout */
    if (NULL == output.of_name) {
        /* invoke the pager */
        rv = skFileptrOpenPager(&output, pager);
        if (rv && rv != SK_FILEPTR_PAGER_IGNORED) {
            skAppPrintErr("Unable to invoke pager");
        }
    }
}


/*
 *  writeAsciiRecord(key, value, distinct);
 *
 *    Verifies that the 'value' and 'distincts' values are within the
 *    limits specified by the user.  If they are not, the function
 *    returns without printing anything.
 *
 *    Unpacks the fields from 'key' and prints the key fields, the
 *    value fields, and the distinct fields to the global output
 *    stream 'output.fp'.
 */
void
writeAsciiRecord(
    uint8_t           **outbuf)
{
    rwRec rwrec;
    uint64_t val64;
    uint32_t val32;
    uint32_t eTime = 0;
    uint16_t dport = 0;
    sk_fieldlist_iterator_t fl_iter;
    sk_fieldentry_t *field;
    builtin_field_t *bf;
    int id;
    char *fmtr_buf = NULL;
    size_t len;
    /* whether IPv4 addresses have been added to a record */
    int added_ipv4 = 0;
    uint8_t ipv6[16];

    /* in mixed IPv4/IPv6 setting, keep record as IPv4 unless an IPv6
     * address forces us to use IPv6. */
#define KEY_TO_REC_IPV6(func_v6, func_v4, rec, field_buf, field_list, field) \
    skFieldListExtractFromBuffer(key_fields, field_buf, field, ipv6);   \
    if (rwRecIsIPv6(rec)) {                                             \
        /* record is already IPv6 */                                    \
        func_v6((rec), ipv6);                                           \
    } else if (SK_IPV6_IS_V4INV6(ipv6)) {                               \
        /* record is IPv4, and so is the IP */                          \
        func_v4((rec), ntohl(*(uint32_t*)(ipv6 + SK_IPV6_V4INV6_LEN))); \
        added_ipv4 = 1;                                                 \
    } else {                                                            \
        /* address is IPv6, but record is IPv4 */                       \
        if (added_ipv4) {                                               \
            /* record has IPv4 addrs; must convert */                   \
            rwRecConvertToIPv6(rec);                                    \
        } else {                                                        \
            /* no addresses on record yet */                            \
            rwRecSetIPv6(rec);                                          \
        }                                                               \
        func_v6((rec), ipv6);                                           \
    }

    /* see if values are within limits */
    if (app_flags.check_limits) {
        /* structure to get the distinct count when using IPv6 */
        union ipv6_distinct_un {
            uint64_t count;
            uint8_t  ip[16];
        } val_ip6;

        skFieldListIteratorBind(value_fields, &fl_iter);
        while (NULL != (field = skFieldListIteratorNext(&fl_iter))) {
            bf = (builtin_field_t*)skFieldListEntryGetContext(field);
            switch (skFieldListEntryGetId(field)) {
              case SK_FIELD_SUM_BYTES:
              case SK_FIELD_SUM_PACKETS:
                skFieldListExtractFromBuffer(value_fields, outbuf[1], field,
                                             (uint8_t*)&val64);
                if ((val64 < bf->bf_min)
                    || (val64 > bf->bf_max))
                {
                    return;
                }
                break;

              case SK_FIELD_RECORDS:
              case SK_FIELD_SUM_ELAPSED:
                skFieldListExtractFromBuffer(value_fields, outbuf[1], field,
                                             (uint8_t*)&val32);
                if ((val32 < bf->bf_min)
                    || (val32 > bf->bf_max))
                {
                    return;
                }
                break;

              default:
                break;
            }
        }
        skFieldListIteratorBind(distinct_fields, &fl_iter);
        while (NULL != (field = skFieldListIteratorNext(&fl_iter))) {
            bf = (builtin_field_t*)skFieldListEntryGetContext(field);
            if (bf) {
                switch (skFieldListEntryGetId(field)) {
                  case SK_FIELD_SIPv6:
                  case SK_FIELD_DIPv6:
                    skFieldListExtractFromBuffer(distinct_fields, outbuf[2],
                                                 field, val_ip6.ip);
                    if ((val_ip6.count < bf->bf_min)
                        || (val_ip6.count > bf->bf_max))
                    {
                        return;
                    }
                    break;

                  case SK_FIELD_SIPv4:
                  case SK_FIELD_DIPv4:
                    skFieldListExtractFromBuffer(distinct_fields, outbuf[2],
                                                 field, (uint8_t*)&val32);
                    if ((val32 < bf->bf_min)
                        || (val32 > bf->bf_max))
                    {
                        return;
                    }
                    break;

                  default:
                    break;
                }
            }
        }
    }

    /* Zero out rwrec to avoid display errors---specifically with msec
     * fields and eTime. */
    RWREC_CLEAR(&rwrec);

    /* Initialize the protocol to 1 (ICMP), so that if the user has
     * requested ICMP type/code but the protocol is not part of the
     * key, we still get ICMP values. */
    rwRecSetProto(&rwrec, IPPROTO_ICMP);

    if (ipv6_policy > SK_IPV6POLICY_MIX) {
        /* Force records to be in IPv6 format */
        rwRecSetIPv6(&rwrec);
    }

    /* unpack the key into 'rwrec' */
    skFieldListIteratorBind(key_fields, &fl_iter);
    while (NULL != (field = skFieldListIteratorNext(&fl_iter))) {
        id = skFieldListEntryGetId(field);
        switch (id) {
          case SK_FIELD_SIPv6:
            KEY_TO_REC_IPV6(rwRecMemSetSIPv6, rwRecSetSIPv4, &rwrec,
                            outbuf[0], key_fields, field);
            break;
          case SK_FIELD_DIPv6:
            KEY_TO_REC_IPV6(rwRecMemSetDIPv6, rwRecSetDIPv4, &rwrec,
                            outbuf[0], key_fields, field);
            break;
          case SK_FIELD_NHIPv6:
            KEY_TO_REC_IPV6(rwRecMemSetNhIPv6, rwRecSetNhIPv4, &rwrec,
                            outbuf[0], key_fields, field);
            break;
          case SK_FIELD_SIPv4:
            KEY_TO_REC_32(rwRecSetSIPv4, &rwrec, outbuf[0],
                          key_fields, field);
            break;
          case SK_FIELD_DIPv4:
            KEY_TO_REC_32(rwRecSetDIPv4, &rwrec, outbuf[0],
                          key_fields, field);
            break;
          case SK_FIELD_NHIPv4:
            KEY_TO_REC_32(rwRecSetNhIPv4, &rwrec, outbuf[0],
                          key_fields, field);
            break;
          case SK_FIELD_SPORT:
            KEY_TO_REC_16(rwRecSetSPort, &rwrec, outbuf[0],
                          key_fields, field);
            break;
          case SK_FIELD_DPORT:
            /* just extract dPort; we will set it later to ensure
             * dPort takes precedence over ICMP type/code */
            skFieldListExtractFromBuffer(key_fields, outbuf[0],
                                         field, (uint8_t*)&dport);
            break;
          case SK_FIELD_ICMP_TYPE:
            KEY_TO_REC_08(rwRecSetIcmpType, &rwrec, outbuf[0],
                          key_fields, field);
            break;
          case SK_FIELD_ICMP_CODE:
            KEY_TO_REC_08(rwRecSetIcmpCode, &rwrec, outbuf[0],
                          key_fields, field);
            break;
          case SK_FIELD_PROTO:
            KEY_TO_REC_08(rwRecSetProto, &rwrec, outbuf[0],
                          key_fields, field);
            break;
          case SK_FIELD_PACKETS:
            KEY_TO_REC_64(rwRecSetPkts, &rwrec, outbuf[0],
                          key_fields, field);
            break;
          case SK_FIELD_BYTES:
            KEY_TO_REC_64(rwRecSetBytes, &rwrec, outbuf[0],
                          key_fields, field);
            break;
          case SK_FIELD_FLAGS:
            KEY_TO_REC_08(rwRecSetFlags, &rwrec, outbuf[0],
                          key_fields, field);
            break;
          case SK_FIELD_SID:
            KEY_TO_REC_16(rwRecSetSensor, &rwrec, outbuf[0],
                          key_fields, field);
            break;
          case SK_FIELD_INPUT:
            KEY_TO_REC_32(rwRecSetInput, &rwrec, outbuf[0],
                          key_fields, field);
            break;
          case SK_FIELD_OUTPUT:
            KEY_TO_REC_32(rwRecSetOutput, &rwrec, outbuf[0],
                          key_fields, field);
            break;
          case SK_FIELD_INIT_FLAGS:
            KEY_TO_REC_08(rwRecSetInitFlags, &rwrec, outbuf[0],
                          key_fields, field);
            break;
          case SK_FIELD_REST_FLAGS:
            KEY_TO_REC_08(rwRecSetRestFlags, &rwrec, outbuf[0],
                          key_fields, field);
            break;
          case SK_FIELD_TCP_STATE:
            KEY_TO_REC_08(rwRecSetTcpState, &rwrec, outbuf[0],
                          key_fields, field);
            break;
          case SK_FIELD_APPLICATION:
            KEY_TO_REC_16(rwRecSetApplication, &rwrec, outbuf[0],
                          key_fields, field);
            break;
          case SK_FIELD_FTYPE_CLASS:
          case SK_FIELD_FTYPE_TYPE:
            KEY_TO_REC_08(rwRecSetFlowType, &rwrec, outbuf[0],
                          key_fields, field);
            break;
          case SK_FIELD_STARTTIME:
            skFieldListExtractFromBuffer(key_fields, outbuf[0],
                                         field, (uint8_t*)&val32);
            rwRecSetStartTime(&rwrec, sktimeCreate(val32, 0));
            break;
          case SK_FIELD_ELAPSED:
            skFieldListExtractFromBuffer(key_fields, outbuf[0],
                                         field, (uint8_t*)&val32);
            rwRecSetElapsed(&rwrec, val32 * 1000);
            break;
          case SK_FIELD_ENDTIME:
            /* just extract eTime; we will set it later */
            skFieldListExtractFromBuffer(key_fields, outbuf[0],
                                         field, (uint8_t*)&eTime);
            break;
          default:
            assert(skFieldListEntryGetId(field) == SK_FIELD_CALLER);
            break;
        }
    }

    if (dport_key) {
        rwRecSetDPort(&rwrec, dport);
    }

    switch (time_fields_key) {
      case PARSE_KEY_ETIME:
        /* etime only; just set sTime to eTime--elapsed is already 0 */
        rwRecSetStartTime(&rwrec, sktimeCreate(eTime, 0));
        break;
      case (PARSE_KEY_ELAPSED | PARSE_KEY_ETIME):
        /* etime and elapsed; set start time based on end time and elapsed */
        val32 = rwRecGetElapsedSeconds(&rwrec);
        rwRecSetStartTime(&rwrec, sktimeCreate((eTime - val32), 0));
        break;
      case (PARSE_KEY_STIME | PARSE_KEY_ETIME):
        /* etime and stime; set elapsed as their difference */
        val32 = rwRecGetStartSeconds(&rwrec);
        assert(val32 <= eTime);
        rwRecSetElapsed(&rwrec, (1000 * (eTime - val32)));
        break;
      case PARSE_KEY_ALL_TIMES:
        /* 'time_fields_key' should contain 0, 1, or 2 time values */
        skAbortBadCase(time_fields_key);
      default:
        assert(0 == time_fields_key
               || PARSE_KEY_STIME == time_fields_key
               || PARSE_KEY_ELAPSED == time_fields_key
               || (PARSE_KEY_STIME | PARSE_KEY_ELAPSED) == time_fields_key);
        break;
    }

    /* print everything */
    len = sk_formatter_record_to_string_extra(fmtr, &rwrec, outbuf, &fmtr_buf);
    fwrite(fmtr_buf, len, 1, output.of_fp);
}


/*
** Local Variables:
** mode:c
** indent-tabs-mode:nil
** c-basic-offset:4
** End:
*/
