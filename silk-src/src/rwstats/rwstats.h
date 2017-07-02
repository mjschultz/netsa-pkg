/*
** Copyright (C) 2001-2017 by Carnegie Mellon University.
**
** @OPENSOURCE_LICENSE_START@
** See license information in ../../LICENSE.txt
** @OPENSOURCE_LICENSE_END@
*/
#ifndef _RWSTATS_H
#define _RWSTATS_H
#ifdef __cplusplus
extern "C" {
#endif

#include <silk/silk.h>

RCSIDENTVAR(rcsID_RWSTATS_H, "$SiLK: rwstats.h efd886457770 2017-06-21 18:43:23Z mthomas $");


/*
**  rwstats.h
**
**    Header file for the rwstats application.  See rwstats.c for a
**    full explanation.
**
*/

#include <silk/hashlib.h>
#include <silk/rwrec.h>
#include <silk/skflowiter.h>
#include <silk/skformat.h>
#include <silk/skipaddr.h>
#include <silk/skplugin.h>
#include <silk/skstream.h>
#include <silk/utils.h>
#include "skunique.h"


/* TYPEDEFS AND DEFINES */

/* whether the program is rwstats or rwuniq */
typedef enum {
    STATSUNIQ_PROGRAM_STATS = 1,
    STATSUNIQ_PROGRAM_UNIQ  = 2,
    STATSUNIQ_PROGRAM_BOTH  = 3
} statsuniq_program_t;

/* symbol names for whether this is a top-N or bottom-N */
typedef enum {
    RWSTATS_DIR_TOP, RWSTATS_DIR_BOTTOM
} rwstats_direction_t;

/* what type of cutoff to use; keep these in same order as appOptionsEnum */
typedef enum {
    /* specify the N for a Top-N or Bottom-N */
    RWSTATS_COUNT = 0,
    /* output bins whose value is at-least/no-more-than this value */
    RWSTATS_THRESHOLD = 1,
    /* output bins whose value relative to the total across all bins
     * is at-least/no-more-than this percentage */
    RWSTATS_PERCENTAGE = 2,
    /* there is no limit; print all */
    RWSTATS_ALL = 3
} rwstats_limit_type_t;

/* number of limit types; used for sizing arrays */
#define NUM_RWSTATS_LIMIT_TYPE      4

/* struct to hold information about built-in aggregate value fields */
typedef struct builtin_field_st {
    /* the title of this field */
    const char         *bf_title;
    /* in rwuniq, do not print this row if the value of this field is
     * less than this value */
    uint64_t            bf_min;
    /* in rwuniq, do not print this row if the value of this field is
     * greater than this value */
    uint64_t            bf_max;
    /* the text width of the field for columnar output */
    int                 bf_text_len;
    /* the id for this column */
    sk_fieldid_t        bf_id;
    /* in which application(s) this field is enabled */
    statsuniq_program_t bf_app;
    /* whether the field is a distinct value */
    unsigned            bf_is_distinct  :1;
    /* in rwuniq, whether this column is used for --all-counts, 1==yes */
    unsigned            bf_all_counts   :1;
    /* in rwuniq, whether the user gave this command line switch */
    unsigned            bf_switched_on  :1;
    /* description of this field */
    const char         *bf_description;
} builtin_field_t;

/* used to convert a percentage or threshold limit to a number of bins */
typedef struct rwstats_limit_st {
    char                    title[256];
    /* values that correspond to rwstats_limit_type_t.  the double
     * value is used for RWSTATS_PERCENTAGE; the uint64_t otherwise */
    union value_un {
        double      d;
        uint64_t    u64;
    }                       value[NUM_RWSTATS_LIMIT_TYPE];
    /* number of entries in the hash table */
    uint64_t                entries;
    /* handles to the field to limit */
    sk_fieldentry_t        *fl_entry;
    skplugin_field_t       *pi_field;
    sk_fieldid_t            fl_id;
    /* index to the limit in builtin_fields */
    uint8_t                 builtin_index;
    /* count, threshold, or percentage */
    rwstats_limit_type_t    type;
    /* whether this is a top-n or bottom-n */
    rwstats_direction_t     direction;
    /* did user provide a stopping condition? (1==yes) */
    unsigned                seen    :1;
    /* is this an aggregate value(0) or a distinct(1)? */
    unsigned                distinct:1;
} rwstats_limit_t;

/* flags set by user options */
typedef struct app_flags_st {
    unsigned presorted_input    :1;      /* Assume input is sorted */
    unsigned no_percents        :1;      /* Whether to include the % cols */
    unsigned no_columns         :1;
    unsigned no_titles          :1;
    unsigned no_final_delimiter :1;
    unsigned integer_sensors    :1;
    unsigned integer_tcp_flags  :1;
    unsigned check_limits       :1;
    unsigned sort_output        :1;
} app_flags_t;

/* sidecar_field_t is a struct for maintaining information about
 * fields that come from sidecar data */
struct sidecar_field_st {
    char               *scf_name;
    sk_sidecar_type_t   scf_type;
    uint8_t             scf_binoct;
};
typedef struct sidecar_field_st sidecar_field_t;


/* VARIABLE DECLARATIONSS */

extern const statsuniq_program_t this_program;

/* non-zero when --overall-stats or --detail-proto-stats is given */
extern int proto_stats;

extern sk_unique_t *uniq;
extern sk_sort_unique_t *ps_uniq;

extern sk_fieldlist_t *key_fields;
extern sk_fieldlist_t *value_fields;
extern sk_fieldlist_t *distinct_fields;

/* hold the value of the N for top-N,bottom-N */
extern rwstats_limit_t limit;

/* the input */
extern sk_flow_iter_t *flowiter;

/* output formattter */
extern sk_formatter_t *fmtr;

/* the output */
extern sk_fileptr_t output;

/* flags set by the user options */
extern app_flags_t app_flags;

/* delimiter between output columns */
extern char delimiter;

/* number of records read */
extern uint64_t record_count;

/* Summation of whatever value (bytes, packets, flows) we are using.
 * When counting flows, this will be equal to record_count. */
extern uint64_t value_total;

/* the Lua state */
extern lua_State *L;


/* FUNCTION DECLARATIONS */

/* rwstatssetup.c */

void
appSetup(
    int                 argc,
    char              **argv);
void
appTeardown(
    void);
void
appExit(
    int                 status)
    NORETURN;
int
readAllRecords(
    void);
void
setOutputHandle(
    void);
void
writeAsciiRecord(
    uint8_t           **outbuf);

/* rwstatsproto.c: Functions for detailed protocol statistics */

int
protoStatsParse(
    const char         *arg);
int
protoStatsMain(
    void);


#ifdef __cplusplus
}
#endif
#endif /* _RWSTATS_H */

/*
** Local Variables:
** mode:c
** indent-tabs-mode:nil
** c-basic-offset:4
** End:
*/
