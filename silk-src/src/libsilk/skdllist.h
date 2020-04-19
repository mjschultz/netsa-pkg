/*
** Copyright (C) 2007-2020 by Carnegie Mellon University.
**
** @OPENSOURCE_LICENSE_START@
** See license information in ../../LICENSE.txt
** @OPENSOURCE_LICENSE_END@
*/

/*
**  skdllist.h
**
**    Doubly-linked lists
**
*/
#ifndef _SKDLLIST_H
#define _SKDLLIST_H
#ifdef __cplusplus
extern "C" {
#endif

#include <silk/silk.h>

RCSIDENTVAR(rcsID_SKDLLIST_H, "$SiLK: skdllist.h ef14e54179be 2020-04-14 21:57:45Z mthomas $");

#include <silk/silk_types.h>

/**
 *  @file
 *
 *    An implementation of a doubly linked list.
 *
 *    This file is part of libsilk.
 */

/*
 *  From silk_types.h:
 *
 *    typedef struct sk_dllist_st      sk_dllist_t;
 *
 *    typedef struct sk_dll_iter_st sk_dll_iter_t;
 *    struct sk_dll_iter_st {
 *        void           *data;
 *        sk_dll_iter_t  *link[2];
 *    };
 */


/**
 *    Function to free the data associated with an sk_dllist_t node
 */
typedef void (*sk_dll_free_fn_t)(
    void   *item);


/*** Creation and destruction ***/

/**
 *    Creates a doubly-linked list for void *'s.  free_fn is a
 *    function used to free the inserted void *'s upon destruction of
 *    the lists, or NULL if they are not to be freed.  Returns the
 *    newly created empty list on success, and NULL if there is a
 *    memory allocation error.
 */
sk_dllist_t *
skDLListCreate(
    sk_dll_free_fn_t    free_fn);

/**
 *    Destroys (and frees) a doubly-linked list.  Will use the
 *    'free-fn' passed into skDLListCreate to free any elements
 *    remaining in the list.  Does nothing if 'list' is NULL.
 */
void
skDLListDestroy(
    sk_dllist_t        *list);


/*** Info ***/

/**
 *    Returns 1 if the given list is empty, 0 otherwise.
 */
int
skDLListIsEmpty(
    const sk_dllist_t  *list);


/*** Deque operations ***/

/**
 *    Fills the pointer pointed to by 'data' with the pointer at the
 *    tail of the list.  Returns 0 if successful, -1 if empty.
 */
int
skDLListPeekTail(
    const sk_dllist_t  *list,
    void              **data);

/**
 *    Fills the pointer pointed to by 'data' with the pointer at the
 *    head of the list.  Returns 0 if successful, -1 if empty.
 */
int
skDLListPeekHead(
    const sk_dllist_t  *list,
    void              **data);

/**
 *    Fills the pointer pointed to by 'data' with the pointer at the
 *    tail of the list, and removes that element from the list.
 *    'data' may be NULL, in which case the pointer will not be
 *    returned.  Returns 0 if successful, -1 if empty.
 */
int
skDLListPopTail(
    sk_dllist_t        *list,
    void              **data);

/**
 *    Fills the pointer pointed to by 'data' with the pointer at the
 *    head of the list, and removes that element from the list.
 *    'data' may be NULL, in which case the pointer will not be
 *    returned.  Returns 0 if successful, -1 if empty.
 */
int
skDLListPopHead(
    sk_dllist_t        *list,
    void              **data);

/**
 *    Adds the pointer 'data' to the tail of the list.  Returns 0 if
 *    successful, -1 if there is a memory allocation error.
 */
int
skDLListPushTail(
    sk_dllist_t        *list,
    void               *data);

/**
 *    Adds the pointer 'data' to the head of the list.  Returns 0 if
 *    successful, -1 if there is a memory allocation error.
 */
int
skDLListPushHead(
    sk_dllist_t        *list,
    void               *data);

/**
 *    Joins the doubly-linked lists 'head' and 'tail' into a single
 *    list by appending tail to head.  After this call, 'head' will
 *    contain the elements of both 'head' and 'tail', and the 'tail'
 *    pointer will be defunct (destroyed).  Returns -1 if the free
 *    functions for 'head' and 'tail' differ, otherwise 0.
 */
int
skDLListJoin(
    sk_dllist_t        *head,
    sk_dllist_t        *tail);

/*** Iterators ***/

/**
 *    Assigns an already existing sk_dll_iter_t (iterator) to a list.
 *    The iterator starts out pointing to nothing.
 */
void
skDLLAssignIter(
    sk_dll_iter_t      *iter,
    sk_dllist_t        *list);

/**
 *    Deletes the item the iterator is pointing to from its list.
 *    Afterward, the value is still pointed to by 'iter' and can be
 *    retrieved by skDLLIterValue().  The value pointed to by iter is
 *    not freed.  Returns 0 on success, -1 if the iterator isn't
 *    pointing to anything.
 */
int
skDLLIterDel(
    sk_dll_iter_t      *iter);

/**
 *    Adds an element to the list 'iter' is referencing after the
 *    element pointed to by 'iter'.  If 'iter is pointing to nothing,
 *    it will be inserted at the head.  Returns 0 if successful, -1 if
 *    there is a memory allocation error.
 */
int
skDLLIterAddAfter(
    sk_dll_iter_t      *iter,
    void               *data);

/**
 *    Adds an element to the list 'iter' is referencing before the
 *    element pointed to by 'iter'.  If 'iter is pointing to nothing,
 *    it will be inserted at the tail.  Returns 0 if successful, -1 if
 *    there is a memory allocation error.
 */
int
skDLLIterAddBefore(
    sk_dll_iter_t      *iter,
    void               *data);

/**
 *    Moves the iterator forward in the list, filling 'data' with the
 *    pointer 'iter' then points to.  If 'iter' is pointing to
 *    nothing, will move to the head of the list.  'data' may be NULL,
 *    in which case the pointer will not be returned.  If 'iter' is
 *    pointing at the tail of the list, iter will afterward point to
 *    nothing.  Returns 0 if successful, -1 if 'iter' was already at
 *    the tail of the list.
 */
int
skDLLIterForward(
    sk_dll_iter_t      *iter,
    void              **data);

/**
 *    Moves the iterator backward in the list, filling 'data' with the
 *    pointer 'iter' then points to.  If 'iter' is pointing to
 *    nothing, will move to the tail of the list.  'data' may be NULL,
 *    in which case the pointer will not be returned.  If 'iter' is
 *    pointing at the head of the list, iter will afterward point to
 *    nothing.  Returns 0 if successful, -1 if 'iter' was already at
 *    the head of the list.
 */
int
skDLLIterBackward(
    sk_dll_iter_t      *iter,
    void              **data);

/**
 *    Fills the pointer pointed to by 'data' with the value pointed to
 *    by the iterator.  Returns 0 if successful, -1 if the iterator is
 *    pointing at nothing.
 */
int
skDLLIterValue(
    const sk_dll_iter_t    *iter,
    void                  **data);

#ifdef __cplusplus
}
#endif
#endif /* _SKDLLIST_H */

/*
** Local Variables:
** mode:c
** indent-tabs-mode:nil
** c-basic-offset:4
** End:
*/
