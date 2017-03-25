/*
** Copyright (C) 2001-2017 by Carnegie Mellon University.
**
** @OPENSOURCE_LICENSE_START@
** See license information in ../../LICENSE.txt
** @OPENSOURCE_LICENSE_END@
*/
#ifndef _RWUNIQ_H
#define _RWUNIQ_H
#ifdef __cplusplus
extern "C" {
#endif

/*
**  rwuniq.h
**
**  Common declarations for the rwuniq application.  See rwuniq.c for
**  an explanation.
*/


#include <silk/silk.h>

RCSIDENTVAR(rcsID_RWUNIQ_H, "$SiLK: rwuniq.h 275df62a2e41 2017-01-05 17:30:40Z mthomas $");

#include <silk/hashlib.h>
#include <silk/rwascii.h>
#include <silk/rwrec.h>
#include <silk/skipaddr.h>
#include <silk/skstream.h>
#include <silk/utils.h>
#include "skunique.h"


/* TYPEDEFS AND DEFINES */

/* default sTime bin size to use when --bin-time is requested */
#define DEFAULT_TIME_BIN  60


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
    /* whether this column is used for --all-counts, 1==yes */
    unsigned            bf_all_counts   :1;
    /* whether the user gave this command line switch */
    unsigned            bf_switched_on  :1;
    /* description of this field */
    const char         *bf_description;
} builtin_field_t;

/* flags set by user options */
typedef struct app_flags_st {
    unsigned presorted_input    :1;      /* Assume input is sorted */
    unsigned sort_output        :1;      /* Whether to sort the output */
    unsigned print_filenames    :1;
    unsigned no_columns         :1;
    unsigned no_titles          :1;
    unsigned no_final_delimiter :1;
    unsigned integer_sensors    :1;
    unsigned integer_tcp_flags  :1;
    unsigned check_limits       :1;      /* Whether output must meet limits */
} app_flags_t;

/* structure to get the distinct count when using IPv6 */
typedef union ipv6_distinct_un {
    uint64_t count;
    uint8_t  ip[16];
} ipv6_distinct_t;


/* VARIABLE DECLARATIONS */

extern sk_unique_t *uniq;
extern sk_sort_unique_t *ps_uniq;

extern sk_fieldlist_t *key_fields;
extern sk_fieldlist_t *value_fields;
extern sk_fieldlist_t *distinct_fields;

/* to convert the key fields (as an rwRec) to ascii */
extern rwAsciiStream_t *ascii_str;

/* flags set by the user options */
extern app_flags_t app_flags;

/* how to handle IPv6 flows */
extern sk_ipv6policy_t ipv6_policy;

extern builtin_field_t builtin_values[];

extern const size_t num_builtin_values;

#define PARSE_KEY_ELAPSED   (1 << 0)
#define PARSE_KEY_STIME     (1 << 1)
#define PARSE_KEY_ETIME     (1 << 2)
#define PARSE_KEY_ALL_TIMES (PARSE_KEY_ELAPSED|PARSE_KEY_STIME|PARSE_KEY_ETIME)


/* which of elapsed, sTime, and eTime will be part of the key. uses
 * the PARSE_KEY_* values above.  See also the local 'time_fields'
 * variable in rwuniqsetup.c */
extern unsigned int time_fields_key;

/* whether dPort is part of the key */
extern unsigned int dport_key;

/* FUNCTION DECLARATIONS */

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
appNextInput(
    skstream_t        **stream);
int
readRecord(
    skstream_t         *stream,
    rwRec              *rwrec);
void
setOutputHandle(
    void);

#ifdef __cplusplus
}
#endif
#endif /* _RWUNIQ_H */

/*
** Local Variables:
** mode:c
** indent-tabs-mode:nil
** c-basic-offset:4
** End:
*/
