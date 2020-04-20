/*
** Copyright (C) 2006-2020 by Carnegie Mellon University.
**
** @OPENSOURCE_LICENSE_START@
** See license information in ../../LICENSE.txt
** @OPENSOURCE_LICENSE_END@
*/

/*
**  Simple tester for the skpolldir library
**
*/

#include <silk/silk.h>

RCSIDENT("$SiLK: skpolldir-test.c ef14e54179be 2020-04-14 21:57:45Z mthomas $");

#include <silk/skpolldir.h>
#include <silk/sklog.h>
#include <silk/utils.h>


static skPollDir_t *pd = NULL;


/*
 *  appHandleSignal(signal_value)
 *
 *    Stop polling the directory
 */
static void
appHandleSignal(
    int          UNUSED(sig))
{
    if (pd) {
        skPollDirStop(pd);
    }
}


/*
 *    Prefix any error messages from skpolldir with the program name
 *    and an abbreviated time instead of the standard logging tag.
 */
static size_t
logprefix(
    char               *buffer,
    size_t              bufsize)
{
    time_t t;
    struct tm ts;

    t = time(NULL);
    localtime_r(&t, &ts);

    return (size_t)snprintf(buffer, bufsize, "%s: %2d:%02d:%02d: ",
                            skAppName(), ts.tm_hour, ts.tm_min, ts.tm_sec);
}


int main(int argc, char **argv)
{
    SILK_FEATURES_DEFINE_STRUCT(features);
    const char *dirname;
    uint32_t interval = 5;
    char path[PATH_MAX];
    char *file;
    int logmask;

    /* register the application */
    skAppRegister(argv[0]);
    skAppVerifyFeatures(&features, NULL);

    /* make certain there are enough args.  If first arg begins with
     * hyphen, print usage. */
    if (argc < 2 || argc > 3 || argv[1][0] == '-') {
        fprintf(stderr, "Usage: %s <dirname> [<poll-interval>]\n",
                skAppName());
        return EXIT_FAILURE;
    }

    /* get directory to poll */
    dirname = argv[1];
    if (!skDirExists(dirname)) {
        skAppPrintErr("Polling dir '%s' does not exist", dirname);
        return EXIT_FAILURE;
    }

    /* get interval if given */
    if (argc == 3) {
        int rv = skStringParseUint32(&interval, argv[2], 1, 0);
        if (rv != 0) {
            skAppPrintErr("Invalid interval '%s': %s",
                          argv[2], skStringParseStrerror(rv));
            return EXIT_FAILURE;
        }
    }

    /* set signal handler to clean up temp files on SIGINT, SIGTERM, etc */
    if (skAppSetSignalHandler(&appHandleSignal)) {
        exit(EXIT_FAILURE);
    }

    /* Must enable the logger */
    sklogSetup(0);
    sklogSetDestination("stderr");
    sklogSetStampFunction(&logprefix);
    /* set level to "warning" to avoid the "Started logging" message */
    logmask = sklogGetMask();
    sklogSetLevel("warning");
    sklogOpen();
    sklogSetMask(logmask);

    pd = skPollDirCreate(dirname, interval);
    if (pd == NULL) {
        skAppPrintErr("Failed to set up polling for directory %s", dirname);
        return EXIT_FAILURE;
    }

    printf("%s: Polling '%s' every %" PRIu32 " seconds\n",
           skAppName(), dirname, interval);
    while (PDERR_NONE == skPollDirGetNextFile(pd, path, &file)) {
        printf("%s\n", file);
    }

    skPollDirDestroy(pd);
    pd = NULL;

    /* set level to "warning" to avoid the "Stopped logging" message */
    sklogSetLevel("warning");
    sklogTeardown();
    skAppUnregister();

    return EXIT_SUCCESS;
}


/*
** Local Variables:
** mode:c
** indent-tabs-mode:nil
** c-basic-offset:4
** End:
*/
