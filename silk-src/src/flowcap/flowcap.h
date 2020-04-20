/*
** Copyright (C) 2004-2020 by Carnegie Mellon University.
**
** @OPENSOURCE_LICENSE_START@
** See license information in ../../LICENSE.txt
** @OPENSOURCE_LICENSE_END@
*/
#ifndef _FLOWCAP_H
#define _FLOWCAP_H
#ifdef __cplusplus
extern "C" {
#endif

#include <silk/silk.h>

RCSIDENTVAR(rcsID_FLOWCAP_H, "$SiLK: flowcap.h ef14e54179be 2020-04-14 21:57:45Z mthomas $");

/*
**  flowcap.h
**
**  Common information between flowcap objects
**
**/

#include <silk/probeconf.h>
#include <silk/skdaemon.h>
#include <silk/sklog.h>
#include <silk/skstream.h>
#include <silk/skvector.h>
#include <silk/utils.h>


/* Max timestamp length (YYYYMMDDhhmmss) */
#define FC_TIMESTAMP_MAX 15
/* Maximum sensor size (including either trailing zero or preceeding hyphen) */
#define FC_SENSOR_MAX (SK_MAX_STRLEN_SENSOR + 1)
/* Maximum probe size (including either trailing zero or preceeding hyphen) */
#define FC_PROBE_MAX (SK_MAX_STRLEN_SENSOR + 1)
/* Size of uniquness extension */
#define FC_UNIQUE_MAX 7
/* Previous two, plus hyphen */
#define FC_NAME_MAX                                     \
    (FC_TIMESTAMP_MAX + FC_SENSOR_MAX +                 \
     FC_PROBE_MAX + FC_UNIQUE_MAX)


/* Minimum flowcap version */
/* We no longer support flowcap version 1 */
#define FC_VERSION_MIN 2

/* Maximum flowcap version */
#define FC_VERSION_MAX 5

/* Default version of flowcap to produce */
#define FC_VERSION_DEFAULT 5

/* minimum number of bytes to leave free on the data disk.  File
 * distribution will stop when the freespace on the disk reaches or
 * falls below this mark.  This value is parsed by
 * skStringParseHumanUint64(). */
#define DEFAULT_FREESPACE_MINIMUM   "1g"

/* maximum percentage of disk space to take */
#define DEFAULT_SPACE_MAXIMUM_PERCENT  ((double)98.00)


/* Where to write files */
extern const char *destination_dir;

/* Compression method for output files */
extern sk_compmethod_t comp_method;

/* The version of flowcap to produce */
extern uint8_t flowcap_version;

/* To ensure records are sent along in a timely manner, the files are
 * closed when a timer fires or once they get to a certain size.
 * These variables define those values. */
extern uint32_t write_timeout;
extern uint32_t max_file_size;

/* Timer base (0 if none) from which we calculate timeouts */
extern sktime_t clock_time;

/* Amount of disk space to allow for a new file when determining
 * whether there is disk space available. */
extern uint64_t alloc_file_size;

/* Probes the user wants to flowcap process */
extern sk_vector_t *probe_vec;

#ifdef SK_HAVE_STATVFS
/* leave at least this much free space on the disk; specified by
 * --freespace-minimum */
extern int64_t freespace_minimum;

/* take no more that this amount of the disk; as a percentage.
 * specified by --space-maximum-percent */
extern double space_maximum_percent;
#endif /* SK_HAVE_STATVFS */


void
appSetup(
    int                 argc,
    char              **argv);
void
appTeardown(
    void);

int
createReaders(
    void);

#ifdef __cplusplus
}
#endif
#endif /* _FLOWCAP_H */

/*
** Local Variables:
** mode:c
** indent-tabs-mode:nil
** c-basic-offset:4
** End:
*/
