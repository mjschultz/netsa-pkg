/*
** Copyright (C) 2004-2020 by Carnegie Mellon University.
**
** @OPENSOURCE_LICENSE_START@
** See license information in ../../LICENSE.txt
** @OPENSOURCE_LICENSE_END@
*/

/*
**  skdeque.h
**
**    Methods for working with thread-safe double-ended queues
**
*/
#ifndef _SKDEQUE_H
#define _SKDEQUE_H
#ifdef __cplusplus
extern "C" {
#endif

#include <silk/silk.h>

RCSIDENTVAR(rcsID_SKDEQUE_H, "$SiLK: skdeque.h ef14e54179be 2020-04-14 21:57:45Z mthomas $");

/**
 *  @file
 *
 *    Implementation of a thread-safe, double-ended queue.
 *
 *    This file is part of libsilk-thrd.
 *
 *
 *  A deque maintains a list of void pointers.  It does not know the
 *  contents of these pointers, and as such the deque knows nothing
 *  about the memory management behind them.  A deque never attemps to
 *  copy or free anything behind the pointer.  It is the caller's
 *  responsibility to ensure that an object in a deque is not
 *  free()'ed until the object has been popped from the deque.
 *
 *
 *  Within this header file, the item most-recently pushed is
 *  considered to be "last", and "behind" all the other items, and the
 *  item which would be returned by a pop is considered to be "first",
 *  and "in front of" all the other items.
 */

/**
 *    The type of deques.
 *
 *    Unlike most SiLK types, the pointer is part of the typedef.
 */
typedef struct sk_deque_st *skDeque_t;


/**
 *    Return values from skSkdeque functions.
 */
typedef enum {
    /** success */
    SKDQ_SUCCESS = 0,
    /** deque is empty */
    SKDQ_EMPTY = -1,
    /** unspecified error */
    SKDQ_ERROR = -2,
    /** deque was destroyed */
    SKDQ_DESTROYED = -3,
    /** deque was unblocked */
    SKDQ_UNBLOCKED = -4,
    /** deque pop timed out */
    SKDQ_TIMEDOUT = -5
} skDQErr_t;

/*** Deque creation functions ***/

/*
 * For all creation functions, a NULL return value indicates an error
 * condition.
 */

/**
 *    Create a deque.  Return NULL on memory alloation error.
 */
skDeque_t
skDequeCreate(
    void);


/**
 *    Creates a copy of a deque.  Operations on both deques will
 *    affect each other.  Return NULL on error.
 */
skDeque_t
skDequeCopy(
    skDeque_t           deque);

/**
 *    Creates a new pseudo-deque which acts like a deque with all the
 *    elements of q1 in front of q2.  q1 and q2 continue behaving
 *    normally.  Return NULL on error.
 */
skDeque_t
skDequeCreateMerged(
    skDeque_t           q1,
    skDeque_t           q2);


/*** Deque destruction ***/

/**
 *    Destroy and free a deque.  (Not reponsible for freeing the
 *    elements within the deque).  Returns SKDQ_ERROR if 'deque' is
 *    NULL.
 */
skDQErr_t
skDequeDestroy(
    skDeque_t           deque);


/*** Deque data manipulation functions ***/

/**
 *    Return the status of a deque.
 */
skDQErr_t
skDequeStatus(
    skDeque_t           deque);

/**
 *    Returns the size of a deque.  Undefined on a bad (destroyed or
 *    error-ridden) deque.
 */
uint32_t
skDequeSize(
    skDeque_t           deque);

/**
 *    Pop an element from the front of 'deque'.  The call will block
 *    until an item is available in 'deque'.  It is the responsibility
 *    of the program using the deque to free any elements popped from
 *    it.
 */
skDQErr_t
skDequePopFront(
    skDeque_t           deque,
    void              **item);

/**
 *    Like skDequePopFront(), but does not block and returns
 *    SKDQ_EMPTY if 'deque' is currently empty.
 */
skDQErr_t
skDequePopFrontNB(
    skDeque_t           deque,
    void              **item);

/**
 *    Like skDequePopFront() except, when 'deque' is empty, waits
 *    'seconds' seconds for an item to appear in 'deque'.  If 'deque'
 *    is still empty after 'seconds' seconds, returns SKDQ_EMPTY.
 */
skDQErr_t
skDequePopFrontTimed(
    skDeque_t           deque,
    void              **item,
    uint32_t            seconds);

/**
 *    Like skDequePopFront(), but returns the last element of 'deque'.
 */
skDQErr_t
skDequePopBack(
    skDeque_t           deque,
    void              **item);

/**
 *    Like skDequePopFrontNB(), but returns the last element of
 *    'deque'.
 */
skDQErr_t
skDequePopBackNB(
    skDeque_t           deque,
    void              **item);

/**
 *    Like skDequePopFrontTimed(), but returns the last element of
 *    'deque'.
 */
skDQErr_t
skDequePopBackTimed(
    skDeque_t           deque,
    void              **item,
    uint32_t            seconds);

/**
 *    Unblock threads blocked on dequeue pops (each of which will
 *    return SKDQ_UNBLOCKED).  They will remain unblocked, ignoring
 *    blocking pushes, until re-blocked.
 */
skDQErr_t
skDequeUnblock(
    skDeque_t           deque);

/**
 *    Reblock a deque unblocked by skDequeUnblock.  Deques are created
 *    in a blockable state.
 */
skDQErr_t
skDequeBlock(
    skDeque_t           deque);

/**
 *    Return the first element of 'deque' without removing it, or
 *    SKDQ_EMPTY if the deque is empty.  This function does not remove
 *    items from the deque.  Do not free() an item until it has been
 *    popped.
 */
skDQErr_t
skDequeFront(
    skDeque_t           deque,
    void              **item);

/**
 *    Like skDequeFront(), but returns the last element of 'deque'.
 */
skDQErr_t
skDequeBack(
    skDeque_t           deque,
    void              **item);

/**
 *    Push 'item' onto the front of 'deque'.  Deques maintain the item
 *    pointer only.  In order for the item to be of any use when it is
 *    later popped, it must survive its stay in the queue (not be
 *    freed).
 */
skDQErr_t
skDequePushFront(
    skDeque_t           deque,
    void               *item);

/**
 *    Like skDequePushFront(), but pushes 'item' onto the end of 'deque'.
 */
skDQErr_t
skDequePushBack(
    skDeque_t           deque,
    void               *item);

#ifdef __cplusplus
}
#endif
#endif /* _SKDEQUE_H */

/*
** Local Variables:
** mode:c
** indent-tabs-mode:nil
** c-basic-offset:4
** End:
*/
