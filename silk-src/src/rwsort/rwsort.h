/*
** Copyright (C) 2001-2020 by Carnegie Mellon University.
**
** @OPENSOURCE_LICENSE_START@
** See license information in ../../LICENSE.txt
** @OPENSOURCE_LICENSE_END@
*/

/*
**  rwsort reads SiLK Flow Records from the standard input or from
**  named files and sorts them on one or more user-specified fields.
**
**  See rwsort.c for implementation details.
**
*/
#ifndef _RWSORT_H
#define _RWSORT_H
#ifdef __cplusplus
extern "C" {
#endif

#include <silk/silk.h>

RCSIDENTVAR(rcsID_RWSORT_H, "$SiLK: rwsort.h ef14e54179be 2020-04-14 21:57:45Z mthomas $");

#include <silk/rwascii.h>
#include <silk/rwrec.h>
#include <silk/skplugin.h>
#include <silk/skipaddr.h>
#include <silk/skstream.h>
#include <silk/sktempfile.h>
#include <silk/utils.h>

/* use TRACEMSG_LEVEL as our tracing variable */
#define TRACEMSG(msg) TRACEMSG_TO_TRACEMSGLVL(1, msg)
#include <silk/sktracemsg.h>


/* LOCAL DEFINES AND TYPEDEFS */

/*
 *    The default buffer size to use, unless the user selects a
 *    different value with the --sort-buffer-size switch.
 *
 *    Support of a buffer of almost 2GB.
 */
#define DEFAULT_SORT_BUFFER_SIZE    "1920m"

/*
 *    We do not allocate the buffer at once, but use realloc() to grow
 *    the buffer linearly to the maximum size.  The following is the
 *    number of steps to take to reach the maximum size.  The number
 *    of realloc() calls will be once less than this value.
 *
 *    If the initial allocation fails, the number of chunks is
 *    incremented---making the size of the initial malloc()
 *    smaller---and allocation is attempted again.
 */
#define NUM_CHUNKS  6

/*
 *    Do not allocate more than this number of bytes at a time.
 *
 *    If dividing the buffer size by NUM_CHUNKS gives a chunk size
 *    larger than this; determine the number of chunks by dividing the
 *    buffer size by this value.
 *
 *    Use a value of 1g
 */
#define MAX_CHUNK_SIZE      ((size_t)(0x40000000))

/*
 *    If we cannot allocate a buffer that will hold at least this many
 *    records, give up.
 */
#define MIN_IN_CORE_RECORDS     1000

/*
 *    Maximum number of files to attempt to merge-sort at once.
 */
#define MAX_MERGE_FILES         1024

/*
 *    Maximum number of fields that can come from plugins.  Allow four
 *    per plug-in.
 */
#define MAX_PLUGIN_KEY_FIELDS   32

/*
 *    Maximum bytes allotted to a "node", which is the complete rwRec
 *    and the bytes required by all keys that can come from plug-ins.
 *    Allow 8 bytes per field, plus enough space for an rwRec.
 */
#define MAX_NODE_SIZE ((size_t)(8 * MAX_PLUGIN_KEY_FIELDS + SK_MAX_RECORD_SIZE))

/*
 *    The maximum buffer size is the maximum size we can allocate.
 */
#define MAXIMUM_SORT_BUFFER_SIZE    ((size_t)(SIZE_MAX))

/*
 *    The minium buffer size.
 */
#define MINIMUM_SORT_BUFFER_SIZE ((size_t)(MAX_NODE_SIZE * MIN_IN_CORE_RECORDS))


/* for key fields that come from plug-ins, this struct will hold
 * information about a single field */
typedef struct key_field_st {
    /* The plugin field handle */
    skplugin_field_t *kf_field_handle;
    /* the byte-offset for this field */
    size_t            kf_offset;
    /* the byte-width of this field */
    size_t            kf_width;
} key_field_t;



/* VARIABLES */

/* number of fields to sort over; skStringMapParse() sets this */
extern uint32_t num_fields;

/* IDs of the fields to sort over; skStringMapParse() sets it; values
 * are from the rwrec_printable_fields_t enum and from values that
 * come from plug-ins. */
extern uint32_t *sort_fields;

/* the size of a "node".  Because the output from rwsort are SiLK
 * records, the node size includes the complete rwRec, plus any binary
 * fields that we get from plug-ins to use as the key.  This node_size
 * value may increase when we parse the --fields switch. */
extern size_t node_size;

/* the columns that make up the key that come from plug-ins */
extern key_field_t key_fields[MAX_PLUGIN_KEY_FIELDS];

/* the number of these key_fields */
extern size_t key_num_fields;

/* output stream */
extern skstream_t *out_stream;

/* temp file context */
extern sk_tempfilectx_t *tmpctx;

/* whether the user wants to reverse the sort order */
extern int reverse;

/* whether to treat the input files as already sorted */
extern int presorted_input;

/* maximum amount of RAM to attempt to allocate */
extern size_t sort_buffer_size;

/* FUNCTIONS */

void
appExit(
    int                 status)
    NORETURN;
void
appSetup(
    int                 argc,
    char              **argv);
int
appNextInput(
    skstream_t        **stream);

#ifdef __cplusplus
}
#endif
#endif /* _RWSORT_H */

/*
** Local Variables:
** mode:c
** indent-tabs-mode:nil
** c-basic-offset:4
** End:
*/
