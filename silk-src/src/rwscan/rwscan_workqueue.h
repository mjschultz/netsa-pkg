/*
** Copyright (C) 2006-2020 by Carnegie Mellon University.
**
** @OPENSOURCE_LICENSE_START@
** See license information in ../../LICENSE.txt
** @OPENSOURCE_LICENSE_END@
*/
#ifndef _RWSCAN_WORKQUEUE_H
#define _RWSCAN_WORKQUEUE_H
#ifdef __cplusplus
extern "C" {
#endif

#include <silk/silk.h>

RCSIDENTVAR(rcsID_RWSCAN_WORKQUEUE_H, "$SiLK: rwscan_workqueue.h ef14e54179be 2020-04-14 21:57:45Z mthomas $");


typedef struct work_queue_node_st {
    struct work_queue_node_st *next;       /* next request in queue */
} work_queue_node_t;

/*
 * This threaded queue structure is specialized for a
 * producer/consumer design in two ways.  First, queues can be created
 * with a maximum queue depth parameter, which governs how large the
 * queue can grow in size.  Second, the queue can be "deactivated" to
 * shut down producer threads when the program exits.
 *
 * The queue just maintains node pointers; it does not manage node
 * memory in any way.
 *
 */
typedef struct work_queue_st {
    work_queue_node_t *head;        /* pointer to first node */
    work_queue_node_t *tail;        /* pointer to last node */

    pthread_mutex_t    mutex;       /* master queue lock mutex */
    pthread_cond_t     cond_posted; /* used to wake up a consumer */
    pthread_cond_t     cond_avail;  /* used to signal a producer */

    int                depth;       /* number of items in queue */
    int                maxdepth;    /* max items allowed in queue */
    int                pending;     /* numitems being processed */
    int                active;      /* if work queue has been activated */
#ifdef RWSCN_WORKQUEUE_DEBUG
    int                consumed;    /* num items consumed */
    int                produced;    /* num items posted by producers */
    int                peakdepth;   /* highest queue depth */
#endif
} work_queue_t;


/* Public work queue API */
work_queue_t *
workqueue_create(
    uint32_t            maxdepth);
int
workqueue_put(
    work_queue_t       *q,
    work_queue_node_t  *newnode);
int
workqueue_get(
    work_queue_t       *q,
    work_queue_node_t **retnode);
int
workqueue_depth(
    work_queue_t       *q);

#if 1
int
workqueue_pending(
    work_queue_t       *q);
#endif
int
workqueue_activate(
    work_queue_t       *q);
int
workqueue_deactivate(
    work_queue_t       *q);
void
workqueue_destroy(
    work_queue_t       *q);

#ifdef __cplusplus
}
#endif
#endif /* _RWSCAN_WORKQUEUE_H */

/*
** Local Variables:
** mode:c
** indent-tabs-mode:nil
** c-basic-offset:4
** End:
*/
