/*
** Copyright (C) 2007-2020 by Carnegie Mellon University.
**
** @OPENSOURCE_LICENSE_START@
** See license information in ../../LICENSE.txt
** @OPENSOURCE_LICENSE_END@
*/

/*
 *
 *  rwsetmember.c
 *
 *    Determine whether the IP wildcard specified on the command line
 *    is a member of the specified IPset(s).
 */

#include <silk/silk.h>

RCSIDENT("$SiLK: rwsetmember.c ef14e54179be 2020-04-14 21:57:45Z mthomas $");

#include <silk/skipset.h>
#include <silk/skstream.h>
#include <silk/utils.h>


/* LOCAL DEFINES AND TYPEDEFS */

/* where to write output from --help */
#define USAGE_FH stdout


/* LOCAL VARIABLES */

/* The address pattern to be matched */
static char *pattern = NULL;

/* If set to true, no output will be produced */
static int quiet = 0;

/* If true, print a count of how many matches */
static int count = 0;

/* index of first option that is not handled by the options handler. */
static int arg_index = 0;


/* OPTIONS SETUP */

typedef enum {
    OPT_COUNT,
    OPT_QUIET
} appOptionsEnum;

static struct option appOptions[] = {
    {"count",       NO_ARG, 0, OPT_COUNT},
    {"quiet",       NO_ARG, 0, OPT_QUIET},
    {0, 0, 0, 0}    /* sentinel entry */
};

static const char *appHelp[] = {
    "Print count of matches along with filenames",
    "No output, only set exit status",
    (char *) NULL
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
#define USAGE_MSG                                                            \
    ("[SWITCHES] WILDCARD_IP INPUT_SET [INPUT_SET...]\n"                     \
     "\tDetermine existence of IP address(es) in one or more IPset files.\n" \
     "\tBy default, print names of INPUT_SETs that contain WILDCARD_IP.\n")

    FILE *fh = USAGE_FH;

    skAppStandardUsage(fh, USAGE_MSG, appOptions, appHelp);
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

    /* verify same number of options and help strings */
    assert((sizeof(appHelp) / sizeof(char *)) ==
           (sizeof(appOptions) / sizeof(struct option)));

    /* register the application */
    skAppRegister(argv[0]);
    skAppVerifyFeatures(&features, NULL);
    skOptionsSetUsageCallback(&appUsageLong);

    /* register the options */
    if (skOptionsRegister(appOptions, & appOptionsHandler, NULL)) {
        skAppPrintErr("Unable to register options");
        exit(EXIT_FAILURE);
    }

    /* register the teardown hanlder */
    if (atexit(appTeardown) < 0) {
        skAppPrintErr("Unable to register appTeardown() with atexit()");
        appTeardown();
        exit(EXIT_FAILURE);
    }

    /* parse options */
    arg_index = skOptionsParse(argc, argv);
    if (arg_index < 0) {
        skAppUsage();                            /* never returns */
    }

    /* get the pattern */
    pattern = argv[arg_index++];
    if (NULL == pattern) {
        skAppPrintErr("No pattern specified");
        skAppUsage();
    }

    /* either need name of set file(s) after options or a set file on stdin */
    if ((arg_index == argc) && (FILEIsATty(stdin))) {
        skAppPrintErr("No files on the command line and"
                      " stdin is connected to a terminal");
        skAppUsage();
    }

    return;                                      /* OK */
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
      case OPT_COUNT:
        count = 1;
        break;

      case OPT_QUIET:
        quiet = 1;
        break;
    }

    return 0;                   /* OK */
}


/*
 *  filename = appNextInput(argc, argv);
 *
 *    Return the name of the next input file from the command line or
 *    the standard input if no files were given on the command line.
 */
static const char *
appNextInput(
    int                 argc,
    char              **argv)
{
    static int initialized = 0;
    const char *fname = NULL;

    if (arg_index < argc) {
        /* get current file and prepare to get next */
        fname = argv[arg_index];
        ++arg_index;
    } else {
        if (initialized) {
            /* no more input */
            return NULL;
        }
        /* input is from stdin */
        fname = "stdin";
    }

    initialized = 1;
    return fname;
}


int main(int argc, char **argv)
{
    char errbuf[2 * PATH_MAX];
    const char *filename = NULL;
    skstream_t *stream = NULL;
    skIPWildcard_t ipwild;
    skipset_t *input_set = NULL;
    skipset_t *wild_set = NULL;
    char buf[64];
    int found_match = 0;        /* application return value */
    int rv;

    appSetup(argc, argv);       /* never returns on error */

    /* Build an IP wildcard from the pattern argument */
    rv = skStringParseIPWildcard(&ipwild, pattern);
    if (rv) {
        skAppPrintErr("Invalid IP '%s': %s",
                      pattern, skStringParseStrerror(rv));
        skAppUsage();
    }

    if (count && !quiet) {
        /* Create an IPset containing the IPwildcard */
        if ((rv = skIPSetCreate(&wild_set, skIPWildcardIsV6(&ipwild)))
            || (rv = skIPSetInsertIPWildcard(wild_set, &ipwild))
            || (rv = skIPSetClean(wild_set)))
        {
            skAppPrintErr("Unable to create temporary IPset: %s",
                          skIPSetStrerror(rv));
            return EXIT_FAILURE;
        }
    }

    /* Iterate over the set files */
    while ((filename = appNextInput(argc, argv)) != NULL) {
        /* Load the input set */
        if ((rv = skStreamCreate(&stream, SK_IO_READ, SK_CONTENT_SILK))
            || (rv = skStreamBind(stream, filename))
            || (rv = skStreamOpen(stream)))
        {
            skStreamLastErrMessage(stream, rv, errbuf, sizeof(errbuf));
            skAppPrintErr("Unable to read IPset from '%s': %s",
                          filename, errbuf);
            skStreamDestroy(&stream);
            continue;
        }
        rv = skIPSetRead(&input_set, stream);
        if (rv) {
            if (SKIPSET_ERR_FILEIO == rv) {
                skStreamLastErrMessage(stream,
                                       skStreamGetLastReturnValue(stream),
                                       errbuf, sizeof(errbuf));
            } else {
                strncpy(errbuf, skIPSetStrerror(rv), sizeof(errbuf));
            }
            skAppPrintErr("Unable to read IPset from '%s': %s",
                          filename, errbuf);
            skStreamDestroy(&stream);
            continue;
        }
        skStreamDestroy(&stream);

        if (quiet || !count) {
            /* Only need to check for a match */
            if (skIPSetCheckIPWildcard(input_set, &ipwild)) {
                found_match = 1;
                if (quiet) {
                    goto done;
                }
                printf("%s\n", filename);
            }
        } else {
            /* Need a count of IPs, so intersect */
            rv = skIPSetIntersect(input_set, wild_set);
            if (rv) {
                skAppPrintErr("Unable to intersect IPsets: %s",
                              skIPSetStrerror(rv));
                skIPSetDestroy(&input_set);
                skIPSetDestroy(&wild_set);
                return EXIT_FAILURE;
            }

            printf("%s:%s\n",
                   filename,
                   skIPSetCountIPsString(input_set,  buf, sizeof(buf)));
            if ('0' != buf[0]) {
                found_match = 1;
            }
        }

        skIPSetDestroy(&input_set);
    }

  done:
    /* done */
    skIPSetDestroy(&input_set);
    skIPSetDestroy(&wild_set);

    return ((found_match) ? 0 : 1);
}


/*
** Local Variables:
** mode:c
** indent-tabs-mode:nil
** c-basic-offset:4
** End:
*/
