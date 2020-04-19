/*
** Copyright (C) 2006-2020 by Carnegie Mellon University.
**
** @OPENSOURCE_LICENSE_START@
** See license information in ../../LICENSE.txt
** @OPENSOURCE_LICENSE_END@
*/

#include <silk/silk.h>

RCSIDENT("$SiLK: rwscan_tcp.c ef14e54179be 2020-04-14 21:57:45Z mthomas $");

#include "rwscan.h"


void
add_count(
    uint32_t           *counts,
    uint32_t            value,
    uint32_t            max)
{
    if (value >= max - 1) {
        counts[max - 1]++;
    } else {
        counts[value]++;
    }
}

void
increment_tcp_counters(
    rwRec              *rwrec,
    event_metrics_t    *metrics)
{
    if (!(rwRecGetFlags(rwrec) & ACK_FLAG)) {
        metrics->flows_noack++;
    }

    if (rwRecGetPkts(rwrec) < SMALL_PKT_CUTOFF) {
        metrics->flows_small++;
    }

    if ((rwRecGetBytes(rwrec) / rwRecGetPkts(rwrec)) > PACKET_PAYLOAD_CUTOFF) {
        metrics->flows_with_payload++;
    }

    if (rwRecGetFlags(rwrec) == RST_FLAG
        || rwRecGetFlags(rwrec) == (SYN_FLAG | ACK_FLAG)
        || rwRecGetFlags(rwrec) == (RST_FLAG | ACK_FLAG))
    {
        metrics->flows_backscatter++;
    }
    add_count(metrics->tcp_flag_counts,
              rwRecGetFlags(rwrec),
              RWSCAN_MAX_FLAGS);

}

void
calculate_tcp_metrics(
    rwRec              *event_flows,
    event_metrics_t    *metrics)
{
    calculate_shared_metrics(event_flows, metrics);

    metrics->proto.tcp.noack_ratio =
        ((double) metrics->flows_noack / metrics->event_size);
    metrics->proto.tcp.small_ratio =
        ((double) metrics->flows_small / metrics->event_size);
    metrics->proto.tcp.sp_dip_ratio =
        ((double) metrics->sp_count / metrics->unique_dips);
    metrics->proto.tcp.payload_ratio =
        ((double) metrics->flows_with_payload / metrics->event_size);
    metrics->proto.tcp.unique_dip_ratio =
        ((double) metrics->unique_dips / metrics->event_size);
    metrics->proto.tcp.backscatter_ratio =
        ((double) metrics->flows_backscatter / metrics->event_size);

    print_verbose_results((RWSCAN_VERBOSE_FH,
                           "\ttcp (%.3f, %.3f, %.3f, %.3f, %.3f, %.3f)",
                           metrics->proto.tcp.noack_ratio,
                           metrics->proto.tcp.small_ratio,
                           metrics->proto.tcp.sp_dip_ratio,
                           metrics->proto.tcp.payload_ratio,
                           metrics->proto.tcp.unique_dip_ratio,
                           metrics->proto.tcp.backscatter_ratio));
}

void
calculate_tcp_scan_probability(
    event_metrics_t    *metrics)
{
    double y = 0;

    y = TCP_BETA0
        + TCP_BETA2 * metrics->proto.tcp.noack_ratio
        + TCP_BETA4 * metrics->proto.tcp.small_ratio
        + TCP_BETA13 * metrics->proto.tcp.sp_dip_ratio
        + TCP_BETA15 * metrics->proto.tcp.payload_ratio
        + TCP_BETA19 * metrics->proto.tcp.unique_dip_ratio
        + TCP_BETA21 * metrics->proto.tcp.backscatter_ratio;
    metrics->scan_probability = exp(y) / (1.0 + exp(y));

    if (metrics->scan_probability > 0.5) {
        metrics->event_class = EVENT_SCAN;
    }

}


/*
** Local Variables:
** mode:c
** indent-tabs-mode:nil
** c-basic-offset:4
** End:
*/
