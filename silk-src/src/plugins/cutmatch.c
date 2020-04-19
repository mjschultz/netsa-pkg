/*
** Copyright (C) 2008-2020 by Carnegie Mellon University.
**
** @OPENSOURCE_LICENSE_START@
** See license information in ../../LICENSE.txt
** @OPENSOURCE_LICENSE_END@
*/

/*
**  cutmatch.c
**
**    A plug-in to be loaded by rwcut to display the values that
**    rwmatch encodes in the NextHopIP field.
**
*/

#include <silk/silk.h>

RCSIDENT("$SiLK: cutmatch.c ef14e54179be 2020-04-14 21:57:45Z mthomas $");

#include <silk/skplugin.h>
#include <silk/rwrec.h>


/* DEFINES AND TYPEDEFS */

#define FIELD_WIDTH 10

/* Plugin protocol version */
#define PLUGIN_API_VERSION_MAJOR 1
#define PLUGIN_API_VERSION_MINOR 0


/* FUNCTION DECLARATIONS */

static skplugin_err_t
recToText(
    const rwRec            *rwrec,
    char                   *text_value,
    size_t                  text_size,
    void            UNUSED(*idx),
    void           UNUSED(**extra));


/* FUNCTION DEFINITIONS */

/* the registration function called by skplugin.c */
skplugin_err_t
SKPLUGIN_SETUP_FN(
    uint16_t            major_version,
    uint16_t            minor_version,
    void        UNUSED(*pi_data))
{
    skplugin_field_t *field = NULL;
    skplugin_err_t rv;
    skplugin_callbacks_t regdata;

    /* Check API version */
    rv = skpinSimpleCheckVersion(major_version, minor_version,
                                 PLUGIN_API_VERSION_MAJOR,
                                 PLUGIN_API_VERSION_MINOR,
                                 skAppPrintErr);
    if (rv != SKPLUGIN_OK) {
        return rv;
    }

    /* register the field to use for rwcut */
    memset(&regdata, 0, sizeof(regdata));
    regdata.column_width = FIELD_WIDTH;
    regdata.rec_to_text  = recToText;
    rv = skpinRegField(&field, "match", NULL, &regdata, NULL);
    if (SKPLUGIN_OK != rv) {
        return rv;
    }
    if (field) {
        rv = skpinSetFieldTitle(field, "<->Match#");
    }

    return rv;
}


/*
 *  status = recToText(rwrec, text_val, text_len, &index);
 *
 *    Given the SiLK Flow record 'rwrec', fill 'text_val', a buffer of
 *    'text_len' characters, with the appropriate direction and match
 *    number.
 */
static skplugin_err_t
recToText(
    const rwRec            *rwrec,
    char                   *text_value,
    size_t                  text_size,
    void            UNUSED(*idx),
    void           UNUSED(**extra))
{
    static const char *match_dir[] = {"->", "<-"};
    uint32_t nhip;
    uint32_t match_count;

    nhip = rwRecGetNhIPv4(rwrec);
    match_count = 0x00FFFFFF & nhip;
    if (match_count) {
        snprintf(text_value, text_size, ("%s%8" PRIu32),
                 match_dir[(nhip & 0xFF000000) ? 1 : 0],
                 match_count);
    } else {
        snprintf(text_value, text_size, "%-10s",
                 match_dir[(nhip & 0xFF000000) ? 1 : 0]);
    }

    return SKPLUGIN_OK;
}


/*
** Local Variables:
** mode:c
** indent-tabs-mode:nil
** c-basic-offset:4
** End:
*/
