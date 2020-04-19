/*
** Copyright (C) 2011-2020 by Carnegie Mellon University.
**
** @OPENSOURCE_LICENSE_START@
** See license information in ../../LICENSE.txt
** @OPENSOURCE_LICENSE_END@
*/
#ifndef _SKHEAP_RWREC_H
#define _SKHEAP_RWREC_H
#ifdef __cplusplus
extern "C" {
#endif

#include <silk/silk.h>

RCSIDENTVAR(rcsID_SKHEAP_RWREC_H, "$SiLK: skheap-rwrec.h ef14e54179be 2020-04-14 21:57:45Z mthomas $");

/*
**  skheap-rwrec.h
**
**  A heap (priority queue) for rwRec pointers
**
**
*/

#include <silk/rwrec.h>

/* The Heap object */
typedef struct skrwrecheap_st skrwrecheap_t;

/* Returns a new heap with space for initial_entries.  Returns NULL on
 * memory allocation faulure. */
skrwrecheap_t *
skRwrecHeapCreate(
    size_t              initial_entries);

/* Destroy the heap (does not destroy the rwRecs in the heap */
void
skRwrecHeapDestroy(
    skrwrecheap_t      *heap);

/* Adds an rwRec to the heap.  Returns 0 on success, -1 on memory
 * allocation failure. */
int
skRwrecHeapInsert(
    skrwrecheap_t      *heap,
    rwRec              *rec);

/* Returns a pointer to the top entry on the heap, NULL if the heap is
 * empty */
const rwRec *
skRwrecHeapPeek(
    skrwrecheap_t      *heap);

/* Removes the top entry on the heap, returns that item.  Returns NULL
 * if the heap is empty.  */
rwRec *
skRwrecHeapPop(
    skrwrecheap_t      *heap);

/* Return the number of entries in the heap. */
size_t
skRwrecHeapCountEntries(
    const skrwrecheap_t    *heap);

/* Return the capacity of the heap */
size_t
skRwrecHeapGetCapacity(
    const skrwrecheap_t    *heap);

#ifdef __cplusplus
}
#endif
#endif /* _SKHEAP_RWREC_H */

/*
** Local Variables:
** mode:c
** indent-tabs-mode:nil
** c-basic-offset:4
** End:
*/
