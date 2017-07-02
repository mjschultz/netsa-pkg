/*
** Copyright (C) 2004-2017 by Carnegie Mellon University.
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

RCSIDENTVAR(rcsID_CIRCBUF_H, "$SiLK: skcircbuf.h efd886457770 2017-06-21 18:43:23Z mthomas $");

#include <silk/silk_types.h>


/**
 *  @file
 *
 *  skcircbuf.h
 *
 *    The sk_circbuf_t provides a thread-safe FIFO to support a single
 *    writer and a single reader.  The amount of data the FIFO may
 *    hold is limited by the maximum allocation size specified when
 *    the sk_circbuf_t is created.  When the sk_circbuf_t reaches the
 *    maximim memory size, further inserts to the sk_circbuf_t are
 *    blocked until data is read from the sk_circbuf_t.
 *
 *    The writer requests a block of (empty) memory from the
 *    sk_circbuf_t.  Once the writer has filled the memory, the writer
 *    commits the memory to the sk_circbuf_t and may request another
 *    block of memory.  The writer may access only a single block of
 *    memory at a time.
 *
 *    The sk_circbuf_t manages these memory until the reader requests
 *    them.
 *
 *    The reader requests a block of (filled) memory from the
 *    sk_circbuf_t.  If no data is available, the call blocks until
 *    data is available (or the sk_circbuf_t is told to stop).  The
 *    reader uses the block of memory then releases it back to the
 *    sk_circbuf_t.  After releasing the memory, the reader may
 *    request another block.  The reader may access only a single
 *    block of memory at a time.  To assist readers, requesting
 *    another block of memory automatically releases any block the
 *    reader currently holds.
 *
 *    The sk_circbuf_t groups the individual blocks of memory the
 *    writer and reader request into a a larger region of memory
 *    called a chunk.  All chunks are the same size.
 *
 *    If the writer fills blocks faster than the reader consumes them,
 *    the sk_circbuf_t allocates another chunk of memory.  This
 *    continues until the maximum memory size for the sk_circbuf_t is
 *    reached.
 */


/**
 *    Minimum size we allow for a chunk is 4k
 */
#define SK_CIRCBUF_MEM_MIN_CHUNK_SIZE     0x1000

/**
 *    Standard chunk size is half a meg
 */
#define SK_CIRCBUF_MEM_STD_CHUNK_SIZE     0x80000

/**
 *    Standard number of chunks the memory-based sk_circbuf_t norally
 *    has---used to compute the maximum allocation.
 */
#define SK_CIRCBUF_MEM_STD_NUMBER_CHUNKS  3


/**
 *    The type for the thread-safe FIFO.
 */
typedef struct sk_circbuf_st sk_circbuf_t;


/**
 *    Status codes returned by the sk_circbuf_t functions.
 */
enum sk_circbuf_status_en {
    /** Success */
    SK_CIRCBUF_OK = 0,
    /** Memory allocation error */
    SK_CIRCBUF_ERR_ALLOC,
    /** Bad parameter to function */
    SK_CIRCBUF_ERR_BAD_PARAM,
    /** Either sk_circbuf_t is stopped or the writer thread indicated
     *  there is no more data and the sk_circbuf_t is empty. */
    SK_CIRCBUF_ERR_STOPPED,
    /** There is either no data for the reader thread to process or
     *  not enough space for the buffer size requested by the writer
     *  thread. */
    SK_CIRCBUF_ERR_WOULD_BLOCK,
    /** Either the size requested to sk_circbuf_get_write_block() is
     *  too large or the size specified in
     *  sk_circbuf_commit_write_block() is larger than the size of the
     *  buffer the writer thread was given. */
    SK_CIRCBUF_ERR_BLOCK_TOO_LARGE,
    /** Thread is attempting to release a read block or commit a write
     *  block with first requested a block. */
    SK_CIRCBUF_ERR_HAS_NO_BLOCK,
    /** Writer thread is requesting another write block before
     *  committing its current write block. */
    SK_CIRCBUF_ERR_UNCOMMITTED_BLOCK
};
typedef enum sk_circbuf_status_en sk_circbuf_status_t;


/**
 *    Create an sk_circbuf_t and store it in the location referenced
 *    by 'cbuf'.
 *
 *    Have the sk_circbuf_t allocate chunks of size 'chunk_size'
 *    octets and support 'max_allocation' octets space for user
 *    data---that is, 'max_allocation' does not include internal
 *    overhead.
 *
 *    When both 'chunk_size' and 'max_allocation' are 0, use
 *    SK_CIRCBUF_MEM_STD_CHUNK_SIZE for 'chunk_size' and set
 *    'max_allocation' to the product of the chunk size and
 *    SK_CIRCBUF_MEM_STD_NUMBER_CHUNKS.
 *

// MORE

 *    Both 'chunk_size' and 'max_allocation' must be larger than
 *    SK_CIRCBUF_MEM_MIN_CHUNK_SIZE.
 *
 *    Return SK_CIRCBUF_OK on success.  Return
 *    SK_CIRCBUF_ERR_BAD_PARAM for invalid 'chunk_size' or
 *    'max_allocation' values.  Return SK_CIRCBUF_ERR_ALLOC on memory
 *    allocation error.
 */
int
sk_circbuf_create(
    sk_circbuf_t      **cbuf,
    size_t              chunk_size,
    size_t              max_allocation);


/**
 *    Create an sk_circbuf_t which manages blocks that have a constant
 *    size.
 *
 *    This interface is deprecated.
 *
 *    The sk_circbuf_t is created with a maximum allocation of the
 *    produce of 'item_count' and 'item_size'.
 */
int
sk_circbuf_create_const_itemsize(
    sk_circbuf_t      **cbuf,
    size_t              item_size,
    size_t              item_count);


/**
 *    Cause all threads waiting on a circular buffer to return and
 *    wait for the writer thread and reader thread to commit/release
 *    the current block, if any.
 */
void
sk_circbuf_stop(
    sk_circbuf_t       *cbuf);


/**
 *    Allow the reader thread to continue to read data from the
 *    sk_circbuf_t until the data is exhausted.
 */
void
sk_circbuf_stop_writing(
    sk_circbuf_t       *cbuf);


/**
 *    Destroy an sk_circbuf_t.  For proper clean-up, the caller should
 *    call sk_circbuf_stop() before calling this function.  Does
 *    nothing if 'cbuf' is NULL.
 */
void
sk_circbuf_destroy(
    sk_circbuf_t       *cbuf);


/**
 *    Provide a reading thread with a block of data to process.  When
 *    the reader is finished processing the data, the reader should
 *    call sk_circbuf_release_read_block() to release the block.
 *
 *    To assist readers, calling this function automatically releases
 *    any previous block the reader had requested.
 *
 *    The location of the buffer is placed in the memory reference by
 *    'block'; thus 'block' should contain the address of a pointer.
 *    The number of octets available is written to the location
 *    referenced by 'size'.
 *
 *    This call blocks when no data is available.  See also
 *    sk_circbuf_get_read_block_nowait().
 *
 *    Return SK_CIRCBUF_OK on success.  Return SK_CIRCBUF_ERR_STOPPED
 *    if either 'cbuf' is stopped or the writer has indicated it has
 *    no more data and 'cbuf' is empty.
 */
int
sk_circbuf_get_read_block(
    sk_circbuf_t       *cbuf,
    void               *block,
    size_t             *size);


/**
 *    Provide a reading thread with a block of data to process or
 *    return SK_CIRCBUF_ERR_WOULD_BLOCK if no data is available.
 *
 *    See sk_circbuf_get_read_block() for addditional information.
 */
int
sk_circbuf_get_read_block_nowait(
    sk_circbuf_t       *cbuf,
    void               *block,
    size_t             *size);


/**
 *    Called by the reader to get a block of data to process when the
 *    sk_circbuf_t is using constant sized blocks of memory.
 *
 *    This interface is deprecated.
 *
 *    Return a pointer to a full memory location from the circular
 *    buffer of size item_size.  This location will be the least
 *    recently added item from a call to sk_circbuf_get_write_pos().
 *    This location should be used to get data from the circular
 *    buffer.  This call blocks if the buffer is empty, and return
 *    NULL if sk_circbuf_stop() or sk_circbuf_destroy() were called
 *    while waiting.
 */
uint8_t*
sk_circbuf_get_read_pos(
    sk_circbuf_t       *cbuf);


/**
 *    Inform 'cbuf' that the reader thread no longer needs access to
 *    the block of data last returned by sk_circbuf_get_read_block().
 *    After calling this function, the reader may no longer reference
 *    that buffer.
 *
 *    Return SK_CIRCBUF_ERR_HAS_NO_BLOCK if the reader did not have a
 *    block, or SK_CIRCBUF_OK otherwise.
 */
int
sk_circbuf_release_read_block(
    sk_circbuf_t       *cbuf);


/**
 *    Provide the writer thread with a buffer of at least 'size'
 *    octets that the writer can fill with data.  When the writer is
 *    finished writing the data, the writer must call
 *    sk_circbuf_commit_write_block() and specify the number of actual
 *    bytes written to the buffer.  The writer is not allowed to
 *    request another block until the current block is returned.
 *
 *    The location of the buffer is placed in the memory referenced by
 *    'block'; thus 'block' should contain the address of a pointer.
 *    The 'size' parameter is updated with the number of octets
 *    available, which may be larger than the request.
 *
 *    This call blocks if the sk_circbuf_t does not have 'size' bytes
 *    of space avaiable.  For a non-blocking version, use
 *    sk_circbuf_get_write_block_nowait().
 *
 *    To commit a block and request a new block, use
 *    sk_circbuf_commit_get_write_block().
 *
 *    Return SK_CIRCBUF_OK on success.  Return
 *    SK_CIRCBUF_ERR_UNCOMMITTED_BLOCK if the writer thread failed to
 *    call sk_circbuf_commit_write_block() for the previous block
 *    returned by this function.  Return SK_CIRCBUF_ERR_STOPPED if
 *    either 'cbuf' is stopped or the writer previously indicated it
 *    has no more data.  Return SK_CIRCBUF_ERR_BLOCK_TOO_LARGE if
 *    'size' is larger than the maximum supported size.  Return
 *    SK_CIRCBUF_ERR_ALLOC if an attempt to allocate a new chunk
 *    fails.
 */
int
sk_circbuf_get_write_block(
    sk_circbuf_t       *cbuf,
    void               *block,
    size_t             *size);


/**
 *    Provide a writing thread with a block of memory to fill or
 *    return SK_CIRCBUF_ERR_WOULD_BLOCK if no space is available.
 *
 *    See sk_circbuf_get_write_block() for addditional information.
 */
int
sk_circbuf_get_write_block_nowait(
    sk_circbuf_t       *cbuf,
    void               *block,
    size_t             *size);


/**
 *    Called by the writer to get a block of memory to fill when the
 *    sk_circbuf_t is using constant sized blocks of memory.
 *
 *    This interface is deprecated.
 *
 *    Return a pointer to an empty memory location in the buffer of
 *    size item_size.  This location should be used to add data to the
 *    circular buffer.  This location will not be returned by
 *    sk_circbuf_get_read_pos() until sk_circbuf_get_write_pos() is
 *    called again at least once.  This call will block if the buffer
 *    is full, and return NULL if sk_circbuf_stop() or
 *    sk_circbuf_destroy() were called while waiting.
 */
uint8_t*
sk_circbuf_get_write_pos(
    sk_circbuf_t       *cbuf);


/**
 *    Inform 'cbuf' that the writer thread wrote 'size' bytes of data
 *    to the block of memory returned by the most recent call to
 *    sk_circbuf_get_write_block().  After calling this function, the
 *    writer may no longer reference the buffer.
 *
 *    Specify a 'size' of 0 to indicate that nothing was written to
 *    the block.  If a writer thread needs a larger block than was
 *    initially requested, the writer may commit the the block with a
 *    size of 0 and then get a larger block.
 *
 *    Return SK_CIRCBUF_OK on success.  Return
 *    SK_CIRCBUF_ERR_HAS_NO_BLOCK if the writer did not have a block.
 *    Return SK_CIRCBUF_ERR_BLOCK_TOO_LARGE if 'size' is larger than
 *    the size that was returned by sk_circbuf_get_write_block().
 */
int
sk_circbuf_commit_write_block(
    sk_circbuf_t       *cbuf,
    size_t              size);


/**
 *    Call sk_circbuf_commit_write_block() with the 'old_size'
 *    parameter.  If that function returns SK_CIRCBUF_OK, call
 *    sk_circbuf_get_write_block() with the 'new_block' and 'new_size'
 *    parameters.
 *
 *    Return any of the statuses from the wrapped functions.
 *
 *    Note: The writer thread must call sk_circbuf_get_write_block()
 *    to get the first write block as with function returns
 *    SK_CIRCBUF_ERR_HAS_NO_BLOCK if there is no active write block.
 */
int
sk_circbuf_commit_get_write_block(
    sk_circbuf_t       *cbuf,
    size_t              old_size,
    void               *new_block,
    size_t             *new_size);


/**
 *    Print the statistics using 'msg_fn'.  The 'name' parameter may
 *    be used to indentify the statistics, or it may be NULL.
 */
void
sk_circbuf_print_stats(
    sk_circbuf_t       *cbuf,
    const char         *name,
    sk_msg_fn_t         msg_fn);


/**
 *    Return a human-readable description for one of the error codes
 *    defined in sk_circbuf_status_t.
 */
const char*
sk_circbuf_strerror(
    int                 err_code);


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
