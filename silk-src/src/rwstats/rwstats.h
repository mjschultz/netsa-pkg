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

RCSIDENTVAR(rcsID_RWSTATS_H, "$SiLK: rwstats.h 275df62a2e41 2017-01-05 17:30:40Z mthomas $");


/*
**  rwstats.h
**
**    Header file for the rwstats application.  See rwstats.c for a
**    full explanation.
**
*/

#include <silk/hashlib.h>
#include <silk/rwascii.h>
#include <silk/rwrec.h>
#include <silk/skplugin.h>
#include <silk/skipaddr.h>
#include <silk/skstream.h>
#include <silk/utils.h>
#include "skunique.h"


/* TYPEDEFS AND DEFINES */

/* default sTime bin size to use when --bin-time is requested */
#define DEFAULT_TIME_BIN  60

#define HEAP_PTR_KEY(hp)                        \
    ((uint8_t*)(hp) + heap_offset_key)

#define HEAP_PTR_VALUE(hp)                      \
    ((uint8_t*)(hp) + heap_offset_value)

#define HEAP_PTR_DISTINCT(hp)                                   \
    ((uint8_t*)(hp) + heap_offset_distinct)


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
    /* only print sums if the minimum value is at least this value */
    uint64_t            bf_min;
    /* only print sums if the maximum value is no more than this value */
    uint64_t            bf_max;
    /* the text width of the field for columnar output */
    int                 bf_text_len;
    /* the id for this column */
    sk_fieldid_t        bf_id;
    /* whether the field is a distinct value */
    unsigned            bf_is_distinct  :1;
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
    builtin_field_t        *bf_value;
    sk_fieldid_t            fl_id;
    /* index to the limit in builtin_fields */
    uint8_t                 builtin_index;
    /* count, threshold, or percentage */
    rwstats_limit_type_t    type;
    /* did user provide a stopping condition? (1==yes) */
    unsigned                seen    :1;
    /* is this an aggregate value(0) or a distinct(1)? */
    unsigned                distinct:1;
} rwstats_limit_t;

/* flags set by user options */
typedef struct app_flags_st {
    unsigned presorted_input    :1;      /* Assume input is sorted */
    unsigned no_percents        :1;      /* Whether to include the % cols */
    unsigned print_filenames    :1;
    unsigned no_columns         :1;
    unsigned no_titles          :1;
    unsigned no_final_delimiter :1;
    unsigned integer_sensors    :1;
    unsigned integer_tcp_flags  :1;
} app_flags_t;

/* names for the columns */
enum width_type {
    WIDTH_KEY, WIDTH_VAL, WIDTH_INTVL, WIDTH_PCT
};

#define RWSTATS_COLUMN_WIDTH_COUNT 4

/* Option indentifiers.  Keep in sync with appOptions.  Need option
 * identifiers in the header so legacy options can invoke them. */
typedef enum {
    OPT_OVERALL_STATS, OPT_DETAIL_PROTO_STATS,

    OPT_HELP_FIELDS,
    OPT_FIELDS, OPT_VALUES, OPT_PLUGIN,

    /* keep these in same order as stat_stat_type_t */
    OPT_COUNT, OPT_THRESHOLD, OPT_PERCENTAGE,

    OPT_TOP, OPT_BOTTOM,

    OPT_PRESORTED_INPUT,
    OPT_NO_PERCENTS,
    OPT_BIN_TIME,
    OPT_INTEGER_SENSORS,
    OPT_INTEGER_TCP_FLAGS,
    OPT_NO_TITLES,
    OPT_NO_COLUMNS,
    OPT_COLUMN_SEPARATOR,
    OPT_NO_FINAL_DELIMITER,
    OPT_DELIMITED,
    OPT_PRINT_FILENAMES,
    OPT_COPY_INPUT,
    OPT_OUTPUT_PATH,
    OPT_PAGER,
    OPT_LEGACY_HELP
} appOptionsEnum;

/* used to handle legacy switches */
typedef struct rwstats_legacy_st {
    const char *fields;
    const char *values;
} rwstats_legacy_t;


/* VARIABLE DECLARATIONSS */

/* non-zero when --overall-stats or --detail-proto-stats is given */
extern int proto_stats;

extern sk_unique_t *uniq;
extern sk_sort_unique_t *ps_uniq;

extern sk_fieldlist_t *key_fields;
extern sk_fieldlist_t *value_fields;
extern sk_fieldlist_t *distinct_fields;

/* whether this is a top-n or bottom-n */
extern rwstats_direction_t direction;

/* hold the value of the N for top-N,bottom-N */
extern rwstats_limit_t limit;

/* for the key, value, and distinct fields used by the heap, the byte
 * lengths of each and the offsets of each when creating a heap
 * node */
extern size_t heap_octets_key;
extern size_t heap_octets_value;
extern size_t heap_octets_distinct;

extern size_t heap_offset_key;
extern size_t heap_offset_value;
extern size_t heap_offset_distinct;

/* the total byte length of a node in the heap */
extern size_t heap_octets_node;

/* to convert the key fields (as an rwRec) to ascii */
extern rwAsciiStream_t *ascii_str;

/* the output */
extern sk_fileptr_t output;

/* flags set by the user options */
extern app_flags_t app_flags;

/* output column widths.  mapped to width_type */
extern int width[RWSTATS_COLUMN_WIDTH_COUNT];

/* delimiter between output columns */
extern char delimiter;

/* the final delimiter on each line */
extern char final_delim[];

/* number of records read */
extern uint64_t record_count;

/* Summation of whatever value (bytes, packets, flows) we are using.
 * When counting flows, this will be equal to record_count. */
extern uint64_t value_total;

/* how to handle IPv6 flows */
extern sk_ipv6policy_t ipv6_policy;

/* CIDR block mask for sIPs and dIPs.  If 0, use all bits; otherwise,
 * the IP address should be bitwised ANDed with this value. */
extern uint32_t cidr_sip;
extern uint32_t cidr_dip;

extern builtin_field_t builtin_values[];

extern const size_t num_builtin_values;

#define PARSE_KEY_ELAPSED   (1 << 0)
#define PARSE_KEY_STIME     (1 << 1)
#define PARSE_KEY_ETIME     (1 << 2)
#define PARSE_KEY_ALL_TIMES (PARSE_KEY_ELAPSED|PARSE_KEY_STIME|PARSE_KEY_ETIME)

/* which of elapsed, sTime, and eTime will be part of the key. uses
 * the PARSE_KEY_* values above.  See also the local 'time_fields'
 * variable in rwstatssetup.c */
extern unsigned int time_fields_key;

/* whether dPort is part of the key */
extern unsigned int dport_key;


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
readRecord(
    skstream_t         *stream,
    rwRec              *rwrec);
int
appNextInput(
    skstream_t        **stream);
void
setOutputHandle(
    void);
int
appOptionsHandler(
    clientData          cData,
    int                 opt_index,
    char               *opt_arg);


/* rwstatsproto.c: Functions for detailed protocol statistics */

int
protoStatsParse(
    const char         *arg);
int
protoStatsMain(
    void);


/* from rwstatslegacy.c */

int
legacyOptionsSetup(
    clientData          cData);
void
legacyOptionsUsage(
    FILE               *fh);

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
