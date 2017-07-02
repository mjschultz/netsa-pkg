/*
** Copyright (C) 2014-2017 by Carnegie Mellon University.
**
** @OPENSOURCE_LICENSE_START@
** See license information in ../../LICENSE.txt
** @OPENSOURCE_LICENSE_END@
*/
#ifndef _SKREDBLACK_H
#define _SKREDBLACK_H
#ifdef __cplusplus
extern "C" {
#endif

#include <silk/silk.h>

RCSIDENTVAR(rcsID_SKREDBLACK_H, "$SiLK: skredblack.h efd886457770 2017-06-21 18:43:23Z mthomas $");

/*
 *  skredblack.h
 *
 *    Red Black Balanced Tree
 *
 *
 *    Based on public domain code written by
 *
 *      > Created (Julienne Walker): August 23, 2003
 *      > Modified (Julienne Walker): March 14, 2008
 *
 *    See http://eternallyconfuzzled.com/jsw_home.aspx for her useful
 *    tutorials.
 *
 *
 *    Incorporated into the SiLK sources in June, 2014.
 */

/**
 *    sk_rbtree_t is the handle to the Red Black Tree Structure.
 */
typedef struct sk_rbtree_st sk_rbtree_t;


/**
 *    sk_rbtree_iter_t is a handle for iterating over the objects in
 *    the Red Black Tree Structure.
 */
typedef struct sk_rbtree_iter_st sk_rbtree_iter_t;


/**
 *    The user-defined comparison function for nodes 'p1' and 'p2'
 *    which is expected to have the return values in the style of
 *    strcmp(3): that is, return an integer greater than, equal to, or
 *    less than 0, according as the node 'p1' is greater than, equal
 *    to, or less than the node 'p2'.
 */
typedef int
(*sk_rbtree_cmp_fn_t)(
    const void         *p1,
    const void         *p2,
    const void         *ctx_data);


/**
 *    The user-defined free function for freeing data inserted into
 *    the tree.  A function with this signature may be passed to
 *    sk_rbtree_create() to have the tree automatically free data when
 *    sk_rbtree_remove() or sk_rbtree_destroy() are called.
 */
typedef void
(*sk_rbtree_free_fn_t)(
    void               *node_data);


/**
 *    Create and initialize an empty red black tree with user-defined
 *    comparison and data release operations.  The tree is stored in
 *    the location referenced by 'tree'.
 *
 *    'cmp_fn' is the user-defined data comparison function.  If NULL,
 *    the default comparison function compares the addresses of the
 *    'data' parameters passed to sk_rbtree_insert().
 *
 *    'free_fn' is the user-defined data release function which is
 *    called when a node is erased or the tree is destroyed.  If NULL,
 *    the user is responsible for freeing the data in the tree.
 *
 *    'ctx_data' is context pointer which is passed to the 'cmp_fn'.
 *    It may be NULL.
 *
 *    Return SK_RBTREE_OK.  Exit on memory allocation error.
 *
 *    The returned tree must be released with sk_rbtree_destroy().
 */
int
sk_rbtree_create(
    sk_rbtree_t           **tree,
    sk_rbtree_cmp_fn_t      cmp_fn,
    sk_rbtree_free_fn_t     free_fn,
    const void             *ctx_data);


/**
 *    Destroy the red black tree located in the memory referenced by
 *    'tree'.  Do nothing if 'tree' or the location it references are
 *    NULL.
 *
 *    The tree must have been created using sk_rbtree_create().
 */
void
sk_rbtree_destroy(
    sk_rbtree_t       **tree);


/**
 *    Search for a copy of the specified data value 'data' in the red
 *    black tree 'tree'.
 *
 *    Return a pointer to the data value stored in the tree, or a NULL
 *    pointer if no matching data could be found.
 */
void *
sk_rbtree_find(
    const sk_rbtree_t  *tree,
    const void         *data);


/**
 *    Insert the data referenced by 'data' into the red black tree
 *    'tree'.  After this call successfully returns, 'tree' assumes it
 *    owns 'data' and will call the user-specified 'free_fn' when the
 *    data is erased (sk_rbtree_remove()) or the tree is destroyed.
 *
 *    When 'found' is not NULL, it is used to return the inserted
 *    'data' or a previously inserted piece of data.
 *
 *    Return SK_RBTREE_OK and fill 'found' with the 'data' parameter
 *    when no previously inserted item matches 'data' and the
 *    insertion is successful.
 *
 *    Return SK_RBTREE_ERR_DUPLICATE, fill 'found' with the previously
 *    inserted item, and do not modify the tree when 'data' matches an
 *    existing item.
 *
 *    Return SK_RBTREE_ERR_ALLOC on a memory allocation error.
 */
int
sk_rbtree_insert(
    sk_rbtree_t        *tree,
    const void         *data,
    const void        **found);


/**
 *    Remove a node from the red black tree 'tree' that matches the
 *    user-specified data value 'data'.
 *
 *    If 'data' is not found in the tree, return
 *    SK_RBTREE_ERR_NOT_FOUND.
 *
 *    When 'data' is in the tree, return SK_RBTREE_OK and ...
 *
 *        if 'found' is not NULL, fill it with the location of the
 *        removed item and ignore any 'free_fn'.
 *
 *        if 'found' is NULL and the 'free_fn' was specified to
 *        sk_rbtree_create(), call that function to free the data.
 *
 *        if 'found' is NULL and the 'free_fn' is NULL, assume the
 *        user is managing the memory.
 */
int
sk_rbtree_remove(
    sk_rbtree_t        *tree,
    const void         *data,
    const void        **found);


/**
 *    Return the number of nodes in a red black tree 'tree'.  Return 0
 *    when 'tree' is NULL.
 */
size_t
sk_rbtree_size(
    const sk_rbtree_t  *tree);


/**
 *    Create and return a new iterator object.
 *
 *    Note: The iterator object is not initialized until
 *    sk_rbtree_iter_bind_first() or sk_rbtree_iter_bind_last() is
 *    called.  The pointer must be released with
 *    sk_rbtree_iter_free().
 *
 *    Note: Modifying the tree while using the iterator is undefined.
 */
sk_rbtree_iter_t *
sk_rbtree_iter_create(
    void);


/**
 *    Release the iterator object 'iter'.  Does nothing if 'iter' is
 *    NULL.
 *
 *    The object must have been created with sk_rbtree_iter_create().
 */
void
sk_rbtree_iter_free(
    sk_rbtree_iter_t   *iter);


/**
 *    Initialize the iterator object 'iter' to the smallest valued
 *    node in the red black tree 'tree' and return a pointer to the
 *    smallest data value or return NULL if 'tree' is empty.
 *
 *    The caller should use sk_rbtree_iter_next() to move to the next
 *    value in ascending order in the tree.
 */
void *
sk_rbtree_iter_bind_first(
    sk_rbtree_iter_t   *iter,
    const sk_rbtree_t  *tree);


/**
 *    Initialize the iterator object 'iter' to the largest valued node
 *    in the red black tree 'tree' and return a pointer to the largest
 *    data value or return NULL if 'tree' is empty.
 *
 *    The caller should use sk_rbtree_iter_prev() to move to the next
 *    value in descending order in the tree.
 */
void *
sk_rbtree_iter_bind_last(
    sk_rbtree_iter_t   *iter,
    const sk_rbtree_t  *tree);


/**
 *    Move the initialized iterator 'iter' to the next value in
 *    ascending order and return that value.  Return NULL when the
 *    iterator is already on the largest value.
 *
 *    The 'iter' object must have been previously initialized by a
 *    call to sk_rbtree_iter_bind_first() (most likely) or
 *    sk_rbtree_iter_bind_last().
 */
void*
sk_rbtree_iter_next(
    sk_rbtree_iter_t   *iter);


/**
 *    Move the initialized iterator 'iter' to the next value in
 *    descending order and return that value.  Return NULL when the
 *    iterator is already on the smallest value.
 *
 *    The 'iter' object must have been previously initialized by a
 *    call to sk_rbtree_iter_bind_last() (most likely) or
 *    sk_rbtree_iter_bind_first().
 */
void*
sk_rbtree_iter_prev(
    sk_rbtree_iter_t   *iter);


/**
 *    Signature of a user-defined function for printing the data.
 */
typedef void
(*sk_rbtree_print_data_fn_t)(
     FILE               *fp,
     const void         *data);


/**
 *    Print the structure of the red-black tree to the file handle
 *    'fp'.  If 'print_data' is provided, it will be called to print
 *    the entries in the red-black tree.
 */
void
sk_rbtree_debug_print(
    const sk_rbtree_t          *tree,
    FILE                       *fp,
    sk_rbtree_print_data_fn_t   print_data);


/**
 *    sk_rbtree_status_t defines the values returned by the functions
 *    declared above.
 */
enum sk_rbtree_status_en {
    SK_RBTREE_OK = 0,
    SK_RBTREE_ERR_DUPLICATE = -1,
    SK_RBTREE_ERR_NOT_FOUND = -2,
    SK_RBTREE_ERR_ALLOC = -3,
    SK_RBTREE_ERR_PARAM = -4
};
typedef enum sk_rbtree_status_en sk_rbtree_status_t;


#ifdef __cplusplus
}
#endif
#endif /* _SKREDBLACK_H */

/*
** Local Variables:
** mode:c
** indent-tabs-mode:nil
** c-basic-offset:4
** End:
*/
