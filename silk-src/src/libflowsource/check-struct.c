/*
** Copyright (C) 2008-2017 by Carnegie Mellon University.
**
** @OPENSOURCE_LICENSE_START@
** See license information in ../../LICENSE.txt
** @OPENSOURCE_LICENSE_END@
*/

/*
**  Verify that the IPFIX data structure looks sound.
**
*/

#define LIBFLOWSOURCE_SOURCE 1
#include <silk/silk.h>

RCSIDENT("$SiLK: check-struct.c efd886457770 2017-06-21 18:43:23Z mthomas $");

#include <silk/skipfix.h>
#include <silk/utils.h>


int main(int UNUSED(argc), char **argv)
{
    SILK_FEATURES_DEFINE_STRUCT(features);
    const char *fname = "/dev/null";
    FILE *f = NULL;
    fBuf_t *fbuf = NULL;
    GError *err = NULL;

    skAppRegister(argv[0]);
    skAppVerifyFeatures(&features, NULL);

    skiCheckDataStructure(stderr);

    /* open an fbuf to /dev/null to ensure all information elements in
     * the struct are available */
    f = fopen(fname, "rb");
    if (NULL == f) {
        skAppPrintSyserror("Unable to open %s for reading", fname);
    } else {
        fbuf = skiCreateReadBufferForFP(f, &err);
        if (NULL == fbuf) {
            skAppPrintErr("Could not open %s for IPFIX: %s",
                          fname, err->message);
            g_clear_error(&err);
        } else {
            fBufFree(fbuf);
        }
        fclose(f);
    }

    skAppUnregister();
    return 0;
}


/*
** Local Variables:
** mode:c
** indent-tabs-mode:nil
** c-basic-offset:4
** End:
*/
