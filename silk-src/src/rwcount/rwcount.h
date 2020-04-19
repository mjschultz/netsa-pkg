/*
** Copyright (C) 2001-2020 by Carnegie Mellon University.
**
** @OPENSOURCE_LICENSE_START@
** See license information in ../../LICENSE.txt
** @OPENSOURCE_LICENSE_END@
*/
#ifndef _RWCOUNT_H
#define _RWCOUNT_H
#ifdef __cplusplus
extern "C" {
#endif

#include <silk/silk.h>

RCSIDENTVAR(rcsID_RWCOUNT_H, "$SiLK: rwcount.h ef14e54179be 2020-04-14 21:57:45Z mthomas $");

#include <silk/rwrec.h>
#include <silk/sksite.h>
#include <silk/skstream.h>
#include <silk/utils.h>

/*
 *  rwcount.h
 *
 *    Header file for the rwcount utility.
 */


/* DEFINES AND TYPEDEFS */

/* bin loading schemata */
typedef enum {
    LOAD_MEAN=0, LOAD_START, LOAD_END, LOAD_MIDDLE,
    LOAD_DURATION, LOAD_MAXIMUM, LOAD_MINIMUM
} bin_load_scheme_enum_t;

#define MAX_LOAD_SCHEME LOAD_MINIMUM

#define DEFAULT_LOAD_SCHEME LOAD_DURATION


/* default size of bins, in milliseconds */
#define DEFAULT_BINSIZE 30000

/* Values to use for the start_time and end_time to denote that they
 * are not set */
#define RWCO_UNINIT_START 0
#define RWCO_UNINIT_END   INT64_MAX


/* counting data structure */
typedef struct count_bin_st {
    double bytes;
    double pkts;
    double flows;
} count_bin_t;


typedef struct count_data_st {
    /* size of each bin, in milliseconds */
    int64_t     size;
    /* total number of bins that are allocated */
    uint64_t    count;
    /* time on the first bin, in UNIX epoch milliseconds */
    sktime_t    window_min;
    /* one millisecond after the final bin, in UNIX epoch milliseconds */
    sktime_t    window_max;
    /* range of dates for printing of data in UNIX epoch milliseconds */
    sktime_t    start_time;
    sktime_t    end_time;

    /* the data */
    count_bin_t *data;
} count_data_t;


typedef struct count_flags_st {
    /* how to label timestamps */
    uint32_t    timeflags;

    /* bin loading scheme */
    bin_load_scheme_enum_t  load_scheme;

    /* delimiter between columns */
    char        delimiter;

    /* when non-zero, print row label with bin's index value */
    unsigned    label_index         :1;

    /* when non-zero, do not print column titles */
    unsigned    no_titles           :1;

    /* when non-zero, suppress the final delimiter */
    unsigned    no_final_delimiter  :1;

    /* when non-zero, do not print bins with zero counts */
    unsigned    skip_zeroes         :1;

    /* when non-zero, do not print column titles */
    unsigned    no_columns          :1;
} count_flags_t;


/* FUNCTIONS */

void
appSetup(
    int                 argc,
    char              **argv);
void
appTeardown(
    void);
FILE *
getOutputHandle(
    void);


/* VARIABLES */

extern sk_options_ctx_t *optctx;

/* the data */
extern count_data_t bins;

/* flags */
extern count_flags_t flags;

#ifdef __cplusplus
}
#endif
#endif /* _RWCOUNT_H */

/*
** Local Variables:
** mode:c
** indent-tabs-mode:nil
** c-basic-offset:4
** End:
*/
