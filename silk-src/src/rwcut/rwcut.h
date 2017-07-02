/*
** Copyright (C) 2001-2017 by Carnegie Mellon University.
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

RCSIDENTVAR(rcsID_RWCUT_H, "$SiLK: rwcut.h efd886457770 2017-06-21 18:43:23Z mthomas $");

/*
**  cut.h
**
**  Header file for the rwcut application.  See rwcut.c for a full
**  explanation.
**
*/

#include <silk/rwrec.h>
#include <silk/skflowiter.h>
#include <silk/skformat.h>
#include <silk/sksite.h>
#include <silk/skstream.h>
#include <silk/utils.h>


/* TYPEDEFS AND DEFINES */

/* The object to convert the record to text */
extern sk_formatter_t *fmtr;

/* handle input streams */
extern sk_options_ctx_t *optctx;
extern sk_flow_iter_t *flowiter;

/* number records to print */
extern uint64_t num_recs;

/* number of records to skip before printing */
extern uint64_t skip_recs;

/* number of records to "tail" */
extern uint64_t tail_recs;

/* buffer used for storing 'tail_recs' records */
extern rwRec *tail_buf;

/* The output stream: where to print the records */
extern sk_fileptr_t output;

extern lua_State *L;


void
appTeardown(
    void);
void
appSetup(
    int                 argc,
    char              **argv);
void
printTitle(
    void);
void
addPluginFields(
    rwRec              *rwrec);


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
