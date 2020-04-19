/*
** Copyright (C) 2007-2020 by Carnegie Mellon University.
**
** @OPENSOURCE_LICENSE_START@
** See license information in ../../LICENSE.txt
** @OPENSOURCE_LICENSE_END@
*/

/*
**  Am implementation of doubly-linked lists.
**
*/


#include <silk/silk.h>

RCSIDENT("$SiLK: skdllist.c ef14e54179be 2020-04-14 21:57:45Z mthomas $");

#include <silk/utils.h>
#include <silk/skdllist.h>

/* LOCAL DEFINES AND TYPEDEFS */

typedef enum sk_dll_dir_en {
    FORWARD = 1,
    BACKWARD = 0,
    TAIL = 0,
    HEAD = 1
} sk_dll_dir_t;

/* a node is equivalent to an iterator */
typedef sk_dll_iter_t sk_dll_node_t;

struct sk_dllist_st {
    sk_dll_node_t    list;
    sk_dll_free_fn_t data_free_fn;
};


/* LOCAL VARIABLE DEFINITIONS */

static void *null_value = &null_value;


/* FUNCTION DEFINITIONS */

sk_dllist_t *
skDLListCreate(
    sk_dll_free_fn_t    free_fn)
{
    sk_dllist_t *list = (sk_dllist_t *)malloc(sizeof(*list));

    if (list != NULL) {
        list->list.data = null_value;
        list->list.link[TAIL] = list->list.link[HEAD] = &list->list;
        list->data_free_fn = free_fn;
    }

    return list;
}


void
skDLListDestroy(
    sk_dllist_t        *list)
{
    sk_dll_node_t *node, *next;

    if (NULL == list) {
        return;
    }

    node = list->list.link[TAIL];
    while (node->data != null_value) {
        if (list->data_free_fn) {
            list->data_free_fn(node->data);
        }
        next = node->link[BACKWARD];
        free(node);
        node = next;
    }
    free(list);
}


int
skDLListIsEmpty(
    const sk_dllist_t  *list)
{
    assert(list);
    return list->list.link[TAIL] == &list->list;
}


static int
sk_dll_peek(
    const sk_dllist_t  *list,
    void              **data,
    sk_dll_dir_t        dir)
{
    sk_dll_node_t *node;

    assert(list);
    assert(data);

    node = list->list.link[dir];

    if (node->data == null_value) {
        return -1;
    }
    *data = node->data;

    return 0;
}


int
skDLListPeekTail(
    const sk_dllist_t  *list,
    void              **data)
{
    return sk_dll_peek(list, data, TAIL);
}


int
skDLListPeekHead(
    const sk_dllist_t  *list,
    void              **data)
{
    return sk_dll_peek(list, data, HEAD);
}


static void
sk_dll_node_del(
    sk_dll_node_t      *node)
{
    node->link[FORWARD]->link[BACKWARD] = node->link[BACKWARD];
    node->link[BACKWARD]->link[FORWARD] = node->link[FORWARD];
    free(node);
}


static int
sk_dll_pop(
    sk_dllist_t        *list,
    void              **data,
    sk_dll_dir_t        dir)
{
    sk_dll_node_t *node;

    assert(list);

    node = list->list.link[dir];
    if (node->data == null_value) {
        return -1;
    }
    if (data != NULL) {
        *data = node->data;
    }
    sk_dll_node_del(node);

    return 0;
}


int
skDLListPopTail(
    sk_dllist_t        *list,
    void              **data)
{
    return sk_dll_pop(list, data, TAIL);
}


int
skDLListPopHead(
    sk_dllist_t        *list,
    void              **data)
{
    return sk_dll_pop(list, data, HEAD);
}


static int
sk_dll_node_add(
    sk_dll_iter_t      *iter,
    void               *data,
    sk_dll_dir_t        dir)
{
    sk_dll_node_t *node, *truenode;;

    assert(iter);

    node = (sk_dll_node_t *)malloc(sizeof(*node));
    if (node == NULL) {
        return -1;
    }

    truenode = iter->link[FORWARD]->link[BACKWARD];

    node->data = data;
    node->link[dir] = truenode->link[dir];
    node->link[1-dir] = truenode;

    node->link[FORWARD]->link[BACKWARD] = node;
    node->link[BACKWARD]->link[FORWARD] = node;

    if (truenode != iter) {
        iter->link[FORWARD] = truenode->link[FORWARD];
        iter->link[BACKWARD] = truenode->link[BACKWARD];
    }

    return 0;
}


int
skDLListPushTail(
    sk_dllist_t        *list,
    void               *data)
{
    return sk_dll_node_add(&list->list, data, TAIL);
}


int
skDLListPushHead(
    sk_dllist_t        *list,
    void               *data)
{
    return sk_dll_node_add(&list->list, data, HEAD);
}


int
skDLListJoin(
    sk_dllist_t        *head,
    sk_dllist_t        *tail)
{
    sk_dll_node_t *tail_h, *tail_t;

    assert(head);
    assert(tail);

    /* Return an error if the free functions do not match. */
    if (head->data_free_fn != tail->data_free_fn) {
        return -1;
    }

    /* If tail is empty, destroy the tail and return */
    if (skDLListIsEmpty(tail)) {
        skDLListDestroy(tail);
        return 0;
    }

    /* Save links to the head and tail nodes of tail */
    tail_h = tail->list.link[HEAD];
    tail_t = tail->list.link[TAIL];

    /* Reset tail to empty, and destroy */
    tail->list.link[HEAD] = tail->list.link[TAIL] = &tail->list;
    skDLListDestroy(tail);

    /* Update the links to insert the list */
    tail_h->link[BACKWARD] = head->list.link[TAIL];
    tail_t->link[FORWARD]  = &head->list;
    head->list.link[TAIL]->link[FORWARD] = tail_h;
    head->list.link[TAIL] = tail_t;

    return 0;
}


void
skDLLAssignIter(
    sk_dll_iter_t      *iter,
    sk_dllist_t        *list)
{
    assert(list);
    (*iter) = list->list;
}


static int
sk_dll_iter_get_next(
    sk_dll_iter_t      *iter,
    void              **data,
    sk_dll_dir_t        dir)
{
    *iter = *iter->link[dir];
    if (iter->data == null_value) {
        return -1;
    }
    if (data != NULL) {
        *data = iter->data;
    }
    return 0;
}


int
skDLLIterForward(
    sk_dll_iter_t      *iter,
    void              **data)
{
    return sk_dll_iter_get_next(iter, data, FORWARD);
}


int
skDLLIterBackward(
    sk_dll_iter_t      *iter,
    void              **data)
{
    return sk_dll_iter_get_next(iter, data, BACKWARD);
}


int
skDLLIterDel(
    sk_dll_iter_t      *iter)
{
    assert(iter);
    if (iter->data == null_value) {
        return -1;
    }
    sk_dll_node_del(iter->link[FORWARD]->link[BACKWARD]);
    return 0;
}


int
skDLLIterAddAfter(
    sk_dll_iter_t      *iter,
    void               *data)
{
    return sk_dll_node_add(iter, data, FORWARD);
}


int
skDLLIterAddBefore(
    sk_dll_iter_t      *iter,
    void               *data)
{
    return sk_dll_node_add(iter, data, BACKWARD);
}


int
skDLLIterValue(
    const sk_dll_iter_t    *iter,
    void                  **data)
{
    assert(iter);
    assert(data);

    if (iter->data == null_value) {
        return -1;
    }
    *data = iter->data;
    return 0;
}


/*
** Local Variables:
** mode:c
** indent-tabs-mode:nil
** c-basic-offset:4
** End:
*/
