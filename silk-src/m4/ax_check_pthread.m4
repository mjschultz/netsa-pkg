dnl Copyright (C) 2004-2020 by Carnegie Mellon University.
dnl
dnl @OPENSOURCE_LICENSE_START@
dnl See license information in ../LICENSE.txt
dnl @OPENSOURCE_LICENSE_END@

dnl RCSIDENT("$SiLK: ax_check_pthread.m4 ef14e54179be 2020-04-14 21:57:45Z mthomas $")


# ---------------------------------------------------------------------------
# AX_CHECK_PTHREAD
#
#    Determine how to use pthreads.  In addition, determine whether
#    pthreads supports read/write locks.
#
#    Output variables: PTHREAD_LDFLAGS
#    Output definition: HAVE_PTHREAD_RWLOCK
#
AC_DEFUN([AX_CHECK_PTHREAD],[
    AC_SUBST(PTHREAD_LDFLAGS)

    AC_MSG_CHECKING([for pthread linker flags])

    # cache current LIBS
    sk_save_LIBS="$LIBS"

    # pthreads requires libexc on OSF/1 or TruUNIX or Tru64 or
    # whatever it is called by DEC or Compaq or HP or whatever they
    # are calling themselves now

    for sk_pthread in "X" "X-pthread" "X-lpthread" "X-lpthread -lexc"
    do
        sk_pthread=`echo $sk_pthread | sed 's/^X//'`
        LIBS="$sk_pthread $sk_save_LIBS"

        # This is a RUN because Solaris will successfully link the
        # program and just leave out the pthread calls!
        AC_RUN_IFELSE([
            AC_LANG_PROGRAM([
#include <stdio.h>
#if HAVE_STDLIB_H
#include <stdlib.h>
#endif
#if HAVE_UNISTD_H
#include <unistd.h>
#endif
#if HAVE_PTHREAD_H
#include <pthread.h>
#endif

static void *dummy(void *v)
{
  return v;
}
                ],[
pthread_t p;
int x = 0;
void *xp = NULL;

pthread_create(&p, NULL, dummy, &x);
pthread_join(p, &xp);
if (xp != &x) return 1;
])],[
            PTHREAD_LDFLAGS="$sk_pthread"
            if test "x$sk_pthread" = "x"
            then
                AC_MSG_RESULT([none required])
            else
                AC_MSG_RESULT([$PTHREAD_LDFLAGS])
            fi
            break])
    done


    AC_MSG_CHECKING([for pthread read/write locks])

    # add pthread library to the saved LIBS
    LIBS="$PTHREAD_LDFLAGS $sk_save_LIBS"

    # This is a RUN because Solaris will successfully link the
    # program and just leave out the pthread calls!
    AC_RUN_IFELSE([
        AC_LANG_PROGRAM([
#include <stdio.h>
#if HAVE_STDLIB_H
#include <stdlib.h>
#endif
#if HAVE_ERRNO_H
#include <errno.h>
#endif
#if HAVE_PTHREAD_H
#include <pthread.h>
#endif
#define FAILIF(x) ++test; if (x) { fprintf(stderr, "Failed test %d (%s) (rv = %d) on line %d\n", test, #x, rv, __LINE__); return 1; }
            ],[
    pthread_rwlock_t lock;
    int rv;
    int test;
    test = 0;
    rv = pthread_rwlock_init(&lock, NULL);
    FAILIF(rv != 0);
    rv = pthread_rwlock_tryrdlock(&lock);
    FAILIF(rv != 0);
    rv = pthread_rwlock_trywrlock(&lock);
    FAILIF(rv != EBUSY);
    rv = pthread_rwlock_tryrdlock(&lock);
    FAILIF(rv != 0);
    rv = pthread_rwlock_trywrlock(&lock);
    FAILIF(rv != EBUSY);
    rv = pthread_rwlock_unlock(&lock);
    FAILIF(rv != 0);
    rv = pthread_rwlock_trywrlock(&lock);
    FAILIF(rv != EBUSY);
    rv = pthread_rwlock_unlock(&lock);
    FAILIF(rv != 0);
    rv = pthread_rwlock_trywrlock(&lock);
    FAILIF(rv != 0);
    rv = pthread_rwlock_tryrdlock(&lock);
    FAILIF(rv != EDEADLK && rv != EBUSY);
    rv = pthread_rwlock_trywrlock(&lock);
    FAILIF(rv != EDEADLK && rv != EBUSY);
    rv = pthread_rwlock_unlock(&lock);
    FAILIF(rv != 0);
    rv = pthread_rwlock_tryrdlock(&lock);
    FAILIF(rv != 0);
    rv = pthread_rwlock_unlock(&lock);
    FAILIF(rv != 0);
    rv = pthread_rwlock_destroy(&lock);
    FAILIF(rv != 0);
    return 0;
            ])],[
           AC_MSG_RESULT([yes])
           AC_DEFINE([HAVE_PTHREAD_RWLOCK], 1,
                     [Define to 1 if your system has working pthread read/write locks])
            ],[
           AC_MSG_RESULT([no])
        ])

    # restore libs
    LIBS="$sk_save_LIBS"
])# AX_CHECK_PTHREAD



AC_DEFUN([AX_CHECK_PTHREAD_ATFORK],[
    AC_REQUIRE([AX_CHECK_PTHREAD])

    AC_MSG_CHECKING([for pthread_atfork()])

    # cache current LIBS
    sk_save_LIBS="$LIBS"

    # add pthread library to the saved LIBS
    LIBS="$PTHREAD_LDFLAGS $sk_save_LIBS"

    # This is a RUN because Solaris will successfully link the
    # program and just leave out the pthread calls!
    AC_RUN_IFELSE([
        AC_LANG_PROGRAM([
#include <stdio.h>
#if HAVE_STDLIB_H
#include <stdlib.h>
#endif
#if HAVE_UNISTD_H
#include <unistd.h>
#endif
#if HAVE_SYS_WAIT_H
#include <sys/wait.h>
#endif
#if HAVE_ERRNO_H
#include <errno.h>
#endif
#if HAVE_PTHREAD_H
#include <pthread.h>
#endif
#define FAILIF(x) ++test; if (x) { fprintf(stderr, "Failed test %d (%s) (rv = %d) on line %d\n", test, #x, rv, __LINE__); return 1; }

pthread_mutex_t lock;

void sk_lock(void) { pthread_mutex_lock(&lock); }

void sk_unlock(void) { pthread_mutex_unlock(&lock); }
            ],[
    int rv;
    int pid;
    int test;
    test = 0;
    rv = pthread_mutex_init(&lock, NULL);
    FAILIF(rv != 0);
    rv = pthread_atfork(sk_lock, sk_unlock, sk_unlock);
    FAILIF(rv != 0);
    pid = fork();
    rv = pthread_mutex_destroy(&lock);
    FAILIF(rv != 0);
#if HAVE_SYS_WAIT_H
    if (pid > 0) {
        waitpid(pid, NULL, 0);
    }
#endif
    return 0;
            ])],[
           AC_MSG_RESULT([yes])
           AC_DEFINE([HAVE_PTHREAD_ATFORK], 1,
                     [Define to 1 if your system has working a pthread_atfork() function])
            ],[
           AC_MSG_RESULT([no])
        ])

    # restore libs
    LIBS="$sk_save_LIBS"
])



dnl Local Variables:
dnl mode:autoconf
dnl indent-tabs-mode:nil
dnl End:
