/*
** Copyright (C) 2004-2016 by Carnegie Mellon University.
**
** @OPENSOURCE_HEADER_START@
**
** Use of the SILK system and related source code is subject to the terms
** of the following licenses:
**
** GNU General Public License (GPL) Rights pursuant to Version 2, June 1991
** Government Purpose License Rights (GPLR) pursuant to DFARS 252.227.7013
**
** NO WARRANTY
**
** ANY INFORMATION, MATERIALS, SERVICES, INTELLECTUAL PROPERTY OR OTHER
** PROPERTY OR RIGHTS GRANTED OR PROVIDED BY CARNEGIE MELLON UNIVERSITY
** PURSUANT TO THIS LICENSE (HEREINAFTER THE "DELIVERABLES") ARE ON AN
** "AS-IS" BASIS. CARNEGIE MELLON UNIVERSITY MAKES NO WARRANTIES OF ANY
** KIND, EITHER EXPRESS OR IMPLIED AS TO ANY MATTER INCLUDING, BUT NOT
** LIMITED TO, WARRANTY OF FITNESS FOR A PARTICULAR PURPOSE,
** MERCHANTABILITY, INFORMATIONAL CONTENT, NONINFRINGEMENT, OR ERROR-FREE
** OPERATION. CARNEGIE MELLON UNIVERSITY SHALL NOT BE LIABLE FOR INDIRECT,
** SPECIAL OR CONSEQUENTIAL DAMAGES, SUCH AS LOSS OF PROFITS OR INABILITY
** TO USE SAID INTELLECTUAL PROPERTY, UNDER THIS LICENSE, REGARDLESS OF
** WHETHER SUCH PARTY WAS AWARE OF THE POSSIBILITY OF SUCH DAMAGES.
** LICENSEE AGREES THAT IT WILL NOT MAKE ANY WARRANTY ON BEHALF OF
** CARNEGIE MELLON UNIVERSITY, EXPRESS OR IMPLIED, TO ANY PERSON
** CONCERNING THE APPLICATION OF OR THE RESULTS TO BE OBTAINED WITH THE
** DELIVERABLES UNDER THIS LICENSE.
**
** Licensee hereby agrees to defend, indemnify, and hold harmless Carnegie
** Mellon University, its trustees, officers, employees, and agents from
** all claims or demands made against them (and any related losses,
** expenses, or attorney's fees) arising out of, or relating to Licensee's
** and/or its sub licensees' negligent use or willful misuse of or
** negligent conduct or willful misconduct regarding the Software,
** facilities, or other rights or assistance granted by Carnegie Mellon
** University under this License, including, but not limited to, any
** claims of product liability, personal injury, death, damage to
** property, or violation of any laws or regulations.
**
** Carnegie Mellon University Software Engineering Institute authored
** documents are sponsored by the U.S. Department of Defense under
** Contract FA8721-05-C-0003. Carnegie Mellon University retains
** copyrights in all material produced under this contract. The U.S.
** Government retains a non-exclusive, royalty-free license to publish or
** reproduce these documents, or allow others to do so, for U.S.
** Government purposes only pursuant to the copyright license under the
** contract clause at 252.227.7013.
**
** @OPENSOURCE_HEADER_END@
*/
#ifndef _CIRCBUF_H
#define _CIRCBUF_H
#ifdef __cplusplus
extern "C" {
#endif

#include <silk/silk.h>

RCSIDENTVAR(rcsID_CIRCBUF_H, "$SiLK: circbuf.h 71c2983c2702 2016-01-04 18:33:22Z mthomas $");

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
