/*
** Copyright (C) 2004-2017 by Carnegie Mellon University.
**
** @OPENSOURCE_LICENSE_START@
** See license information in ../../LICENSE.txt
** @OPENSOURCE_LICENSE_END@
*/

#include <silk/silk.h>

RCSIDENT("$SiLK: skdeque.c efd886457770 2017-06-21 18:43:23Z mthomas $");

/* #define SKTHREAD_DEBUG_MUTEX 1 */
#include <silk/skdeque.h>
#include <silk/skthread.h>


/*
 *  Deque API
 *
 *    A deque is a thread-safe double-ended queue.
 *
 *    There are two types of deques:
 *
 *    --A standard deque is a double-ended queue of objects.
 *
 *    --A merged deque is a deque of deques.  It is a pseudo-deque
 *    which acts like a deque with all the elements of deque-1 in
 *    front of deque-2.  deque-1 and deque-2 behave normally.
 *
 *    Aside from the create function, the functions for the two deques
 *    are implemented using function pointers.
 *
 */


/* *** typedefs for manipulation functions *** */

/*
 *    Block 'deque' if flag is true; unblock 'deque' if 'flag' is
 *    false.
 */
typedef skDQErr_t
(*dq_fn_block)(
    sk_deque_t         *deque,
    uint8_t             flag);

/*
 *   Free all the items in 'deque' and destroy 'deque'.
 */
typedef skDQErr_t
(*dq_fn_destroy)(
    sk_deque_t         *deque);

/*
 *   Join the first element of 'back' to final element of 'front'.
 */
typedef skDQErr_t
(*dq_fn_join)(
    sk_deque_t         *front,
    sk_deque_t         *back);

/*
 *    Fill 'item' with the first element (when 'front' is true) or the
 *    final element (when 'front' is false') in 'deque' leaving the
 *    item in the deque.  If the deque is empty, return SKDQ_EMPTY.
 */
typedef skDQErr_t
(*dq_fn_peek)(
    sk_deque_t         *deque,
    void              **item,
    uint8_t             front);

/*
 *    Fill 'item' with the first element (when 'front' is true) or the
 *    final element (when 'front' is false') in 'deque' removing the
 *    item from the deque.  If the deque is empty and 'block' is
 *    false, return SKDQ_EMPTY.  Otherwise, wait for no more than
 *    'seconds' seconds for an item to appear, or wait indefinitely
 *    when 'seconds' is 0.
 */
typedef skDQErr_t
(*dq_fn_pop)(
    sk_deque_t         *deque,
    void              **item,
    uint8_t             block,
    uint8_t             front,
    uint32_t            seconds);

/*
 *    Add 'item' to 'deque'.  Put 'item' at the front of the deque
 *    when 'front' is true or at the end of 'deque' when 'front' is
 *    false.
 */
typedef skDQErr_t
(*dq_fn_push)(
    sk_deque_t         *deque,
    void               *item,
    uint8_t             front);

/*
 *    Return the number of elements in 'deque'.
 */
typedef uint32_t
(*dq_fn_size)(
    sk_deque_t         *deque);

/*
 *    Return SKDQ_EMPTY if 'deque' is empty or 'SKDQ_SUCCESS'
 *    otherwise.
 */
typedef skDQErr_t
(*dq_fn_status)(
    sk_deque_t         *deque);


/* typedef struct sk_deque_st sk_deque_t;  // skdeque.h  */

/* Deque data structure */
struct sk_deque_st {
    /* Function pointers to either the std_* functions or the merged_*
     * functions defined below depending on the type of deque. */
    struct methods_st {
        dq_fn_block         block;
        dq_fn_destroy       destroy;
        dq_fn_join          join;
        dq_fn_peek          peek;
        dq_fn_pop           pop;
        dq_fn_push          push;
        dq_fn_size          size;
        dq_fn_status        status;
    }                   methods;

    /* The mutex for this deque object */
    pthread_mutex_t     mutex_data; /* mutex storage */

    /* The condition variable for this deque object */
    pthread_cond_t      cond_data;

    /* When this deque is part of a merged deque, the 'mutex' variable
     * points to the 'mutex_data' on the merged deque; otherwise,
     * 'mutex' is the address of the 'mutex_data' above. */
    pthread_mutex_t    *mutex;      /* mutex */

    /* Similar to 'mutex' for the condition variable */
    pthread_cond_t     *cond;

    /* Pointer to either an 'skdq_std_t' for a standard deque or an
     * 'skdq_merged_t' for a merged deque. */
    void               *data;

    /* Reference count */
    uint8_t             ref;
};

/* Deque item */
typedef struct skdq_item_st skdq_item_t;
struct skdq_item_st {
    void           *item;
    skdq_item_t    *dir[2];      /* 0 == back, 1 == front */
};



/*
 *  Functions for the standard deque
 *  ******************************************************************
 */

/*
 *    skdq_std_t is a normal (non-merged) deque.
 */
struct skdq_std_st {
    skdq_item_t    *dir[2];     /* 0 == back, 1 == front */
    uint32_t        size;
    uint8_t         blocked;
};
typedef struct skdq_std_st skdq_std_t;

static skDQErr_t
std_block(
    sk_deque_t         *self,
    uint8_t             flag)
{
    skdq_std_t *q = (skdq_std_t *)self->data;

    if (q == NULL) {
        return SKDQ_ERROR;
    }

    q->blocked = flag;
    if (!flag) {
        MUTEX_BROADCAST(self->cond);
    }

    return SKDQ_SUCCESS;
}

static uint32_t
std_size(
    sk_deque_t         *self)
{
    skdq_std_t *q = (skdq_std_t *)self->data;

    return q->size;
}

static skDQErr_t
std_status(
    sk_deque_t         *self)
{
    skdq_std_t *q = (skdq_std_t *)self->data;

    if (q == NULL) {
        return SKDQ_ERROR;
    }
    if (q->dir[0] == NULL) {
        return SKDQ_EMPTY;
    }
    return SKDQ_SUCCESS;
}

static skDQErr_t
std_peek(
    sk_deque_t         *self,
    void              **item,
    uint8_t             front)
{
    skdq_std_t *q = (skdq_std_t *)self->data;

    if (q == NULL) {
        return SKDQ_ERROR;
    }

    if (q->dir[0] == NULL) {
        return SKDQ_EMPTY;
    }

    *item = q->dir[front]->item;

    return SKDQ_SUCCESS;
}

static skDQErr_t
std_pop(
    sk_deque_t         *self,
    void              **item,
    uint8_t             block,
    uint8_t             f,
    uint32_t            seconds)
{
    skdq_std_t *q = (skdq_std_t *)self->data;
    skdq_item_t *t;
    uint8_t b;
    int rv;
    struct timeval now;
    struct timespec to;

    if (q == NULL) {
        return SKDQ_ERROR;
    }

    if (block) {
        gettimeofday(&now, NULL);
        to.tv_sec = now.tv_sec + seconds;
        to.tv_nsec = now.tv_usec * 1000;
        while (self->data && q->dir[0] == NULL && q->blocked) {
            if (seconds) {
                rv = pthread_cond_timedwait(self->cond, self->mutex, &to);
                if (rv == ETIMEDOUT) {
                    return SKDQ_TIMEDOUT;
                }
            } else {
                MUTEX_WAIT(self->cond, self->mutex);
            }
        }
        if (self->data && !q->blocked) {
            return SKDQ_UNBLOCKED;
        }
    }
    if (self->data == NULL) {
        return SKDQ_DESTROYED;
    }
    if (q->dir[0] == NULL) {
        return SKDQ_EMPTY;
    }

    b = 1 - f;

    t = q->dir[f];
    *item = t->item;

    q->dir[f] = t->dir[b];
    if (q->dir[f]) {
        q->dir[f]->dir[f] = NULL;
    } else {
        q->dir[b] = NULL;
    }

    q->size--;

    free(t);

    return SKDQ_SUCCESS;
}

static skDQErr_t
std_push(
    sk_deque_t         *self,
    void               *item,
    uint8_t             f)
{
    skdq_std_t *q = (skdq_std_t *)self->data;
    skdq_item_t *t;
    uint8_t b;

    if (q == NULL) {
        return SKDQ_ERROR;
    }

    if ((t = (skdq_item_t *)malloc(sizeof(skdq_item_t))) == NULL) {
        return SKDQ_ERROR;
    }

    t->item = item;

    b = 1 - f;

    t->dir[f] = NULL;
    t->dir[b] = q->dir[f];
    q->dir[f] = t;
    if (q->dir[b]) {
        t->dir[b]->dir[f] = t;
    } else {
        q->dir[b] = t;
        MUTEX_BROADCAST(self->cond);
    }

    q->size++;

    return SKDQ_SUCCESS;
}

static skDQErr_t
std_destroy(
    sk_deque_t         *self)
{
    skdq_std_t *q = (skdq_std_t *)self->data;
    skdq_item_t *t, *x;

    if (q == NULL) {
        return SKDQ_ERROR;
    }

    for (t = q->dir[1]; t; t = x) {
        x = t->dir[0];
        free(t);
    }

    free(q);

    self->data = NULL;

    return SKDQ_SUCCESS;
}

static skDQErr_t
std_join(
    sk_deque_t         *front,
    sk_deque_t         *back)
{
    skdq_std_t *fq = (skdq_std_t *)front->data;
    skdq_std_t *bq = (skdq_std_t *)back->data;

    if (fq == NULL || bq == NULL) {
        return SKDQ_ERROR;
    }

    if (0 == fq->size) {
        /* have fq take over bq */
        fq->dir[0] = bq->dir[0];
        fq->dir[1] = bq->dir[1];
    } else {
        /* join the two lists */
        bq->dir[1]->dir[1] = fq->dir[0];
        fq->dir[0]->dir[0] = bq->dir[1];
        fq->dir[0] = bq->dir[0];
    }
    fq->size += bq->size;

    /* set the back deque to be empty */
    bq->dir[0] = bq->dir[1] = NULL;
    bq->size = 0;

    MUTEX_BROADCAST(front->cond);

    return SKDQ_SUCCESS;
}

/* Create a standard deque */
sk_deque_t*
skDequeCreate(
    void)
{
    sk_deque_t *deque;
    skdq_std_t *data;

    /* memory allocation */
    if ((deque = (sk_deque_t*)malloc(sizeof(sk_deque_t))) == NULL) {
        return NULL;
    }
    if ((data = (skdq_std_t *)malloc(sizeof(skdq_std_t))) == NULL) {
        free(deque);
        return NULL;
    }

    /* Initialize data */
    data->dir[0] = data->dir[1] = NULL;
    data->size = 0;
    data->blocked = 1;

    /* Initialize deque */
    deque->ref = 1;
    MUTEX_INIT(&deque->mutex_data);
    pthread_cond_init(&deque->cond_data, NULL);
    deque->mutex = &deque->mutex_data;
    deque->cond = &deque->cond_data;
    deque->methods.block = std_block;
    deque->methods.destroy = std_destroy;
    deque->methods.join = std_join;
    deque->methods.peek = std_peek;
    deque->methods.pop = std_pop;
    deque->methods.push = std_push;
    deque->methods.size = std_size;
    deque->methods.status = std_status;
    deque->data = (void *)data;

    return deque;
}



/*
 *  Functions for a deque of deques (a merged deque)
 *  ******************************************************************
 */

/*
 *    skdq_merged_t is a pair of deque objects (which themselves could
 *    be merged deques).
 */
struct skdq_merged_st {
    sk_deque_t *q[2];           /* 0 == back, 1 == front */
};
typedef struct skdq_merged_st skdq_merged_t;

static skDQErr_t
merged_block(
    sk_deque_t         *self,
    uint8_t             flag)
{
    skdq_merged_t *q = (skdq_merged_t *)self->data;
    skDQErr_t err = SKDQ_SUCCESS;
    uint8_t i;

    if (q == NULL) {
        return SKDQ_ERROR;
    }

    for (i = 0; i <= 1 && err == SKDQ_SUCCESS; i++) {
        err = q->q[i]->methods.block(q->q[i], flag);
    }

    return err;
}

static uint32_t
merged_size(
    sk_deque_t         *self)
{
    skdq_merged_t *q = (skdq_merged_t *)self->data;

    return (q->q[0]->methods.size(q->q[0]) + q->q[1]->methods.size(q->q[1]));
}

static skDQErr_t
merged_status(
    sk_deque_t         *self)
{
    skdq_merged_t *q = (skdq_merged_t *)self->data;
    skDQErr_t retval;

    if (q == NULL) {
        return SKDQ_ERROR;
    }

    if ((retval = q->q[0]->methods.status(q->q[0])) == SKDQ_EMPTY) {
        retval = q->q[1]->methods.status(q->q[1]);
    }

    return retval;
}

static skDQErr_t
merged_peek(
    sk_deque_t         *self,
    void              **item,
    uint8_t             f)
{
    skdq_merged_t *q = (skdq_merged_t *)self->data;
    skDQErr_t retval;
    uint8_t b;

    if (q == NULL) {
        return SKDQ_ERROR;
    }

    b = 1 - f;

    if ((retval = q->q[f]->methods.peek(q->q[f], item, f)) == SKDQ_EMPTY) {
        retval = q->q[b]->methods.peek(q->q[b], item, f);
    }

    return retval;
}

static skDQErr_t
merged_pop(
    sk_deque_t         *self,
    void              **item,
    uint8_t             block,
    uint8_t             f,
    uint32_t            seconds)
{
    skdq_merged_t *q = (skdq_merged_t *)self->data;
    skDQErr_t retval = SKDQ_SUCCESS;
    uint8_t b;
    int rv;
    struct timeval now;
    struct timespec to;

    if (q == NULL) {
        return SKDQ_ERROR;
    }

    if (block) {
        gettimeofday(&now, NULL);
        to.tv_sec = now.tv_sec + seconds;
        to.tv_nsec = now.tv_usec * 1000;
        while (self->data && merged_status(self) == SKDQ_EMPTY) {
            if (seconds) {
                rv = pthread_cond_timedwait(self->cond, self->mutex, &to);
                if (rv == ETIMEDOUT) {
                    return SKDQ_TIMEDOUT;
                }
            } else {
                MUTEX_WAIT(self->cond, self->mutex);
            }
        }
    }

    if (self->data == NULL) {
        return SKDQ_DESTROYED;
    }

    if ((retval = merged_status(self)) != SKDQ_SUCCESS) {
        return retval;
    }

    b = 1 - f;

    retval = q->q[f]->methods.pop(q->q[f], item, 0, f, 0);
    if (retval == SKDQ_EMPTY) {
        retval = q->q[b]->methods.pop(q->q[b], item, 0, f, 0);
    }

    return retval;
}

static skDQErr_t
merged_push(
    sk_deque_t         *self,
    void               *item,
    uint8_t             f)
{
    skdq_merged_t *q = (skdq_merged_t *)self->data;

    if (q == NULL) {
        return SKDQ_ERROR;
    }

    return q->q[f]->methods.push(q->q[f], item, f);
}

static skDQErr_t
merged_destroy(
    sk_deque_t         *self)
{
    skdq_merged_t *q = (skdq_merged_t *)self->data;
    uint8_t i;

    if (q == NULL) {
        return SKDQ_ERROR;
    }

    for (i = 0; i <= 1; i++) {
        q->q[i]->mutex = &q->q[i]->mutex_data;
        q->q[i]->cond = &q->q[i]->cond_data;
        skDequeDestroy(q->q[i]);
    }

    free(q);

    return SKDQ_SUCCESS;
}

static skDQErr_t
merged_join(
    sk_deque_t         *front,
    sk_deque_t         *back)
{
    skdq_merged_t *fq = (skdq_merged_t *)front->data;
    skdq_merged_t *bq = (skdq_merged_t *)back->data;

    if (fq == NULL || bq == NULL) {
        return SKDQ_ERROR;
    }

    fq->q[1]->methods.join(fq->q[1], bq->q[1]);
    fq->q[0]->methods.join(fq->q[0], bq->q[0]);

    return SKDQ_SUCCESS;
}


/* Creates a new pseudo-deque which acts like a deque with all the
 * elements of q1 in front of q2.  q1 and q2 continue behaving
 * normally. */
sk_deque_t*
skDequeCreateMerged(
    sk_deque_t         *q1,
    sk_deque_t         *q2)
{
    sk_deque_t * volatile deque;
    skdq_merged_t *data;
    pthread_mutex_t *mutex;
    pthread_cond_t *cond;
    uint8_t i;

    if (q1 == NULL || q2 == NULL ||
        q1->data == NULL || q2->data == NULL) {
        return NULL;
    }

    /* memory allocation */
    if ((deque = (sk_deque_t*)malloc(sizeof(sk_deque_t))) == NULL) {
        return NULL;
    }
    if ((data = (skdq_merged_t *)malloc(sizeof(skdq_merged_t))) == NULL) {
        free(deque);
        return NULL;
    }

    /* Initialize data */
    if ((data->q[1] = skDequeCopy(q1)) == NULL) {
        free(data);
        free(deque);
        return NULL;
    }
    if ((data->q[0] = skDequeCopy(q2)) == NULL) {
        skDequeDestroy(data->q[1]);
        free(data);
        free(deque);
        return NULL;
    }

    /* Initialize deque */
    deque->ref = 1;
    MUTEX_INIT(&deque->mutex_data);
    pthread_cond_init(&deque->cond_data, NULL);
    deque->mutex = &deque->mutex_data;
    deque->cond = &deque->cond_data;
    deque->methods.block = merged_block;
    deque->methods.destroy = merged_destroy;
    deque->methods.join = merged_join;
    deque->methods.peek = merged_peek;
    deque->methods.pop = merged_pop;
    deque->methods.push = merged_push;
    deque->methods.size = merged_size;
    deque->methods.status = merged_status;
    deque->data = (void *)data;

    /** Reorganize subdeques' mutexes and condition variables **/

    /* Lock our own */
    MUTEX_LOCK(deque->mutex);

    for (i = 0; i <= 1; ++i) {
        /* Grab q[i] */
        MUTEX_LOCK(data->q[i]->mutex);
        /* Note its current settings */
        mutex = data->q[i]->mutex;
        cond = data->q[i]->cond;
        /* Fix its mutex and condition variable to be ours */
        data->q[i]->mutex = deque->mutex;
        data->q[i]->cond = deque->cond;
        /* Wake up anything waiting on it so they pick up the new
         * condition variable. */
        MUTEX_BROADCAST(cond);
        /* Unlock */
        MUTEX_UNLOCK(mutex);
    }

    MUTEX_UNLOCK(deque->mutex);

    return deque;
}



/*
 *  Generic Functions that operate on any deque
 *  ******************************************************************
 */

/* Creates a copy of a deque.  Operations on both deques will affect
 * each other. */
sk_deque_t*
skDequeCopy(
    sk_deque_t * volatile   deque)
{
    int die = 0;

    if (deque == NULL || deque->data == NULL) {
        return NULL;
    }
    MUTEX_LOCK(deque->mutex);
    if (deque->data == NULL) {
        die = 1;
    } else {
        deque->ref++;
    }
    MUTEX_UNLOCK(deque->mutex);

    if (die) {
        return NULL;
    }
    return deque;
}


/* Destroy and free a deque.  (Not reponsible for freeing the elements
 * within the deque). */
skDQErr_t
skDequeDestroy(
    sk_deque_t         *deque)
{
    int destroy;

    if (deque == NULL) {
        return SKDQ_ERROR;
    }

    MUTEX_LOCK(deque->mutex);

    if ((destroy = (--deque->ref == 0))) {
        /* Call destructor method */
        deque->methods.destroy(deque);

        /* Mark as destroyed */
        deque->data = NULL;

        /* Give all the condition waiting threads a chance to exit. */
        MUTEX_BROADCAST(deque->cond);
    }

    MUTEX_UNLOCK(deque->mutex);

    if (destroy) {
        while (MUTEX_DESTROY(deque->mutex) == EBUSY)
            ; /* empty */
        while (pthread_cond_destroy(deque->cond) == EBUSY) {
            MUTEX_BROADCAST(deque->cond);
        }
        free(deque);
    }

    return SKDQ_SUCCESS;
}


skDQErr_t
skDequeBlock(
    sk_deque_t         *deque)
{
    skDQErr_t retval;

    MUTEX_LOCK(deque->mutex);

    retval = deque->methods.block(deque, 1);

    MUTEX_UNLOCK(deque->mutex);

    return retval;
}

skDQErr_t
skDequeUnblock(
    sk_deque_t         *deque)
{
    skDQErr_t retval;

    MUTEX_LOCK(deque->mutex);

    retval = deque->methods.block(deque, 0);

    MUTEX_UNLOCK(deque->mutex);

    return retval;
}


/* Return the size of a deque. */
uint32_t
skDequeSize(
    sk_deque_t         *deque)
{
    uint32_t retval;

    MUTEX_LOCK(deque->mutex);

    retval = deque->methods.size(deque);

    MUTEX_UNLOCK(deque->mutex);

    return retval;
}


/* Return the status of a deque. */
skDQErr_t
skDequeStatus(
    sk_deque_t         *deque)
{
    skDQErr_t retval;

    MUTEX_LOCK(deque->mutex);

    retval = deque->methods.status(deque);

    MUTEX_UNLOCK(deque->mutex);

    return retval;
}


/* Peek at an element from a deque. */
static skDQErr_t
sk_deque_peek(
    sk_deque_t         *deque,
    void              **item,
    uint8_t             front)
{
    skDQErr_t retval;

    MUTEX_LOCK(deque->mutex);

    retval = deque->methods.peek(deque, item, front);

    MUTEX_UNLOCK(deque->mutex);

    return retval;
}

/* Return the first or last element of a deque without removing it, or
 * SKDQ_EMPTY if the deque is empty.  */
skDQErr_t
skDequeFront(
    sk_deque_t         *deque,
    void              **item)
{
    return sk_deque_peek(deque, item, 1);
}
skDQErr_t
skDequeBack(
    sk_deque_t         *deque,
    void              **item)
{
    return sk_deque_peek(deque, item, 0);
}


/* Pop an element from a deque.  */
static skDQErr_t
sk_deque_pop(
    sk_deque_t         *deque,
    void              **item,
    uint8_t             block,
    uint8_t             front,
    uint32_t            seconds)
{
    skDQErr_t retval;

    MUTEX_LOCK(deque->mutex);

    retval = deque->methods.pop(deque, item, block, front, seconds);

    MUTEX_UNLOCK(deque->mutex);

    return retval;
}

/*
 *  Pop an element from a deque.
 *
 *  skDequePop{Front,Back} will block until an item is available in
 *  the deque.  skDequePop{Front,Back}NB will return SKDQ_EMPTY if the
 *  deque is empty instead.
 */
skDQErr_t
skDequePopFront(
    sk_deque_t         *deque,
    void              **item)
{
    return sk_deque_pop(deque, item, 1, 1, 0);
}
skDQErr_t
skDequePopFrontNB(
    sk_deque_t         *deque,
    void              **item)
{
    return sk_deque_pop(deque, item, 0, 1, 0);
}
skDQErr_t
skDequePopFrontTimed(
    sk_deque_t         *deque,
    void              **item,
    uint32_t            seconds)
{
    return sk_deque_pop(deque, item, 1, 1, seconds);
}
skDQErr_t
skDequePopBack(
    sk_deque_t         *deque,
    void              **item)
{
    return sk_deque_pop(deque, item, 1, 0, 0);
}
skDQErr_t
skDequePopBackNB(
    sk_deque_t         *deque,
    void              **item)
{
    return sk_deque_pop(deque, item, 0, 0, 0);
}
skDQErr_t
skDequePopBackTimed(
    sk_deque_t         *deque,
    void              **item,
    uint32_t            seconds)
{
    return sk_deque_pop(deque, item, 1, 0, seconds);
}


/* Push an element onto a deque. */
static skDQErr_t
sk_deque_push(
    sk_deque_t         *deque,
    void               *item,
    uint8_t             front)
{
    skDQErr_t retval;

    MUTEX_LOCK(deque->mutex);

    retval = deque->methods.push(deque, item, front);

    MUTEX_UNLOCK(deque->mutex);

    return retval;
}

/* Push an item onto a deque. */
skDQErr_t
skDequePushFront(
    sk_deque_t         *deque,
    void               *item)
{
    return sk_deque_push(deque, item, 1);
}
skDQErr_t
skDequePushBack(
    sk_deque_t         *deque,
    void               *item)
{
    return sk_deque_push(deque, item, 0);
}


skDQErr_t
skDequeJoin(
    sk_deque_t         *front,
    sk_deque_t         *back)
{
    skdq_merged_t *mq;
    skDQErr_t retval;

    MUTEX_LOCK(front->mutex);
    MUTEX_LOCK(back->mutex);

    if (0 == back->methods.size(back)) {
        /* when back is empty, there is nothing to do */
        retval = SKDQ_SUCCESS;

    } else if (front->methods.join == back->methods.join) {
        /* when the deques use the same join() method, call it */
        retval = front->methods.join(front, back);

    } else if (front->methods.join == merged_join) {
        /* when the 'front' deque is merged, join its back deque with
         * the 'back' deque passed into this function */
        assert(back->methods.join == std_join);
        mq = (skdq_merged_t *)front->data;

        retval = mq->q[0]->methods.join(mq->q[0], back);

    } else if (front->methods.join == std_join) {
        /* when the 'front' deque is a standard deque, join both
         * members of the 'back' deque to it */
        assert(back->methods.join == merged_join);
        mq = (skdq_merged_t *)back->data;

        retval = front->methods.join(front, mq->q[1]);
        if (SKDQ_SUCCESS == retval) {
            retval = front->methods.join(front, mq->q[0]);
        }

    } else {
        skAbort();
    }

    MUTEX_UNLOCK(back->mutex);
    MUTEX_UNLOCK(front->mutex);
    return retval;
}


/*
** Local Variables:
** mode:c
** indent-tabs-mode:nil
** c-basic-offset:4
** End:
*/
