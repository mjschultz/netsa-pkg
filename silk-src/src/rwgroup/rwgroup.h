/*
** Copyright (C) 2005-2020 by Carnegie Mellon University.
**
** @OPENSOURCE_LICENSE_START@
** See license information in ../../LICENSE.txt
** @OPENSOURCE_LICENSE_END@
*/

/*
**  rwgroup.h
**
**    See rwgroup.c for description.
**
*/
#ifndef _RWGROUP_H
#define _RWGROUP_H
#ifdef __cplusplus
extern "C" {
#endif

#include <silk/silk.h>

RCSIDENTVAR(rcsID_RWGROUP_H, "$SiLK: rwgroup.h ef14e54179be 2020-04-14 21:57:45Z mthomas $");

#include <silk/rwascii.h>
#include <silk/rwrec.h>
#include <silk/skipaddr.h>
#include <silk/skplugin.h>
#include <silk/skstream.h>
#include <silk/utils.h>


/* TYPEDEFS AND DEFINES */


/*
 *    Maximum size of the --rec-treshold
 */
#define MAX_THRESHOLD 65535

/*
 *    Value indicating the delta_field value is unset
 */
#define DELTA_FIELD_UNSET   UINT32_MAX

/*
 *    Maximum number of fields that can come from plugins.  Allow four
 *    per plug-in.
 */
#define MAX_PLUGIN_KEY_FIELDS  32

/*
 *    Maximum bytes allotted to a "node", which is the complete rwRec
 *    and the bytes required by all keys that can come from plug-ins.
 *    Allow 8 bytes per field, plus enough space for an rwRec.
 */
#define MAX_NODE_SIZE  (256 + SK_MAX_RECORD_SIZE)

/* for key fields that come from plug-ins, this struct will hold
 * information about a single field */
typedef struct key_field_st {
    /* The plugin field handle, if kf_fxn is null */
    skplugin_field_t *kf_field_handle;
    /* the byte-offset for this field */
    size_t            kf_offset;
    /* the byte-width of this field */
    size_t            kf_width;
} key_field_t;


/* VARIABLES */

/* number of fields to group by; skStringMapParse() sets this */
extern uint32_t num_fields;

/* IDs of the fields to group by; skStringMapParse() sets it; values
 * are from the rwrec_printable_fields_t enum and from values that
 * come from plugins. */
extern uint32_t *id_fields;

/* the size of a "node".  Because the output from rwgroup is SiLK
 * records, the node size includes the complete rwRec, plus any binary
 * fields that we get from plug-ins to use as the key.  This node_size
 * value may increase when we parse the --fields switch. */
extern uint32_t node_size;

/* the columns that make up the key that come from plug-ins */
extern key_field_t key_fields[MAX_PLUGIN_KEY_FIELDS];

/* the number of these key_fields */
extern size_t key_num_fields;

/* input stream */
extern skstream_t *in_stream;

/* output stream */
extern skstream_t *out_stream;

/* the id of the field to match with fuzzy-ness */
extern uint32_t delta_field;

/* the amount of fuzzy-ness allowed */
extern uint64_t delta_value;

/* for IPv6, use a delta_value that is an skipaddr_t */
extern skipaddr_t delta_value_ip;

/* number of records to that must be in a group before the group is
 * printed. */
extern uint32_t threshold;

/* where to store the records while waiting to meet the threshold */
extern rwRec *thresh_buf;

/* the value to write into the next hop IP field */
extern skipaddr_t group_id;

/* whether the --summarize switch was given */
extern int summarize;

/* whether the --objective switch was given */
extern int objective;


void
appTeardown(
    void);
void
appSetup(
    int                 argc,
    char              **argv);


#ifdef __cplusplus
}
#endif
#endif /* _RWGROUP_H */

/*
** Local Variables:
** mode:c
** indent-tabs-mode:nil
** c-basic-offset:4
** End:
*/
