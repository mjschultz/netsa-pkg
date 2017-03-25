/*
** Copyright (C) 2001-2017 by Carnegie Mellon University.
**
** @OPENSOURCE_LICENSE_START@
** See license information in ../../LICENSE.txt
** @OPENSOURCE_LICENSE_END@
*/
#ifndef _INTERVAL_H
#define _INTERVAL_H
#ifdef __cplusplus
extern "C" {
#endif

#include <silk/silk.h>

RCSIDENTVAR(rcsID_INTERVAL_H, "$SiLK: interval.h 275df62a2e41 2017-01-05 17:30:40Z mthomas $");


int
intervalInit(
    void);
void
intervalShutdown(
    void);
double *
intervalQuartiles(
    const uint32_t     *data,
    const uint32_t     *intervals,
    uint32_t            numIntervals);
double *
intervalMoments(
    const uint32_t     *data,
    const uint32_t     *intervals,
    uint32_t            numIntervals);


#define NUM_INTERVALS   10

/*
** intervals are defined for each protocol separately. Till we decide
** we want to change it, treat icmp like udp
*/
extern uint32_t tcpByteIntervals[NUM_INTERVALS];
extern uint32_t udpByteIntervals[NUM_INTERVALS];
extern uint32_t tcpPktIntervals[NUM_INTERVALS];
extern uint32_t udpPktIntervals[NUM_INTERVALS];
extern uint32_t tcpBppIntervals[NUM_INTERVALS];
extern uint32_t udpBppIntervals[NUM_INTERVALS];

#ifdef __cplusplus
}
#endif
#endif /* _INTERVAL_H */

/*
** Local Variables:
** mode:c
** indent-tabs-mode:nil
** c-basic-offset:4
** End:
*/
