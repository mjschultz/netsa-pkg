/*
** Copyright (C) 2010-2016 by Carnegie Mellon University.
**
** @OPENSOURCE_LICENSE_START@
** See license information in ../../LICENSE.txt
** @OPENSOURCE_LICENSE_END@
*/

/*
**  rwreadonly.c
**
**    Read SiLK Flow records from files listed on the command line.
**    Use a file name of "-" to read records from the standard input.
**
**    This is a test program to that can be used for library timings.
**
*/

#include <silk/silk.h>

RCSIDENT("$SiLK: rwreadonly.c 85572f89ddf9 2016-05-05 20:07:39Z mthomas $");

#include <silk/rwrec.h>
#include <silk/skstream.h>
#include <silk/utils.h>

#define PLURAL(s) (((s) == 1) ? "" : "s")


int main(int argc, char **argv)
{
    SILK_FEATURES_DEFINE_STRUCT(features);
    int64_t ticks = 0;
    rwRec rwrec;
    uint64_t rec_count = 0;
    skstream_t *rwio;
    int exit_val = 0;
    int rv;
    unsigned int i;

    skAppRegister(argv[0]);
    skAppVerifyFeatures(&features, NULL);

    for (i = 1; i < (unsigned int)argc; ++i) {
        rwio = NULL;
        if ((rv = skStreamCreate(&rwio, SK_IO_READ, SK_CONTENT_SILK_FLOW))
            || (rv = skStreamBind(rwio, argv[i]))
            || (rv = skStreamOpen(rwio))
            || (rv = skStreamReadSilkHeader(rwio, NULL)))
        {
            skStreamPrintLastErr(rwio, rv, &skAppPrintErr);
            skStreamDestroy(&rwio);
            exit_val = 1;
            break;
        }
        ticks -= clock();
        while ((rv = skStreamReadRecord(rwio, &rwrec)) == SKSTREAM_OK) {
            ++rec_count;
        }
        ticks += clock();
        if (rv != SKSTREAM_ERR_EOF) {
            skStreamPrintLastErr(rwio, rv, &skAppPrintErr);
            skStreamDestroy(&rwio);
            exit_val = 1;
            break;
        }
        skStreamDestroy(&rwio);
    }

    fprintf(stderr, ("%s: Read %" PRIu64 " record%s from %d file%s"
                     " in %.4f seconds\n"),
            skAppName(), rec_count, PLURAL(rec_count),
            (i - 1), PLURAL(i - 1), (double)ticks / CLOCKS_PER_SEC);

    skAppUnregister();
    return exit_val;
}


/*
** Local Variables:
** mode:c
** indent-tabs-mode:nil
** c-basic-offset:4
** End:
*/
