/*
** Copyright (C) 2004-2020 by Carnegie Mellon University.
**
** @OPENSOURCE_LICENSE_START@
** See license information in ../../LICENSE.txt
** @OPENSOURCE_LICENSE_END@
*/
#ifndef _UDPSOURCE_H
#define _UDPSOURCE_H
#ifdef __cplusplus
extern "C" {
#endif

#include <silk/silk.h>

RCSIDENTVAR(rcsID_UDPSOURCE_H, "$SiLK: udpsource.h ef14e54179be 2020-04-14 21:57:45Z mthomas $");

#include <silk/utils.h>

struct skUDPSource_st;
typedef struct skUDPSource_st skUDPSource_t;


/**
 *    Signature of callback function.
 *
 *    The UDP source calls this function for each packet it collects
 *    from network or reads from a file.  The function is called with
 *    the length of the packet, the packet's data, and a
 *    'callback_data' object supplied by the caller.
 *
 *    If the function returns non-zero, the packet is rejected.  If
 *    the function returns 0, the packet is stored until request by
 *    the caller via the skUDPSourceNext() function.
 */
typedef int (*udp_source_reject_fn)(
    ssize_t         recv_data_len,
    void           *recv_data,
    void           *callback_data);


/**
 *    Creates and returns a UDP source representing the connectivity
 *    information in 'probe' and 'params'.
 *
 *    'itemsize' is the maximum size of an individual packet.
 *
 *    'reject_pkt_fn' is a function that will be called for every
 *    packet the UDP source receives, and 'fn_callback_data' is a
 *    parameter passed to that function.  If the 'reject_pkt_fn'
 *    returns a true value, the packet will be ignored.
 *
 *    Returns the UDP source on success, or NULL on failure.
 */
skUDPSource_t *
skUDPSourceCreate(
    const skpc_probe_t         *probe,
    const skFlowSourceParams_t *params,
    uint32_t                    itemsize,
    udp_source_reject_fn        reject_pkt_fn,
    void                       *fn_callback_data);


/**
 *    Tell the UDP Source to stop processing data.
 */
void
skUDPSourceStop(
    skUDPSource_t      *source);


/**
 *    Free all memory associated with the UDP Source.  Does nothing if
 *    'source' is NULL.
 */
void
skUDPSourceDestroy(
    skUDPSource_t      *source);


/**
 *    Get the next piece of data collected/read by the UDP Source.
 */
uint8_t *
skUDPSourceNext(
    skUDPSource_t      *source);

#ifdef __cplusplus
}
#endif
#endif /* _UDPSOURCE_H */

/*
** Local Variables:
** mode:c
** indent-tabs-mode:nil
** c-basic-offset:4
** End:
*/
