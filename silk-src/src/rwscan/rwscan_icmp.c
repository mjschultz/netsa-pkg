/*
** Copyright (C) 2006-2020 by Carnegie Mellon University.
**
** @OPENSOURCE_LICENSE_START@
** See license information in ../../LICENSE.txt
** @OPENSOURCE_LICENSE_END@
*/

#include <silk/silk.h>

RCSIDENT("$SiLK: rwscan_icmp.c ef14e54179be 2020-04-14 21:57:45Z mthomas $");

#include "rwscan.h"


void
increment_icmp_counters(
    rwRec              *rwrec,
    event_metrics_t    *metrics)
{
    uint8_t type = 0, code = 0;

    type = rwRecGetIcmpType(rwrec);
    code = rwRecGetIcmpCode(rwrec);

    if ((type == 8 || type == 13 || type == 15 || type == 17)
        && (code == 0))
    {
        metrics->flows_icmp_echo++;
    }
}


void
calculate_icmp_metrics(
    rwRec              *event_flows,
    event_metrics_t    *metrics)
{
    uint32_t i;
    uint32_t class_c_next = 0, class_c_curr = 0;
    uint32_t dip_next     = 0, dip_curr = 0;

    uint8_t  run               = 1, max_run_curr = 1;
    uint32_t class_c_run       = 1, max_class_c_run = 1;
    uint8_t  class_c_dip_count = 1, max_class_c_dip_count = 1;

    rwRec *rwcurr = NULL;
    rwRec *rwnext = NULL;

    calculate_shared_metrics(event_flows, metrics);

    for (i = 0; i < metrics->event_size; i++) {
        rwcurr = &(event_flows[i]);
        rwnext =
            (i + 1 < (metrics->event_size)) ? &(event_flows[i + 1]) : NULL;

        dip_curr     = rwRecGetDIPv4(rwcurr);
        class_c_curr = dip_curr & 0xFFFFFF00;

        if (rwnext != NULL) {
            dip_next     = rwRecGetDIPv4(rwnext);
            class_c_next = dip_next & 0xFFFFFF00;
        }

        if ((rwnext != NULL) && (class_c_curr == class_c_next)) {
            if (dip_curr != dip_next) {
                class_c_dip_count++;
                if (dip_next - dip_curr == 1) {
                    run++;
                } else {
                    if (run > max_run_curr) {
                        max_run_curr = run;
                    }
                    run = 1;
                }
            }
        } else {
            if (((class_c_next - class_c_curr) >> 8) == 1) {
                class_c_run++;
            } else {
                if (class_c_run > max_class_c_run) {
                    max_class_c_run = class_c_run;
                }
                class_c_run = 1;
            }

            if (max_run_curr >
                metrics->proto.icmp.max_class_c_dip_run_length)
            {
                metrics->proto.icmp.max_class_c_dip_run_length = max_run_curr;
            }

            if (class_c_dip_count > max_class_c_dip_count) {
                max_class_c_dip_count = class_c_dip_count;
            }
            class_c_dip_count = 1;
        }
    }

    metrics->proto.icmp.max_class_c_subnet_run_length = max_class_c_run;

    metrics->proto.icmp.echo_ratio =
        ((double) metrics->flows_icmp_echo / metrics->event_size);
    metrics->proto.icmp.max_class_c_dip_count = max_class_c_dip_count;
    metrics->proto.icmp.total_dip_count       = metrics->unique_dsts;

    print_verbose_results((RWSCAN_VERBOSE_FH, "\ticmp (%u, %u, %u, %u, %.3f)",
                           metrics->proto.icmp.max_class_c_subnet_run_length,
                           metrics->proto.icmp.max_class_c_dip_run_length,
                           metrics->proto.icmp.max_class_c_dip_count,
                           metrics->proto.icmp.total_dip_count,
                           metrics->proto.icmp.echo_ratio));
}


void
calculate_icmp_scan_probability(
    event_metrics_t    *metrics)
{
    double y = 0;

    y = ICMP_BETA0
        + ICMP_BETA1 * metrics->proto.icmp.max_class_c_subnet_run_length
        + ICMP_BETA5 * metrics->proto.icmp.max_class_c_dip_run_length
        + ICMP_BETA6 * metrics->proto.icmp.max_class_c_dip_count
        + ICMP_BETA11 * metrics->proto.icmp.total_dip_count
        + ICMP_BETA22 * metrics->proto.icmp.echo_ratio;

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
