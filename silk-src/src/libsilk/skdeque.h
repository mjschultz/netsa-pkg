/*
** Copyright (C) 2004-2017 by Carnegie Mellon University.
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

RCSIDENTVAR(rcsID_SKDEQUE_H, "$SiLK: skdeque.h efd886457770 2017-06-21 18:43:23Z mthomas $");

/**
 *  @file
 *
 *    Implementation of a thread-safe, double-ended queue.
 *
 *    This file is part of libsilk.
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
typedef struct sk_deque_st sk_deque_t;


/**
 *    Return values from skDeque functions.
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
sk_deque_t*
skDequeCreate(
    void);


/**
 *    Creates a copy of a deque.  Operations on both deques will
 *    affect each other.  Return NULL on error.
 */
sk_deque_t*
skDequeCopy(
    sk_deque_t         *deque);

/**
 *    Creates a new pseudo-deque which acts like a deque with all the
 *    elements of q1 in front of q2.  q1 and q2 continue behaving
 *    normally.  Return NULL on error.
 */
sk_deque_t*
skDequeCreateMerged(
    sk_deque_t         *q1,
    sk_deque_t         *q2);


/*** Deque destruction ***/

/**
 *    Destroy and free a deque.  (Not reponsible for freeing the
 *    elements within the deque).  Returns SKDQ_ERROR if 'deque' is
 *    NULL.
 */
skDQErr_t
skDequeDestroy(
    sk_deque_t         *deque);


/*** Deque data manipulation functions ***/

/**
 *    Return the status of a deque: SKDQ_EMPTY when empty; SKDQ_ERROR
 *    when 'deque' is internally inconsistent; SKDQ_SUCCESS otherwise.
 */
skDQErr_t
skDequeStatus(
    sk_deque_t         *deque);

/**
 *    Return the number of elements in the deque.  Result is undefined
 *    on a bad (destroyed or error-ridden) deque.
 */
uint32_t
skDequeSize(
    sk_deque_t         *deque);

/**
 *    Pop an element from the front of 'deque'.  The call will block
 *    until an item is available in 'deque'.  It is the responsibility
 *    of the program using the deque to free any elements popped from
 *    it.
 *
 *    Return SKDQ_SUCCESS when an element is popped.  If the deque is
 *    unblocked (c.f., skDequeUnblock()) when the function is first
 *    invoked or if an empty deque becomes unblocked while waiting for
 *    an element, return SKDQ_UNBLOCKED.  Return SKDQ_DESTROYED if the
 *    deque is destroyed.
 */
skDQErr_t
skDequePopFront(
    sk_deque_t         *deque,
    void              **item);

/**
 *    Like skDequePopFront(), but does not block and returns
 *    SKDQ_EMPTY if 'deque' is currently empty.
 */
skDQErr_t
skDequePopFrontNB(
    sk_deque_t         *deque,
    void              **item);

/**
 *    Like skDequePopFront() except, when 'deque' is empty, waits
 *    'seconds' seconds for an item to appear in 'deque'.  If 'deque'
 *    is still empty after 'seconds' seconds, returns SKDQ_TIMEOUT.
 */
skDQErr_t
skDequePopFrontTimed(
    sk_deque_t         *deque,
    void              **item,
    uint32_t            seconds);

/**
 *    Like skDequePopFront(), but returns the last element of 'deque'.
 */
skDQErr_t
skDequePopBack(
    sk_deque_t         *deque,
    void              **item);

/**
 *    Like skDequePopFrontNB(), but returns the last element of
 *    'deque'.
 */
skDQErr_t
skDequePopBackNB(
    sk_deque_t         *deque,
    void              **item);

/**
 *    Like skDequePopFrontTimed(), but returns the last element of
 *    'deque'.
 */
skDQErr_t
skDequePopBackTimed(
    sk_deque_t         *deque,
    void              **item,
    uint32_t            seconds);

/**
 *    Unblock threads blocked on dequeue pops (each of which will
 *    return SKDQ_UNBLOCKED).  They will remain unblocked, ignoring
 *    blocking pushes, until re-blocked.
 */
skDQErr_t
skDequeUnblock(
    sk_deque_t         *deque);

/**
 *    Reblock a deque unblocked by skDequeUnblock.  Deques are created
 *    in a blockable state.
 */
skDQErr_t
skDequeBlock(
    sk_deque_t         *deque);

/**
 *    Return the first element of 'deque' without removing it and
 *    return SKDQ_SUCCESS, or return SKDQ_EMPTY if the deque is empty.
 *    This function does not remove items from the deque.  Do not
 *    free() an item until it has been popped.
 */
skDQErr_t
skDequeFront(
    sk_deque_t         *deque,
    void              **item);

/**
 *    Like skDequeFront(), but returns the last element of 'deque'.
 */
skDQErr_t
skDequeBack(
    sk_deque_t         *deque,
    void              **item);

/**
 *    Push 'item' onto the front of 'deque'.  Deques maintain the item
 *    pointer only.  In order for the item to be of any use when it is
 *    later popped, it must survive its stay in the queue (not be
 *    freed).
 *
 *    Return SKDQ_SUCCESS on success.  Return SKDQ_ERROR on memory
 *    allocation error or when the deque is in an inconsistent state.
 */
skDQErr_t
skDequePushFront(
    sk_deque_t         *deque,
    void               *item);

/**
 *    Like skDequePushFront(), but pushes 'item' onto the end of 'deque'.
 */
skDQErr_t
skDequePushBack(
    sk_deque_t         *deque,
    void               *item);

/**
 *    Join the deques 'front' and 'back' into a single deque by
 *    appending 'back' to 'front'.  After this call, 'front' contains
 *    the elements of both 'front' and 'back', and 'back' is empty.
 *
 *    When 'front' and 'back' are both merged deques, join the front
 *    sub-deques of each deque argument and join the back sub-deques
 *    of each deque argument.
 *
 *    When 'front' is a merged deque and 'back' is not, append the
 *    elements of 'back' to the back sub-deque of 'front'.
 *
 *    When 'front' is a standard deque and 'back' is a merged deque,
 *    first append the front sub-deque of 'back' to 'front' then
 *    append the back sub-deque of 'back' to 'front'.
 *
 *    Return SKDQ_SUCCESS on success, or SKDQ_ERROR if either deque is
 *    in an inconsistent state.
 */
skDQErr_t
skDequeJoin(
    sk_deque_t         *head,
    sk_deque_t         *tail);

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
