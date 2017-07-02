/*
** Copyright (C) 2004-2017 by Carnegie Mellon University.
**
** @OPENSOURCE_LICENSE_START@
** See license information in ../../LICENSE.txt
** @OPENSOURCE_LICENSE_END@
*/

/* #define SKTHREAD_DEBUG_MUTEX 1 */
#include <silk/silk.h>

RCSIDENT("$SiLK: stream-cache.c efd886457770 2017-06-21 18:43:23Z mthomas $");

#include <silk/utils.h>
#include <silk/sklog.h>
#include <silk/sksite.h>
#include <silk/redblack.h>
#include "stream-cache.h"

/* use TRACEMSG_LEVEL as our tracing variable */
#define TRACEMSG(lvl, msg) TRACEMSG_TO_TRACEMSGLVL(lvl, msg)
#include <silk/sktracemsg.h>


/* DEFINES AND TYPEDEFS */

/* Maximum time stamp */
#define MAX_TIME  (sktime_t)INT64_MAX

/* Message to print when fail to initialize mutex */
#define FMT_MUTEX_FAILURE \
    "Could not initialize a mutex at %s:%d", __FILE__, __LINE__

/*
 *  The stream_cache_t contains an array of cache_entry_t objects.
 *  The array is allocated with 'max_open_size' entries when the cache is
 *  created.  The number of valid entries in the array is specified by
 *  'size'.  The cache also contains a redblack tree that is used to
 *  index the entries.  Finally, there is a mutex on the cache; the
 *  mutex will be a pthread_rwlock_t mutex if read/write locks are
 *  supported on this system.
 */
struct stream_cache_st {
    /* the redblack tree of entries, both opened and closed */
    struct rbtree      *rbtree;
    /* function called by skCacheLookupOrOpenAdd() to open a file */
    cache_open_fn_t     open_callback;
    /* current number of open entries */
    unsigned int        open_count;
    /* maximum number of open entries the user specified */
    unsigned int        max_open_size;
    /* total number of entries (open and closed) */
    unsigned int        total_count;
    /* mutex for the cache */
    RWMUTEX             mutex;
};


/* LOCAL FUNCTION DECLARATIONS */



/* FUNCTION DEFINITIONS */

/*  Destroy a cache_closed_file_t */
void
cache_closed_file_destroy(
    cache_closed_file_t    *closed)
{
    if (closed) {
        if (closed->filename) {
            free((void*)closed->filename);
        }
        free(closed);
    }
}


/**
 *    Close the stream that 'entry' wraps and destroy the stream.  In
 *    addition, update the entry's 'total_rec_count'.
 *
 *    This function expects the caller to have the entry's mutex.
 *
 *    The entry's stream must be open.
 *
 *    Return the result of calling skStreamClose().  Log an error
 *    message if skStreamClose() fails.
 */
static int
cache_entry_close(
    cache_entry_t      *entry)
{
    uint64_t new_count;
    int rv;

    assert(entry);
    assert(entry->stream);
    ASSERT_MUTEX_LOCKED(&entry->mutex);

    TRACEMSG(2, ("cache: Closing file '%s'", entry->filename));

    /* update the record count */
    new_count = skStreamGetRecordCount(entry->stream);
    assert(entry->opened_rec_count <= new_count);
    entry->total_rec_count += new_count - entry->opened_rec_count;

    /* close the stream */
    rv = skStreamClose(entry->stream);
    if (rv) {
        skStreamPrintLastErr(entry->stream, rv, &NOTICEMSG);
    }
    skStreamDestroy(&entry->stream);

    return rv;
}


/**
 *  direction = cache_entry_compare(a, b, config);
 *
 *    The comparison function used by the redblack tree.
 */
static int
cache_entry_compare(
    const void         *entry1_v,
    const void         *entry2_v,
    const void  UNUSED(*config))
{
    sksite_repo_key_t key1 = ((cache_entry_t*)entry1_v)->key;
    sksite_repo_key_t key2 = ((cache_entry_t*)entry2_v)->key;

    if (key1.sensor_id < key2.sensor_id) {
        return -1;
    }
    if (key1.sensor_id > key2.sensor_id) {
        return 1;
    }
    if (key1.flowtype_id < key2.flowtype_id) {
        return -1;
    }
    if (key1.flowtype_id > key2.flowtype_id) {
        return 1;
    }
    if (key1.timestamp < key2.timestamp) {
        return -1;
    }
    if (key1.timestamp > key2.timestamp) {
        return 1;
    }
    return 0;
}


/**
 *    Create a new cache entry.
 */
static cache_entry_t*
cache_entry_create(
    void)
{
    cache_entry_t *entry;

    entry = (cache_entry_t*)calloc(1, sizeof(cache_entry_t));
    if (NULL == entry) {
        skAppPrintOutOfMemory(NULL);
        return NULL;
    }
    if (MUTEX_INIT(&entry->mutex)) {
        CRITMSG(FMT_MUTEX_FAILURE);
        free(entry);
        return NULL;
    }
    MUTEX_LOCK(&entry->mutex);
    entry->last_accessed = MAX_TIME;
    return entry;
}


/**
 *    Close the stream associated with the cache_entry_t 'entry' if it
 *    is open and destroy the 'entry'.  Does not remove 'entry' from
 *    the red-black tree.  This function assumes the caller holds the
 *    entry's mutex.  This is a no-op if 'entry' is NULL.
 *
 *    Return 0 if stream is closed or the result of skStreamClose().
 */
static int
cache_entry_destroy(
    cache_entry_t      *entry)
{
    int rv = 0;

    if (entry) {
        ASSERT_MUTEX_LOCKED(&entry->mutex);

        if (entry->stream) {
            rv = cache_entry_close(entry);
        }
        if (entry->filename) {
            free((void*)entry->filename);
        }
        MUTEX_UNLOCK(&entry->mutex);
        MUTEX_DESTROY(&entry->mutex);
        free(entry);
    }
    return rv;
}


/**
 *  entry = cache_entry_lookup(cache, key);
 *
 *    Return the stream entry for the specified key.  Return NULL if
 *    no stream entry for the specified values is found.  The cache
 *    should be locked for reading or writing.  The entry is returned
 *    in a locked state.
 */
static cache_entry_t*
cache_entry_lookup(
    stream_cache_t             *cache,
    const sksite_repo_key_t    *key)
{
    cache_entry_t *entry;
    cache_entry_t search_key;

    ASSERT_RW_MUTEX_LOCKED(&cache->mutex);

    /* fill the search key */
    search_key.key.timestamp = key->timestamp;
    search_key.key.sensor_id = key->sensor_id;
    search_key.key.flowtype_id = key->flowtype_id;

    /* try to find the entry */
    entry = (cache_entry_t*)rbfind(&search_key, cache->rbtree);
#if TRACEMSG_LEVEL >= 3
    {
        char tstamp[SKTIMESTAMP_STRLEN];
        char sensor[SK_MAX_STRLEN_SENSOR+1];
        char flowtype[SK_MAX_STRLEN_FLOWTYPE+1];

        sktimestamp_r(tstamp, key->timestamp, SKTIMESTAMP_NOMSEC),
        sksiteSensorGetName(sensor, sizeof(sensor), key->sensor_id);
        sksiteFlowtypeGetName(flowtype, sizeof(flowtype), key->flowtype_id);

        TRACEMSG(3, ("Cache %s for stream %s %s %s",
                     ((entry == NULL) ? "miss" : "hit"),
                     tstamp, sensor, flowtype));
    }
#endif /* TRACEMSG_LEVEL */

    /* lock it if we find it */
    if (entry) {
        MUTEX_LOCK(&entry->mutex);
    }

    return entry;
}


/**
 *    Create a cache_closed_file_t structure from the existing
 *    cache_entry_t and destroy the cache_entry_t.
 *
 *    If unable to create a cache_closed_file_t, return NULL, but
 *    destroy the cache_entry_t regardless.
 */
static cache_closed_file_t *
cache_entry_to_closed_file(
    cache_entry_t      *entry,
    int                *status)
{
    cache_closed_file_t *closed;

    ASSERT_MUTEX_LOCKED(&entry->mutex);

    closed = (cache_closed_file_t*)calloc(1, sizeof(cache_closed_file_t));
    if (NULL == closed) {
        skAppPrintOutOfMemory(NULL);
    } else {
        /* close stream first to ensure record count is correct */
        if (entry->stream) {
            *status = cache_entry_close(entry);
        }
        closed->key = entry->key;
        closed->rec_count = entry->total_rec_count;
        closed->filename = entry->filename;
        entry->filename = NULL;
    }
    cache_entry_destroy(entry);
    return closed;
}


int
skCacheCloseAll(
    stream_cache_t     *cache,
    sk_vector_t       **out_vector)
{
    sk_vector_t *vector;
    RBLIST *iter;
    cache_entry_t *entry;
    cache_closed_file_t *closed;
    int retval;

    retval = 0;

    if (NULL == out_vector) {
        vector = NULL;
    } else {
        vector = skVectorNew(sizeof(cache_closed_file_t*));
        *out_vector = vector;
    }

    WRITE_LOCK(&cache->mutex);

    if (0 == cache->total_count) {
        RW_MUTEX_UNLOCK(&cache->mutex);
        return 0;
    }

    TRACEMSG(1, ("cache: Closing cache with %u open and %u closed entries",
                 cache->open_count, cache->total_count - cache->open_count));

    /*
     *  Consider changing this code to get a local handle to the
     *  existing rbtree, create a new rbtree on the cache, unlocking
     *  the cache, and having the remainder of this function work on
     *  the local handle.  Doing this would cause other threads to be
     *  blocked only during the time this thread swaps the trees.
     *  However, it may allow the number of open file handles to
     *  exceed the maximum briefly if new files are opened while the
     *  old files are still being closed.
     */

    iter = rbopenlist(cache->rbtree);
    if (NULL == vector) {
        /* destroy all entries */
        while ((entry = (cache_entry_t*)rbreadlist(iter)) != NULL) {
            MUTEX_LOCK(&entry->mutex);
            if (cache_entry_destroy(entry)) {
                retval = -1;
            }
        }
    } else {
        /* move all entries from the rbtree to the vector, closing any
         * that are open */
        int rv = 0;

        skVectorSetCapacity(vector, cache->total_count);
        while ((entry = (cache_entry_t*)rbreadlist(iter)) != NULL) {
            MUTEX_LOCK(&entry->mutex);
            closed = cache_entry_to_closed_file(entry, &rv);
            if (closed) {
                skVectorAppendValue(vector, &closed);
            }
            if (rv) {
                retval = -1;
                rv = 0;
            }
        }
    }
    rbcloselist(iter);

    /* destroy and re-create the red-black tree */
    rbdestroy(cache->rbtree);
    cache->rbtree = rbinit(&cache_entry_compare, NULL);
    if (cache->rbtree == NULL) {
        skAppPrintOutOfMemory(NULL);
        RW_MUTEX_UNLOCK(&cache->mutex);
        skAbort();
    }

    cache->open_count = 0;
    cache->total_count = 0;

    RW_MUTEX_UNLOCK(&cache->mutex);

    return retval;
}


/* create a cache with the specified size and open callback function */
stream_cache_t*
skCacheCreate(
    int                 max_open_size,
    cache_open_fn_t     open_fn)
{
    stream_cache_t *cache = NULL;

    /* verify input */
    if (max_open_size < STREAM_CACHE_MINIMUM_SIZE) {
        CRITMSG(("Illegal maximum size (%d) for stream cache;"
                 " must use value >= %u"),
                max_open_size, STREAM_CACHE_MINIMUM_SIZE);
        return NULL;
    }

    cache = (stream_cache_t*)calloc(1, sizeof(stream_cache_t));
    if (cache == NULL) {
        skAppPrintOutOfMemory(NULL);
        return NULL;
    }
    if (RW_MUTEX_INIT(&cache->mutex)) {
        CRITMSG(FMT_MUTEX_FAILURE);
        free(cache);
        return NULL;
    }

    cache->rbtree = rbinit(&cache_entry_compare, NULL);
    if (cache->rbtree == NULL) {
        skAppPrintOutOfMemory(NULL);
        goto ERROR;
    }

    cache->max_open_size = max_open_size;
    cache->open_callback = open_fn;

    return cache;

  ERROR:
    RW_MUTEX_DESTROY(&cache->mutex);
    free(cache);
    return NULL;
}


/* close all streams, destroy them, and destroy the cache */
int
skCacheDestroy(
    stream_cache_t     *cache)
{
    RBLIST *iter;
    cache_entry_t *entry;
    int retval;

    retval = 0;

    if (NULL == cache) {
        INFOMSG("Tried to destroy uninitialized stream cache.");
        return retval;
    }

    WRITE_LOCK(&cache->mutex);

    TRACEMSG(1, ("Destroying cache with %u open and %u closed entries",
                 cache->open_count, cache->total_count - cache->open_count));

    iter = rbopenlist(cache->rbtree);
    while ((entry = (cache_entry_t*)rbreadlist(iter)) != NULL) {
        MUTEX_LOCK(&entry->mutex);
        if (cache_entry_destroy(entry)) {
            retval = -1;
        }
    }
    rbcloselist(iter);

    /* destroy the redblack tree */
    rbdestroy(cache->rbtree);

    RW_MUTEX_UNLOCK(&cache->mutex);
    RW_MUTEX_DESTROY(&cache->mutex);

    /* Free the structure itself */
    free(cache);

    return retval;
}


/* find an entry in the cache.  if not present, use the open-callback
 * function to open/create the stream and then add it. */
int
skCacheLookupOrOpenAdd(
    stream_cache_t             *cache,
    const sksite_repo_key_t    *key,
    void                       *caller_data,
    cache_entry_t             **out_entry)
{
    cache_entry_t *entry;
    const cache_entry_t *node;
    int retval;
    int rv;

    /* do a lookup holding only the read lock; if there is no support
     * for read-write locks, the entire cache is locked. */
    READ_LOCK(&cache->mutex);

    /* if we find it and the stream is open, return it */
    entry = cache_entry_lookup(cache, key);
    if (entry && entry->stream) {
        TRACEMSG(2, ("cache: Returning open stream '%s'",
                     entry->filename));
        entry->last_accessed = sktimeNow();
        *out_entry = entry;
        retval = 0;
        goto END;
    }

#ifdef SK_HAVE_PTHREAD_RWLOCK
    /*
     *  we need to either add or reopen the stream.  We want to get a
     *  write lock on the cache, but first we must release the read
     *  lock on the cache and the lock on the stream (if the stream
     *  exists and is closed).
     *
     *  skip all of these steps if there is no support for read-write
     *  locks, since the entire cache is already locked.
     */
    if (entry) {
        MUTEX_UNLOCK(&entry->mutex);
    }
    RW_MUTEX_UNLOCK(&cache->mutex);
    WRITE_LOCK(&cache->mutex);

    /* search for the entry again, in case it was added or opened
     * between releasing the read lock on the cache and getting the
     * write lock on the cache */
    entry = cache_entry_lookup(cache, key);
    if (entry && entry->stream) {
        /* found it.  we can return */
        TRACEMSG(2, ("cache: Returning open stream '%s'--second attempt",
                     entry->filename));
        entry->last_accessed = sktimeNow();
        *out_entry = entry;
        retval = 0;
        goto END;
    }
#endif  /* SK_HAVE_PTHREAD_RWLOCK */

    *out_entry = NULL;
    retval = -1;

    if (entry) {
        /* re-open existing file for append, and read its header */
        const char *base;

        TRACEMSG(1, ("cache: Opening existing file '%s'", entry->filename));

        base = strrchr(entry->filename, '/');
        if (base) {
            ++base;
        } else {
            base = entry->filename;
        }
        DEBUGMSG("Opening existing file '%s'", base);

        if ((rv = skStreamCreate(
                 &entry->stream, SK_IO_APPEND, SK_CONTENT_SILK_FLOW))
            || (rv = skStreamBind(entry->stream, entry->filename))
            || (rv = skStreamOpen(entry->stream))
            || (rv = skStreamReadSilkHeader(entry->stream, NULL))
            )
        {
            /* FIXME: The code should probably remove the key from the
             * red-black tree and call the open_callback to open a new
             * file. */
            skStreamPrintLastErr(entry->stream, rv, &WARNINGMSG);
            skStreamDestroy(&entry->stream);
            WARNINGMSG("cache: Failed to open existing file '%s'",
                       entry->filename);
            MUTEX_UNLOCK(&entry->mutex);
            goto END;
        }
        ++cache->open_count;

    } else {
        /* create a new entry */
        entry = cache_entry_create();
        if (NULL == entry) {
            goto END;
        }
        entry->key.timestamp = key->timestamp;
        entry->key.sensor_id = key->sensor_id;
        entry->key.flowtype_id = key->flowtype_id;
        entry->total_rec_count = 0;

        /* use the callback to open the file */
        entry->stream = cache->open_callback(key, caller_data);
        if (NULL == entry->stream) {
            cache_entry_destroy(entry);
            goto END;
        }
        entry->filename = strdup(skStreamGetPathname(entry->stream));
        if (NULL == entry->filename) {
            skAppPrintOutOfMemory(NULL);
            cache_entry_destroy(entry);
            goto END;
        }

        /* add the entry to the redblack tree */
        node = (const cache_entry_t*)rbsearch(entry, cache->rbtree);
        if (node != entry) {
            if (node == NULL) {
                skAppPrintOutOfMemory(NULL);
                cache_entry_destroy(entry);
                goto END;
            }
            CRITMSG(("Duplicate entries in stream cache "
                     "for time=%" PRId64 " sensor=%d flowtype=%d"),
                    key->timestamp, key->sensor_id, key->flowtype_id);
            skAbort();
        }

        ++cache->total_count;
        ++cache->open_count;

        TRACEMSG(1, ("cache: Opened new file '%s'", entry->filename));
    }

    retval = 0;

    TRACEMSG(2, ("cache: Current entry count: %u open, %u max, %u total",
                 cache->open_count, cache->max_open_size, cache->total_count));

    if (cache->open_count > cache->max_open_size) {
        /* FIXME: instead of closing a single file, consider closing
         * a small number of files. To do this, we would probably
         * want to fill a heap of the LRU entries. */
        /* FIXME: if the stream cache maintained its own timer and
         * invoked a user-provided callback when the timer fired, we
         * could have the timer invoke the callback when the cache got
         * full */
        /* Currently we loop through all the files in the red-black
         * tree: open and closed.  An alternate implementation would
         * be to have an additional doubly linked-list structure
         * containing only the open files.  Of course, this
         * complicates the implementation because there is an extra
         * structure to maintain. */

        /* close the least recently used file */
        RBLIST *iter;
        cache_entry_t *e;
        cache_entry_t *min_entry;
        sktime_t min_time;

        min_entry = NULL;
        min_time = MAX_TIME;

        /* unlock the entry's mutex to avoid a deadlock */
        MUTEX_UNLOCK(&entry->mutex);

        /* visit entries in the red-black tree; this only finds open
         * entries since closed entries have their time set to
         * MAX_TIME. */
        iter = rbopenlist(cache->rbtree);
        while ((e = (cache_entry_t*)rbreadlist(iter)) != NULL) {
            MUTEX_LOCK(&e->mutex);
            if (e->last_accessed < min_time) {
                min_entry = e;
                min_time = e->last_accessed;
            }
            MUTEX_UNLOCK(&e->mutex);
        }
        rbcloselist(iter);

        assert(min_time < MAX_TIME);
        assert(min_entry != NULL);
        assert(min_entry != entry);
        assert(min_entry->stream != NULL);

        MUTEX_LOCK(&min_entry->mutex);
        cache_entry_close(min_entry);
        min_entry->last_accessed = MAX_TIME;
        MUTEX_UNLOCK(&min_entry->mutex);

        --cache->open_count;

        /* re-lock the entry's mutex */
        MUTEX_LOCK(&entry->mutex);
    }

    /* update access time and record count */
    entry->last_accessed = sktimeNow();
    entry->opened_rec_count = skStreamGetRecordCount(entry->stream);
    *out_entry = entry;

  END:
    RW_MUTEX_UNLOCK(&cache->mutex);
    return retval;
}


/*
** Local Variables:
** mode:c
** indent-tabs-mode:nil
** c-basic-offset:4
** End:
*/
