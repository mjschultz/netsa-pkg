/*
** Copyright (C) 2001-2020 by Carnegie Mellon University.
**
** @OPENSOURCE_LICENSE_START@
** See license information in ../../LICENSE.txt
** @OPENSOURCE_LICENSE_END@
*/

/*
**  rwdedupe.h
**
**    Common declarations needed by rwdedupe.  See rwdedupe.c for
**    implementation details.
*/
#ifndef _RWDEDUPE_H
#define _RWDEDUPE_H
#ifdef __cplusplus
extern "C" {
#endif

#include <silk/silk.h>

RCSIDENTVAR(rcsID_RWDEDUPE_H, "$SiLK: rwdedupe.h ef14e54179be 2020-04-14 21:57:45Z mthomas $");

#include <silk/rwascii.h>
#include <silk/rwrec.h>
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
 *    different value with the --buffer-size switch.
 *
 *    Support of a buffer of almost 2GB.
 */
#define DEFAULT_BUFFER_SIZE     "1920m"

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
 *    Size of a node is constant: the size of a complete rwRec.
 */
#define NODE_SIZE               sizeof(rwRec)

/*
 *    The maximum buffer size is the maximum size we can allocate.
 */
#define MAXIMUM_BUFFER_SIZE     ((size_t)(SIZE_MAX))

/*
 *    The minium buffer size.
 */
#define MINIMUM_BUFFER_SIZE     (NODE_SIZE * MIN_IN_CORE_RECORDS)


/*
 *    Number of delta fields
 */
#define RWDEDUP_DELTA_FIELD_COUNT  4

typedef struct flow_delta_st {
    int64_t     d_stime;
    uint32_t    d_elapsed;
    uint32_t    d_packets;
    uint32_t    d_bytes;
} flow_delta_t;



/* VARIABLES */

/* number of fields to sort over */
extern uint32_t num_fields;

/* IDs of the fields to sort over; values are from the
 * rwrec_printable_fields_t enum. */
extern uint32_t sort_fields[RWREC_PRINTABLE_FIELD_COUNT];

/* output stream */
extern skstream_t *out_stream;

/* temp file context */
extern sk_tempfilectx_t *tmpctx;

/* maximum amount of RAM to attempt to allocate */
extern size_t buffer_size;

/* differences to allow between flows */
extern flow_delta_t delta;


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
#endif /* _RWDEDUPE_H */

/*
** Local Variables:
** mode:c
** indent-tabs-mode:nil
** c-basic-offset:4
** End:
*/
