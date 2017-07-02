/*
** Copyright (C) 2004-2017 by Carnegie Mellon University.
**
** @OPENSOURCE_LICENSE_START@
** See license information in ../../LICENSE.txt
** @OPENSOURCE_LICENSE_END@
*/
#ifndef _STREAM_CACHE_H
#define _STREAM_CACHE_H
#ifdef __cplusplus
extern "C" {
#endif

#include <silk/silk.h>

RCSIDENTVAR(rcsID_STREAM_CACHE_H, "$SiLK: stream-cache.h efd886457770 2017-06-21 18:43:23Z mthomas $");

#include <silk/skthread.h>
#include <silk/skstream.h>
#include <silk/skvector.h>

/*
**  stream-cache.h
**
**    A simple interface for maintaining a list of open file handles
**    so we can avoid a lot of open/close cycles.  File handles are
**    indexed by the timestamp of the file, the sensor_id, and the
**    flowtype (class/type) of the data they contain.
**
**    Files have individual locks (mutexes) associated with them to
**    prevent multiple threads from writing to the same stream.  In
**    addition, the entire cache is locked whenever it is modified.
*/


/**
 *    Smallest maximum cache size.  Code that handles removing items
 *    from end of list assumes at least two entries in the list.
 */
#define STREAM_CACHE_MINIMUM_SIZE 2


/**
 *     The stream cache object.
 */
struct stream_cache_st;
typedef struct stream_cache_st stream_cache_t;


/**
 *    The cache_entry_t contains information about an active file in
 *    the stream cache.  The structure contains the file key, the
 *    number of records in the file, and the open file handle.
 *
 *    Users of the stream-cache should view the cache_entry_t as
 *    opaque.  Use the macros and functions to access its members.
 */
typedef struct cache_entry_st cache_entry_t;
struct cache_entry_st {
    /** The mutex associated with this entry */
    pthread_mutex_t     mutex;
    /** the key */
    sksite_repo_key_t   key;
    /** the number of records written to the file since it was added
     * to the cache */
    uint64_t            total_rec_count;
    /** the number of records in the file when it was opened */
    uint64_t            opened_rec_count;
    /** when this entry was last accessed */
    sktime_t            last_accessed;
    /** the name of the file */
    const char         *filename;
    /** the open file handle */
    skstream_t         *stream;
};
/* cache_entry_t */


/**
 *    The cache_closed_file_t contains information about a file that
 *    previously existed in the stream cache.  The structure contains
 *    the file's key, name, and the number of records written to the file
 *    since it was added to the cache.
 *
 *    A vector of pointers to these structures may be returned by
 *    skCacheCloseAll().  The caller is responsible for freeing these
 *    structures, either by calling cache_closed_file_destroy() or by
 *    calling free() on the 'filename' member and then free() on the
 *    structure itself.
 *
 *    Since the caller owns the structures returned by
 *    skCacheCloseAll(), she is free to manipulate them as she wishes.
 */
typedef struct cache_closed_file_st cache_closed_file_t;
struct cache_closed_file_st {
    /* the key for this closed file */
    sksite_repo_key_t   key;
    /* the number of records in the file as of opening or the most
     * recent flush, used for log messages */
    uint64_t            rec_count;
    /* the name of the file */
    const char         *filename;
};
/*  cache_closed_file_t */


/**
 *    Destroy the cache_closed_file_t argument.
 */
void
cache_closed_file_destroy(
    cache_closed_file_t    *closed);


/**
 *  stream = cache_open_fn_t(key, caller_data);
 *
 *    A callback function with this signature must be registered as
 *    the 'open_callback' parameter to to skCacheCreate() function.
 *
 *    This function is used by skCacheLookupOrOpenAdd() when the
 *    stream associated with 'key' is not in the cache.  This function
 *    should open an existing file or create a new file as
 *    appriopriate.  The 'caller_data' is for the caller to use as she
 *    sees fit.  The stream cache does nothing with this value.
 *
 *    This function should return NULL if there was an error opening
 *    the file.
 */
typedef skstream_t *
(*cache_open_fn_t)(
    const sksite_repo_key_t    *key,
    void                       *caller_data);


/**
 *  status = skCacheCloseAll(cache, vector);
 *
 *    Close all open streams in the cache and remove all knowledge of
 *    opened and closed streams from the cache.
 *
 *    If the 'vector' parameter is not null, an sk_vector_t of
 *    pointers is created.  Each pointer references a
 *    'cache_closed_file_t' structure that contains the file's name
 *    and the number of records that were written to the stream while
 *    the cache owned the stream.  The address of the newly created
 *    vector is put into 'vector'.  The caller should call
 *    cache_closed_file_destroy() on each entry in the vector and then
 *    skVectorDestroy() on the vector itself.
 *
 *    If 'vector' is NULL, all record of the streams handled by the
 *    stream cache is lost.
 *
 *    Return zero if all streams were successfully flushed and closed.
 *    Return -1 if calling the skStreamClose() function for any stream
 *    returns non-zero, though all streams will still be closed and
 *    destroyed.
 */
int
skCacheCloseAll(
    stream_cache_t     *cache,
    sk_vector_t       **vector);


/**
 *  cache = skCacheCreate(max_open_count, open_callback);
 *
 *    Create a stream_cache capable of keeping 'max_open_count' files
 *    open.  The 'open_callback' is the function that the stream_cache
 *    will invoke when skCacheLookupOrOpenAdd() is called on a key
 *    that the stream cache has not seen before.
 *
 *    Returns NULL if memory cannot be allocated.
 */
stream_cache_t*
skCacheCreate(
    int                 max_open_count,
    cache_open_fn_t     open_callback);


/**
 *  status = skCacheDestroy(cache);
 *
 *    Close all streams and free all memory associated with the
 *    streams.  Free the memory associated with the cache.  The cache
 *    pointer is invalid after a call to this function.
 *
 *    As part of its processing, this function calls
 *    skCacheCloseAll(), and that function's return value is the
 *    return value of this function.
 */
int
skCacheDestroy(
    stream_cache_t     *cache);


/**
 *  stream = skCacheEntryGetStream(entry);
 *
 *    Returns the stream associated with a stream entry.
 */
#define skCacheEntryGetStream(entry) ((entry)->stream)


/**
 *  skCacheEntryRelease(entry);
 *
 *    Releases (unlocks) a stream entry.
 */
#define skCacheEntryRelease(entry)  MUTEX_UNLOCK(&(entry)->mutex)


/**
 *  status = skCacheLookupOrOpenAdd(cache, key, caller_data, &entry);
 *
 *    Fill 'entry' with the stream cache entry whose key is 'key'.
 *    The entry is returned in a locked state.  The caller should call
 *    skCacheEntryRelease() to unlock the entry once processing is
 *    complete.
 *
 *    Specifically, if a stream entry associated with 'key' already
 *    exists in the cache and if the stream is open, lock the entry,
 *    set 'entry' to the entry's location and return 0.
 *
 *    If the stream entry exists in the cache but the stream is
 *    closed, then open the stream, lock the entry, fill 'entry' with
 *    its location, and return 0.
 *
 *    If there is no entry with 'key' in the cache, call the
 *    'open_callback' function that was registered with
 *    skCacheCreate().  The arguments to that function are the 'key'
 *    and the 'caller_data' specified to this function.  If the
 *    'open_callback' returns NULL, this function returns -1.
 *
 *    Otherwise, create use the stream returned by the 'open_callback'
 *    to create an entry keyed by 'key'.  Lock the entry, put it into
 *    the locatation pointed at by 'entry', and return 0.
 *
 *    After this call, the cache owns the stream returned by
 *    'open_callback' and will free it when the cache is full or when
 *    skCacheCloseAll() or skCacheDestroy() is called.
 *
 *    If the stream cache is at the max_open_count when a new stream
 *    is inserted or an existing entry is re-opened, the least
 *    recently opened stream will be closed.
 */
int
skCacheLookupOrOpenAdd(
    stream_cache_t             *cache,
    const sksite_repo_key_t    *key,
    void                       *caller_data,
    cache_entry_t             **entry);


#ifdef __cplusplus
}
#endif
#endif /* _STREAM_CACHE_H */

/*
** Local Variables:
** mode:c
** indent-tabs-mode:nil
** c-basic-offset:4
** End:
*/
