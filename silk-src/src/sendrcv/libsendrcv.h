/*
** Copyright (C) 2012-2020 by Carnegie Mellon University.
**
** @OPENSOURCE_LICENSE_START@
** See license information in ../../LICENSE.txt
** @OPENSOURCE_LICENSE_END@
*/
#ifndef _LIBSENDRCV_H
#define _LIBSENDRCV_H
#ifdef __cplusplus
extern "C" {
#endif

#include <silk/silk.h>

RCSIDENTVAR(rcsID_LIBSENDRCV_H, "$SiLK: libsendrcv.h ef14e54179be 2020-04-14 21:57:45Z mthomas $");

/*
**  libsendrcv.h
**
**  Maintenance macros for the libsendrcv convenience library
*/

/*
 *  ENABLING DEBUGGING
 *
 *    Setting the SENDRCV_DEBUG macro (below) to an bitmask of the
 *    following flags turns on specific types of debug output for
 *    rwsender/rwreceiver.  To see the messages in the running
 *    application, you MUST run with --log-level=debug.
 *
 *    The bit flags are:
 *
 *    0x0001 DEBUG_SKMSG_OTHER -- Logs messages for channel
 *    creation/destruction, network connection issues, and thread
 *    start/stop in skmsg.c.
 *
 *    0x0002 DEBUG_RWTRANSFER_PROTOCOL -- Logs messages regarding the
 *    higher-level protocol between rwsender and rwreceiver and thread
 *    start/stop in rwtransfer.c, rwsender.c, rwreceiver.c.
 *
 *    0x0004 DEBUG_RWTRANSFER_CONTENT -- Logs messages consisting of
 *    offset and length information for each message sent between the
 *    sender and receiver that is part of the actual file being
 *    transferred.  Generally used to debug file corruption.
 *
 *    0x0008 DEBUG_RWRECEIVER_DISKFREE -- Logs a message reporting
 *    disk usage each time rwreceiver asks about the amount of free
 *    space available on the disk.
 *
 *    0x0010 DEBUG_SKMSG_POLL_TIMEOUT -- Logs a message once a second
 *    when the call to poll() times out---this includes reading on the
 *    internal channel, which mostly waits for events.
 *
 *    The following are mostly used to debug lock-ups:
 *
 *    0x0100 DEBUG_SKMSG_MUTEX -- Logs a message for each mutex
 *    lock/unlock in skmsg.c
 *
 *    0x0200 DEBUG_RWTRANSFER_MUTEX -- Logs a message for each mutex
 *    lock/unlock in rwtransfer.c, rwsender.c, rwreceiver.c
 *
 *    0x0400 DEBUG_MULTIQUEUE_MUTEX -- Logs a message for each mutex
 *    lock/unlock in multiqueue.c
 *
 *    0x0800 DEBUG_INTDICT_MUTEX -- Logs a message for each mutex
 *    lock/unlock in intdict.c
 *
 *    0x1000 DEBUG_SKMSG_FN -- Logs a message for each function entry
 *    and function return in skmsg.c
 *
 *
 */

#define DEBUG_SKMSG_OTHER           0x0001
#define DEBUG_RWTRANSFER_PROTOCOL   0x0002
#define DEBUG_RWTRANSFER_CONTENT    0x0004
#define DEBUG_RWRECEIVER_DISKFREE   0x0008

#define DEBUG_SKMSG_POLL_TIMEOUT    0x0010

#define DEBUG_SKMSG_MUTEX           0x0100
#define DEBUG_RWTRANSFER_MUTEX      0x0200
#define DEBUG_MULTIQUEUE_MUTEX      0x0400
#define DEBUG_INTDICT_MUTEX         0x0800

#define DEBUG_SKMSG_FN              0x1000


/* #define SENDRCV_DEBUG 0 */
#ifndef SENDRCV_DEBUG
#  if defined(GLOBAL_TRACE_LEVEL) && GLOBAL_TRACE_LEVEL == 9
#    define SENDRCV_DEBUG 0xffff
#  else
#    define SENDRCV_DEBUG 0
#  endif
#endif


#ifdef __cplusplus
}
#endif
#endif /* _LIBSENDRCV_H */

/*
** Local Variables:
** mode:c
** indent-tabs-mode:nil
** c-basic-offset:4
** End:
*/
