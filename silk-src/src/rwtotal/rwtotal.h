/*
** Copyright (C) 2001-2020 by Carnegie Mellon University.
**
** @OPENSOURCE_LICENSE_START@
** See license information in ../../LICENSE.txt
** @OPENSOURCE_LICENSE_END@
*/
#ifndef _RWTOTAL_H
#define _RWTOTAL_H
#ifdef __cplusplus
extern "C" {
#endif

#include <silk/silk.h>

RCSIDENTVAR(rcsID_RWTOTAL_H, "$SiLK: rwtotal.h ef14e54179be 2020-04-14 21:57:45Z mthomas $");

#include <silk/rwrec.h>
#include <silk/sksite.h>
#include <silk/skstream.h>
#include <silk/utils.h>


/*
 * rwtotal.h
 *
 * Various handy header information for rwtotal.
 */

/* number of things to compute (used to compute size of count_array[]) */
#define NUM_TOTALS 3

/* offsets into the count_array[] array */
#define C_RECS  0
#define C_BYTES 1
#define C_PKTS  2



/* define the options; also these determine how to compute the key for
 * each bin */
typedef enum {
    OPT_SIP_FIRST_8=0, OPT_SIP_FIRST_16,  OPT_SIP_FIRST_24,
    OPT_SIP_LAST_8,    OPT_SIP_LAST_16,
    OPT_DIP_FIRST_8,   OPT_DIP_FIRST_16,  OPT_DIP_FIRST_24,
    OPT_DIP_LAST_8,    OPT_DIP_LAST_16,
    OPT_SPORT,         OPT_DPORT,
    OPT_PROTO,
    OPT_PACKETS,
    OPT_BYTES,
    OPT_DURATION,
    OPT_ICMP_CODE,

    /* above map to count-modes; below control output */

    OPT_SUMMATION,
    OPT_MIN_BYTES, OPT_MIN_PACKETS, OPT_MIN_RECORDS,
    OPT_MAX_BYTES, OPT_MAX_PACKETS, OPT_MAX_RECORDS,
    OPT_SKIP_ZEROES,
    OPT_NO_TITLES,
    OPT_NO_COLUMNS,
    OPT_COLUMN_SEPARATOR,
    OPT_NO_FINAL_DELIMITER,
    OPT_DELIMITED,
    OPT_OUTPUT_PATH,
    OPT_PAGER
} appOptionsEnum;


#define COUNT_MODE_UNSET     -1

/* which of the above is the maximum possible count_mode */
#define COUNT_MODE_MAX_OPTION OPT_ICMP_CODE

/* which of the above is final value to handle IP addresses.  used for
 * ignoring IPv6 addresses */
#define COUNT_MODE_FINAL_ADDR  OPT_DIP_LAST_16

/* which count mode to use */
extern int count_mode;

extern sk_options_ctx_t *optctx;

extern int  summation;
extern int  no_titles;
extern int  no_columns;
extern int  no_final_delimiter;
extern char delimiter;

/* array that holds the counts */
extern uint64_t *count_array;

extern uint64_t bounds[2 * NUM_TOTALS];

void
appTeardown(
    void);
void
appSetup(
    int                 argc,
    char              **argv);
FILE *
getOutputHandle(
    void);


#ifdef __cplusplus
}
#endif
#endif /* _RWTOTAL_H */

/*
** Local Variables:
** mode:c
** indent-tabs-mode:nil
** c-basic-offset:4
** End:
*/
