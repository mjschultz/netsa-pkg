/*
** Copyright (C) 2014-2017 by Carnegie Mellon University.
**
** @OPENSOURCE_LICENSE_START@
** See license information in ../../LICENSE.txt
** @OPENSOURCE_LICENSE_END@
*/

/*
 *  Red Black balanced tree library
 *
 *    This code is based on the following:
 *
 *  ************************************************************
 *
 *    Red Black balanced tree library
 *
 *      > Created (Julienne Walker): August 23, 2003
 *      > Modified (Julienne Walker): March 14, 2008
 *
 *    This code is in the public domain. Anyone may
 *    use it or change it in any way that they see
 *    fit. The author assumes no responsibility for
 *    damages incurred through use of the original
 *    code or any variations thereof.
 *
 *    It is requested, but not required, that due
 *    credit is given to the original author and
 *    anyone who has modified the code through
 *    a header comment, such as this one.
 *
 *  ************************************************************
 *
 */

#include <silk/silk.h>

RCSIDENT("$SiLK: skredblack.c efd886457770 2017-06-21 18:43:23Z mthomas $");

#include <silk/skredblack.h>
#include <silk/utils.h>


/* LOCAL DEFINES AND TYPEDEFS */

/* Tallest allowable tree */
#ifndef RBT_HEIGHT_LIMIT
#define RBT_HEIGHT_LIMIT     64
#endif

enum rbtree_color_en {
    RBT_BLACK = 0,
    RBT_RED = 1
};
typedef enum rbtree_color_en rbtree_color_t;

#define RBT_LEFT   0
#define RBT_RIGHT  1


/*
 *    Typedef for the terminal node
 */
#define RBT_NIL    &rbtree_nil

/*
 *    Structure of a false tree root node and of the terminal nodes.
 */
#define RBT_FALSE_ROOT  {{RBT_NIL, RBT_NIL}, NULL, RBT_BLACK}

/*
 *    Return true if 'rnir_node' is red.
 */
#define rbtree_node_is_red(rnir_node)           \
    (RBT_RED == (rnir_node)->color)


/*
 *    rbtree_node_t defines the elements in the red-black tree.
 */
typedef struct rbtree_node_st rbtree_node_t;
struct rbtree_node_st {
    /* The childred: Left (0) and Right (1) */
    rbtree_node_t      *link[2];
    /* User-defined content */
    const void         *data;
    /* Node's color */
    rbtree_color_t      color;
};


/*
 *    sk_rbtree_iter_t is used to iterate over the items in the tree
 */
struct sk_rbtree_iter_st {
    /* Paired tree */
    const sk_rbtree_t      *tree;
    /* Current node */
    const rbtree_node_t    *cur;
    /* Traversal path */
    const rbtree_node_t    *path[RBT_HEIGHT_LIMIT];
    /* Current depth in 'path' */
    size_t                  depth;
};
/* typedef struct sk_rbtree_iter_st sk_rbtree_iter_t; */


/*
 *    The red-black tree sk_rbtree_t
 */
struct sk_rbtree_st {
    /* The top of the tree */
    rbtree_node_t      *root;
    /* The comparison function */
    sk_rbtree_cmp_fn_t  cmp_fn;
    /* The data free() function: May be NULL */
    sk_rbtree_free_fn_t free_fn;
    /* User's context pointer */
    const void         *ctx;
    /* Number of items in the tree */
    size_t              size;
};
/* typedef struct sk_rbtree_st sk_rbtree_t; */


/* LOCAL VARIABLES */

/*
 *    Global variable used as the terminal node.
 */
static rbtree_node_t rbtree_nil = RBT_FALSE_ROOT;


/* FUNCTION DEFINITIONS */

/**
 *    Default comparison function when none provided by the user.
 *
 *    Return an integer greater than, equal to, or less than 0,
 *    according as the node 'p1' is greater than, equal to, or less
 *    than the node 'p2'.
 */
static int
rbtree_default_compare(
    const void         *p1,
    const void         *p2,
    const void  UNUSED(*ctx_data))
{
    return ((p1 < p2) ? -1 : (p1 > p2));
}


/**
 *    Perform a single red black rotation in the specified direction.
 *    This function assumes that all nodes are valid for a rotation.
 *
 *    'root' is the original root to rotate around.
 *    'dir' is the direction to rotate (0 = left, 1 = right).
 *
 *    Return the new root ater rotation
 */
static rbtree_node_t*
rbtree_rotate_single(
    rbtree_node_t      *root,
    int                 dir)
{
    rbtree_node_t *save = root->link[!dir];

    root->link[!dir] = save->link[dir];
    save->link[dir] = root;

    root->color = RBT_RED;
    save->color = RBT_BLACK;

    return save;
}


/**
 *    Perform a double red black rotation in the specified direction.
 *    This function assumes that all nodes are valid for a rotation.
 *
 *    'root' is the original root to rotate around.
 *    'dir' is the direction to rotate (0 = left, 1 = right).
 *
 *    Return the new root after rotation.
 */
static rbtree_node_t*
rbtree_rotate_double(
    rbtree_node_t      *root,
    int                 dir)
{
    root->link[!dir] = rbtree_rotate_single(root->link[!dir], !dir);

    return rbtree_rotate_single(root, dir);
}


/**
 *    Create and initialize a new red black node with a copy of the
 *    data. This function does not insert the new node into a tree.
 *
 *    'data' is the data value to store in this node.
 *
 *    Return a pointer to the new node, or NULL on allocation failure.
 *
 *    The data for this node must be freed using the same tree's
 *    free_fn function. The returned pointer must be freed using C's
 *    free().
 */
static rbtree_node_t*
rbtree_node_create(
    const void         *data)
{
    rbtree_node_t *rn;

    rn = (rbtree_node_t*)malloc(sizeof *rn);
    if (rn == NULL) {
        return NULL;
    }

    rn->color = RBT_RED;
    rn->data = data;
    rn->link[RBT_LEFT] = rn->link[RBT_RIGHT] = RBT_NIL;

    return rn;
}


/**
 *    Initialize the iterator object 'iter' and attach it to 'tree'.
 *    The user-specified direction 'dir' determines whether to begin
 *    iterator at the smallest (0) or largest (1) valued node.
 *
 *    Return a pointer to the smallest or largest data value.
 */
static void*
rbtree_iter_start(
    sk_rbtree_iter_t   *iter,
    const sk_rbtree_t  *tree,
    int                 dir)
{
    iter->tree = tree;
    iter->cur = tree->root;
    iter->depth = 0;

    if (RBT_NIL == iter->cur) {
        return NULL;
    }
    while (iter->cur->link[dir] != RBT_NIL) {
        iter->path[iter->depth++] = iter->cur;
        iter->cur = iter->cur->link[dir];
    }
    return (void*)iter->cur->data;
}


/**
 *    Move the initialized iterator 'iter' in the user-specified
 *    direction 'dir' (0 = ascending, 1 = descending).
 *
 *    Return a pointer to the next data value in the specified
 *    direction.
 *
 */
static void*
rbtree_iter_move(
    sk_rbtree_iter_t   *iter,
    int                 dir)
{
    if (iter->cur->link[dir] != RBT_NIL) {
        /* Continue down this branch */
        iter->path[iter->depth++] = iter->cur;
        iter->cur = iter->cur->link[dir];

        while (iter->cur->link[!dir] != RBT_NIL) {
            iter->path[iter->depth++] = iter->cur;
            iter->cur = iter->cur->link[!dir];
        }
    } else {
        /* Move to the next branch */
        const rbtree_node_t *last;

        do {
            if (0 == iter->depth) {
                iter->cur = RBT_NIL;
                return NULL;
            }
            last = iter->cur;
            iter->cur = iter->path[--iter->depth];
        } while (last == iter->cur->link[dir]);
    }

    return ((iter->cur == RBT_NIL) ? NULL : (void*)iter->cur->data);
}


/**
 *    Print the address of the data pointer.
 *
 *    This is a helper function for rbtree_node_debug_print() to print
 *    the data when the user does not provide a printing function.
 */
static void
rbtree_node_default_data_printer(
    FILE               *fp,
    const void         *data)
{
    fprintf(fp, "%p", data);
}


/**
 *    Print the address of the data pointer.
 *
 *    This is a helper function for rbtree_node_debug_print() to print
 *    the data when the user does not provide a printing function.
 */
static void
rbtree_node_debug_print(
    const rbtree_node_t        *node,
    FILE                       *fp,
    sk_rbtree_print_data_fn_t   print_data,
    int                         indentation)
{
    int i;

    if (node != RBT_NIL) {
        ++indentation;
        fprintf(fp,
                "Tree: %*s %p: left=%p, right=%p, color=%s, data=",
                indentation, "", (void*)node,
                (void*)node->link[RBT_LEFT], (void*)node->link[RBT_RIGHT],
                (node->color == RBT_BLACK) ? "BLACK" : "RED");
        print_data(fp, node->data);
        fprintf(fp, "\n");
        for (i = RBT_LEFT; i <= RBT_RIGHT; ++i) {
            rbtree_node_debug_print(
                node->link[i], fp, print_data, indentation);
        }
    }
}


static int
rbtree_assert(
    const sk_rbtree_t      *tree,
    const rbtree_node_t    *root,
    FILE                   *fp)
{
    const rbtree_node_t *ln;
    const rbtree_node_t *rn;
    int lh, rh;

    if (RBT_NIL == root) {
        return 1;
    }

    ln = root->link[RBT_LEFT];
    rn = root->link[RBT_RIGHT];

    /* Consecutive red links */
    if (rbtree_node_is_red(root)) {
        if (rbtree_node_is_red(ln) || rbtree_node_is_red(rn)) {
            fprintf(fp, "Red violation at %p\n", (void*)root);
            return 0;
        }
    }

    lh = rbtree_assert(tree, ln, fp);
    rh = rbtree_assert(tree, rn, fp);

    /* Invalid binary search tree */
    if ((ln != RBT_NIL
         && tree->cmp_fn(ln->data, root->data, tree->ctx) >= 0)
        || (rn != RBT_NIL
            && tree->cmp_fn(rn->data, root->data, tree->ctx) <= 0))
    {
        fprintf(fp, "Binary tree violation at %p\n", (void*)root);
        return 0;
    }

    /* Black height mismatch */
    if (lh != 0 && rh != 0 && lh != rh) {
        fprintf(fp, "Black violation at %p\n", (void*)root);
        return 0;
    }

    /* Only count black links */
    if (lh != 0 && rh != 0) {
        return rbtree_node_is_red(root ) ? lh : lh + 1;
    }
    return 0;
}


/*
 *  ************************************************************
 *  Public functions
 *  ************************************************************
 */

int
sk_rbtree_create(
    sk_rbtree_t           **tree,
    sk_rbtree_cmp_fn_t      cmp_fn,
    sk_rbtree_free_fn_t     free_fn,
    const void             *ctx_data)
{
    sk_rbtree_t *rbt;

    if (NULL == cmp_fn) {
        cmp_fn = rbtree_default_compare;
    }

    rbt = sk_alloc(sk_rbtree_t);
    rbt->root = RBT_NIL;
    rbt->cmp_fn = cmp_fn;
    rbt->free_fn = free_fn;
    rbt->ctx = ctx_data;
    rbt->size = 0;

    *tree = rbt;
    return SK_RBTREE_OK;
}


void
sk_rbtree_destroy(
    sk_rbtree_t       **tree)
{
    sk_rbtree_t *rbt;
    rbtree_node_t *node;
    rbtree_node_t *save;

    if (!tree || !*tree) {
        return;
    }
    rbt = *tree;
    *tree = NULL;

    node = rbt->root;
    if (node != RBT_NIL) {
        /* Rotate away the left links so that we can treat this like
         * the destruction of a linked list */
        for (;;) {
            if (RBT_NIL != node->link[RBT_LEFT]) {
                /* Rotate away the left link and check again */
                save = node->link[RBT_LEFT];
                node->link[RBT_LEFT] = save->link[RBT_RIGHT];
                save->link[RBT_RIGHT] = node;
                node = save;
            } else {
                /* No left links, just kill the node and move on */
                save = node->link[RBT_RIGHT];
                if (rbt->free_fn) {
                    rbt->free_fn((void*)node->data);
                }
                free(node);
                if (RBT_NIL == save) {
                    break;
                }
                node = save;
            }
        }
    }

    free(rbt);
}


void*
sk_rbtree_find(
    const sk_rbtree_t  *tree,
    const void         *data)
{
    const rbtree_node_t *node;
    int cmp;

    node = tree->root;
    while (node != RBT_NIL) {
        cmp = tree->cmp_fn(node->data, data, tree->ctx);
        if (cmp < 0) {
            node = node->link[RBT_RIGHT];
        } else if (cmp > 0) {
            node = node->link[RBT_LEFT];
        } else {
            return (void*)node->data;
        }
        /* If the tree supports duplicates, they should be chained to
         * the right subtree for this to work */
        /* node = node->link[cmp < 0]; */
    }
    return NULL;
}


int
sk_rbtree_insert(
    sk_rbtree_t        *tree,
    const void         *data,
    const void        **found)
{
    rbtree_node_t head = RBT_FALSE_ROOT;
    rbtree_node_t *t, *g, *p, *q;
    int dir, last, cmp;
    int rv = SK_RBTREE_OK;

    /* 't' is great-grandparent; 'g' is grandparent; 'p' is parent;
     * and 'q' is iterator. */
    t = g = p = &head;
    q = t->link[RBT_RIGHT] = tree->root;
    dir = last = RBT_RIGHT;

    /* Search down the tree for a place to insert */
    for (;;) {
        if (q == RBT_NIL) {
            /* Insert a new node at the first null link */
            p->link[dir] = q = rbtree_node_create(data);
            if (NULL == q) {
                return SK_RBTREE_ERR_ALLOC;
            }
            ++tree->size;
            if (found) {
                *found = data;
            }
        } else if (rbtree_node_is_red(q->link[RBT_LEFT])
                   && rbtree_node_is_red(q->link[RBT_RIGHT]))
        {
            /* Simple red violation: color flip */
            q->color = RBT_RED;
            q->link[RBT_LEFT]->color = RBT_BLACK;
            q->link[RBT_RIGHT]->color = RBT_BLACK;
        }

        if (rbtree_node_is_red(p) && rbtree_node_is_red(q)) {
            /* Hard red violation: rotations necessary */
            int dir2 = (t->link[RBT_RIGHT] == g);

            if (q == p->link[last]) {
                t->link[dir2] = rbtree_rotate_single(g, !last);
            } else {
                t->link[dir2] = rbtree_rotate_double(g, !last);
            }
        }

        /* Stop working if we inserted a node */
        if (q->data == data) {
            break;
        }

        /* Choose a direction and check for a match */
        cmp = tree->cmp_fn(q->data, data, tree->ctx);
        if (0 == cmp) {
            rv = SK_RBTREE_ERR_DUPLICATE;
            if (found) {
                *found = q->data;
            }
            break;
        }

        last = dir;
        dir = (cmp < 0);

        /* Move the helpers down */
        t = g;
        g = p;
        p = q;
        q = q->link[dir];
    }

    /* Update the root (it may be different) */
    tree->root = head.link[RBT_RIGHT];

    /* Make the root black for simplified logic */
    tree->root->color = RBT_BLACK;

    return rv;
}


int
sk_rbtree_remove(
    sk_rbtree_t        *tree,
    const void         *data,
    const void        **found)
{
    rbtree_node_t head = RBT_FALSE_ROOT;
    rbtree_node_t *q, *p, *g, *s; /* Helpers */
    rbtree_node_t *f;             /* Found item */
    int dir;
    int last;
    int cmp;
    int rv = SK_RBTREE_ERR_NOT_FOUND;

    if (RBT_NIL == tree->root) {
        return rv;
    }

    /* Set up our helpers */
    g = p = NULL;
    q = &head;
    q->link[RBT_RIGHT] = tree->root;
    dir = RBT_RIGHT;
    f = NULL;

    /* Search and push a red node down to fix red violations as we
     * go */
    do {
        /* Move the helpers down */
        g = p;
        p = q;
        q = q->link[dir];

        cmp = tree->cmp_fn(q->data, data, tree->ctx);
        last = dir;
        dir = cmp < 0;

        /* Save the node with matching data and keep going; we'll do
         * removal tasks at the end */
        if (0 == cmp) {
            f = q;
        }

        /* Push the red node down with rotations and color flips */
        if (!rbtree_node_is_red(q) && !rbtree_node_is_red(q->link[dir])) {
            if (rbtree_node_is_red(q->link[!dir])) {
                p = p->link[last] = rbtree_rotate_single(q, dir);
            } else if ((s = p->link[!last]) != RBT_NIL) {
                if (!rbtree_node_is_red(s->link[RBT_LEFT])
                    && !rbtree_node_is_red(s->link[RBT_RIGHT]))
                {
                    /* Color flip */
                    p->color = RBT_BLACK;
                    s->color = RBT_RED;
                    q->color = RBT_RED;
                } else {
                    int dir2 = (g->link[RBT_RIGHT] == p);

                    if (rbtree_node_is_red(s->link[last])) {
                        g->link[dir2] = rbtree_rotate_double(p, last);
                    } else if (rbtree_node_is_red(s->link[!last])) {
                        g->link[dir2] = rbtree_rotate_single(p, last);
                    }

                    /* Ensure correct coloring */
                    q->color = g->link[dir2]->color = RBT_RED;
                    g->link[dir2]->link[RBT_LEFT]->color = RBT_BLACK;
                    g->link[dir2]->link[RBT_RIGHT]->color = RBT_BLACK;
                }
            }
        }
    } while (q->link[dir] != RBT_NIL);

    /* Replace and remove the saved node */
    if (f != NULL) {
        if (found) {
            *found = f->data;
        } else if (tree->free_fn) {
            tree->free_fn((void*)f->data);
        }
        f->data = q->data;
        p->link[p->link[RBT_RIGHT] == q] =
            q->link[q->link[RBT_LEFT] == RBT_NIL];
        free(q);
        --tree->size;
        rv = SK_RBTREE_OK;
    }

    /* Update the root (it may be different) */
    tree->root = head.link[RBT_RIGHT];

    /* Make the root black for simplified logic */
    tree->root->color = RBT_BLACK;

    return rv;
}


size_t
sk_rbtree_size(
    const sk_rbtree_t  *tree)
{
    return tree->size;
}


sk_rbtree_iter_t*
sk_rbtree_iter_create(
    void)
{
    return (sk_rbtree_iter_t*)calloc(1, sizeof(sk_rbtree_iter_t));
}


void
sk_rbtree_iter_free(
    sk_rbtree_iter_t   *iter)
{
    free(iter);
}


void*
sk_rbtree_iter_bind_first(
    sk_rbtree_iter_t   *iter,
    const sk_rbtree_t  *tree)
{
    /* Min value */
    return rbtree_iter_start(iter, tree, 0);
}


void*
sk_rbtree_iter_bind_last(
    sk_rbtree_iter_t   *iter,
    const sk_rbtree_t  *tree)
{
    /* Max value */
    return rbtree_iter_start(iter, tree, 1);
}


void*
sk_rbtree_iter_next(
    sk_rbtree_iter_t   *iter)
{
    /* Toward larger items */
    return rbtree_iter_move(iter, 1);
}


void*
sk_rbtree_iter_prev(
    sk_rbtree_iter_t   *iter)
{
    /* Toward smaller items */
    return rbtree_iter_move(iter, 0);
}


void
sk_rbtree_debug_print(
    const sk_rbtree_t  *tree,
    FILE               *fp,
    void              (*print_data)(FILE *fp, const void *data))
{
    if (NULL == tree) {
        fprintf(fp, "Tree: Pointer is NULL\n");
        return;
    }
    if (NULL == print_data) {
        print_data = rbtree_node_default_data_printer;
    }

    fprintf(fp, "Tree: %p has %" SK_PRIuZ " nodes\n", (void*)tree, tree->size);
    rbtree_node_debug_print(tree->root, fp, print_data, 0);

    rbtree_assert(tree, tree->root, fp);
}


/*
** Local Variables:
** mode:c
** indent-tabs-mode:nil
** c-basic-offset:4
** End:
*/
