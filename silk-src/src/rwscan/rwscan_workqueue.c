/*
** Copyright (C) 2006-2020 by Carnegie Mellon University.
**
** @OPENSOURCE_LICENSE_START@
** See license information in ../../LICENSE.txt
** @OPENSOURCE_LICENSE_END@
*/

#include <silk/silk.h>

RCSIDENT("$SiLK: rwscan_workqueue.c ef14e54179be 2020-04-14 21:57:45Z mthomas $");

#include "rwscan_workqueue.h"



work_queue_t *
workqueue_create(
    uint32_t            maxdepth)
{
    work_queue_t *q;

    q = (work_queue_t *) calloc(1, sizeof(work_queue_t));
    if (q == NULL) {
        return (work_queue_t *) NULL;
    }

    pthread_mutex_init(&q->mutex, NULL);
    pthread_cond_init(&q->cond_posted, NULL);
    pthread_cond_init(&q->cond_avail, NULL);

    q->maxdepth = maxdepth;
    q->active   = 1;

    return q;
}

int
workqueue_activate(
    work_queue_t       *q)
{
    if (pthread_mutex_lock(&q->mutex)) {
        return 0;
    }
    q->active = 1;
    pthread_mutex_unlock(&q->mutex);
    pthread_cond_broadcast(&q->cond_posted);
    return 1;
}

int
workqueue_deactivate(
    work_queue_t       *q)
{
    if (pthread_mutex_lock(&q->mutex)) {
        return 0;
    }
    q->active = 0;
    pthread_mutex_unlock(&q->mutex);
    pthread_cond_broadcast(&q->cond_posted);
    return 1;
}



void
workqueue_destroy(
    work_queue_t       *q)
{
    if (q == NULL) {
        return;
    }

    pthread_mutex_destroy(&q->mutex);
    pthread_cond_destroy(&q->cond_posted);
    pthread_cond_destroy(&q->cond_avail);
    free(q);
}


int
workqueue_put(
    work_queue_t       *q,
    work_queue_node_t  *newnode)
{
    if (newnode == NULL || q == NULL) {
        return -1;
    }

    pthread_mutex_lock(&q->mutex);

    while ((q->maxdepth > 0) && (q->depth + q->pending >= q->maxdepth)) {
        /* queue is full - wait on cond var for notification that a slot has
         * become open */
        pthread_cond_wait(&q->cond_avail, &q->mutex);
    }

    if (q->tail == NULL) {
        q->tail = q->head = newnode;
    } else {
        q->tail->next = newnode;
        q->tail       = newnode;
    }

    newnode->next = NULL;

    q->depth++;

    /* release queue mutex and signal consumer that an item is ready */
    pthread_mutex_unlock(&q->mutex);
    pthread_cond_signal(&q->cond_posted);


#ifdef RWSCN_WORKQUEUE_DEBUG
    /* update the peak depth, if needed */
    if (q->depth > q->peakdepth) {
        q->peakdepth = q->depth;
    }

    q->produced++;
#endif

    return q->depth;
}


int
workqueue_get(
    work_queue_t       *q,
    work_queue_node_t **retnode)
{
    work_queue_node_t *node;

    if (q->head == NULL || q->depth == 0) {
        retnode = NULL;
        return -1;
    }

    node = q->head;
    if (node->next) {
        q->head = node->next;
    } else {
        q->head = NULL;
        q->tail = q->head = NULL;
    }

    node->next = NULL;
    *retnode   = node;

    q->depth--;
    q->pending++;

#ifdef RWSCN_WORKQUEUE_DEBUG
    q->consumed++;
#endif

    return 0;
}

int
workqueue_depth(
    work_queue_t       *q)
{
    return q->depth;
}

int
workqueue_pending(
    work_queue_t       *q)
{
    return q->pending;
}


/*
** Local Variables:
** mode:c
** indent-tabs-mode:nil
** c-basic-offset:4
** End:
*/
