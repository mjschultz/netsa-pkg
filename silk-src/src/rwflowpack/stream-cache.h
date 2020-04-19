/*
** Copyright (C) 2004-2020 by Carnegie Mellon University.
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

RCSIDENTVAR(rcsID_STREAM_CACHE_H, "$SiLK: stream-cache.h ef14e54179be 2020-04-14 21:57:45Z mthomas $");

#include <silk/skthread.h>
#include <silk/skstream.h>

/*
 *  stream-cache.h
 *
 *    A simple interface for maintaining a list of open file handles
 *    so we can avoid a lot of open/close cycles.  File handles are
 *    indexed by the timestamp of the file, the sensor_id, and the
 *    flowtype (class/type) of the data they contain.
 *
 *    Files have individual locks (mutexes) associated with them to
 *    prevent multiple threads from writing to the same stream.  In
 *    addition, the entire cache is locked whenever it is modified.
 */


/**
 *    Smallest maximum cache size.  Code that handles removing items
 *    from end of list assumes at least two entries in the list.
 */
#define STREAM_CACHE_MINIMUM_SIZE 2

/**
 *    When skStreamFlush() is called, streams that have not been
 *    written to in the last STREAM_CACHE_INACTIVE_TIMEOUT
 *    milliseconds are closed and removed from the cache.
 *
 *    TODO: Change this to a multiple of the flush timeout.
 */
#define STREAM_CACHE_INACTIVE_TIMEOUT  (5 * 60 * 1000)


/**
 *     The stream cache object.
 */
typedef struct stream_cache_st stream_cache_t;


/**
 *    The cache_entry_t contains information about an active file in
 *    the stream cache.  Calling skCacheLookupOrOpenAdd() fills a
 *    cache_entry_t object.  The caller has a lock on that stream
 *    until skCacheEntryRelease() is called.  The caller may use
 *    skCacheEntryGetStream() to get the stream object from the entry.
 */
typedef struct cache_entry_st cache_entry_t;


/**
 *    cache_file_iter_t is an iterator over the files that have been
 *    returned by skCacheLookupOrOpenAdd() since the previous call to
 *    skCacheCloseAll() or skCacheFlush().
 *
 *    The caller may request an iterator by providing a non-null value
 *    as the second argument to skCacheFlush() and skCacheCloseAll().
 *
 *    The iterator allows the caller to report the number of records
 *    written to each file since the file was opened or the previous
 *    flush, whichever is more recent.
 */
typedef struct cache_file_iter_st cache_file_iter_t;


/**
 *    cache_key_t is used as the key to the stream.  The caller fills
 *    this structure and passes it to skCacheLookupOrOpenAdd().
 */
struct cache_key_st {
    /* the hour that this file is for */
    sktime_t            time_stamp;
    /* the sensor that this file is for */
    sk_sensor_id_t      sensor_id;
    /* the flowtype (class/type) that this file is for */
    sk_flowtype_id_t    flowtype_id;
};
typedef struct cache_key_st cache_key_t;


/**
 *  stream = cache_open_fn_t(key, caller_data, filename);
 *
 *    A callback function with this signature must be registered as
 *    the 'open_callback' parameter to to skCacheCreate() function.
 *
 *    This function is used by skCacheLookupOrOpenAdd() when the
 *    stream associated with 'key' is either not in the cache or not
 *    currently open.  If the file represented by 'key' has not been
 *    handled by the cache recently, the 'filename' parameter is NULL.
 *    If the file having 'key' is known to the cache but is currently
 *    closed, the 'filename' parameter is the complete path that was
 *    used the previous time this callback was invoked.
 *
 *    This callback function should open an existing file or create a
 *    new file as appriopriate.  The 'caller_data' parameter here is
 *    the same 'caller_data' parameter to skCacheLookupOrOpenAdd() and
 *    it is for the caller's use as she sees fit.  The stream cache
 *    does nothing with this value.
 *
 *    This callback function should return NULL if there was an error
 *    opening the file.
 */
typedef skstream_t *
(*cache_open_fn_t)(
    const cache_key_t  *key,
    void               *caller_data,
    const char         *filename);


/**
 *    Close all the streams in the cache and remove them from the
 *    cache.
 *
 *    Return 0 if all streams were successfully flushed and closed.
 *    Return -1 if calling skStreamClose() for any stream returns
 *    non-zero, though all streams will still be closed and destroyed.
 *
 *    When 'file_iter' is non-NULL, its referent is set to a newly
 *    created iterator.  The iterator may be used to see the files
 *    which were accessed, and the number of records written, since
 *    the previous call to skCacheCloseAll() or skCacheFlush().
 *
 *    The caller must use skCacheFileIterDestroy() to destroy the
 *    iterator.
 */
int
skCacheCloseAll(
    stream_cache_t     *cache,
    cache_file_iter_t **iter);


/**
 *    Create a stream cache capable of keeping 'max_open_count' files
 *    open.  The 'open_callback' is the function that the stream cache
 *    invokes when skCacheLookupOrOpenAdd() is called on a key that
 *    either the cache has not seen or is that is currently closed.
 *
 *    Returns NULL if memory cannot be allocated, if the
 *    'open_callback' is NULL, or if 'max_open_count' is less than
 *    STREAM_CACHE_MINIMUM_SIZE.
 */
stream_cache_t *
skCacheCreate(
    size_t              max_open_count,
    cache_open_fn_t     open_callback);


/**
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
 *    Return the stream associated with a cache entry.  A locked cache
 *    entry is obtained by calling skCacheLookupOrOpenAdd().
 */
skstream_t *
skCacheEntryGetStream(
    const cache_entry_t    *entry);


/**
 *    Release (unlock) a cache entry.  A locked cache entry is
 *    obtained by calling skCacheLookupOrOpenAdd().
 */
void
skCacheEntryRelease(
    cache_entry_t  *entry);


/**
 *    Return the number of entries in the iterator returned by
 *    skCacheCloseAll() or skCacheFlush().
 */
size_t
skCacheFileIterCountEntries(
    const cache_file_iter_t    *iter);


/**
 *    Destroy an iterator returned by skCacheCloseAll() or
 *    skCacheFlush().
 */
void
skCacheFileIterDestroy(
    cache_file_iter_t  *iter);


/**
 *    Move the iterator returned by skCacheCloseAll() or
 *    skCacheFlush() to the next (or first) entry, set the referent of
 *    'filename' to the complete pathname of the file, set the
 *    referent of 'record_count' to the number of records written to
 *    the file since the previous call to skCacheCloseAll() or
 *    skCacheFlush(), and return SK_ITERATOR_OK.
 *
 *    If no more files are in the iterator, leave the 'filename' and
 *    'record_count' values unchanged and return
 *    SK_ITERATOR_NO_MORE_ENTRIES.
 */
int
skCacheFileIterNext(
    cache_file_iter_t  *iter,
    const char        **filename,
    uint64_t           *record_count);


/**
 *    Flush all the streams in the cache.  Remove entries from the
 *    cache that have not been accessed in the previous
 *    STREAM_CACHE_INACTIVE_TIMEOUT milliseconds.
 *
 *    Return 0 if all streams were successfully flushed.  Return -1 on
 *    memory allocation error or if calling skStreamFlush() for any
 *    stream returns non-zero, though all streams will still be
 *    flushed.
 *
 *    The 'file_iter' argument must be non-NULL.  Its referent is set
 *    to a newly created iterator.  The iterator may be used to see
 *    the files which were accessed, and the number of records
 *    written, since the previous call to skCacheFlush() or
 *    skCacheCloseAll().
 *
 *    The caller must use skCacheFileIterDestroy() to destroy the
 *    iterator.
 */
int
skCacheFlush(
    stream_cache_t     *cache,
    cache_file_iter_t **file_iter);


/**
 *    Fill 'entry' with the stream cache entry whose key is 'key'.
 *    The entry is returned in a locked state.  The caller must call
 *    skCacheEntryRelease() to unlock the entry once processing is
 *    complete.
 *
 *    Specifically, if a stream entry associated with 'key' already
 *    exists in the cache and if the stream is open, lock the entry,
 *    set 'entry' to the entry's location. and return 0.
 *
 *    If there is no entry with 'key' in the cache, call the
 *    'open_callback' function (having signature cache_open_fn_t) that
 *    was registered with skCacheCreate().  The arguments to the
 *    callback are the 'key' and the 'caller_data' specified to this
 *    function and NULL as the third parameter.  Once the callback
 *    returns, put the 'key' and the stream returned by the callback
 *    in a new stream cache entry, lock the entry, set 'entry' to
 *    location, and return 0.
 *
 *    If a stream entry exists in the cache but the stream is closed,
 *    call the 'open_callback' function with the 'key', the
 *    'caller_data', and the full path to the file that was returned
 *    the previous time the open_callback was invoked.  Once the
 *    callback returns, lock the entry, fill 'entry' with its
 *    location, and return 0.
 *
 *    If the 'open_callback' returns NULL, this function returns -1.
 *
 *    After a call to this function, the cache owns the stream
 *    returned by 'open_callback' and frees it when the cache is
 *    full or when skCacheCloseAll() or skCacheDestroy() is called.
 *
 *    If the stream cache is at the max_open_count when a new stream
 *    is inserted or an existing entry is re-opened, the least
 *    recently opened stream is closed.
 */
int
skCacheLookupOrOpenAdd(
    stream_cache_t     *cache,
    const cache_key_t  *key,
    void               *caller_data,
    cache_entry_t     **entry);


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
