/*
** Copyright (C) 2006-2020 by Carnegie Mellon University.
**
** @OPENSOURCE_LICENSE_START@
** See license information in ../../LICENSE.txt
** @OPENSOURCE_LICENSE_END@
*/

#include <silk/silk.h>

RCSIDENT("$SiLK: rwscan_db.c ef14e54179be 2020-04-14 21:57:45Z mthomas $");

#include "rwscan_db.h"

#define RWSCAN_TIME_BUFFER_SIZE 32


static field_def_t field_defs[] = {
    {RWSCAN_FIELD_SIP,       "sip",        16},
    {RWSCAN_FIELD_PROTO,     "proto",       6},
    {RWSCAN_FIELD_STIME,     "stime",      24},
    {RWSCAN_FIELD_ETIME,     "etime",      24},
    {RWSCAN_FIELD_FLOWS,     "flows",      10},
    {RWSCAN_FIELD_PKTS,      "packets",    10},
    {RWSCAN_FIELD_BYTES,     "bytes",      10},
    {RWSCAN_FIELD_MODEL,     "scan_model", 12},
    {RWSCAN_FIELD_SCAN_PROB, "scan_prob",  10},
    {(field_id_t)0,          0,             0}
};

int
write_scan_header(
    FILE               *out,
    uint8_t             no_columns,
    char                delimiter,
    uint8_t             model_fields)
{
    unsigned int i;
    int width;

    for (i = 0; field_defs[i].id != 0; ++i) {
        assert(i < RWSCAN_MAX_FIELD_DEFS);
        width = (no_columns) ? 0 : (field_defs[i].width);
        if ((field_defs[i].id == RWSCAN_FIELD_MODEL
             || field_defs[i].id == RWSCAN_FIELD_SCAN_PROB)
            && (!model_fields))
        {
            continue;
        }
        if (i != 0) {
            fprintf(out, "%c", delimiter);
        }
        fprintf(out, "%*s", width, field_defs[i].label);
    }
    if (!options.no_final_delimiter) {
        fprintf(out, "%c", delimiter);
    }
    fprintf(out, "\n");
    return 0;
}

int
write_scan_record(
    scan_info_t        *rec,
    FILE               *out,
    uint8_t             no_columns,
    char                delimiter,
    uint8_t             model_fields)
{
    unsigned int  i;
    int  width;
    char stimestr[RWSCAN_TIME_BUFFER_SIZE];
    char etimestr[RWSCAN_TIME_BUFFER_SIZE];
    char sipstr[SKIPADDR_STRLEN+1];

    timestamp_to_datetime(stimestr, rec->stime);
    timestamp_to_datetime(etimestr, rec->etime);

    for (i = 0; field_defs[i].id != 0; ++i) {
        assert(i < RWSCAN_MAX_FIELD_DEFS);
        if ((field_defs[i].id == RWSCAN_FIELD_MODEL
             || field_defs[i].id == RWSCAN_FIELD_SCAN_PROB)
            && (!model_fields))
        {
            continue;
        }
        if (i != 0) {
            fprintf(out, "%c", delimiter);
        }

        width = (no_columns) ? 0 : (field_defs[i].width);

        switch (field_defs[i].id) {
          case RWSCAN_FIELD_SIP:
            if (options.integer_ips) {
                fprintf(out, "%*u", width, rec->ip);
            } else {
                skipaddr_t ipaddr;
                skipaddrSetV4(&ipaddr, &rec->ip);
                fprintf(out, "%*s", width, skipaddrString(sipstr, &ipaddr, 0));
            }
            break;
          case RWSCAN_FIELD_PROTO:
            fprintf(out, "%*u", width, rec->proto);
            break;
          case RWSCAN_FIELD_STIME:
            fprintf(out, "%*s", width, stimestr);
            break;
          case RWSCAN_FIELD_ETIME:
            fprintf(out, "%*s", width, etimestr);
            break;
          case RWSCAN_FIELD_FLOWS:
            fprintf(out, "%*u", width, rec->flows);
            break;
          case RWSCAN_FIELD_PKTS:
            fprintf(out, "%*u", width, rec->pkts);
            break;
          case RWSCAN_FIELD_BYTES:
            fprintf(out, "%*u", width, rec->bytes);
            break;
          case RWSCAN_FIELD_MODEL:
            if (options.model_fields) {
                fprintf(out, "%*d", width, rec->model);
            }
            break;
          case RWSCAN_FIELD_SCAN_PROB:
            if (options.model_fields) {
                fprintf(out, "%*f", width, rec->scan_prob);
            }
            break;
          default:
            skAbortBadCase(field_defs[i].id); /* NOTREACHED */
        }
    }
    if (!options.no_final_delimiter) {
        fprintf(out, "%c", delimiter);
    }
    fprintf(out, "\n");

    return 0;
}


int
timestamp_to_datetime(
    char               *buf,
    uint32_t            timestamp)
{
    struct tm date_tm;
    time_t    t = (time_t)timestamp;

    gmtime_r(&t, &date_tm);
    snprintf(buf, RWSCAN_TIME_BUFFER_SIZE,
             "%04d-%02d-%02d %02d:%02d:%02d", 1900 + date_tm.tm_year,
             1 + date_tm.tm_mon, date_tm.tm_mday, date_tm.tm_hour,
             date_tm.tm_min, date_tm.tm_sec);
    return 0;
}


/*
** Local Variables:
** mode:c
** indent-tabs-mode:nil
** c-basic-offset:4
** End:
*/
