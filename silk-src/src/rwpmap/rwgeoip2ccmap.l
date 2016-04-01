%{
/*
** Copyright (C) 2016 by Carnegie Mellon University.
**
** @OPENSOURCE_HEADER_START@
**
** Use of the SILK system and related source code is subject to the terms
** of the following licenses:
**
** GNU General Public License (GPL) Rights pursuant to Version 2, June 1991
** Government Purpose License Rights (GPLR) pursuant to DFARS 252.227.7013
**
** NO WARRANTY
**
** ANY INFORMATION, MATERIALS, SERVICES, INTELLECTUAL PROPERTY OR OTHER
** PROPERTY OR RIGHTS GRANTED OR PROVIDED BY CARNEGIE MELLON UNIVERSITY
** PURSUANT TO THIS LICENSE (HEREINAFTER THE "DELIVERABLES") ARE ON AN
** "AS-IS" BASIS. CARNEGIE MELLON UNIVERSITY MAKES NO WARRANTIES OF ANY
** KIND, EITHER EXPRESS OR IMPLIED AS TO ANY MATTER INCLUDING, BUT NOT
** LIMITED TO, WARRANTY OF FITNESS FOR A PARTICULAR PURPOSE,
** MERCHANTABILITY, INFORMATIONAL CONTENT, NONINFRINGEMENT, OR ERROR-FREE
** OPERATION. CARNEGIE MELLON UNIVERSITY SHALL NOT BE LIABLE FOR INDIRECT,
** SPECIAL OR CONSEQUENTIAL DAMAGES, SUCH AS LOSS OF PROFITS OR INABILITY
** TO USE SAID INTELLECTUAL PROPERTY, UNDER THIS LICENSE, REGARDLESS OF
** WHETHER SUCH PARTY WAS AWARE OF THE POSSIBILITY OF SUCH DAMAGES.
** LICENSEE AGREES THAT IT WILL NOT MAKE ANY WARRANTY ON BEHALF OF
** CARNEGIE MELLON UNIVERSITY, EXPRESS OR IMPLIED, TO ANY PERSON
** CONCERNING THE APPLICATION OF OR THE RESULTS TO BE OBTAINED WITH THE
** DELIVERABLES UNDER THIS LICENSE.
**
** Licensee hereby agrees to defend, indemnify, and hold harmless Carnegie
** Mellon University, its trustees, officers, employees, and agents from
** all claims or demands made against them (and any related losses,
** expenses, or attorney's fees) arising out of, or relating to Licensee's
** and/or its sub licensees' negligent use or willful misuse of or
** negligent conduct or willful misconduct regarding the Software,
** facilities, or other rights or assistance granted by Carnegie Mellon
** University under this License, including, but not limited to, any
** claims of product liability, personal injury, death, damage to
** property, or violation of any laws or regulations.
**
** Carnegie Mellon University Software Engineering Institute authored
** documents are sponsored by the U.S. Department of Defense under
** Contract FA8721-05-C-0003. Carnegie Mellon University retains
** copyrights in all material produced under this contract. The U.S.
** Government retains a non-exclusive, royalty-free license to publish or
** reproduce these documents, or allow others to do so, for U.S.
** Government purposes only pursuant to the copyright license under the
** contract clause at 252.227.7013.
**
** @OPENSOURCE_HEADER_END@
*/

/*
 *  rwgeoip2ccmap.l
 *
 *    Create a country code prefixmap (pmap) file from textual CSV input.
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

RCSIDENT("$SiLK: rwgeoip2ccmap.l 9dcc642f0092 2016-03-24 15:26:17Z mthomas $");

#include <silk/skcountry.h>
#include <silk/skipaddr.h>
#include <silk/skprefixmap.h>
#include <silk/skstream.h>
#include <silk/utils.h>


/* LOCAL DEFINES AND TYPEDEFS */

/* where to write --help output */
#define USAGE_FH stdout

/* label to use for unknown ranges */
#define DEFAULT_LABEL   "--"

/* since a country code map is for IP addresses, this file uses the
 * proto/port prefixmap type to indicate that the type of prefix map
 * to create is determined by the first entry we parse */
#define GEOIP2CCMAP_CONT_AUTO   SKPREFIXMAP_CONT_PROTO_PORT

/* default mode to use */
#define DEFAULT_MODE    GEOIP2CCMAP_CONT_AUTO

/* number of fields expected on each line */
#define NUM_FIELDS      6

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

/* type of input: ipv4, ipv6, or determined by first IP */
static skPrefixMapContent_t mode = DEFAULT_MODE;

/* default country code value */
static uint32_t default_value;

/* structure to map between mode names and mode IDs */
static struct mode_name_id_map_st {
    const char            *name;
    skPrefixMapContent_t   id;
} mode_name_id_map[] = {
    {"auto",       GEOIP2CCMAP_CONT_AUTO},
    {"ipv6",       SKPREFIXMAP_CONT_ADDR_V6},
    {"ipv4",       SKPREFIXMAP_CONT_ADDR_V4}
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
    ("Read the comma-separated-value form of the GeoIP Legacy\n"
     "\tcountry code data from this file. Read from the standard input\n"
     "\twhen switch is not provided"),
    ("Write the binary country code prefix map to this file.\n"
     "\tWrite to the standard output when the switch is not provided"),
    ("Set the type of country code file to create. By default, the\n"
     "\ttype of prefix map is determined by the first IP address seen.\n"),
    "Parse the input but do not write the output file",
    ("Strip invocation history from the prefix map file.\n"
     "\tDef. Record command used to create the file"),
    ("Deprecated.  Replace with --mode=ipv4.\n"
     "\tAssume the input is the CSV GeoIP Legacy country code data for IPv4"),
    ("Deprecated.  Replace with --mode=ipv6.\n"
     "\tAssume the input is the CSV GeoIP Legacy country code data for IPv6"),
    "No longer supported.  The CSV input is required",
    (char *)NULL
};


/* LOCAL FUNCTION PROTOTYPES */

static int appOptionsHandler(clientData cData, int opt_index, char *opt_arg);
static int parseMode(int idx, const char *str, skPrefixMapContent_t *new_mode);
static const char *modeToName(skPrefixMapContent_t m);

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
     "\tCreate a binary prefix map file from a textual input file.  The\n"    \
     "\tinput is read from the named input file or from the standard\n"       \
     "\tinput.  The prefix map file is written to the named location or\n"    \
     "\tto the standard output if stdout is not connected to a terminal.\n")

    FILE *fh = USAGE_FH;
    int i;
    size_t j;

    fprintf(fh, "%s %s", skAppName(), USAGE_MSG);
    fprintf(fh, "\nSWITCHES:\n");
    skOptionsDefaultUsage(fh);
    for (i = 0; appOptions[i].name; ++i) {
        switch (appOptions[i].val) {
          case OPT_MODE:
            fprintf(fh, "--%s %s. %s\tDef. %s. Choices: %s",
                    appOptions[i].name,
                    SK_OPTION_HAS_ARG(appOptions[i]), appHelp[i],
                    modeToName(DEFAULT_MODE), mode_name_id_map[0].name);
            for (j = 1; j < mode_name_id_map_len; ++j) {
                fprintf(fh, ", %s", mode_name_id_map[j].name);
            }
            fprintf(fh, "\n");
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
        if (parseMode(opt_index, modeToName(SKPREFIXMAP_CONT_ADDR_V4), &mode)){
            return 1;
        }
        break;

      case OPT_V6_CSV_INPUT:
        if (parseMode(opt_index, modeToName(SKPREFIXMAP_CONT_ADDR_V6), &mode)){
            return 1;
        }
        break;

      case OPT_ENCODED_INPUT:
        skAppPrintErr("Binary encoded input is no longer supported."
                      " Use the CSV input method instead");
        exit(EXIT_FAILURE);
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
    int                     opt_index,
    const char             *string,
    skPrefixMapContent_t   *new_mode)
{
    static int mode_seen = 0;
    size_t j;

    if (mode_seen) {
        skAppPrintErr("Invalid %s: Attempting to set mode multiple times",
                      appOptions[opt_index].name);
        return -1;
    }
    mode_seen = 1;

    for (j = 0; j < mode_name_id_map_len; ++j) {
        if (0 == strcmp(string, mode_name_id_map[j].name)) {
            *new_mode = mode_name_id_map[j].id;
            return 0;
        }
    }

    skAppPrintErr("Invalid %s '%s': Unrecognized mode name",
                  appOptions[opt_index].name, string);
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
    skPrefixMapContent_t    m)
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
        if (GEOIP2CCMAP_CONT_AUTO == mode) {
            if (skipaddrIsV6(&stmt.range_start)) {
                mode = SKPREFIXMAP_CONT_ADDR_V6;
            } else {
                mode = SKPREFIXMAP_CONT_ADDR_V4;
            }
        }
        rv = skPrefixMapSetContentType(map, mode);
        if (rv) {
            skAppPrintErr("Error setting prefix map content type: %s",
                          skPrefixMapStrerror(rv));
        }
    }

#if SK_ENABLE_IPV6
    if (SKPREFIXMAP_CONT_ADDR_V4 == mode) {
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


int main(
    int         argc,
    char      **argv)
{
    int rv;

    appSetup(argc, argv);       /* never returns on error */

    /* Create the global prefix map */
    if (skPrefixMapCreate(&map) != SKPREFIXMAP_OK) {
        skAppPrintErr("Error creating prefix map");
        exit(EXIT_FAILURE);
    }

    /* set the content type to the current mode, or to IPv4 if the
     * mode is "auto"; this can be changed at a later time */
    skPrefixMapSetContentType(map, ((GEOIP2CCMAP_CONT_AUTO == mode)
                                    ? SKPREFIXMAP_CONT_ADDR_V4
                                    : mode));

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
