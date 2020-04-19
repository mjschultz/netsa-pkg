/*
** Copyright (C) 2004-2020 by Carnegie Mellon University.
**
** @OPENSOURCE_LICENSE_START@
** See license information in ../../LICENSE.txt
** @OPENSOURCE_LICENSE_END@
*/
#ifndef _CIRCBUF_H
#define _CIRCBUF_H
#ifdef __cplusplus
extern "C" {
#endif

#include <silk/silk.h>

RCSIDENTVAR(rcsID_CIRCBUF_H, "$SiLK: circbuf.h ef14e54179be 2020-04-14 21:57:45Z mthomas $");

/*
**  circbuf.h
**
**    Circular buffer API
**
**    A circular buffer is a thread-safe FIFO with a maximum memory
**    size.
*/

/*
 *    The type for the circular buffer.
 */
struct sk_circbuf_st;
typedef struct sk_circbuf_st sk_circbuf_t;


/*
 *    The normal maximum size (in bytes) of a single chunk in a
 *    circular buffer.  (Circular buffers are allocated in chunks, as
 *    neeeded.)  A single chunk will will always be at least 3 times
 *    the item_size, regardless of the value of
 *    SK_CIRCBUF_CHUNK_MAX_SIZE.
 */
#define SK_CIRCBUF_CHUNK_MAX_SIZE 0x20000   /* 128k */


/*
 *    Status codes returned by the sk_circbuf_t functions.
 */
enum sk_circbuf_status_en {
    /*  Success */
    SK_CIRCBUF_OK = 0,
    /*  Memory allocation error */
    SK_CIRCBUF_E_ALLOC,
    /*  Bad parameter to function */
    SK_CIRCBUF_E_BAD_PARAM,
    /*  The sk_circbuf_t is stopped. */
    SK_CIRCBUF_E_STOPPED
};
typedef enum sk_circbuf_status_en sk_circbuf_status_t;


/*
 *    Creates a circular buffer which can contain at least
 *    'item_count' items each of size 'item_size' and stores the
 *    circular buffer at the location specified by 'buf'.
 *
 *    Returns SK_CIRCBUF_E_BAD_PARAM if 'buf' is NULL, if either
 *    numeric parameter is 0, or if 'item_size' is larger than 85MiB.
 *    Returns SK_CIRCBUF_E_ALLOC if there is not enough memory.  The
 *    created circular buffer may contain space for more than
 *    'item_count' items, up to the size of a circular buffer chunk.
 */
int
skCircBufCreate(
    sk_circbuf_t      **buf,
    uint32_t            item_size,
    uint32_t            item_count);

/*
 *    Causes all threads waiting on the circular buffer 'buf' to
 *    return.
 */
void
skCircBufStop(
    sk_circbuf_t       *buf);

/*
 *    Destroys the circular buffer 'buf'.  For proper clean-up, the
 *    caller should call skCircBufStop() before calling this function.
 *    Does nothing if 'buf' is NULL.
 */
void
skCircBufDestroy(
    sk_circbuf_t       *buf);

/*
 *    Sets the location referenced by 'writer_pos'--which should be a
 *    pointer-pointer---to an empty memory block in the circular
 *    buffer 'buf' and returns SK_CIRCBUF_OK.  When 'item_count' is
 *    not NULL, the location it references is set to number of items
 *    currently in 'buf' (the returned block is included in the item
 *    count).
 *
 *    This block should be used to add data to the circular buffer.
 *    The size of the block is the 'item_size' specified when 'buf'
 *    was created.
 *
 *    This call blocks if the buffer is full. The function returns
 *    SK_CIRCBUF_E_STOPPED if skCircBufStop() or skCircBufDestroy()
 *    are called while waiting.  The function returns
 *    SK_CIRCBUF_E_ALLOC when an attempt to allocate a new chunk
 *    fails.
 *
 *    When the function returns a value other than SK_CIRCBUF_OK, the
 *    pointer referenced by 'writer_pos' is set to NULL and the value
 *    in 'item_count' is not defined.
 *
 *    The circular buffer considers the returned block locked by the
 *    caller.  The block is not made available for use by
 *    skCircBufGetReaderBlock() until skCircBufGetWriterBlock() is
 *    called again.
 */
int
skCircBufGetWriterBlock(
    sk_circbuf_t       *buf,
    void               *writer_pos,
    uint32_t           *item_count);

/*
 *    Sets the location referenced by 'reader_pos'--which should be a
 *    pointer-pointer---to a full memory block in the circular buffer
 *    'buf' and returns SK_CIRCBUF_OK.  When 'item_count' is not NULL,
 *    the location it references is set to number of items currently
 *    in 'buf' (the returned item is included in the item count).
 *
 *    This block should be used to get data from the circular buffer.
 *    The size of the block is the 'item_size' specified when 'buf'
 *    was created.  The block is the least recently added item from a
 *    call to skCircBufGetWriterBlock().
 *
 *    This call blocks if the buffer is full. The function returns
 *    SK_CIRCBUF_E_STOPPED if skCircBufStop() or skCircBufDestroy()
 *    are called while waiting.
 *
 *    When the function returns a value other than SK_CIRCBUF_OK, the
 *    pointer referenced by 'reader_pos' is set to NULL and the value
 *    in 'item_count' is not defined.
 *
 *    The circular buffer considers the returned block locked by the
 *    caller.  The block is not made available for use by
 *    skCircBufGetWriterBlock() until skCircBufGetReaderBlock() is
 *    called again.
 */
int
skCircBufGetReaderBlock(
    sk_circbuf_t       *buf,
    void               *reader_pos,
    uint32_t           *item_count);

#ifdef __cplusplus
}
#endif
#endif /* _CIRCBUF_H */

/*
** Local Variables:
** mode:c
** indent-tabs-mode:nil
** c-basic-offset:4
** End:
*/
