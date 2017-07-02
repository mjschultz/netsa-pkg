/*
** Copyright (C) 2004-2017 by Carnegie Mellon University.
**
** @OPENSOURCE_LICENSE_START@
** See license information in ../../LICENSE.txt
** @OPENSOURCE_LICENSE_END@
*/

#include <silk/silk.h>

RCSIDENT("$SiLK: skcircbuf.c efd886457770 2017-06-21 18:43:23Z mthomas $");

#include <silk/skcircbuf.h>
#include <silk/sklog.h>
#include <silk/utils.h>

/* Trace level to report "I am here" and current 'cbuf' pointer */
#define TRACE_FUNC_LEVEL       3

/* Trace level to report chunk operations */
#define TRACE_MEM_CHUNK_LEVEL  4

#ifdef  SKCIRCBUF_TRACE_LEVEL
#define TRACEMSG_LEVEL SKCIRCBUF_TRACE_LEVEL
#endif
#define TRACEMSG(lvl, msg) TRACEMSG_TO_TRACEMSGLVL(lvl, msg)
#include <silk/sktracemsg.h>


/* Write a trace message for the containing function and the current
 * sk_circbuf_t object pointer. */
#define TRACE_FUNC                                                      \
    TRACEMSG(TRACE_FUNC_LEVEL, ("Function: (%p) %s", cbuf, __func__))

/* Write trace info for a cbuf_mem_chunk_t data structure. */
#define TRACE_MEM_CHUNK(chunk)                                  \
    TRACEMSG(TRACE_MEM_CHUNK_LEVEL,                             \
             (("%s:%d Chunk: %p, writer_pos: %zd"               \
               "  reader_pos: %zd  max_reader_pos: %zd"),       \
              __FILE__, __LINE__,                               \
              chunk, chunk->writer_pos, chunk->reader_pos,      \
              chunk->max_reader_pos))



/* LOCAL DEFINES AND TYPEDEFS */

/*
 *    Various levels of statistics it is possible to maintain on each
 *    sk_circbuf_t.
 */
#define SKCIRCBUF_STATS_DO_NONE   0
#define SKCIRCBUF_STATS_DO_BYTES  1
#define SKCIRCBUF_STATS_DO_TIMES  2

/*
 *    Level of statistics to maintain.
 */
#define SKCIRCBUF_STATS_LEVEL     SKCIRCBUF_STATS_DO_NONE


/*
 *    This is old documentation about the implementation; keeping it
 *    here for now until I can update it.
 *
 *
 *    The sk_circbuf_t hands cells to the writer which the writer fills.
 *    The sk_circbuf_t holds onto these cells until the reader requests
 *    them.  The maxinum number of cells a sk_circbuf_t may allocate is
 *    specified at creatation time.  However, the cells are not
 *    allocated as one block of memory.  Instead, the sk_circbuf_t
 *    allocates smaller blocks of memory called chunks.  All chunks
 *    are the same size.  To summarize, the sk_circbuf_t is composed of
 *    multiple chunks, and a chunk is composed of multiple cells.
 *
 *    For each chunk, the 'head' points to the cell currently in use
 *    by the writer, and the 'tail' points to the cell currently in
 *    use by the reader.
 *
 *    All cells "between" the tail and the head have data.  In the
 *    diagram below, the writer (head) has wrapped around, and all
 *    cells with 'D' have data.  'W' is where the writer is currently
 *    writing data, and 'R' is where the reader is reading.
 *        _ _ _ _ _ _ _ _ _ _ _ _
 *       |D|D|W|_|_|_|_|_|R|D|D|D|
 *            A A         A A
 *            | |         | |
 *            | next_head | next_tail
 *            |           |
 *            head        tail
 *
 *    When the writer or reader finishes with a cell, they call the
 *    appropriate function which releases the current cell and moves
 *    them to the next cell.
 *
 *    If a chunk becomes full, a new chunk is allocated and the writer
 *    starts using cells from the new chunk.  Depending on the chunk
 *    size and maximum number of cells allowed, there may be multiple
 *    chunks in the chunk list between the writer and the reader.
 *
 *    Once the reader finishes with all the cells in the current
 *    chunk, the reader moves to the first cell of the next chunk in
 *    the chunk list, and the chunk the reader just completed is
 *    discarded.  The sk_circbuf_t is circular within a chunk, but linear
 *    between multiple chunks.
 *
 *    The first time the sk_circbuf_t has a chunk to discard, the
 *    sk_circbuf_t stores the chunk as spare (instead of deallocating the
 *    chunk).  When a chunk needs to be discard and the sk_circbuf_t
 *    already has a spare chunk, the chunk is deallocated.
 *
 */


/**
 *    Return the space required between the end of the writer's block
 *    and the start of the reader's block when the the writer position
 *    wraps so it appears before the reader position in a chunk.
 */
#define CIRCBUF_WRAP_GAP  sizeof(uint64_t)

/**
 *    Compute the total space required for a circbuf_block_t that
 *    holds 'bts_size' bytes of memory in its 'data' member; includes
 *    the 'block_size' member and ensures 'data' is 64-bit aligned
 */
#define CIRCBUF_BLOCK_TOTAL_SIZE(bts_size)                      \
    (((bts_size) + 2 * sizeof(uint64_t) - 1) & ~(UINT64_C(7)))

/**
 *    Compute the maximum write_block size a caller may request in a
 *    chunk whose size is 'bms_chunk_size' bytes.  We require at least
 *    3 blocks per chunk; allow for overhead within the chunk.
 */
#define CIRCBUF_BLOCK_MAX_SIZE_FOR_CHUNK(bms_chunk_size)        \
    (((bms_chunk_size) - 4 * CIRCBUF_WRAP_GAP) / 3)


/**
 *    circbuf_block_t represents a block of memory accessible by the
 *    users of the sk_circbuf_t.  The 'data' element is the memory
 *    that is handed to the reader and writer.  'data' is aligned on a
 *    64-bit boundary.  The 'block_size' element is the number of
 *    bytes in the 'data' element.
 */
struct circbuf_block_st {
    /* number of bytes in this block */
    uint64_t    block_size;
    /* the module's handle to the data */
    uint8_t     data[1];
};
typedef struct circbuf_block_st circbuf_block_t;


/**
 *    cbuf_mem_chunk_t is a chunk of memory that contains multiple
 *    blocks (circbuf_block_t structures), where each circbuf_block_t
 *    corresponds to memory region returned to the user of the
 *    sk_circbuf_t.
 */
typedef struct cbuf_mem_chunk_st cbuf_mem_chunk_t;
struct cbuf_mem_chunk_st {
    /* pointer to the next chunk */
    cbuf_mem_chunk_t   *next;

    /* the data used for circbuf_block_t, which 'writer_pos' and
     * 'reader_pos' are offsets into */
    uint8_t            *blocks;

    /* offset to the location for the writer */
    size_t              writer_pos;
    /* offset to the location for the reader */
    size_t              reader_pos;

    /* last valid byte that can be read in 'blocks'; used to determine
     * when the reader_pos needs to be reset to 0 */
    size_t              max_reader_pos;

    /* total number of bytes in 'blocks'; this space includes blocks
     * the callers access and any overhead */
    size_t              capacity;
};


/**
 *    circbuf_mem_t contains information for maintaining the sk_circbuf_t
 *    in memory.
 */
struct circbuf_mem_st {
    /* linked list of chunks of memory */
    cbuf_mem_chunk_t   *writer_chunk;
    cbuf_mem_chunk_t   *reader_chunk;
    cbuf_mem_chunk_t   *spare_chunk;

    /* size of an individual chunk of memory; may be specified by the
     * caller */
    size_t              chunk_size;

    /* maximum block size a module may request; calculated as
     * roughly 1/3 of the chunk_size */
    size_t              block_max_size;

};
typedef struct circbuf_mem_st circbuf_mem_t;


#if SKCIRCBUF_STATS_LEVEL
/**
 *    sk_circbuf_stats_t keeps statistics about the number of bytes
 *    written and read by the sk_circbuf_t.
 */
struct sk_circbuf_stats_st {
    /* Number of bytes the writer has written into the circbuf. */
    uint64_t    bytes_written;

    /* Number of bytes the reader has read from the circbuf. */
    uint64_t    bytes_read;

    /* Number of times buffer is full */
    uint64_t    full_count;

    /* Number of times buffer is empty */
    uint64_t    empty_count;

#if SKCIRCBUF_STATS_LEVEL >= SKCIRCBUF_STATS_DO_TIMES
    /* Time when circbuf was created. */
    sktime_t    creation_time;

    /* Time of the first call to sk_circbuf_commit_write_block(). */
    sktime_t    first_write;

    /* Time of the most recent call to
     * sk_circbuf_commit_write_block(). */
    sktime_t    last_write;

    /* Time of the call to sk_circbuf_stopped_write(). */
    sktime_t    stopped_write;

    /* Time of the first call to sk_circbuf_release_read_block(). */
    sktime_t    first_read;

    /* Time of the most recent call to
     * sk_circbuf_release_read_block(). */
    sktime_t    last_read;

    /* Time of the call to sk_circbuf_stop(). */
    sktime_t    stopped_read;
#endif  /* SKCIRCBUF_STATS_LEVEL >= SKCIRCBUF_STATS_DO_TIMES */
};
typedef struct sk_circbuf_stats_st sk_circbuf_stats_t;
#endif  /* SKCIRCBUF_STATS_LEVEL */


/* typedef struct sk_circbuf_st sk_circbuf_t; */
struct sk_circbuf_st {
#if SKCIRCBUF_STATS_LEVEL
    /* timing/throughput statistics */
    sk_circbuf_stats_t      stats;

    /* thread variable for the stats */
    pthread_mutex_t         stats_mutex;
#endif  /* SKCIRCBUF_STATS_LEVEL */

    /* thread variables for everything except the stats */
    pthread_mutex_t         mutex;
    pthread_cond_t          cond;

    /* implementation of a memory-based circular buffer */
    circbuf_mem_t           mem;

    /* the block the writing currently holds; NULL if none */
    const circbuf_block_t  *writer_block;

    /* the block the reader currently holds; NULL if none */
    const circbuf_block_t  *reader_block;

    /* maximum amount of bytes that callers are allowed to allocate
     * across all blocks on all chunks; does not include internal
     * overhead.  May be specified by the caller */
    size_t                  max_allocation;

    /* total number of bytes that the callers have used across all
     * blocks on all chunks; when this is 0, the reader is starved for
     * data; when this is near max_allocation, the writer is starved
     * for space; does not include internal overhead. */
    size_t                  total_used;

    /* when a non-zero value, indicates that all items in the
     * sk_circbuf_t have the same size */
    size_t                  fixed_item_size;

    /* sk_circbuf_get_read_block() and sk_circbuf_get_write_block()
     * increment this value while waiting for the sk_circbuf_t not to
     * be empty or full, respectively, to ensure sk_circbuf_stop()
     * does not return until all functions complete. */
    uint32_t                wait_count;

    /* do any writer requests need to block because the circbuf has
     * used the maximum amount of memory? */
    unsigned                full  :1;

    /* do any reader requests need to block because the circbuf is
     * completely empty? */
    unsigned                empty :1;

    /* has the writer indicated it has no more data? */
    unsigned                writer_stopped : 1;

    /* has the circular buffer been told to stop? */
    unsigned                stopped : 1;
};



/* FUNCTION DEFINITIONS */


/*
 *
 *    The following functions are specific for a memory-based
 *    sk_circbuf_t.
 *
 */

/**
 *    For a memory-based sk_circbuf_t, allocate a new cbuf_mem_chunk_t
 *    chunk---the structure and 'blocks' block---for the circular
 *    buffer 'cbuf' and return the new chunk.  The chunk is bzero()ed
 *    except for the 'chunk_size' and 'blocks' elements.
 *
 *    Return NULL if memory cannot be allocated.
 */
static cbuf_mem_chunk_t*
cbuf_mem_chunk_alloc(
    sk_circbuf_t       *cbuf)
{
    cbuf_mem_chunk_t *chunk;

    chunk = (cbuf_mem_chunk_t*)calloc(1, sizeof(cbuf_mem_chunk_t));
    if (!chunk) {
        return NULL;
    }
    chunk->blocks = (uint8_t*)malloc(cbuf->mem.chunk_size);
    if (!chunk->blocks) {
        free(chunk);
        return NULL;
    }

    chunk->capacity = cbuf->mem.chunk_size;
    return chunk;
}


/**
 *    For a memory-based sk_circbuf_t, free all memory associated with
 *    'chunk'.
 */
static void
cbuf_mem_chunk_free(
    cbuf_mem_chunk_t   *chunk)
{
    if (chunk) {
        if (chunk->blocks) {
            free(chunk->blocks);
        }
        free(chunk);
    }
}


/**
 *    For a memory-based sk_circbuf_t, move the circular buffer's
 *    reader_chunk to the next chunk.  The previous chunk is either
 *    stored as the spare chunk or freed.
 */
static void
cbuf_mem_chunk_pop(
    sk_circbuf_t       *cbuf)
{
    cbuf_mem_chunk_t *chunk;

    assert(cbuf);
    assert(cbuf->mem.reader_chunk);
    assert(cbuf->mem.reader_chunk->next);

    if (!cbuf->mem.spare_chunk) {
        cbuf->mem.spare_chunk = cbuf->mem.reader_chunk;
        cbuf->mem.reader_chunk = cbuf->mem.reader_chunk->next;
    } else {
        chunk = cbuf->mem.reader_chunk;
        cbuf->mem.reader_chunk = cbuf->mem.reader_chunk->next;
        cbuf_mem_chunk_free(chunk);
    }
}


/**
 *    For a memory-based sk_circbuf_t, add a new memory chunk (either
 *    use the spare chunk or allocate a new chunk), and move the
 *    circular buffer's writer_chunk pointer to that new chunk.
 *    Return SK_CIRCBUF_OK on success.  On memory error, return
 *    SK_CIRCBUF_ERR_ALLOC.
 */
static int
cbuf_mem_chunk_push(
    sk_circbuf_t       *cbuf)
{
    assert(cbuf);

    if (cbuf->mem.spare_chunk) {
        cbuf->mem.spare_chunk->next = NULL;
        cbuf->mem.spare_chunk->writer_pos = 0;
        cbuf->mem.spare_chunk->reader_pos = 0;
        cbuf->mem.spare_chunk->max_reader_pos = 0;
        cbuf->mem.writer_chunk->next = cbuf->mem.spare_chunk;
        cbuf->mem.writer_chunk = cbuf->mem.writer_chunk->next;
        cbuf->mem.spare_chunk = NULL;
    } else {
        cbuf->mem.writer_chunk->next = cbuf_mem_chunk_alloc(cbuf);
        if (NULL == cbuf->mem.writer_chunk->next) {
            return SK_CIRCBUF_ERR_ALLOC;
        }
        cbuf->mem.writer_chunk = cbuf->mem.writer_chunk->next;
    }

    return SK_CIRCBUF_OK;
}


/**
 *    Destroy the parts of the sk_circbuf_t which are specific to
 *    managing chunks of data in RAM.
 */
static void
circbuf_mem_destroy(
    sk_circbuf_t       *cbuf)
{
    cbuf_mem_chunk_t *chunk;

    if (cbuf) {
        while ((chunk = cbuf->mem.reader_chunk) != NULL) {
            cbuf->mem.reader_chunk = chunk->next;
            cbuf_mem_chunk_free(chunk);
        }
        if (cbuf->mem.spare_chunk) {
            cbuf_mem_chunk_free(cbuf->mem.spare_chunk);
        }
    }
}


/**
 *    Initialize the parts the sk_circbuf_t which are specific to
 *    managing chunks of data in RAM.
 *
 *    In particular, this ensures that the max_allocation and
 *    chunk_size parameters in sk_circbuf_create() are reasonable.
 *
 *    In addition, create the initial chunk.
 *
 *    Return SK_CIRCBUF_OK on success.  Return SK_CIRCBUF_ERR_ALLOC on
 *    memory allocation error.  Return SK_CIRCBUF_ERR_BAD_PARAM if the
 *    parameters are invalid.
 */
static int
circbuf_mem_initialize(
    sk_circbuf_t       *cbuf,
    size_t              chunk_size)
{
    assert(cbuf);

    if (chunk_size) {
        if (chunk_size < SK_CIRCBUF_MEM_MIN_CHUNK_SIZE) {
            /* chunk size is too small */
            return SK_CIRCBUF_ERR_BAD_PARAM;
        }
        if (0 == cbuf->max_allocation) {
            /* use caller's chunk size, and the default multiple of it
             * for the maximum allocation */
            cbuf->mem.chunk_size = chunk_size;
            cbuf->max_allocation
                = SK_CIRCBUF_MEM_STD_NUMBER_CHUNKS * chunk_size;
        } else if (chunk_size > cbuf->max_allocation) {
            /* chunk size is larger than max allocation */
            return SK_CIRCBUF_ERR_BAD_PARAM;
        }
        /* else both values specified and are valid */
        cbuf->mem.chunk_size = chunk_size;
    } else if (cbuf->max_allocation) {
        if (cbuf->max_allocation < SK_CIRCBUF_MEM_MIN_CHUNK_SIZE) {
            /* max allocation is too small */
            return SK_CIRCBUF_ERR_BAD_PARAM;
        }
        /* determine the chunk size */
        if (cbuf->max_allocation >= SK_CIRCBUF_MEM_STD_CHUNK_SIZE) {
            /* use standard chunk size */
            cbuf->mem.chunk_size = SK_CIRCBUF_MEM_STD_CHUNK_SIZE;
        } else {
            cbuf->mem.chunk_size = (cbuf->max_allocation
                                    / SK_CIRCBUF_MEM_STD_NUMBER_CHUNKS);
            if (cbuf->mem.chunk_size < SK_CIRCBUF_MEM_MIN_CHUNK_SIZE) {
                cbuf->mem.chunk_size = SK_CIRCBUF_MEM_MIN_CHUNK_SIZE;
            }
        }
    } else {
        /* use the default sizes */
        cbuf->mem.chunk_size = SK_CIRCBUF_MEM_STD_CHUNK_SIZE;
        cbuf->max_allocation
            = SK_CIRCBUF_MEM_STD_NUMBER_CHUNKS * cbuf->mem.chunk_size;
    }

    assert(cbuf->mem.chunk_size);
    assert(cbuf->max_allocation);

    cbuf->mem.block_max_size
        = CIRCBUF_BLOCK_MAX_SIZE_FOR_CHUNK(cbuf->mem.chunk_size);

    /* allocate initial block */
    cbuf->mem.reader_chunk = cbuf_mem_chunk_alloc(cbuf);
    cbuf->mem.writer_chunk = cbuf->mem.reader_chunk;
    if (NULL == cbuf->mem.reader_chunk) {
        return SK_CIRCBUF_ERR_ALLOC;
    }
    return SK_CIRCBUF_OK;
}


/**
 *    For a memory-based sk_circbuf_t, set the 'reader_block' member
 *    of 'cbuf' to point to the next block of data.  If no data is
 *    currently available---that is, 'cbuf' is empty---wait until data
 *    becomes available unless 'no_wait' is non-zero.
 *
 *    Return SK_CIRCBUF_OK on success.  Return SK_CIRCBUF_ERR_STOPPED
 *    if either 'cbuf' is stopped or the writer has indicated it has
 *    no more data and 'cbuf' is empty.  Return
 *    SK_CIRCBUF_ERR_WOULD_BLOCK if 'cbuf' is empty and 'no_wait' is
 *    true.
 *
 *    This is a helper function for circbuf_read_block_get().
 */
static int
circbuf_mem_read_block_get(
    sk_circbuf_t       *cbuf,
    int                 no_wait)
{
    cbuf_mem_chunk_t *chunk;

    assert(cbuf);

    for (;;) {
        if (cbuf->stopped) {
            pthread_cond_broadcast(&cbuf->cond);
            return SK_CIRCBUF_ERR_STOPPED;
        }
        chunk = cbuf->mem.reader_chunk;
        if (chunk->reader_pos != chunk->writer_pos) {
            if (chunk->max_reader_pos
                && (chunk->reader_pos == chunk->max_reader_pos))
            {
                /* wrap the reader if the writer has wrapped */
                chunk->reader_pos = 0;
                chunk->max_reader_pos = 0;
                continue;
            }
            /* there is data to return */
            break;
        }
        /* else, there is no data in this block */
        if (chunk->next) {
            /* I don't think this ever gets called, since we should
             * have moved to the next block when the reader returned
             * its previous block */

            /* free this block and move to the next */
            cbuf_mem_chunk_pop(cbuf);
            continue;
        }
        /* else no data is available */

        if (cbuf->writer_stopped) {
            cbuf->stopped = 1;
            pthread_cond_broadcast(&cbuf->cond);
            return SK_CIRCBUF_ERR_STOPPED;
        }

        /* wait for data unless no_wait was specified */
        if (no_wait) {
            return SK_CIRCBUF_ERR_WOULD_BLOCK;
        }
        cbuf->empty = 1;
#if SKCIRCBUF_STATS_LEVEL
        ++cbuf->stats.empty_count;
#endif
        /* wait_count is here to ensure sk_circbuf_stop() does not
         * stop a blocked sk_circbuf_t. */
        ++cbuf->wait_count;
        pthread_cond_wait(&cbuf->cond, &cbuf->mutex);
        --cbuf->wait_count;
    }

    TRACE_FUNC;

    /* we get here when there is data to return */
    cbuf->reader_block = (circbuf_block_t*)&chunk->blocks[chunk->reader_pos];
    return 0;
}


/**
 *    For a memory-based sk_circbuf_t, tell 'cbuf' that the reader is
 *    finished with the block in 'reader_block', and move to the next
 *    read position or to the next memory chunk.
 *
 *    This is a helper function for circbuf_read_block_get().
 */
static void
circbuf_mem_read_block_release(
    sk_circbuf_t       *cbuf)
{
    cbuf_mem_chunk_t *chunk;

    assert(cbuf);

    chunk = cbuf->mem.reader_chunk;
    assert(cbuf->reader_block
           == (circbuf_block_t*)&chunk->blocks[chunk->reader_pos]);

    /* move to next reader position */
    if (cbuf->fixed_item_size) {
        chunk->reader_pos += cbuf->fixed_item_size;
    } else {
        chunk->reader_pos
            += CIRCBUF_BLOCK_TOTAL_SIZE(cbuf->reader_block->block_size);
    }
    if (chunk->reader_pos == chunk->max_reader_pos) {
        chunk->reader_pos = 0;
        chunk->max_reader_pos = 0;
    }
    TRACE_MEM_CHUNK(chunk);

    /* check whether we need to move to the next chunk */
    if (chunk->reader_pos == chunk->writer_pos
        && chunk->next)
    {
        /* free this block and move to the next */
        cbuf_mem_chunk_pop(cbuf);
    }
}


/**
 *    For a memory-based sk_circbuf_t, set the 'writer_block' member
 *    of 'cbuf' to point to the next 'size' octets of available space
 *    in 'cbuf'.  If 'size' octets are currently unavailable---that
 *    is, 'cbuf' is full---wait until space becomes available unless
 *    'no_wait' is non-zero.
 *
 *    Return SK_CIRCBUF_OK on success.  Return SK_CIRCBUF_ERR_STOPPED
 *    if either 'cbuf' is stopped or the writer has indicated it has
 *    no more data.  Return SK_CIRCBUF_ERR_BLOCK_TOO_LARGE if 'size'
 *    is larger than the maximum supported size.  Return
 *    SK_CIRCBUF_ERR_WOULD_BLOCK if 'cbuf' is empty and 'no_wait' is
 *    true.  Return SK_CIRCBUF_ERR_ALLOC if an attempt to allocate a
 *    new chunk fails.
 *
 *    This is a helper function for circbuf_write_block_get().
 */
static int
circbuf_mem_write_block_get(
    sk_circbuf_t       *cbuf,
    size_t              size,
    int                 no_wait)
{
    cbuf_mem_chunk_t *chunk;
    size_t required_size;

    assert(cbuf);
    assert(size);

    cbuf->writer_block = NULL;

    if (cbuf->mem.block_max_size < size) {
        return SK_CIRCBUF_ERR_BLOCK_TOO_LARGE;
    }

    /* account for overhead */
    if (cbuf->fixed_item_size) {
        required_size = size = cbuf->fixed_item_size;
    } else {
        required_size = CIRCBUF_BLOCK_TOTAL_SIZE(size);
    }

    /* loop until there is space available */
    for (;;) {
        /* handle stopped condition */
        if (cbuf->stopped || cbuf->writer_stopped) {
            pthread_cond_broadcast(&cbuf->cond);
            return SK_CIRCBUF_ERR_STOPPED;
        }

        if (cbuf->total_used + size > cbuf->max_allocation) {
            /* we are full; wait for space unless no_wait was
             * specified */
            if (no_wait) {
                return 1;
            }
            cbuf->full = 1;
#if SKCIRCBUF_STATS_LEVEL
            ++cbuf->stats.full_count;
#endif
            /* wait_count is here to ensure sk_circbuf_stop() does not
             * stop a blocked sk_circbuf_t. */
            ++cbuf->wait_count;
            pthread_cond_wait(&cbuf->cond, &cbuf->mutex);
            --cbuf->wait_count;
            continue;
        }

        chunk = cbuf->mem.writer_chunk;
        if (chunk->writer_pos < chunk->reader_pos) {
            /* the writer has wrapped; look for space between the
             * writer_pos and the reader_pos */
            if ((chunk->reader_pos - chunk->writer_pos)
                >= (required_size + CIRCBUF_WRAP_GAP))
            {
                /* space is available */
                break;
            }
        } else {
            if ((chunk->capacity - chunk->writer_pos) >= required_size) {
                /* space is available at end of the buffer */
                break;
            }
            /* else, see if space is available at the front */
            if (chunk->reader_pos >= required_size + CIRCBUF_WRAP_GAP) {
                /* space is available; wrap around */
                chunk->max_reader_pos = chunk->writer_pos;
                chunk->writer_pos = 0;
                TRACE_MEM_CHUNK(chunk);
                break;
            }
        }

        /* space is not available; allocate new block */
        if (cbuf_mem_chunk_push(cbuf)) {
            return SK_CIRCBUF_ERR_ALLOC;
        }
    }

    TRACE_FUNC;

    /* when we get here, space is available */
    cbuf->writer_block = (circbuf_block_t*)&chunk->blocks[chunk->writer_pos];
    if (!cbuf->fixed_item_size) {
        ((circbuf_block_t*)&chunk->blocks[chunk->writer_pos])->block_size
            = required_size - sizeof(uint64_t);
    }

    return  0;
}


/**
 *    For a memory-based sk_circbuf_t, tell 'cbuf' that the writer
 *    wrote 'size' bytes of data to the block in 'writer_block', and
 *    move to the next write position.
 *
 *    This function assumes the calling function has ensured that
 *    'size' is a vaild size.
 *
 *    This is a helper function for circbuf_write_block_commit().
 */
static void
circbuf_mem_write_block_commit(
    sk_circbuf_t       *cbuf,
    size_t              size)
{
    cbuf_mem_chunk_t *chunk;

    assert(cbuf);
    assert(size > 0);

    chunk = cbuf->mem.writer_chunk;
    assert(cbuf->writer_block
           == (circbuf_block_t*)&chunk->blocks[chunk->writer_pos]);

    if (cbuf->fixed_item_size) {
        chunk->writer_pos += cbuf->fixed_item_size;
    } else {
        ((circbuf_block_t*)&chunk->blocks[chunk->writer_pos])->block_size
            = size;
        chunk->writer_pos += CIRCBUF_BLOCK_TOTAL_SIZE(size);
    }
    TRACE_MEM_CHUNK(chunk);
}


/*
 *
 *    The following functions are wrappers for the above functions.
 *    These wrapper functions could also wrap a different set of
 *    functions that supported a disk-based sk_circbuf_t.
 *
 */

/**
 *    When 'v_block' is NULL, release the current reader_block if
 *    'cbuf' is currently holding one for the reader and return.
 *    Return SK_CIRCBUF_ERR_HAS_NO_BLOCK if 'cbuf' did not have a
 *    block; return SK_CIRCBUF_OK otherwise.
 *
 *    When 'v_block' is not NULL, release the current reader_block, if
 *    any, and get a new block of data for the reader to process.
 *    Store the location of the data in the memory location referenced
 *    by 'v_block' and store the number of octets in the block in the
 *    location referenced by 'size'.
 *
 *    This is a wrapper over circbuf_mem_read_block_get() and
 *    circbuf_mem_read_block_release().
 *
 *    This is a helper function for the public functions
 *    sk_circbuf_get_read_block() and sk_circbuf_release_read_block().
 */
static int
circbuf_read_block_get(
    sk_circbuf_t       *cbuf,
    void               *v_block,
    size_t             *size,
    int                 no_wait)
{
    uint8_t **block = (uint8_t**)v_block;
    uint64_t block_size;
    int rv;

    assert(cbuf);

    TRACE_FUNC;

    /* release the current reader_block */
    if (cbuf->reader_block) {
        /* reduce number of bytes in use */
        if (cbuf->fixed_item_size) {
            block_size = cbuf->fixed_item_size;
        } else {
            block_size = cbuf->reader_block->block_size;
        }
        assert(cbuf->total_used >= block_size);
        cbuf->total_used -= block_size;

#if SKCIRCBUF_STATS_LEVEL
        /* update the stats */
        pthread_mutex_lock(&cbuf->stats_mutex);
        cbuf->stats.bytes_read += block_size;
#if SKCIRCBUF_STATS_LEVEL >= SKCIRCBUF_STATS_DO_TIMES
        /* cbuf->stats.last_read = sktimeNow(); */
        if (0 == cbuf->stats.first_read) {
            cbuf->stats.first_read = cbuf->stats.last_read;
        }
#endif  /* SKCIRCBUF_STATS_LEVEL >= SKCIRCBUF_STATS_DO_TIMES */
        pthread_mutex_unlock(&cbuf->stats_mutex);
#endif  /* SKCIRCBUF_STATS_LEVEL */

        /* when the circbuf is blocked waiting for space to free,
         * signal the writer. */
        if (cbuf->full) {
            cbuf->full = 0;
            pthread_cond_broadcast(&cbuf->cond);
        }

        /* release the block according to the type of circbuf */
        circbuf_mem_read_block_release(cbuf);
    }
    if (NULL == block) {
        /* this was a call to sk_circbuf_release_read_block() */
        if (cbuf->stopped) {
            pthread_cond_broadcast(&cbuf->cond);
        }
        if (cbuf->reader_block) {
            cbuf->reader_block = NULL;
            return SK_CIRCBUF_OK;
        }
        return SK_CIRCBUF_ERR_HAS_NO_BLOCK;
    }

    assert(size);

    /* get a block according to the type of circbuf */
    rv = circbuf_mem_read_block_get(cbuf, no_wait);
    if (0 != rv) {
        cbuf->reader_block = NULL;
        return rv;
    }

    if (cbuf->fixed_item_size) {
        *block = (uint8_t*)cbuf->reader_block;
        *size = cbuf->fixed_item_size;
    } else {
        *block = (uint8_t*)cbuf->reader_block->data;
        *size = cbuf->reader_block->block_size;
    }

    return SK_CIRCBUF_OK;
}


/**
 *    When 'v_block' is NULL, release the current reader_block if
 *    'cbuf' is currently holding one for the reader and return.
 *    Return SK_CIRCBUF_ERR_HAS_NO_BLOCK if 'cbuf' did not have a
 *    block; return SK_CIRCBUF_OK otherwise.
 *
 *    When 'v_block' is not NULL, release the current reader_block, if
 *    any, and get a new block of data for the reader to process.
 *    Store the location of the data in the memory location referenced
 *    by 'v_block' and store the number of octets in the block in the
 *    location referenced by 'size'.
 *
 *    This is a wrapper over circbuf_mem_write_block_commit().
 *
 *    This is a helper function for the public function
 *    sk_circbuf_commit_write_block().
 */
static int
circbuf_write_block_commit(
    sk_circbuf_t       *cbuf,
    size_t              size)
{
    assert(cbuf);

    TRACE_FUNC;

    if (!cbuf->writer_block) {
        return SK_CIRCBUF_ERR_HAS_NO_BLOCK;
    }

    if (cbuf->fixed_item_size) {
        size = cbuf->fixed_item_size;
        cbuf->total_used -= cbuf->fixed_item_size;
    } else {
        if (cbuf->writer_block->block_size < size) {
            return SK_CIRCBUF_ERR_BLOCK_TOO_LARGE;
        }

        /* for now, reduce total_used by the block size */
        assert(cbuf->total_used >= cbuf->writer_block->block_size);
        cbuf->total_used -= cbuf->writer_block->block_size;
    }

    if (size == 0) {
        cbuf->writer_block = NULL;
    } else {
#if SKCIRCBUF_STATS_LEVEL
        /* update stats */
        pthread_mutex_lock(&cbuf->stats_mutex);
        cbuf->stats.bytes_written += size;
#if SKCIRCBUF_STATS_LEVEL >= SKCIRCBUF_STATS_DO_TIMES
        cbuf->stats.last_write = sktimeNow();
        if (0 == cbuf->stats.first_write) {
            cbuf->stats.first_write = cbuf->stats.last_write;
        }
#endif  /* SKCIRCBUF_STATS_LEVEL >= SKCIRCBUF_STATS_DO_TIMES */
        pthread_mutex_unlock(&cbuf->stats_mutex);
#endif  /* SKCIRCBUF_STATS_LEVEL */

        /* update total_used with actual size */
        cbuf->total_used += size;

        /* update the type-specific information */
        circbuf_mem_write_block_commit(cbuf, size);
        cbuf->writer_block = NULL;

        /* when the circbuf is blocked waiting for data, signal the
         * reader. */
        if (cbuf->empty) {
            cbuf->empty = 0;
            pthread_cond_broadcast(&cbuf->cond);
        }
    }

    /* handle stopping condition */
    if (cbuf->stopped) {
        pthread_cond_broadcast(&cbuf->cond);
        return SK_CIRCBUF_ERR_STOPPED;
    }

    return SK_CIRCBUF_OK;
}


/**
 *    Find space in sk_circbuf_t for at least 'size' octets of
 *    available space for the writer thread to use.  Store the location
 *    of that space in the memory location referenced by 'v_block' and
 *    store the number of octets in the block in the location
 *    referenced by 'size'.  If size is 0, set the location referenced
 *    by 'v_block' to NULL.
 *
 *    Return SK_CIRCBUF_ERR_UNCOMMITTED_BLOCK if the writer thread
 *    failed to call sk_circbuf_commit_write_block() for the previous
 *    block returned by this function.
 *
 *    This is a wrapper over circbuf_mem_write_block_get().
 *
 *    This is a helper function for the public function
 *    sk_circbuf_get_write_block().
 */
static int
circbuf_write_block_get(
    sk_circbuf_t       *cbuf,
    void               *v_block,
    size_t             *size,
    int                 no_wait)
{
    uint8_t **block = (uint8_t**)v_block;
    int rv = -1;

    assert(cbuf);
    assert(block);
    assert(size);

    /* ensure upstream released the previous block */
    if (cbuf->writer_block) {
        /* FIXME: do we want to provide some way to auto-commit the
         * previous block using the complete size the caller
         * requested? */
        return SK_CIRCBUF_ERR_UNCOMMITTED_BLOCK;
    }
    /* set block to NULL and return 0 if requested size is 0 */
    if (0 == *size) {
        *block = NULL;
        return SK_CIRCBUF_OK;
    }

    /* get the block according to the type of circbuf */
    rv = circbuf_mem_write_block_get(cbuf, *size, no_wait);
    if (0 != rv) {
        return rv;
    }

    if (cbuf->fixed_item_size) {
        *block = (uint8_t*)cbuf->writer_block;
        *size = cbuf->fixed_item_size;
    } else {
        *block = (uint8_t*)cbuf->writer_block->data;
        *size = cbuf->writer_block->block_size;
    }

    cbuf->total_used += *size;
    return SK_CIRCBUF_OK;
}


/**
 *    Stop the circular buffer and wait for all functions to complete
 *    and the reader and writer to return their current blocks.
 *
 *    Helper function for sk_circbuf_stop() and sk_circbuf_destroy().
 */
static void
circbuf_stop_helper(
    sk_circbuf_t       *cbuf)
{
    cbuf->stopped = 1;
    pthread_cond_broadcast(&cbuf->cond);
    while (cbuf->wait_count) {
        /*
         *  FIXME: we probably want to modify this to be
         *
         *  (cbuf->wait_count || cbuf->writer_block || cbuf->reader_block)
         *
         *  to ensure that all blocks are accounted for.  However,
         *  that does not work in the current usage of sk_circbuf_t
         *  where the writer thread grabs a write location and hangs
         *  to it forever.
         *
         *  In current usage, the incoming data from the network goes
         *  to a temporary buffer and then is copied into the circbuf
         *  corresponding to the appropriate source, so there is no
         *  need for the writer to get a write location until it is
         *  required.
         */

        /* ensure no threads are waiting for data */
        pthread_cond_wait(&cbuf->cond, &cbuf->mutex);
    }
}


/*
 *
 *    The following functions implement the public API for
 *    sk_circbuf_t.  For their documenation, see skcircbuf.h.
 *
 */


int
sk_circbuf_create(
    sk_circbuf_t      **circbuf,
    size_t              chunk_size,
    size_t              max_allocation)
{
    sk_circbuf_t *cbuf;
    int rv;

    cbuf = (sk_circbuf_t*)calloc(1, sizeof(sk_circbuf_t));
    if (NULL == cbuf) {
        return SK_CIRCBUF_ERR_ALLOC;
    }
    cbuf->max_allocation = max_allocation;

    rv = circbuf_mem_initialize(cbuf, chunk_size);
    if (rv) {
        free(cbuf);
        return rv;
    }

    pthread_mutex_init(&cbuf->mutex, NULL);
    pthread_cond_init(&cbuf->cond, NULL);

#if SKCIRCBUF_STATS_LEVEL
    pthread_mutex_init(&cbuf->stats_mutex, NULL);
#endif
#if SKCIRCBUF_STATS_LEVEL >= SKCIRCBUF_STATS_DO_TIMES
    cbuf->stats.creation_time = sktimeNow();
#endif

    *circbuf = cbuf;
    return rv;
}


int
sk_circbuf_create_const_itemsize(
    sk_circbuf_t      **circbuf,
    size_t              item_size,
    size_t              item_count)
{
    sk_circbuf_t *cbuf;
    size_t max_alloc;
    size_t chunk_size;
    size_t num_chunks;
    int rv;

    assert(circbuf);

    if (item_count == 0
        || item_size == 0)
    {
        return SK_CIRCBUF_ERR_BAD_PARAM;
    }

    max_alloc = ~0;
    if (max_alloc / item_size < item_count) {
        return SK_CIRCBUF_ERR_BAD_PARAM;
    }

    /* Add one item since there must be a blank element between the
     * writer-position and reader-position. */
    max_alloc = item_size * (item_count + 1);
    if (max_alloc < SK_CIRCBUF_MEM_MIN_CHUNK_SIZE) {
        chunk_size = max_alloc = SK_CIRCBUF_MEM_MIN_CHUNK_SIZE;
    } else if (max_alloc <= SK_CIRCBUF_MEM_STD_CHUNK_SIZE) {
        chunk_size = max_alloc;
    } else {
        num_chunks = 1 + (max_alloc / SK_CIRCBUF_MEM_STD_CHUNK_SIZE);
        /* add one wasted element to max_alloc for each chunk */
        chunk_size = item_size + max_alloc / num_chunks;
        max_alloc = chunk_size * num_chunks;
    }

    rv = sk_circbuf_create(&cbuf, 1 + chunk_size, max_alloc);
    if (rv) {
        return rv;
    }
    if (cbuf->mem.block_max_size < item_size) {
        sk_circbuf_destroy(cbuf);
        return SK_CIRCBUF_ERR_BAD_PARAM;
    }

    cbuf->fixed_item_size = item_size;
    *circbuf = cbuf;
    return SK_CIRCBUF_OK;
}


int
sk_circbuf_commit_get_write_block(
    sk_circbuf_t       *cbuf,
    size_t              prev_size,
    void               *next_block,
    size_t             *next_size)
{
    int rv;

    pthread_mutex_lock(&cbuf->mutex);
    rv = circbuf_write_block_commit(cbuf, prev_size);
    if (0 == rv) {
        rv = circbuf_write_block_get(cbuf, next_block, next_size, 0);
    }
    pthread_mutex_unlock(&cbuf->mutex);
    return rv;
}


int
sk_circbuf_commit_write_block(
    sk_circbuf_t       *cbuf,
    size_t              size)
{
    int rv;

    pthread_mutex_lock(&cbuf->mutex);
    rv = circbuf_write_block_commit(cbuf, size);
    pthread_mutex_unlock(&cbuf->mutex);
    return rv;
}


void
sk_circbuf_destroy(
    sk_circbuf_t       *cbuf)
{
    if (NULL == cbuf) {
        return;
    }

    pthread_mutex_lock(&cbuf->mutex);
    circbuf_stop_helper(cbuf);

#if SKCIRCBUF_STATS_LEVEL
    pthread_mutex_lock(&cbuf->stats_mutex);
#endif

#if 0
    /* what to do when attempting to destroy circbuf that is still
     * sending data to the reader? */
    while (cbuf->writer_block
           || cbuf->reader_block
           || cbuf->total_used)
    {
        /* this block needs some help... */
        if (NULL == cbuf->stopped_reason) {
            cbuf->stopped_reason = sk_error_create(
                SK_ERROR_DOMAIN_CIRCBUF, SKC_ERR_FORCE_STOP,
                                                   "Destroying circbuf");
        }

        /* maybe need some way to ensure that writer and reader will
         * not attempt to get another block.  maybe we need to
         * discconnect the circbuf before destroying it? */

        /* need some way to have the current reader read be the last
         * one; would like to set total_used to the size of the blocks
         * used by the writer module and reader_module. */
        if (!cbuf->writer_block && !cbuf->reader_block) {
            cbuf->total_used = 0;
            break; /* ?? */
        }
        pthread_cond_broadcast(&cbuf->cond); /* ?? */
        pthread_cond_wait(&cbuf->cond, &cbuf->mutex); /* ?? */
    }
#endif  /* 0 */

    circbuf_mem_destroy(cbuf);

#if SKCIRCBUF_STATS_LEVEL
    pthread_mutex_unlock(&cbuf->stats_mutex);
    pthread_mutex_destroy(&cbuf->stats_mutex);
#endif

    pthread_cond_destroy(&cbuf->cond);
    pthread_mutex_unlock(&cbuf->mutex);
    pthread_mutex_destroy(&cbuf->mutex);

    free(cbuf);
}


int
sk_circbuf_get_read_block(
    sk_circbuf_t       *cbuf,
    void               *block,
    size_t             *size)
{
    int rv;

    assert(cbuf);
    assert(block);
    assert(size);

    pthread_mutex_lock(&cbuf->mutex);
    rv = circbuf_read_block_get(cbuf, block, size, 0);
    pthread_mutex_unlock(&cbuf->mutex);
    return rv;
}


int
sk_circbuf_get_read_block_nowait(
    sk_circbuf_t       *cbuf,
    void               *block,
    size_t             *size)
{
    int rv;

    assert(cbuf);
    assert(block);
    assert(size);

    pthread_mutex_lock(&cbuf->mutex);
    rv = circbuf_read_block_get(cbuf, block, size, 1);
    pthread_mutex_unlock(&cbuf->mutex);
    return rv;
}


uint8_t*
sk_circbuf_get_read_pos(
    sk_circbuf_t       *cbuf)
{
    uint8_t *data;
    size_t size = 0;
    int rv;

    assert(cbuf);
    assert(cbuf->fixed_item_size);

    pthread_mutex_lock(&cbuf->mutex);
    rv = circbuf_read_block_get(cbuf, &data, &size, 0);
    pthread_mutex_unlock(&cbuf->mutex);
    if (rv) {
        return NULL;
    }
    assert(size == cbuf->fixed_item_size);
    return data;
}


int
sk_circbuf_get_write_block(
    sk_circbuf_t       *cbuf,
    void               *block,
    size_t             *size)
{
    int rv;

    pthread_mutex_lock(&cbuf->mutex);
    rv = circbuf_write_block_get(cbuf, block, size, 0);
    pthread_mutex_unlock(&cbuf->mutex);
    return rv;
}


int
sk_circbuf_get_write_block_nowait(
    sk_circbuf_t       *cbuf,
    void               *block,
    size_t             *size)
{
    int rv;

    pthread_mutex_lock(&cbuf->mutex);
    rv = circbuf_write_block_get(cbuf, block, size, 1);
    pthread_mutex_unlock(&cbuf->mutex);
    return rv;
}


uint8_t*
sk_circbuf_get_write_pos(
    sk_circbuf_t       *cbuf)
{
    uint8_t *data;
    size_t size;
    int rv;

    assert(cbuf);
    assert(cbuf->fixed_item_size);
    size = cbuf->fixed_item_size;

    pthread_mutex_lock(&cbuf->mutex);
    if (cbuf->writer_block) {
        rv = circbuf_write_block_commit(cbuf, size);
        if (rv) {
            pthread_mutex_unlock(&cbuf->mutex);
            return NULL;
        }
    }
    rv = circbuf_write_block_get(cbuf, &data, &size, 0);
    pthread_mutex_unlock(&cbuf->mutex);
    if (rv) {
        return NULL;
    }
    assert(size == cbuf->fixed_item_size);
    return data;
}


void
sk_circbuf_print_stats(
    sk_circbuf_t       *cbuf,
    const char         *name,
    sk_msg_fn_t         msg_fn)
{
#if SKCIRCBUF_STATS_LEVEL == SKCIRCBUF_STATS_DO_NONE
    msg_fn("%s[%p]: No stats available",
           ((NULL == name) ? "" : name), (void *)cbuf);
#elif SKCIRCBUF_STATS_LEVEL == SKCIRCBUF_STATS_DO_BYTES

    sk_circbuf_stats_t cbuf_stats;

    pthread_mutex_lock(&cbuf->stats_mutex);
    cbuf_stats = cbuf->stats;
    pthread_mutex_unlock(&cbuf->stats_mutex);

    msg_fn(("%s[%p]: Full count = %" PRIu64 ", Empty count = %" PRIu64
            ", Bytes in = %" PRIu64 ", Bytes out = %" PRIu64),
            ((NULL == name) ? "" : name), cbuf,
           cbuf_stats.full_count, cbuf_stats.empty_count,
           cbuf_stats.bytes_written, cbuf_stats.bytes_read);
#else
    sk_circbuf_stats_t cbuf_stats;
    const int tflags = SKTIMESTAMP_ISO | SKTIMESTAMP_LOCAL;
    char t[12][SKTIMESTAMP_STRLEN];

    pthread_mutex_lock(&cbuf->stats_mutex);
    cbuf_stats = cbuf->stats;
    pthread_mutex_unlock(&cbuf->stats_mutex);

    /* The upstream side uses the write calls, and the downstream side
     * uses the read calls */

    msg_fn(("%s[%p]: Full count = %" PRIu64 ", Empty count = %" PRIu64 "\n"
            "\tBytes in = %" PRIu64 ", Bytes out = %" PRIu64 "\n"
            "\tCreated:       %s\n"
            "\tFirst Write:   %s    First Read:   %s\n"
            "\tFinal Write:   %s    Final Read:   %s\n"
            "\tStopped Write: %s    Stopped Read: %s\n"),
           ((NULL == name) ? "" : name), cbuf,
           cbuf_stats.full_count, cbuf_stats.empty_count,
           cbuf_stats.bytes_written, cbuf_stats.bytes_read,
           sktimestamp_r(t[0], cbuf_stats.creation_time, tflags),
           sktimestamp_r(t[1], cbuf_stats.first_write, tflags),
           sktimestamp_r(t[2], cbuf_stats.first_read, tflags),
           sktimestamp_r(t[3], cbuf_stats.last_write, tflags),
           sktimestamp_r(t[4], cbuf_stats.last_read, tflags),
           sktimestamp_r(t[5], cbuf_stats.stopped_write, tflags),
           sktimestamp_r(t[6], cbuf_stats.stopped_read, tflags));
#endif
}


int
sk_circbuf_release_read_block(
    sk_circbuf_t       *cbuf)
{
    int rv;

    assert(cbuf);

    pthread_mutex_lock(&cbuf->mutex);
    rv = circbuf_read_block_get(cbuf, NULL, NULL, 0);
    pthread_mutex_unlock(&cbuf->mutex);
    return rv;
}


const char*
sk_circbuf_strerror(
    int                 err_code)
{
    static char errbuf[PATH_MAX];

    switch ((sk_circbuf_status_t)err_code) {
      case SK_CIRCBUF_OK:
        return ("Success");
      case SK_CIRCBUF_ERR_ALLOC:
        return ("Memory allocation error");
      case SK_CIRCBUF_ERR_BAD_PARAM:
        return ("Bad parameter to function");
      case SK_CIRCBUF_ERR_STOPPED:
        return ("No more data");
      case SK_CIRCBUF_ERR_WOULD_BLOCK:
        return ("Request would block");
      case SK_CIRCBUF_ERR_BLOCK_TOO_LARGE:
        return ("Block size is too large in writer request/commit");
      case SK_CIRCBUF_ERR_HAS_NO_BLOCK:
        return ("Do not have a block to release/commit");
      case SK_CIRCBUF_ERR_UNCOMMITTED_BLOCK:
        return ("Failed to commit current write block");
    }

    snprintf(errbuf, sizeof(errbuf),
             "Unrecognized sk_circbuf_status_t error code %d", err_code);
    return errbuf;
}


void
sk_circbuf_stop(
    sk_circbuf_t       *cbuf)
{
    pthread_mutex_lock(&cbuf->mutex);
    circbuf_stop_helper(cbuf);
    pthread_mutex_unlock(&cbuf->mutex);
}


void
sk_circbuf_stop_writing(
    sk_circbuf_t       *cbuf)
{
    pthread_mutex_lock(&cbuf->mutex);
    cbuf->writer_stopped = 1;
    pthread_cond_broadcast(&cbuf->cond);
    pthread_mutex_unlock(&cbuf->mutex);
}


/*
** Local Variables:
** mode:c
** indent-tabs-mode:nil
** c-basic-offset:4
** End:
*/
