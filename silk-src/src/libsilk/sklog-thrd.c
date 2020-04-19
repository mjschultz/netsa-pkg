/*
** Copyright (C) 2007-2020 by Carnegie Mellon University.
**
** @OPENSOURCE_LICENSE_START@
** See license information in ../../LICENSE.txt
** @OPENSOURCE_LICENSE_END@
*/

/*
**  Create a version of sklog that works well in a multi-threaded
**  environment by creating a mutex and using it when logging.
**
*/


#include <silk/silk.h>

RCSIDENT("$SiLK: sklog-thrd.c ef14e54179be 2020-04-14 21:57:45Z mthomas $");

#include <silk/sklog.h>


/* Mutex for the log in the non-syslog case. */
static pthread_mutex_t logmutex = PTHREAD_MUTEX_INITIALIZER;


int
sklogEnableThreadedLogging(
    void)
{
    /* Set the lock/unlock function pointers on the log and the mutex
     * on which they operate. */
    return sklogSetLocking((sklog_lock_fn_t)&pthread_mutex_lock,
                           (sklog_lock_fn_t)&pthread_mutex_unlock,
                           (sklog_lock_fn_t)&pthread_mutex_trylock,
                           &logmutex);
}


/*
** Local Variables:
** mode:c
** indent-tabs-mode:nil
** c-basic-offset:4
** End:
*/
