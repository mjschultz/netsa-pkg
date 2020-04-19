/*
** Copyright (C) 2006-2020 by Carnegie Mellon University.
**
** @OPENSOURCE_LICENSE_START@
** See license information in ../../LICENSE.txt
** @OPENSOURCE_LICENSE_END@
*/

#include <silk/silk.h>

RCSIDENT("$SiLK: rwscan_udp.c ef14e54179be 2020-04-14 21:57:45Z mthomas $");

#include "rwscan.h"


void
increment_udp_counters(
    rwRec              *rwrec,
    event_metrics_t    *metrics)
{
    if (rwRecGetPkts(rwrec) < SMALL_PKT_CUTOFF) {
        metrics->flows_small++;
    }

    if ((rwRecGetBytes(rwrec) / rwRecGetPkts(rwrec)) > PACKET_PAYLOAD_CUTOFF) {
        metrics->flows_with_payload++;
    }

}

void
calculate_udp_metrics(
    rwRec              *event_flows,
    event_metrics_t    *metrics)
{
    uint32_t     i;
    uint32_t     class_c_next = 0, class_c_curr = 0;
    uint32_t     dip_next     = 0, dip_curr = 0;
    sk_bitmap_t *low_dp_bitmap;
    uint32_t     low_dp_hit = 0;

    sk_bitmap_t *sp_bitmap;

    uint32_t subnet_run = 1, max_subnet_run = 1;
    rwRec   *rwcurr     = NULL;
    rwRec   *rwnext     = NULL;

    skBitmapCreate(&low_dp_bitmap, 1024);
    skBitmapCreate(&sp_bitmap, UINT16_MAX);
    if (!low_dp_bitmap || !sp_bitmap) {
        skAppPrintOutOfMemory("bitmap");
        skBitmapDestroy(&low_dp_bitmap);
        skBitmapDestroy(&sp_bitmap);
        return;
    }

    calculate_shared_metrics(event_flows, metrics);

    rwcurr = event_flows;
    rwnext = event_flows;

    skBitmapSetBit(low_dp_bitmap, rwRecGetDPort(rwcurr));
    dip_next     = rwRecGetDIPv4(rwnext);
    class_c_next = dip_next & 0xFFFFFF00;

    for (i = 0; i < metrics->event_size; ++i, ++rwcurr) {
        skBitmapSetBit(sp_bitmap, rwRecGetSPort(rwcurr));

        dip_curr     = dip_next;
        class_c_curr = class_c_next;

        if (i + 1 == metrics->event_size) {
            rwnext = NULL;
            dip_next = dip_curr - 1;
            class_c_next = class_c_curr - 0x100;

            if (subnet_run > max_subnet_run) {
                max_subnet_run = subnet_run;
            }
        } else {
            ++rwnext;

            dip_next     = rwRecGetDIPv4(rwnext);
            class_c_next = dip_next & 0xFFFFFF00;

            if (dip_curr == dip_next) {
                skBitmapSetBit(low_dp_bitmap, rwRecGetDPort(rwnext));
            } else if (class_c_curr == class_c_next) {
                if (dip_next - dip_curr == 1) {
                    ++subnet_run;
                } else if (subnet_run > max_subnet_run) {
                    max_subnet_run = subnet_run;
                    subnet_run = 1;
                }
            }
        }

        if (dip_curr != dip_next) {
            uint32_t j;
            uint32_t port_run = 0;

            /* determine longest consecutive run of low ports */
            for (j = 0; j < 1024; j++) {
                if (skBitmapGetBit(low_dp_bitmap, j)) {
                    ++port_run;
                } else if (port_run) {
                    if (port_run > metrics->proto.udp.max_low_port_run_length){
                        metrics->proto.udp.max_low_port_run_length = port_run;
                    }
                    port_run = 0;
                }
            }

            /* determine number of hits on low ports */
            low_dp_hit = skBitmapGetHighCount(low_dp_bitmap);
            if (low_dp_hit > metrics->proto.udp.max_low_dp_hit) {
                metrics->proto.udp.max_low_dp_hit = low_dp_hit;
            }

            /* reset */
            skBitmapClearAllBits(low_dp_bitmap);
            skBitmapSetBit(low_dp_bitmap, rwRecGetDPort(rwcurr));
        }

        if (class_c_curr != class_c_next) {
            if (max_subnet_run > metrics->proto.udp.max_class_c_dip_run_length)
            {
                metrics->proto.udp.max_class_c_dip_run_length = max_subnet_run;
            }
            max_subnet_run = 1;
        }
    }

    metrics->unique_sp_count = skBitmapGetHighCount(sp_bitmap);

    metrics->proto.udp.sp_dip_ratio =
        ((double) metrics->sp_count / metrics->unique_dsts);
    metrics->proto.udp.payload_ratio =
        ((double) metrics->flows_with_payload / metrics->event_size);
    metrics->proto.udp.unique_sp_ratio =
        ((double) metrics->unique_sp_count / metrics->event_size);
    metrics->proto.udp.small_ratio =
        ((double) metrics->flows_small / metrics->event_size);

    print_verbose_results((RWSCAN_VERBOSE_FH,
                           "\tudp (%.3f, %u, %u, %u, %.3f, %.3f, %.3f)",
                           metrics->proto.udp.small_ratio,
                           metrics->proto.udp.max_class_c_dip_run_length,
                           metrics->proto.udp.max_low_dp_hit,
                           metrics->proto.udp.max_low_port_run_length,
                           metrics->proto.udp.sp_dip_ratio,
                           metrics->proto.udp.payload_ratio,
                           metrics->proto.udp.unique_sp_ratio));

    skBitmapDestroy(&low_dp_bitmap);
    skBitmapDestroy(&sp_bitmap);
}

void
calculate_udp_scan_probability(
    event_metrics_t    *metrics)
{
    double y = 0;

    y = UDP_BETA0
        + UDP_BETA4 * metrics->proto.udp.small_ratio
        + UDP_BETA5 * metrics->proto.udp.max_class_c_dip_run_length
        + UDP_BETA8 * metrics->proto.udp.max_low_dp_hit
        + UDP_BETA10 * metrics->proto.udp.max_low_port_run_length
        + UDP_BETA13 * metrics->proto.udp.sp_dip_ratio
        + UDP_BETA15 * metrics->proto.udp.payload_ratio
        + UDP_BETA20 * metrics->proto.udp.unique_sp_ratio;

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
