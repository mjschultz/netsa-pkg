/*
** Copyright (C) 2006-2018 by Carnegie Mellon University.
**
** @OPENSOURCE_LICENSE_START@
** See license information in ../../LICENSE.txt
** @OPENSOURCE_LICENSE_END@
*/
#ifndef _RWSCAN_DB_H
#define _RWSCAN_DB_H
#ifdef __cplusplus
extern "C" {
#endif

#include <silk/silk.h>

RCSIDENTVAR(rcsID_RWSCAN_DB_H, "$SiLK: rwscan_db.h 2e9b8964a7da 2017-12-22 18:13:18Z mthomas $");

#include "rwscan.h"


int
write_scan_header(
    FILE               *out,
    uint8_t             no_columns,
    char                delimiter,
    uint8_t             model_fields);

int
write_scan_record(
    scan_info_t        *rec,
    FILE               *out,
    uint8_t             no_columns,
    char                delimiter,
    uint8_t             model_fields);

int
timestamp_to_datetime(
    char               *buf,
    uint32_t            timestamp);

#ifdef __cplusplus
}
#endif
#endif /* _RWSCAN_DB_H */

/*
** Local Variables:
** mode:c
** indent-tabs-mode:nil
** c-basic-offset:4
** End:
*/
