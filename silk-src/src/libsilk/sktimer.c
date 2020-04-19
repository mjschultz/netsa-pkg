/*
** Copyright (C) 2004-2020 by Carnegie Mellon University.
**
** @OPENSOURCE_LICENSE_START@
** See license information in ../../LICENSE.txt
** @OPENSOURCE_LICENSE_END@
*/

/*
**  Generic timers which will run callback functions in a separate
**  thread context after a given amount of time.
*/

#include <silk/silk.h>

RCSIDENT("$SiLK: sktimer.c ef14e54179be 2020-04-14 21:57:45Z mthomas $");

#include <silk/utils.h>
#include <silk/sktimer.h>
#include <silk/skthread.h>

#ifdef SKTIMER_TRACE_LEVEL
#define TRACEMSG_LEVEL SKTIMER_TRACE_LEVEL
#endif
#define TRACEMSG(lvl, msg)  TRACEMSG_TO_TRACEMSGLVL(lvl, msg)
#include <silk/sktracemsg.h>


/* LOCAL DEFINES AND TYPEDEFS */

#if TRACEMSG_LEVEL < 2

#define sk_timer_lock(m_timer)      pthread_mutex_lock(&(m_timer)->mutex)

#define sk_timer_unlock(m_timer)    pthread_mutex_unlock(&(m_timer)->mutex)

#define sk_timer_wake(m_timer)      pthread_cond_broadcast(&(m_timer)->cond)

#define sk_timer_wait(m_timer)                                  \
    pthread_cond_wait(&(m_timer)->cond, &(m_timer)->mutex)

#else

#define sk_timer_lock(m_timer)                          \
    {                                                   \
        TRACEMSG(2, ("Timer %p: Acquiring lock [%d]",   \
                     (void *)(m_timer), __LINE__));     \
        pthread_mutex_lock(&(m_timer)->mutex);          \
        TRACEMSG(2, ("Timer %p: Acquired lock [%d]",    \
                     (void *)(m_timer), __LINE__));     \
    }

#define sk_timer_unlock(m_timer)                        \
    {                                                   \
        TRACEMSG(2, ("Timer %p: Releasing lock [%d]",   \
                     (void *)(m_timer), __LINE__));     \
        pthread_mutex_unlock(&(m_timer)->mutex);        \
    }

#define sk_timer_wake(m_timer)                          \
    {                                                   \
        TRACEMSG(2, ("Timer %p: Waking threads [%d]",   \
                     (void *)(m_timer), __LINE__));     \
        pthread_cond_broadcast(&(m_timer)->cond);       \
    }

#define sk_timer_wait(m_timer)                                          \
    {                                                                   \
        TRACEMSG(2, ("Timer %p: Releasing lock and waiting [%d]",       \
                     (void *)(m_timer), __LINE__));                     \
        pthread_cond_wait(&(m_timer)->cond, &(m_timer)->mutex);         \
        TRACEMSG(2, ("Timer %p: Acquired lock after waiting [%d]",      \
                     (void *)(m_timer), __LINE__));                     \
    }

#endif  /* #else of #if TRACEMSG_LEVEL < 2 */


/*
 *    Definition of sk_timer_t.
 */
struct sk_timer_st {
    /** function the user provided that is called when the timer fires */
    skTimerFn_t      callback_fn;

    /** data pointer the user provided that is passed to 'callback_fn' */
    void            *callback_data;

    /** object to protect access to the timer */
    pthread_mutex_t  mutex;

    /** object to signal the timer */
    pthread_cond_t   cond;

    /** reference time to use so timer fires at predictable times;
     *  e.g., at 0,15,30,45 minutes past the hour */
    struct timeval   base_time;

    /** how often the timer should fire, in seconds */
    int64_t          interval;

    /** whether the timer thread has started */
    unsigned         started : 1;

    /** whether the timer has been told to stop */
    unsigned         stopping : 1;

    /** whether the timer thread has stopped */
    unsigned         stopped : 1;
};
typedef struct sk_timer_st sk_timer_t; /* skTimer_t */


/* FUNCTION DEFINITIONS */

/*
 *    THREAD ENTRY POINT
 *
 *    This is the function the timer thread runs until the callback_fn
 *    function returns SK_TIMER_END or the timer is destroyed.
 */
static void *
sk_timer_thread(
    void               *v_timer)
{
    sk_timer_t      *timer = (sk_timer_t *)v_timer;
    int              rv;
    skTimerRepeat_t  repeat;

    /* wait_time is when the timer is scheduled to fire */
    struct timespec  wait_time;

    /* next_time is one interval ahead of wait_time */
    struct timeval   next_time;

    /* current_time is now */
    struct timeval   current_time;

    /* Lock the mutex */
    sk_timer_lock(timer);

    /* Have we been destroyed before we've even started? */
    if (timer->stopping) {
        TRACEMSG(1, ("Timer %p: Thread stopped before initial run", v_timer));
        goto END;
    }

    /* We do no calculations with fractional seconds in this function;
     * simply initialize the wait_time fractional seconds from the
     * fractional seconds on the base_time */
    wait_time.tv_nsec = timer->base_time.tv_usec * 1000;

    /* Initialize next_time to the base_time */
    next_time = timer->base_time;

    do {
        /* Skip to the next interval greater than the current time;
         * this way we avoid calling the function multiple times if
         * the function takes longer than 'interval' seconds to
         * complete. */
        gettimeofday(&current_time, NULL);
        if (next_time.tv_sec < current_time.tv_sec) {
            int64_t seconds_into_interval
                = ((int64_t)(current_time.tv_sec - timer->base_time.tv_sec)
                   % timer->interval);
            TRACEMSG(1, (("Timer %p: next_time < current_time (%" PRId64
                           " < %" PRId64 "); %" PRId64 " seconds into an"
                          " interval; setting next_time to %" PRId64),
                         v_timer, (int64_t)next_time.tv_sec,
                         (int64_t)current_time.tv_sec, seconds_into_interval,
                         (int64_t)((current_time.tv_sec + timer->interval
                                    - seconds_into_interval))));
            next_time.tv_sec = (current_time.tv_sec + timer->interval
                                - seconds_into_interval);
        }

        wait_time.tv_sec = next_time.tv_sec;
        next_time.tv_sec += timer->interval;

        TRACEMSG(1, (("Timer %p: Scheduled to wake at %" PRId64 ".%09" PRId64),
                     v_timer, (int64_t)wait_time.tv_sec,
                     (int64_t)wait_time.tv_nsec));

        /* Loop around pthread_cond_timedwait() forever until the
         * timer actually fires or the condition variable is signaled
         * (for example, during shutdown).  When the timer fires,
         * invoke the callback_fn function and exit this for() loop. */
        for (;;) {
            /* Mutex is released while within pthread_cond_timedwait */
            TRACEMSG(2, ("Timer %p: Releasing lock and waiting [%d]",
                         v_timer, __LINE__));
            rv = pthread_cond_timedwait(&timer->cond, &timer->mutex,
                                        &wait_time);
            TRACEMSG(2, ("Timer %p: Acquired lock due to %s after waiting [%d]",
                         v_timer, ((ETIMEDOUT == rv) ? "time-out"
                                   : ((0 == rv) ? "signal" : "other")),
                         __LINE__));
            if (timer->stopping) {
                TRACEMSG(1, ("Timer %p: Noticed stopping variable", v_timer));
                goto END;
            }
            if (ETIMEDOUT == rv) {
                /* Timer timed out */
#ifdef CHECK_PTHREAD_COND_TIMEDWAIT
                /* Well, at least the timer thinks it timed out.
                 * Let's see whether it did....
                 *
                 * THIS IS A HORRIBLE HACK!!  BACKGROUND: We have seen
                 * instances where pthread_cond_timedwait() will fire
                 * before it is supposed to.  Since we only have
                 * second resolution in the log, it may be firing a
                 * full second early or it may firing a few
                 * milliseconds early.  This shouldn't really matter,
                 * but the early timedwait() return is causing flowcap
                 * files to get timestamps that are a second earlier
                 * than expected, which means that the --clock-time
                 * switch isn't operating as advertised.  */
                struct timeval now;
                gettimeofday(&now, NULL);
                if (now.tv_sec < wait_time.tv_sec) {
                    /* Timer fired early.  Call timedwait() again.
                     * timedwait() may return immediately, so this has
                     * the potential to spike the CPU until the
                     * wait_time is reached. */
                    TRACEMSG(1, (("Timer %p: pthread_cond_timedwait() fired"
                                  " %" PRId64 " nanoseconds early"),
                                 v_timer,
                                 (((int64_t)(wait_time.tv_sec - now.tv_sec)
                                   * 1000000000)
                                  + (int64_t)wait_time.tv_nsec
                                  - ((int64_t)now.tv_usec * 1000))));
                    continue;
                }
                /* else timer fired at correct time (or later) */
#endif  /* CHECK_PTHREAD_COND_TIMEDWAIT */

                TRACEMSG(1, ("Timer %p: Invoking callback", v_timer));
                repeat = timer->callback_fn(timer->callback_data);
                TRACEMSG(1, ("Timer %p: Callback returned %d",
                             v_timer, (int)repeat));
                break;
            }
            /* else, a signal interrupted the call; continue waiting */
            TRACEMSG(1, (("Timer %p: pthread_cond_timedwait()"
                          " returned unexpected value %d"),
                         v_timer, rv));
        }
    } while (SK_TIMER_REPEAT == repeat);

  END:
    /* Notify destroy function that we have ended properly. */
    TRACEMSG(1, ("Timer %p: Thread is ending", v_timer));
    timer->stopped = 1;
    sk_timer_wake(timer);
    sk_timer_unlock(timer);

    return NULL;
}


int
skTimerCreate(
    sk_timer_t        **new_timer,
    uint32_t            interval,
    skTimerFn_t         callback_fn,
    void               *callback_data)
{
    struct timeval current_time;

    gettimeofday(&current_time, NULL);
    current_time.tv_sec += interval;
    return skTimerCreateAtTime(new_timer, interval,
                               sktimeCreateFromTimeval(&current_time),
                               callback_fn, callback_data);
}


int
skTimerCreateAtTime(
    sk_timer_t        **new_timer,
    uint32_t            interval,
    sktime_t            start,
    skTimerFn_t         callback_fn,
    void               *callback_data)
{
#if TRACEMSG_LEVEL > 0
    char tstamp[SKTIMESTAMP_STRLEN];
#endif
    sk_timer_t *timer;
    pthread_t   thread;
    int         err;

    timer = (sk_timer_t *)calloc(1, sizeof(sk_timer_t));
    if (NULL == timer) {
        return errno;
    }
    timer->interval = (int64_t)interval;
    timer->callback_fn = callback_fn;
    timer->callback_data = callback_data;
    timer->base_time.tv_sec = sktimeGetSeconds(start);
    timer->base_time.tv_usec = sktimeGetMilliseconds(start) * 1000;
    pthread_mutex_init(&timer->mutex, NULL);
    pthread_cond_init(&timer->cond, NULL);

    TRACEMSG(1, (("Timer %p: Created with interval=%" PRId64
                  ", start_time=%sZ"),
                 (void *)timer, timer->interval,
                 sktimestamp_r(tstamp, start, SKTIMESTAMP_UTC)));

    /* Mutex starts locked */
    sk_timer_lock(timer);
    timer->started = 1;
    err = skthread_create_detached("sktimer", &thread, sk_timer_thread,
                                   (void *)timer);
    if (err) {
        timer->started = 0;
        sk_timer_unlock(timer);
        TRACEMSG(1, ("Timer %p: Failed to start; errno = %d",
                     (void *)timer, err));
        skTimerDestroy(timer);
        return err;
    }

    sk_timer_unlock(timer);
    TRACEMSG(1, ("Timer %p: Started", (void *)timer));
    *new_timer = timer;
    return 0;
}


int
skTimerDestroy(
    sk_timer_t         *timer)
{
    if (NULL == timer) {
        return 0;
    }
    TRACEMSG(1, ("Timer %p: Starting to destroy", (void *)timer));

    /* Grab the mutex */
    sk_timer_lock(timer);
    timer->stopping = 1;
    if (timer->started) {
        /* Wake the timer thread so it can check 'stopping' */
        sk_timer_wake(timer);
        /* Wait for timer process to end */
        while (!timer->stopped) {
            sk_timer_wait(timer);
        }
    }
    /* Unlock and destroy mutexes */
    sk_timer_unlock(timer);
    TRACEMSG(1, ("Timer %p: Freeing all resources", (void *)timer));
    pthread_mutex_destroy(&timer->mutex);
    pthread_cond_destroy(&timer->cond);
    free(timer);
    return 0;
}


/*
** Local Variables:
** mode:c
** indent-tabs-mode:nil
** c-basic-offset:4
** End:
*/
