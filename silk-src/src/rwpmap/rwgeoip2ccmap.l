%{
/*
** Copyright (C) 2016-2017 by Carnegie Mellon University.
**
** @OPENSOURCE_LICENSE_START@
** See license information in ../../LICENSE.txt
** @OPENSOURCE_LICENSE_END@
*/

/*
 *  rwgeoip2ccmap.l
 *
 *    Create a country code prefixmap (pmap) file from textual CSV
 *    input or binary input in the GeoIP Legacy format.
 *
 *
 *    The lexer expects each line of the input to have six
 *    comma-separated values (six values and five commas), where every
 *    value is wrapped in double-quotes.  Optional whitespace may
 *    surround a value.  The values are
 *
 *    0. Starting IP address in the canonical form
 *
 *    1. Ending IP address in the canonical form
 *
 *    2. Starting IP address as an integer
 *
 *    3. Ending IP address as an integer
 *
 *    4. Two letter country code abbreviation
 *
 *    5. Country name.
 *
 *
 *  Mark Thomas
 *  March 2016
 */

#include <silk/silk.h>

RCSIDENT("$SiLK: rwgeoip2ccmap.l 275df62a2e41 2017-01-05 17:30:40Z mthomas $");

#include <silk/skcountry.h>
#include <silk/skipaddr.h>
#include <silk/skprefixmap.h>
#include <silk/skstream.h>
#include <silk/skvector.h>
#include <silk/utils.h>

/* use TRACEMSG_LEVEL as our tracing variable */
#define TRACEMSG(msg) TRACEMSG_TO_TRACEMSGLVL(1, msg)
#include <silk/sktracemsg.h>


/* LOCAL DEFINES AND TYPEDEFS */

/* where to write --help output */
#define USAGE_FH stdout

/* label to use for unknown ranges */
#define DEFAULT_LABEL   "--"

/* default mode (type of input) to use */
#define DEFAULT_MODE    GEOIP2CCMAP_MODE_AUTO

/* number of fields expected on each line */
#define NUM_FIELDS      6

/* type of input to expect */
enum geoip2ccmap_mode_en {
    GEOIP2CCMAP_MODE_AUTO,
    GEOIP2CCMAP_MODE_IPV4,
    GEOIP2CCMAP_MODE_IPV6,
    GEOIP2CCMAP_MODE_BINARY
};
typedef enum geoip2ccmap_mode_en geoip2ccmap_mode_t;

/* information for the current statement */
typedef struct pmap_stmt_st {
    skipaddr_t          range_start;
    skipaddr_t          range_end;
    uint32_t            value;
    uint32_t            position;
} pmap_stmt_t;


/* LOCAL VARIABLE DEFINITIONS */

/* the pmap that we create */
static skPrefixMap_t *map = NULL;

/* information for the current statement */
static pmap_stmt_t stmt;

/* which line of input we are reading */
static int linenum = 1;

/* number of errors encountered during processing */
static int error_count = 0;

/* type of input: ipv4, ipv6, binary, or determined by first IP */
static geoip2ccmap_mode_t mode = DEFAULT_MODE;

/* default country code value */
static uint32_t default_value;

/* structure to map between mode names and mode IDs */
static struct mode_name_id_map_st {
    const char         *name;
    geoip2ccmap_mode_t  id;
} mode_name_id_map[] = {
    {"auto",       GEOIP2CCMAP_MODE_AUTO},
    {"ipv6",       GEOIP2CCMAP_MODE_IPV6},
    {"ipv4",       GEOIP2CCMAP_MODE_IPV4},
    {"binary",     GEOIP2CCMAP_MODE_BINARY}
};

/* help for each mode */
static const char *mode_help[] = {
    "Automatically determine the type of input",
    "Read textual IPv6 input and create an IPv6 prefix map",
    "Read textual IPv4 input and create an IPv4 prefix map",
    "Read binary input and create an IPv4 or IPv6 prefix map"
};

/* number of entries in 'mode_name_id_map' */
static const size_t mode_name_id_map_len
    = sizeof(mode_name_id_map)/sizeof(mode_name_id_map[0]);

/* input stream for reading the prefix map text */
static sk_fileptr_t in_stream;

/* output stream for the resulting prefix map */
static skstream_t *out_stream = NULL;

/* do not write the output file; simply check syntax of the input.
 * set by --dry-run */
static int dry_run = 0;

/* do not record the command line invocation in the generated prefix
 * map file. set by --invocation-strip */
static int invocation_strip = 0;


/* OPTIONS SETUP */

typedef enum {
    OPT_INPUT_FILE,
    OPT_OUTPUT_FILE,
    OPT_MODE,
    OPT_DRY_RUN,
    OPT_INVOCATION_STRIP,
    OPT_CSV_INPUT,
    OPT_V6_CSV_INPUT,
    OPT_ENCODED_INPUT
} appOptionsEnum;

static struct option appOptions[] = {
    {"input-file",          REQUIRED_ARG, 0, OPT_INPUT_FILE},
    {"output-file",         REQUIRED_ARG, 0, OPT_OUTPUT_FILE},
    {"mode",                REQUIRED_ARG, 0, OPT_MODE},
    {"dry-run",             NO_ARG,       0, OPT_DRY_RUN},
    {"invocation-strip",    NO_ARG,       0, OPT_INVOCATION_STRIP},
    {"csv-input",           NO_ARG,       0, OPT_CSV_INPUT},
    {"v6-csv-input",        NO_ARG,       0, OPT_V6_CSV_INPUT},
    {"encoded-input",       NO_ARG,       0, OPT_ENCODED_INPUT},
    {0,0,0,0}               /* sentinel entry */
};


static const char *appHelp[] = {
    ("Read the CSV form or the binary form of the GeoIP Legacy\n"
     "\tcountry code data from this file. Read from the standard input when\n"
     "\tthe switch is not provided"),
    ("Write the binary country code prefix map to this file.\n"
     "\tWrite to the standard output when the switch is not provided"),
    ("Set the type of the input and the type country code file to\n"
     "\tcreate. By default, the type of prefix map is determined by the\n"
     "\tfirst IP address seen."),
    "Parse the input but do not write the output file",
    ("Strip invocation history from the prefix map file.\n"
     "\tDef. Record command used to create the file"),
    ("Deprecated.  Replace with --mode=ipv4.\n"
     "\tAssume the input is the CSV GeoIP Legacy country code data for IPv4"),
    ("Deprecated.  Replace with --mode=ipv6.\n"
     "\tAssume the input is the CSV GeoIP Legacy country code data for IPv6"),
    ("Deprecated.  Replace with --mode=binary.\n"
     "\tAssume the input is binary GeoIP Legacy country code data for either\n"
     "\tIPv4 or IPv6"),
    (char *)NULL
};


/* LOCAL FUNCTION PROTOTYPES */

static int appOptionsHandler(clientData cData, int opt_index, char *opt_arg);
static int parseMode(int idx, const char *str, geoip2ccmap_mode_t *new_mode);
static const char *modeToName(geoip2ccmap_mode_t m);

static int  stmtEntry(const char *string);
static void stmtCreateEntry(void);
static void stmtReset(void);

/* to keep gcc quiet  */
int
yylex(
    void);

SK_DIAGNOSTIC_IGNORE_PUSH("-Wwrite-strings")


/*  *****  LEX INPUT FOLLOWS  *****  */

%}

%option prefix="geoip2ccmap_" outfile="lex.yy.c"
%option never-interactive
%option noinput nounput noyywrap noreject

    /* %option noyyget_extra noyyset_extra noyyget_leng noyyget_text */
    /* %option noyyget_lineno noyyset_lineno noyyget_in noyyset_in */
    /* %option noyyget_out noyyset_out noyyget_lval noyyset_lval */
    /* %option noyyget_lloc noyyset_lloc noyyget_debug noyyset_debug */


%s ST_ENTRY
%s ST_CLOSE_ENTRY
%s ST_NEXT_ENTRY
%s ST_ERROR

ws_opt  [ \t\r]*

nl      [ \t\r]*\n

dq      \"
not_dq  [^\"\r\n]+

%%

<INITIAL>{nl}                         { ++linenum; stmtReset(); }

<INITIAL>{ws_opt}{dq}                 { BEGIN(ST_ENTRY); }

<ST_ENTRY>{not_dq}                    { if (stmtEntry(yytext)) {
                                            ++error_count;
                                            BEGIN(ST_ERROR);
                                        } else {
                                            BEGIN(ST_CLOSE_ENTRY);
                                        } }

<ST_CLOSE_ENTRY>{dq}                  { BEGIN(ST_NEXT_ENTRY); }

<ST_NEXT_ENTRY>,                      { ++stmt.position; }

<ST_NEXT_ENTRY>{dq}                   { BEGIN(ST_ENTRY); }

<ST_NEXT_ENTRY>{ws_opt}               ;

<ST_NEXT_ENTRY>{nl}                   |
<ST_NEXT_ENTRY><<EOF>>                { ++stmt.position;
                                        stmtCreateEntry();
                                        ++linenum;
                                        stmtReset();
                                        BEGIN(INITIAL); }

<ST_ENTRY>{dq}                        { skAppPrintErr("Empty value on line %d",
                                                      linenum);
                                        ++error_count;
                                        BEGIN(ST_ERROR);
                                      }

<ST_ENTRY,ST_CLOSE_ENTRY>\n           { skAppPrintErr(("Line break appears"
                                                       " in value on line %d"),
                                                      linenum);
                                        ++error_count;
                                        ++linenum;
                                        stmtReset();
                                        BEGIN(INITIAL); }

<ST_ENTRY,ST_CLOSE_ENTRY><<EOF>>      { skAppPrintErr(("End of stream appears"
                                                       " in value on line %d"),
                                                      linenum);
                                        ++error_count;
                                        BEGIN(ST_ERROR); }

.                                     { skAppPrintErr(
                                            "Unexpected input on line %d",
                                            linenum);
                                        ++error_count;
                                        BEGIN(ST_ERROR); }

<ST_ERROR>.*                          ;
<ST_ERROR>\n                          { ++linenum;
                                        stmtReset();
                                        BEGIN(INITIAL); }

<INITIAL,ST_ERROR><<EOF>>             { return 0; }

%%

SK_DIAGNOSTIC_IGNORE_POP("-Wwrite-strings")

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
    ("[SWITCHES]\n"                                                           \
     "\tCreate a binary prefix map file from an input file in the MaxMind\n"  \
     "\tGeoIP Legacy format.  Both the comma separated value (CSV) and the\n" \
     "\tbinary forms of the input are supported.  The input is read from\n"   \
     "\tthe named input file or from the standard input.  The prefix map\n"   \
     "\tfile is written to the named location or to the standard output if\n" \
     "\tstdout is not connected to a terminal.\n")

    FILE *fh = USAGE_FH;
    int i;
    size_t j;

    fprintf(fh, "%s %s", skAppName(), USAGE_MSG);
    fprintf(fh, "\nSWITCHES:\n");
    skOptionsDefaultUsage(fh);
    for (i = 0; appOptions[i].name; ++i) {
        switch (appOptions[i].val) {
          case OPT_MODE:
            fprintf(fh, "--%s %s. %s\tDef. %s. Choices:\n",
                    appOptions[i].name,
                    SK_OPTION_HAS_ARG(appOptions[i]), appHelp[i],
                    modeToName(DEFAULT_MODE));
            for (j = 0; j < mode_name_id_map_len; ++j) {
                fprintf(fh, "\t%-6s - %s\n",
                        mode_name_id_map[j].name, mode_help[j]);
            }
            break;

          case OPT_INVOCATION_STRIP:
            /* include the help for --notes before
             * --invocation-strip */
            skOptionsNotesUsage(fh);
            /* FALLTHROUGH */

          default:
            fprintf(fh, "--%s %s. %s\n", appOptions[i].name,
                    SK_OPTION_HAS_ARG(appOptions[i]), appHelp[i]);
            break;
        }
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

    /* close streams; destroy prefix map */
    skFileptrClose(&in_stream, NULL);
    skStreamDestroy(&out_stream);
    if (map) {
        skPrefixMapDelete(map);
    }

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
    int rv;

    /* verify same number of options and help strings */
    assert((sizeof(appHelp)/sizeof(char *)) ==
           (sizeof(appOptions)/sizeof(struct option)));
    assert((sizeof(mode_help)/sizeof(mode_help[0])) == mode_name_id_map_len);

    /* register the application */
    skAppRegister(argv[0]);
    skAppVerifyFeatures(&features, NULL);
    skOptionsSetUsageCallback(&appUsageLong);
    memset(&in_stream, 0, sizeof(in_stream));

    /* initialize */
    default_value = skCountryNameToCode(DEFAULT_LABEL);

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
        skAppUsage(); /* never returns */
    }

    /* Complain about extra args on command line */
    if (arg_index != argc) {
        skAppPrintErr("Too many or unrecognized argument specified: '%s'",
                      argv[arg_index]);
        exit(EXIT_FAILURE);
    }

    /* check for input; if none specified, use stdin */
    if (!in_stream.of_name) {
        if (FILEIsATty(stdin)) {
            skAppPrintErr("Please specify '--%s=-' to read from the terminal",
                          appOptions[OPT_INPUT_FILE].name);
            exit(EXIT_FAILURE);
        }
        in_stream.of_name = "-";
    }

    /* open input stream */
    rv = skFileptrOpen(&in_stream, SK_IO_READ);
    if (rv) {
        skAppPrintErr("Unable to open input '%s': %s",
                      in_stream.of_name, skFileptrStrerror(rv));
        exit(EXIT_FAILURE);
    }

    /* output is "stdout" if none specified.  do not bind to stdout
     * when --dry-run is active */
    if (!out_stream && !dry_run) {
        if ((rv = skStreamCreate(&out_stream, SK_IO_WRITE, SK_CONTENT_SILK))
            || (rv = skStreamBind(out_stream, "stdout")))
        {
            skStreamPrintLastErr(out_stream, rv, &skAppPrintErr);
            exit(EXIT_FAILURE);
        }
    }

    return; /* OK */
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
    int rv;

    switch ((appOptionsEnum)opt_index) {
      case OPT_INPUT_FILE:
        if (in_stream.of_name) {
            skAppPrintErr("Invalid %s: Switch used multiple times",
                          appOptions[opt_index].name);
            return 1;
        }
        in_stream.of_name = opt_arg;
        break;

      case OPT_OUTPUT_FILE:
        if (out_stream) {
            skAppPrintErr("Invalid %s: Switch used multiple times",
                          appOptions[opt_index].name);
            return 1;
        }
        if ((rv = skStreamCreate(&out_stream, SK_IO_WRITE, SK_CONTENT_SILK))
            || (rv = skStreamBind(out_stream, opt_arg)))
        {
            skStreamPrintLastErr(out_stream, rv, &skAppPrintErr);
            skStreamDestroy(&out_stream);
            return 1;
        }
        break;

      case OPT_MODE:
        if (parseMode(opt_index, opt_arg, &mode)) {
            return 1;
        }
        break;

      case OPT_DRY_RUN:
        dry_run = 1;
        break;

      case OPT_INVOCATION_STRIP:
        invocation_strip = 1;
        break;

      case OPT_CSV_INPUT:
        if (parseMode(opt_index, modeToName(GEOIP2CCMAP_MODE_IPV4), &mode)) {
            return 1;
        }
        break;

      case OPT_V6_CSV_INPUT:
        if (parseMode(opt_index, modeToName(GEOIP2CCMAP_MODE_IPV6), &mode)) {
            return 1;
        }
        break;

      case OPT_ENCODED_INPUT:
        if (parseMode(opt_index, modeToName(GEOIP2CCMAP_MODE_BINARY), &mode)) {
            return 1;
        }
        break;
    }

    return 0; /* OK */
}


/*
 *  ok = parseMode(string, &new_mode);
 *
 *    Parse the value in 'string' as the name of a mode and put the
 *    mode's value in 'new_mode'.
 *
 *    Return 0 on success, or -1 if 'string' does not match a mode.
 */
static int
parseMode(
    int                 opt_index,
    const char         *string,
    geoip2ccmap_mode_t *new_mode)
{
    static int mode_seen = 0;
    const char *err;
    size_t j;

    if (mode_seen) {
        skAppPrintErr("Invalid %s: Attempting to set mode multiple times",
                      appOptions[opt_index].name);
        return -1;
    }

    for (j = 0; j < mode_name_id_map_len; ++j) {
        if (0 == strncmp(string, mode_name_id_map[j].name, strlen(string))) {
            ++mode_seen;
            *new_mode = mode_name_id_map[j].id;
        }
    }
    if (1 == mode_seen) {
        return 0;
    }
    if (mode_seen > 1) {
        err = "Name is ambiguous";
    } else {
        err = "Unrecognized name";
        mode_seen = -1;
    }
    skAppPrintErr("Invalid %s '%s': %s",
                  appOptions[opt_index].name, string, err);
    return -1;
}


/*
 *  name = modeToName(mode);
 *
 *    Return the string that presents the mode 'mode'.  Abort if
 *    'mode' is not a valid mode.
 */
static const char *
modeToName(
    geoip2ccmap_mode_t  m)
{
    size_t j;

    for (j = 0; j < mode_name_id_map_len; ++j) {
        if (m == mode_name_id_map[j].id) {
            return mode_name_id_map[j].name;
        }
    }

    skAppPrintErr("Unknown mode value %d", (int)m);
    skAbort();
}


static void
stmtReset(
    void)
{
    memset(&stmt, 0, sizeof(stmt));
    stmt.value = default_value;
}

/*
 *  status = stmtEntry(string);
 *
 *    Handle part of the current line of input.
 *
 *    Return 0 on success or -1 for failure.
 */
static int
stmtEntry(
    const char         *string)
{
    int rv;

    switch (stmt.position) {
      case 0:
        /* starting IP */
        rv = skStringParseIP(&stmt.range_start, string);
        if (rv) {
            goto PARSE_ERROR;
        }
        break;
      case 1:
        /* ending IP */
        rv = skStringParseIP(&stmt.range_end, string);
        if (rv) {
            goto PARSE_ERROR;
        }
        if (skipaddrCompare(&stmt.range_start, &stmt.range_end) > 0) {
            skAppPrintErr("Invalid range on line %d: start is greater then end",
                          linenum);
            return -1;
        }
        break;
      case 2:
      case 3:
        /* starting or ending IP as an integer */
        break;
      case 4:
        /* country code as upper case characters */
        stmt.value = (uint32_t)skCountryNameToCode(string);
        if (SK_COUNTRYCODE_INVALID == stmt.value) {
            skAppPrintErr("Invalid country code on line %d '%s'",
                          linenum, string);
            return -1;
        }
        break;
      case 5:
        /* country name */
        break;
      case NUM_FIELDS:
        /* too many fields; only print the error for the first
         * field */
        skAppPrintErr("Invalid input on line %d:"
                      " More than %d CSV values are present",
                      linenum, NUM_FIELDS);
        return -1;
        break;
      default:
        break;
    }

    return 0;

  PARSE_ERROR:
    skAppPrintErr("Invalid IP on line %d '%s': %s",
                  linenum, string, skStringParseStrerror(rv));
    return -1;
}


static void
stmtCreateEntry(
    void)
{
    static int first_entry = 1;
    skPrefixMapErr_t rv;

    if (stmt.position < NUM_FIELDS) {
        skAppPrintErr("Invalid input on line %d:"
                      " Fewer than %d CSV values are present",
                      linenum, NUM_FIELDS);
        ++error_count;
    }

    if (first_entry) {
        first_entry = 0;
        if (GEOIP2CCMAP_MODE_AUTO == mode) {
            skPrefixMapContent_t content;
            if (skipaddrIsV6(&stmt.range_start)) {
                mode = GEOIP2CCMAP_MODE_IPV6;
                content = SKPREFIXMAP_CONT_ADDR_V6;
            } else {
                mode = GEOIP2CCMAP_MODE_IPV4;
                content = SKPREFIXMAP_CONT_ADDR_V4;
            }
            skPrefixMapSetContentType(map, content);
        }
    }

#if SK_ENABLE_IPV6
    if (GEOIP2CCMAP_MODE_IPV4 == mode) {
        uint32_t ipv4;
        if (skipaddrGetAsV4(&stmt.range_start, &ipv4)
            || skipaddrGetAsV4(&stmt.range_end, &ipv4))
        {
            skAppPrintErr(("Invalid IP on line %d:"
                           " IPv6 address not allowed in %s prefix map"),
                          linenum, modeToName(mode));
            ++error_count;
            return;
        }
    }
#endif  /* SK_ENABLE_IPV6 */

    rv = skPrefixMapAddRange(map, &stmt.range_start, &stmt.range_end,
                             stmt.value);
    if (rv) {
        skAppPrintErr("Cannot add entry on line %d to prefix map: %s",
                      linenum, skPrefixMapStrerror(rv));
        ++error_count;
    }
}


/*
 *    Read the binary GeoIP Legacy format and create a prefix map.
 *
 *    Each node in input consists of 6 bytes: two 24-bit,
 *    little-endian numbers representing the left and right branches
 *    of the tree.  If the most significant 16 bits of the input value
 *    are all high, that value is a leaf and the least-significant
 *    btye is an index into the country code look-up table.
 *
 */
/* number of country codes */
#define NUM_CC          256

/* how our prefixmap code marks a leaf */
#define PMAP_LEAF_BIT   UINT32_C(0x80000000)

/* how the input marks a leaf */
#define INPUT_LEAF_MASK UINT32_C(0x00ffff00)

static void
handleBinaryInput(
    int                 argc,
    char              **argv)
{
    const char cc[NUM_CC][3] = {
        "--", "ap", "eu", "ad", "ae", "af", "ag", "ai",
        "al", "am", "cw", "ao", "aq", "ar", "as", "at",
        "au", "aw", "az", "ba", "bb", "bd", "be", "bf",
        "bg", "bh", "bi", "bj", "bm", "bn", "bo", "br",

        "bs", "bt", "bv", "bw", "by", "bz", "ca", "cc",
        "cd", "cf", "cg", "ch", "ci", "ck", "cl", "cm",
        "cn", "co", "cr", "cu", "cv", "cx", "cy", "cz",
        "de", "dj", "dk", "dm", "do", "dz", "ec", "ee",

        "eg", "eh", "er", "es", "et", "fi", "fj", "fk",
        "fm", "fo", "fr", "sx", "ga", "gb", "gd", "ge",
        "gf", "gh", "gi", "gl", "gm", "gn", "gp", "gq",
        "gr", "gs", "gt", "gu", "gw", "gy", "hk", "hm",

        "hn", "hr", "ht", "hu", "id", "ie", "il", "in",
        "io", "iq", "ir", "is", "it", "jm", "jo", "jp",
        "ke", "kg", "kh", "ki", "km", "kn", "kp", "kr",
        "kw", "ky", "kz", "la", "lb", "lc", "li", "lk",

        "lr", "ls", "lt", "lu", "lv", "ly", "ma", "mc",
        "md", "mg", "mh", "mk", "ml", "mm", "mn", "mo",
        "mp", "mq", "mr", "ms", "mt", "mu", "mv", "mw",
        "mx", "my", "mz", "na", "nc", "ne", "nf", "ng",

        "ni", "nl", "no", "np", "nr", "nu", "nz", "om",
        "pa", "pe", "pf", "pg", "ph", "pk", "pl", "pm",
        "pn", "pr", "ps", "pt", "pw", "py", "qa", "re",
        "ro", "ru", "rw", "sa", "sb", "sc", "sd", "se",

        "sg", "sh", "si", "sj", "sk", "sl", "sm", "sn",
        "so", "sr", "st", "sv", "sy", "sz", "tc", "td",
        "tf", "tg", "th", "tj", "tk", "tm", "tn", "to",
        "tl", "tr", "tt", "tv", "tw", "tz", "ua", "ug",

        "um", "us", "uy", "uz", "va", "vc", "ve", "vg",
        "vi", "vn", "vu", "wf", "ws", "ye", "yt", "rs",
        "za", "zm", "me", "zw", "a1", "a2", "o1", "ax",
        "gg", "im", "je", "bl", "mf", "bq", "ss", "o1"
    };
    uint32_t leaf_cc[NUM_CC];
    sk_file_header_t *hdr;
    sk_vector_t *vec;
    uint8_t buf[6];
    size_t i;
    size_t j;
    uint32_t count;
    ssize_t rv;
    uint32_t node[2];
    int is_ipv6;

    /* convert string country codes to leaf values */
    for (i = 0; i < NUM_CC; ++i) {
        assert(skCountryNameToCode(cc[i]) != SK_COUNTRYCODE_INVALID);
        leaf_cc[i] = PMAP_LEAF_BIT | skCountryNameToCode(cc[i]);
    }

    /* create a vector to hold the nodes */
    vec = skVectorNew(sizeof(node));
    if (NULL == vec) {
        skAppPrintOutOfMemory("vector");
        exit(EXIT_FAILURE);
    }

    /* read input and add nodes to the vector */
    while (fread(buf, sizeof(buf), 1, in_stream.of_fp)) {
        for (i = 0, j = 0; i < 2; ++i, j += 3) {
            node[i] = ((buf[j+2] << 16) | (buf[j+1] << 8) | buf[j]);
            if (node[i] >= INPUT_LEAF_MASK) {
                node[i] = leaf_cc[buf[j]];
            }
        }
        if (skVectorAppendValue(vec, &node)) {
            skAppPrintOutOfMemory("vector elements");
            exit(EXIT_FAILURE);
        }
    }

    /* finished reading the input; verify the tree */

    /* visit each node until we find a node that points to a node
     * outside the tree; set the number of nodes to the index of the
     * node that points outside the tree */
    count = skVectorGetCount(vec);
    for (i = 0; i < count; ++i) {
        skVectorGetValue(&node, vec, i);
        if (((node[0] >= count) && !(node[0] & PMAP_LEAF_BIT))
            || ((node[1] >= count) && !(node[1] & PMAP_LEAF_BIT)))
        {
            count = i;
            break;
        }
    }
    if (0 == count) {
        skAppPrintErr("No valid nodes read from input");
        exit(EXIT_FAILURE);
    }

    /* detemine whether the input is IPv4 by looking over all paths
     * and recording the maximum depth */
    is_ipv6 = 0;
    {
#define MAX_DEPTH           128
#define LEAF_VALUE(leaf)    (((leaf) & ~PMAP_LEAF_BIT) & UINT32_MAX)

#if TRACEMSG_LEVEL > 0
        const char isleaf[2] = {' ', 'L'};
#endif
        uint32_t path[MAX_DEPTH];
        uint8_t lr[MAX_DEPTH];
        uint32_t n;
        int depth;
        int max_seen;

        depth = max_seen = 0;
        lr[depth] = 0;
        path[depth] = 0;
        while (depth >= 0) {
            if (lr[depth] > 1) {
                /* done with this level */
                TRACEMSG(("%4d  %8u  %u", depth, path[depth], lr[depth]));
                --depth;
            } else {
                skVectorGetValue(&node, vec, path[depth]);
                TRACEMSG(("%4d  %8u  %u  %8u%c  %8u%c",
                          depth, path[depth], lr[depth],
                          LEAF_VALUE(node[0]), isleaf[ node[0]>>31 ],
                          LEAF_VALUE(node[1]), isleaf[ node[1]>>31 ]));
                n = node[lr[depth]];
                ++lr[depth];
                if (!(n & PMAP_LEAF_BIT)) {
                    /* add a level */
                    ++depth;
                    if (depth > max_seen) {
                        max_seen = depth;
                        if (depth >= MAX_DEPTH) {
                            skAppPrintErr(
                                "Tree is malformed; tree is too deep");
                            exit(EXIT_FAILURE);
                        }
                    }
                    /* initialize the level */
                    lr[depth] = 0;
                    path[depth] = n;
                    if (n >= count) {
                        skAppPrintErr(("Tree is malformed; value %" PRIu32
                                       " is larger than tree size %" PRIu32),
                                      n, count);
                        exit(EXIT_FAILURE);
                    }
                }
            }
        }
        TRACEMSG(("max depth is %d", max_seen));
        if (max_seen > 31) {
            is_ipv6 = 1;
        }
    }

    /* we finished with verification; prepare and write the output */
    if (dry_run) {
        return;
    }

    /* initialize the stream's header (from skprefixmap.c) */
    hdr = skStreamGetSilkHeader(out_stream);
    skHeaderSetFileFormat(hdr, FT_PREFIXMAP);
    /* Country Code file versions: IPv4 is v1; IPv6 is v5 */
    skHeaderSetRecordVersion(hdr, (is_ipv6 ? 5 : 1));
    skHeaderSetCompressionMethod(hdr, SK_COMPMETHOD_NONE);
    skHeaderSetRecordLength(hdr, 1);

    /* add invocation */
    if (!invocation_strip) {
        rv = skHeaderAddInvocation(hdr, 1, argc, argv);
        if (rv) {
            skStreamPrintLastErr(out_stream, rv, &skAppPrintErr);
            exit(EXIT_FAILURE);
        }
    }

    /* add notes */
    rv = skOptionsNotesAddToStream(out_stream);
    if (rv) {
        skStreamPrintLastErr(out_stream, rv, &skAppPrintErr);
        exit(EXIT_FAILURE);
    }
    skOptionsNotesTeardown();

    /* open the output and write the header output */
    if ((rv = skStreamOpen(out_stream))
        || (rv = skStreamWriteSilkHeader(out_stream)))
    {
        skStreamPrintLastErr(out_stream, rv, &skAppPrintErr);
        exit(EXIT_FAILURE);
    }

    /* write the number of nodes, then write each node */
    if (skStreamWrite(out_stream, &count, sizeof(count)) == -1) {
        skStreamPrintLastErr(out_stream, -1, &skAppPrintErr);
        exit(EXIT_FAILURE);
    }
    for (i = 0; i < count; ++i) {
        skVectorGetValue(&node, vec, i);
        if (skStreamWrite(out_stream, &node, sizeof(node)) == -1) {
            skStreamPrintLastErr(out_stream, -1, &skAppPrintErr);
            exit(EXIT_FAILURE);
        }
    }

    skVectorDestroy(vec);

    rv = skStreamClose(out_stream);
    if (rv) {
        skStreamPrintLastErr(out_stream, rv, &skAppPrintErr);
        exit(EXIT_FAILURE);
    }
}


int main(
    int         argc,
    char      **argv)
{
    int rv;

    appSetup(argc, argv);       /* never returns on error */

    if (GEOIP2CCMAP_MODE_AUTO == mode) {
        /* read first char to see if stream is binary */
        int c;
        c = getc(in_stream.of_fp);
        if (EOF == c) {
            if (ferror(in_stream.of_fp)) {
                skAppPrintSyserror("Unable to read from %s",
                                   in_stream.of_name);
                exit(EXIT_FAILURE);
            }
        } else {
            if (!isprint(c) && !isspace(c)) {
                mode = GEOIP2CCMAP_MODE_BINARY;
            }
            c = ungetc(c, in_stream.of_fp);
            if (EOF == c) {
                skAppPrintErr("Unable to put back character");
                exit(EXIT_FAILURE);
            }
        }
    }
    if (GEOIP2CCMAP_MODE_BINARY == mode) {
        handleBinaryInput(argc, argv);
        return 0;
    }

    /* Create the output prefix map */
    if (skPrefixMapCreate(&map) != SKPREFIXMAP_OK) {
        skAppPrintErr("Error creating prefix map");
        exit(EXIT_FAILURE);
    }

    /* set the content type to the current mode, or to IPv4 if the
     * mode is "auto"; if auto, the content type may be changed once
     * we have the first line of input */
    switch (mode) {
      case GEOIP2CCMAP_MODE_IPV6:
        skPrefixMapSetContentType(map, SKPREFIXMAP_CONT_ADDR_V6);
        break;
      case GEOIP2CCMAP_MODE_IPV4:
      case GEOIP2CCMAP_MODE_AUTO:
        skPrefixMapSetContentType(map, SKPREFIXMAP_CONT_ADDR_V4);
        break;
      case GEOIP2CCMAP_MODE_BINARY:
        skAbortBadCase(mode);
    }

    rv = skPrefixMapSetDefaultVal(map, default_value);
    if (rv) {
        skAppPrintErr("Error setting default value: %s",
                      skPrefixMapStrerror(rv));
        exit(EXIT_FAILURE);
    }

    stmtReset();

    /* Process input */
    yyin = in_stream.of_fp;
    yylex();

    /* finished scanning.  clean up flex's state */
#ifdef SK_HAVE_YYLEX_DESTROY
    yylex_destroy();
#else
    yy_delete_buffer(YY_CURRENT_BUFFER);
#endif

    if (error_count) {
        skAppPrintErr("Encountered %d error%s during processing.%s",
                      error_count, ((error_count > 1) ? "s" : ""),
                      (dry_run ? "  Output not written." : ""));
        if (dry_run) {
            exit(EXIT_FAILURE);
        }
    }
    if (dry_run) {
        appTeardown();
        return 0;
    }

    /* add invocation */
    if (!invocation_strip) {
        rv = skHeaderAddInvocation(skStreamGetSilkHeader(out_stream), 1,
                                   argc, argv);
        if (rv) {
            skStreamPrintLastErr(out_stream, rv, &skAppPrintErr);
            exit(EXIT_FAILURE);
        }
    }

    /* add notes if given */
    rv = skOptionsNotesAddToStream(out_stream);
    if (rv) {
        skStreamPrintLastErr(out_stream, rv, &skAppPrintErr);
        exit(EXIT_FAILURE);
    }
    skOptionsNotesTeardown();

    /* write output */
    rv = skStreamOpen(out_stream);
    if (rv) {
        skStreamPrintLastErr(out_stream, rv, &skAppPrintErr);
        exit(EXIT_FAILURE);
    }

    rv = skPrefixMapWrite(map, out_stream);
    if (rv != SKPREFIXMAP_OK) {
        if (rv == SKPREFIXMAP_ERR_IO) {
            skStreamPrintLastErr(out_stream,
                                 skStreamGetLastReturnValue(out_stream),
                                 &skAppPrintErr);
        } else {
            skAppPrintErr("Error writing prefix map to '%s': %s",
                          skStreamGetPathname(out_stream),
                          skPrefixMapStrerror(rv));
        }
        exit(EXIT_FAILURE);
    }

    rv = skStreamClose(out_stream);
    if (rv) {
        skStreamPrintLastErr(out_stream, rv, &skAppPrintErr);
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
