/*
** Copyright (C) 2001-2020 by Carnegie Mellon University.
**
** @OPENSOURCE_LICENSE_START@
** See license information in ../../LICENSE.txt
** @OPENSOURCE_LICENSE_END@
*/

/*
**  skheap.h
**
**    The heap (priority queue) data structure.
**
**    Mark Thomas
**
*/
#ifndef _SKHEAP_H
#define _SKHEAP_H
#ifdef __cplusplus
extern "C" {
#endif

#include <silk/silk.h>

RCSIDENTVAR(rcsID_SKHEAP_H, "$SiLK: skheap.h ef14e54179be 2020-04-14 21:57:45Z mthomas $");

/**
 *  @file
 *
 *    Implementation of a heap (priority queue) data structure.
 *
 *    This file is part of libsilk.
 */


/**
 *    Return value to indicate success
 */
#define SKHEAP_OK 0

/**
 *    Return value when attempting to add node to a full heap.
 */
#define SKHEAP_ERR_FULL 3

/**
 *    Return value when attempting to get or to delete the top element
 *    of an empty heap.
 */
#define SKHEAP_ERR_EMPTY 4

/**
 *    Return value when heap iterator reaches end-of-data.
 */
#define SKHEAP_NO_MORE_ENTRIES 5


/**
 *    The Heap object
 */
typedef struct skheap_st skheap_t;

/**
 *    The nodes stored in the heap data structure
 */
typedef void* skheapnode_t;

/**
 *    Used to iterate over the entries in the heap
 */
typedef struct skheapiterator_st skheapiterator_t;


/**
 *    The signature of the comparator function that the caller must
 *    pass to the skHeapCreate() function.  The function takes two
 *    skheapnode_t's, node1 and node2, and returns:
 *     -- an integer value > 0 if node1 should be a closer to the root
 *        of the heap than node2
 *     -- an integer value < 0 if node2 should be a closer to the root
 *        of the heap than node1
 *
 *    To use a comparison function that takes a context value, create
 *    the heap with skHeapCreate2() which uses a comparator that
 *    matches the signature of skheapcmp2fn_t below.
 *
 *    For example: a heap with the lowest value at the root could
 *    return 1 if node1<node2.  If an implementation of this function
 *    wraps memcmp(), the function should
 *
 *        return -memcmp(node1,node2,...)
 *
 *    to order the values from low to high, and
 *
 *        return memcmp(node1,node2,...)
 *
 *    to order the values from high to low.
 *
 *    (When computing a Top-N, the lowest value should be at the root
 *    of the heap.)
 */
typedef int (*skheapcmpfn_t)(
    const skheapnode_t  node1,
    const skheapnode_t  node2);

/**
 *    The signature of the comparator function that the caller must
 *    pass to the skHeapCreate2() function.  This function is similar
 *    to skheapcmpfn_t, except it takes a third argument that is the
 *    pointer the caller supplied as the 'cmpfun_data' argument to
 *    skHeapCreate2().
 *
 *    If the comparator does not take a third argument, create the
 *    heap using skHeapCreate().
 */
typedef int (*skheapcmp2fn_t)(
    const skheapnode_t  node1,
    const skheapnode_t  node2,
    void               *cmp_data);


/**
 *    Similar to skHeapCreate2(), but the 'cmpfun' does not take a
 *    caller-provided argument.
 */
skheap_t *
skHeapCreate(
    skheapcmpfn_t       cmpfun,
    uint32_t            init_count,
    uint32_t            entry_size,
    skheapnode_t       *memory_buf);


/**
 *    Creates a heap that is initially capable of holding 'init_count'
 *    entries (skheapnode_t's) each of size 'entry_size'.  The
 *    'cmpfun' determines how the nodes are ordered in the heap.  The
 *    'cmpfun_data' is passed as the third argument to
 *    'cmpfun'.
 *
 *    If the 'memory_buf' is NULL, the heap manages the memory for
 *    entries itself.  An attempt to insert more than 'init_count'
 *    entries into the heap causes the heap to reallocate memory
 *    for the entries.
 *
 *    If 'memory_buf' is non-NULL, it must be a block of memory whose
 *    size is at least init_count * entry_size bytes.  The heap
 *    uses 'memory_buf' to store the entries, and the 'init_count'
 *    value represents the maximum size of the heap.
 */
skheap_t *
skHeapCreate2(
    skheapcmp2fn_t      cmpfun,
    uint32_t            init_count,
    uint32_t            entry_size,
    skheapnode_t       *memory_buf,
    void               *cmpfun_data);


/**
 *    Set the number of entries in the heap to 0, effectively emptying
 *    the heap.  This function does not modify the bytes in the data
 *    array.
 *
 *    (To determine if the heap is empty, compare the return value of
 *    skHeapGetNumberEntries() to zero.)
 */
void
skHeapEmpty(
    skheap_t           *heap);


/**
 *    Remove the entry at the top of the heap.  If 'top_node' is
 *    non-NULL, the removed entry is copied there---that is,
 *    'entry_size' bytes of data is written to the location
 *    pointed to by 'top_node'.  Return SKHEAP_ERR_EMPTY if the heap
 *    is empty.
 *
 *    See also skHeapPeekTop() and skHeapReplaceTop().
 */
int
skHeapExtractTop(
    skheap_t           *heap,
    skheapnode_t        top_node);


/**
 *    Destroy an existing heap.  This function does not modify the
 *    data array when using caller-supplied data---that is, when a
 *    non-NULL 'memory_buf' value was passed to skHeapCreate().
 *
 *    If the 'heap' parameter is NULL, this function immediately
 *    returns.
 */
void
skHeapFree(
    skheap_t           *heap);


/**
 *    Return the number of entries the heap can accommodate.  To get
 *    the number of free entries in the heap, subtract the result of
 *    skHeapGetNumberEntries() from this function's result.
 */
uint32_t
skHeapGetCapacity(
    const skheap_t     *heap);


/**
 *    Return the size of each element that is stored in the heap.
 */
uint32_t
skHeapGetEntrySize(
    const skheap_t     *heap);


/**
 *    Return the number of entries currently in the heap.
 */
uint32_t
skHeapGetNumberEntries(
    const skheap_t     *heap);


/**
 *    Add the entry at 'new_node' to the heap.  Return SKHEAP_ERR_FULL
 *    if the heap is full.  This function reads 'entry_size' bytes
 *    of data from the location pointed to by 'new_node'.
 */
int
skHeapInsert(
    skheap_t           *heap,
    const skheapnode_t  new_node);


/**
 *    Return an skheapiterator_t that can be used to iterate over the
 *    nodes in 'heap'.  Return NULL if the iterator cannot be created.
 *    If 'direction' is non-negative, the iterator starts at the root
 *    and works toward the leaves; otherwise, the iterator works from
 *    the leaves to the root.  The iterator visits all nodes on one
 *    level before moving to the next.  By calling skHeapSortEntries()
 *    before creating the iterator, the nodes are traversed in the
 *    order determined by the 'cmpfun' that was specified when the
 *    heap was created.
 */
skheapiterator_t *
skHeapIteratorCreate(
    const skheap_t     *heap,
    int                 direction);


/**
 *    Free the memory associated with the heap iterator 'iter'.
 *    Does nothing if 'iter' is NULL.
 */
void
skHeapIteratorFree(
    skheapiterator_t   *iter);


/**
 *    Set 'heap_node' to the memory location of the next entry.
 *    Return SKHEAP_OK if 'heap_node' was set to the next node; return
 *    SKHEAP_NO_MORE_ENTRIES if all nodes have been visited.
 */
int
skHeapIteratorNext(
    skheapiterator_t   *iter,
    skheapnode_t       *heap_node);


/**
 *    Set the value of 'top_node' to point at the entry at the top of
 *    the heap.  This function does not modify the heap.  The caller
 *    must not modify the data that 'top_node' is pointing to.  Return
 *    SKHEAP_ERR_EMPTY if the heap is empty.
 *
 *    See also skHeapExtractTop() and skHeapReplaceTop().
 */
int
skHeapPeekTop(
    const skheap_t     *heap,
    skheapnode_t       *top_node);


/**
 *    Remove the entry at the top of the heap and insert a new entry
 *    into the heap.  If 'top_node' is non-NULL, the removed entry is
 *    copied there---that is, 'entry_size' bytes of data is
 *    written to the location pointed to by 'top_node'.  This function
 *    reads 'entry_size' bytes of data from the location pointed
 *    to by 'new_node'.  Return SKHEAP_ERR_EMPTY if the heap is empty
 *    and do NOT add 'new_node' to the heap.
 *
 *    See also skHeapExtractTop() and skHeapPeekTop().
 */
int
skHeapReplaceTop(
    skheap_t           *heap,
    const skheapnode_t  new_node,
    skheapnode_t        top_node);


/**
 *    Sort the entries in 'heap'.  (Note that a sorted heap is still a
 *    heap).  This can be used to order the entries before calling
 *    skHeapIteratorCreate(), or for sorting the entries in the
 *    user-defined storage.
 */
int
skHeapSortEntries(
    skheap_t           *heap);

#ifdef __cplusplus
}
#endif
#endif /* _SKHEAP_H */

/*
** Local Variables:
** mode:c
** indent-tabs-mode:nil
** c-basic-offset:4
** End:
*/
