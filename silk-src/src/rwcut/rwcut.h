/*
** Copyright (C) 2001-2020 by Carnegie Mellon University.
**
** @OPENSOURCE_LICENSE_START@
** See license information in ../../LICENSE.txt
** @OPENSOURCE_LICENSE_END@
*/
#ifndef _RWCUT_H
#define _RWCUT_H
#ifdef __cplusplus
extern "C" {
#endif

#include <silk/silk.h>

RCSIDENTVAR(rcsID_RWCUT_H, "$SiLK: rwcut.h ef14e54179be 2020-04-14 21:57:45Z mthomas $");

/*
**  cut.h
**
**  Header file for the rwcut application.  See rwcut.c for a full
**  explanation.
**
*/

#include <silk/rwascii.h>
#include <silk/rwrec.h>
#include <silk/sksite.h>
#include <silk/skstream.h>
#include <silk/utils.h>


/* TYPEDEFS AND DEFINES */

/* The object to convert the record to text */
extern rwAsciiStream_t *ascii_str;

/* handle input streams */
extern sk_options_ctx_t *optctx;

/* number records to print */
extern uint64_t num_recs;

/* number of records to skip before printing */
extern uint64_t skip_recs;

/* number of records to "tail" */
extern uint64_t tail_recs;

/* buffer used for storing 'tail_recs' records */
extern rwRec *tail_buf;

/* how to handle IPv6 flows */
extern sk_ipv6policy_t ipv6_policy;

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
#endif /* _RWCUT_H */

/*
** Local Variables:
** mode:c
** indent-tabs-mode:nil
** c-basic-offset:4
** End:
*/
