/*
** Copyright (C) 2004-2017 by Carnegie Mellon University.
**
** @OPENSOURCE_LICENSE_START@
** See license information in ../../LICENSE.txt
** @OPENSOURCE_LICENSE_END@
*/
#ifndef _RWFILTER_H
#define _RWFILTER_H
#ifdef __cplusplus
extern "C" {
#endif

#include <silk/silk.h>

RCSIDENTVAR(rcsID_RWFILTER_H, "$SiLK: rwfilter.h efd886457770 2017-06-21 18:43:23Z mthomas $");

/*
**  rwfilter.h
**
**  Privater header information for the rwfilter application
**
*/

#include <silk/rwrec.h>
#include <silk/skfglob.h>
#include <silk/skflowiter.h>
#include <silk/skipaddr.h>
#include <silk/skplugin.h>
#include <silk/sksite.h>
#include <silk/skstream.h>
#include <silk/utils.h>

/* TYPEDEFS AND DEFINES */

/* where to write --help output */
#define USAGE_FH stdout

/* where to send --dry-run output */
#define DRY_RUN_FH stdout

/* where to send file names when --print-filenames is active */
#define PRINT_FILENAMES_FH stderr

/* whether rwfilter supports threads */
#define SK_RWFILTER_THREADED 1


/* environment variable that determines number of threads */
#define RWFILTER_THREADS_ENVAR  "SILK_RWFILTER_THREADS"

/* default number of threads to use */
#define RWFILTER_THREADS_DEFAULT 1

/*
 *    Size of buffer, in bytes, for storing records prior to writing
 *    them.  There will be one of these buffers per destination type
 *    per thread.
 */
#define THREAD_RECBUF_SIZE   0x10000

/*
 *    Maximum number of records the recbuf can hold
 */
#define RECBUF_MAX_RECS ((uint32_t)(THREAD_RECBUF_SIZE / sizeof(rwRec)))

/*
 *    Maximum number of dynamic libraries that we support
 */
#define APP_MAX_DYNLIBS 8

/* maximum number of filter checks */
#define MAX_CHECKERS (APP_MAX_DYNLIBS + 8)



/*
 *  The number and types of skstream_t output streams: pass, fail, all
 */
#define DESTINATION_TYPES 3
enum {
    DEST_PASS = 0, DEST_FAIL, DEST_ALL
};


typedef struct destination_st destination_t;

struct destination_st {
    skstream_t     *stream;
    destination_t  *next;
};

typedef struct dest_type_st {
    uint64_t        max_records;
    destination_t  *dest_list;
    int             count;
} dest_type_t;

/* for counting the flows, packets, and bytes */
typedef struct rec_count_st {
    uint64_t  flows;
    uint64_t  pkts;
    uint64_t  bytes;
} rec_count_t;

/* holds filter-statistics data */
typedef struct filter_stats_st {
    rec_count_t     read;           /* count of records read */
    rec_count_t     pass;           /* count of records that passed */
    uint32_t        files;          /* count of files */
} filter_stats_t;

/* holds records for a single destination */
typedef struct recbuf_st {
    rwRec              *buf;
    uint32_t            count;
    uint32_t            max_count;
} recbuf_t;

/* holds state for a single thread */
typedef struct filter_thread_st {
    recbuf_t        recbuf[DESTINATION_TYPES];
    filter_stats_t  stats;
    pthread_t       thread;
    rwRec           rwrec;
    lua_State      *lua_state;
    int             rv;
} filter_thread_t;

/* output of checker functions */
typedef enum {
    /* filter fails the record */
    RWF_FAIL,
    /* filter passes the record */
    RWF_PASS,
    /* filter passes the record; run no more filters */
    RWF_PASS_NOW,
    /* this record neither passes or fails; run no more filters */
    RWF_IGNORE
} checktype_t;


/*
 *  INCR_REC_COUNT(count, rec)
 *
 *    Increment the values in the filter_stats_t_t 'count' by the
 *    values in the rwRec* 'rec'.
 *
 */
#define INCR_REC_COUNT(count, rec)                 \
    {                                              \
        (count).flows++;                           \
        (count).pkts  += rwRecGetPkts(rec);        \
        (count).bytes += rwRecGetBytes(rec);       \
    }





/* EXTERNAL VARIABLES (rwfilter.c) */

/* information about the destination types (ALL, PASS, FAIL); includes
 * a linked list of destination streams */
extern dest_type_t dest_type[DESTINATION_TYPES];

/* support for --print-statistics and --print-volume-statistics; NULL
 * when the switch has not been provided */
extern skstream_t *print_stat;

/* where to print output during a --dry-run; NULL when the switch has
 * not been provided */
extern FILE *dryrun_fp;

/* handle command line switches for input files, xargs, fglob */
extern sk_options_ctx_t *optctx;

/* handle for looping over the input records or files */
extern sk_flow_iter_t *flowiter;

/* true as long as we are reading records */
extern int reading_records;

/* whether to print volume statistics */
extern int print_volume_stats;

/* number of total threads */
extern uint32_t thread_count;

/* number of checks to preform */
extern int checker_count;

/* function pointers to handle checking and or processing */
extern checktype_t (*checker[MAX_CHECKERS])(const rwRec*);

/* The Lua state */
extern lua_State *L;


/* FUNCTION DECLARATIONS */

/* application setup functions (rwfiltersetup.c) */

void
appSetup(
    int                 argc,
    char              **argv);
void
appTeardown(
    void);
void
filterIgnoreSigPipe(
    void);
int
filterOpenInputData(
    skstream_t        **stream,
    skcontent_t         content_type,
    const char         *filename);
lua_State *
filterLuaCreateState(
    void);


/* application functions (rwfilter.c) */

int
closeAllDests(
    void);
int
closeOutputDests(
    const int           dest_id,
    int                 quietly);
int
closeOneOutput(
    const int           dest_id,
    destination_t      *dest);
int
filterFile(
    skstream_t         *in_stream,
    const char         *ipfile_basename,
    filter_thread_t    *thread);


/* filtering  functions (rwfiltercheck.c) */

int
filterCheckFile(
    skstream_t         *path,
    const char         *ip_dir);
checktype_t
filterCheck(
    const rwRec        *rwrec);
void
filterUsage(
    FILE*);
int
filterGetCheckCount(
    void);
int
filterSetup(
    void);
void
filterTeardown(
    void);


/* filtering functions (rwfiltertuple.c) */

int
tupleSetup(
    void);
void
tupleTeardown(
    void);
void
tupleUsage(
    FILE               *fh);
checktype_t
tupleCheck(
    const rwRec        *rwrec);
int
tupleGetCheckCount(
    void);



/* "main" for filtering when threaded (rwfilterthread.c) */

int
threadedFilter(
    filter_stats_t     *stats);
int
writeBufferThreaded(
    filter_thread_t    *thread,
    int                 dest_id);





#ifdef __cplusplus
}
#endif
#endif /* _RWFILTER_H */

/*
** Local Variables:
** mode:c
** indent-tabs-mode:nil
** c-basic-offset:4
** End:
*/
