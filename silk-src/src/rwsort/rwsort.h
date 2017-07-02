/*
** Copyright (C) 2001-2017 by Carnegie Mellon University.
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

RCSIDENTVAR(rcsID_RWSORT_H, "$SiLK: rwsort.h efd886457770 2017-06-21 18:43:23Z mthomas $");

#include <silk/rwrec.h>
#include <silk/skflowiter.h>
#include <silk/skipaddr.h>
#include <silk/skplugin.h>
#include <silk/skstream.h>
#include <silk/sktempfile.h>
#include <silk/utils.h>

/* use TRACEMSG_LEVEL as our tracing variable */
#define TRACEMSG(msg) TRACEMSG_TO_TRACEMSGLVL(1, msg)
#include <silk/sktracemsg.h>


/* LOCAL DEFINES AND TYPEDEFS */

/*
 *    The approximate maximum amount of memory we attempt to use for
 *    storing records by default.  The user may select a different
 *    value with the --sort-buffer-size switch.
 *
 *    Support of a buffer of almost 2GB.
 */
#define DEFAULT_SORT_BUFFER_SIZE    "1920m"

/*
 *    We do not allocate the pointer buffer at once, but use realloc()
 *    to grow the buffer linearly to the maximum size.  The following
 *    is the number of steps to take to reach the maximum size.  The
 *    number of realloc() calls will be once less than this value.
 *
 *    If the initial allocation fails, the number of chunks is
 *    incremented---making the size of the initial malloc()
 *    smaller---and alloation is attempted again.
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
 *    If we can't allocate a buffer that will hold at least this many
 *    records, give up.
 */
#define MIN_IN_CORE_RECORDS     1000

/*
 *    Maximum number of files to attempt to merge-sort at once.
 */
#define MAX_MERGE_FILES         1024

/*
 *    The size of a node, which is the complete rwRec.
 */
#define NODE_SIZE       sizeof(rwRec)

/*
 *    The maximum buffer size is the maximum size we can allocate.
 */
#define MAXIMUM_SORT_BUFFER_SIZE    ((size_t)(SIZE_MAX))

/*
 *    The minium buffer size.
 */
#define MINIMUM_SORT_BUFFER_SIZE    ((size_t)(NODE_SIZE * MIN_IN_CORE_RECORDS))

/*
 *    When this bit is set in a sk_stringmap_entry_t, the entry comes
 *    from a plug-in.
 */
#define PLUGIN_FIELD_BIT    0x80000000

/*
 *    When this bit is set in a sk_stringmap_entry_t, the entry comes
 *    from a sidecar.
 */
#define SIDECAR_FIELD_BIT   0x40000000


/* for key fields that come from plug-ins, this struct will hold
 * information about a single field */
typedef struct key_field_st {
    /* The plugin field handle */
    skplugin_field_t   *kf_pi_handle;
    /* the name of this field in the rwRec's sidecar */
    char               *kf_name;
    /* the byte-width of this field */
    size_t              kf_width;
    /* The id of this field */
    uint32_t            kf_id;
    /* the type of this field */
    sk_sidecar_type_t   kf_type;
} key_field_t;



/* VARIABLES */

/* number of fields to sort over; skStringMapParse() sets this */
extern uint32_t num_fields;

/* the fields that make up the key */
extern key_field_t *key_fields;

/* for looping over the input streams */
extern sk_flow_iter_t *flowiter;

/* output stream */
extern skstream_t *out_stream;

/* sidecar to write to the output file */
extern sk_sidecar_t *out_sidecar;

/* temp file context */
extern sk_tempfilectx_t *tmpctx;

/* whether the user wants to reverse the sort order */
extern int reverse;

/* whether to treat the input files as already sorted */
extern int presorted_input;

/* maximum amount of RAM to attempt to allocate */
extern size_t sort_buffer_size;

extern lua_State *L;


void
appExit(
    int                 status)
    NORETURN;
void
appSetup(
    int                 argc,
    char              **argv);
void
sort_stream_record_init(
    rwRec              *rwrec,
    void               *v_lua_state);
void
addPluginFields(
    rwRec              *out_rwrec);
int
fillRecordAndKey(
    skstream_t         *stream,
    rwRec              *rwrec);

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
