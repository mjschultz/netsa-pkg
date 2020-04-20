/*
** Copyright (C) 2007-2020 by Carnegie Mellon University.
**
** @OPENSOURCE_LICENSE_START@
** See license information in ../../LICENSE.txt
** @OPENSOURCE_LICENSE_END@
*/

#include <silk/silk.h>

RCSIDENT("$SiLK: rwipautils.c ef14e54179be 2020-04-14 21:57:45Z mthomas $");

#include "rwipa.h"


#define IPA_CONFIG_FILE "silk-ipa.conf"
#define IPA_CONFIG_LINE_LENGTH 1024


char *
get_ipa_config(
    void)
{
    skstream_t *conf_stream = NULL;
    char filename[PATH_MAX];
    char line[IPA_CONFIG_LINE_LENGTH];
    char *ipa_url = NULL;
    int rv;

    /* Read in the data file */
    if (NULL == skFindFile(IPA_CONFIG_FILE, filename, sizeof(filename), 1)) {
        skAppPrintErr("Could not locate config file '%s'.",
                      IPA_CONFIG_FILE);
        return NULL;
    }

    /* open input */
    if ((rv = skStreamCreate(&conf_stream, SK_IO_READ, SK_CONTENT_TEXT))
        || (rv = skStreamBind(conf_stream, filename))
        || (rv = skStreamSetCommentStart(conf_stream, "#"))
        || (rv = skStreamOpen(conf_stream)))
    {
        skStreamPrintLastErr(conf_stream, rv, &skAppPrintErr);
        skStreamDestroy(&conf_stream);
        exit(EXIT_FAILURE);
    }
    while (skStreamGetLine(conf_stream, line, sizeof(line), NULL)
           == SKSTREAM_OK)
    {
        /* FIXME: smarter config file reading, please */
        if (strlen(line) > 0) {
            ipa_url = strdup(line);
            break;
        }
    }

    skStreamDestroy(&conf_stream);
    /* Should be free()d by the caller */
    return ipa_url;
}


/*
** Local Variables:
** mode:c
** indent-tabs-mode:nil
** c-basic-offset:4
** End:
*/
