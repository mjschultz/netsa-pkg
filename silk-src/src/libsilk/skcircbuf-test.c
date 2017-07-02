/*
** Copyright (C) 2009-2017 by Carnegie Mellon University.
**
** @OPENSOURCE_LICENSE_START@
** See license information in ../../LICENSE.txt
** @OPENSOURCE_LICENSE_END@
*/

/*
**  Small application to test the circbuf library.
**
*/

#include <silk/silk.h>

RCSIDENT("$SiLK: skcircbuf-test.c efd886457770 2017-06-21 18:43:23Z mthomas $");

#include <silk/skcircbuf.h>
#include <silk/sklog.h>
#include <silk/utils.h>


/* LOCAL DEFINES AND TYPEDEFS */

/* size of items in the circbuf */
#define ITEM_SIZE 1024

/* number of items in the circbuf */
#define ITEM_COUNT 1024

/* default number of times to run with timestamps */
#define VERBOSE_COUNT 5

/* default total number of times to run */
#define TOTAL_COUNT 2048


/* LOCAL VARIABLE DEFINITIONS */

/* actual number of verbose runs */
static int verbose_count = VERBOSE_COUNT;

/* actual number of total runs */
static int total_count = TOTAL_COUNT;

/* variables to handle shutdown */
static pthread_mutex_t shutdown_mutex;
static pthread_cond_t shutdown_ok;


/* FUNCTION DEFINITIONS */

/*
 *    Prefix any log messages from libflowsource with the program name
 *    instead of the standard logging tag.
 */
static size_t
logprefix(
    char               *buffer,
    size_t              bufsize)
{
    return (size_t)snprintf(buffer, bufsize, "%s: ", skAppName());
}


/*
 *    Entry point for thread to put stuff into the circbuf.
 */
static void*
writer(
    void               *arg)
{
    sk_circbuf_t *cbuf = (sk_circbuf_t*)arg;
    int count = 0;
    struct timeval t_pre;
    struct timeval t_post;
    uint8_t *h;

    for ( ; count < verbose_count; ++count) {
        gettimeofday(&t_pre, NULL);
        h = sk_circbuf_get_write_pos(cbuf);
        gettimeofday(&t_post, NULL);
        if (NULL == h) {
            skAppPrintErr("Stopped writing after %d puts", count);
            return NULL;
        }
        memset(h, count, ITEM_SIZE);
        memcpy(h, &count, sizeof(count));
        fprintf(stderr, ("Writer %5d %17ld.%06u  %4ld.%06u\n"),
                count, t_pre.tv_sec % 3600, (unsigned int)t_pre.tv_usec,
                t_post.tv_sec % 3600, (unsigned int)t_post.tv_usec);
        sleep(1);
    }

    /* ensure buffer is empty */
    sleep(3);

    for ( ; count < 1 + total_count; ++count) {
        gettimeofday(&t_pre, NULL);
        h = sk_circbuf_get_write_pos(cbuf);
        gettimeofday(&t_post, NULL);
        if (NULL == h) {
            skAppPrintErr("Stopped writing after %d puts", count);
            return NULL;
        }
        memset(h, count, ITEM_SIZE);
        memcpy(h, &count, sizeof(count));
        if (t_post.tv_sec - t_pre.tv_sec >= 2) {
            fprintf(stderr, "Assuming circbuf is full at %5d elements\n",
                    count - verbose_count);
            sk_circbuf_print_stats(cbuf, NULL, skAppPrintErr);
            ++count;
            break;
        }
    }

    for ( ; count < 1 + total_count; ++count) {
        h = sk_circbuf_get_write_pos(cbuf);
        if (NULL == h) {
            skAppPrintErr("Stopped writing after %d puts", count);
            return NULL;
        }
        memset(h, count, ITEM_SIZE);
        memcpy(h, &count, sizeof(count));
    }

    /* we've written all we need to write.  continue to write until
     * the circbuf is destroyed  */

    while ((h = sk_circbuf_get_write_pos(cbuf)) != NULL) {
        memset(h, count, ITEM_SIZE);
        memcpy(h, &count, sizeof(count));
        ++count;
    }

    fprintf(stderr, "Final put count = %d\n", count);

    return NULL;
}


/*
 *    Entry point for thread to get stuff from the circbuf.
 */
static void*
reader(
    void               *arg)
{
    sk_circbuf_t *cbuf = (sk_circbuf_t*)arg;
    uint8_t cmpbuf[ITEM_SIZE];
    int count = 0;
    struct timeval t_pre;
    struct timeval t_post;
    uint8_t *t;
    int i;

    for ( ; count < verbose_count; ++count) {
        memset(cmpbuf, count, sizeof(cmpbuf));
        memcpy(cmpbuf, &count, sizeof(count));
        gettimeofday(&t_pre, NULL);
        t = sk_circbuf_get_read_pos(cbuf);
        gettimeofday(&t_post, NULL);
        if (NULL == t) {
            skAppPrintErr("Stopped reading after %d gets", count);
            return NULL;
        }
        if (0 != memcmp(t, cmpbuf, sizeof(cmpbuf))) {
            skAppPrintErr("Invalid data for count %d", count);
        }
        fprintf(stderr, ("Reader %5d %4ld.%06u  %4ld.%06u\n"),
                count, t_pre.tv_sec % 3600, (unsigned int)t_pre.tv_usec,
                t_post.tv_sec % 3600, (unsigned int)t_post.tv_usec);
    }

    for (i = 1; i >= 0; --i) {

        for ( ; (count < (total_count >> i)); ++count) {
            t = sk_circbuf_get_read_pos(cbuf);
            if (NULL == t) {
                skAppPrintErr("Stopped reading after %d gets", count);
                return NULL;
            }
            memset(cmpbuf, count, sizeof(cmpbuf));
            memcpy(cmpbuf, &count, sizeof(count));
            if (0 != memcmp(t, cmpbuf, sizeof(cmpbuf))) {
                skAppPrintErr("Invalid data for count %d", count);
            }
        }

        /* give the writer time to fill up the circbuf */
        if (i == 1) {
            sleep(6);
        }
    }

    /* we've read all we need to read.  let the main program know it
     * can shutdown. */
    pthread_mutex_lock(&shutdown_mutex);
    pthread_cond_broadcast(&shutdown_ok);
    pthread_mutex_unlock(&shutdown_mutex);

    while ((t = sk_circbuf_get_read_pos(cbuf)) != NULL) {
        memset(cmpbuf, count, sizeof(cmpbuf));
        memcpy(cmpbuf, &count, sizeof(count));
        if (0 != memcmp(t, cmpbuf, sizeof(cmpbuf))) {
            skAppPrintErr("Invalid data for count %d", count);
        }
        ++count;
    }

    fprintf(stderr, "Final get count = %d\n", count);

    return NULL;
}


int main(int argc, char **argv)
{
    SILK_FEATURES_DEFINE_STRUCT(features);
    pthread_t read_thrd;
    pthread_t write_thrd;
    sk_circbuf_t *cbuf;
    uint32_t tmp32;
    int rv;

    skAppRegister(argv[0]);
    skAppVerifyFeatures(&features, NULL);

    pthread_mutex_init(&shutdown_mutex, NULL);
    pthread_cond_init(&shutdown_ok, NULL);

    if (argc > 1) {
        if (skStringParseUint32(&tmp32, argv[1], 0, INT32_MAX)) {
            skAppPrintErr("First arg should be total number of runs");
            exit(EXIT_FAILURE);
        }
        total_count = (int)tmp32;
    }

    if (argc > 2) {
        if (skStringParseUint32(&tmp32, argv[2], 0, INT32_MAX)) {
            skAppPrintErr("Second arg should be number of verbose runs");
            exit(EXIT_FAILURE);
        }
        verbose_count = (int)tmp32;
    }

    if (verbose_count > total_count) {
        verbose_count = total_count;
    }

    /* enable the logger */
    sklogSetup(0);
    sklogSetDestination("stderr");
    sklogSetStampFunction(&logprefix);
    sklogSetLevel("debug");
    sklogOpen();

    /* should fail due to item_size == 0 */
    rv = sk_circbuf_create_const_itemsize(&cbuf, 0, 1);
    if (SK_CIRCBUF_OK == rv) {
        skAppPrintErr("FAIL at %d:"
                      " Creation succeeded; expected failure",
                      __LINE__);
        exit(EXIT_FAILURE);
    }
    if (SK_CIRCBUF_ERR_BAD_PARAM != rv) {
        skAppPrintErr("FAIL at %d:"
                      " Creation failed with status %d, expected status %d",
                      __LINE__, rv, SK_CIRCBUF_ERR_BAD_PARAM);
        exit(EXIT_FAILURE);
    }

    /* should fail due to item_count == 0 */
    rv = sk_circbuf_create_const_itemsize(&cbuf, 1, 0);
    if (SK_CIRCBUF_OK == rv) {
        skAppPrintErr("FAIL at %d:"
                      " Creation succeeded; expected failure",
                      __LINE__);
        exit(EXIT_FAILURE);
    }
    if (SK_CIRCBUF_ERR_BAD_PARAM != rv) {
        skAppPrintErr("FAIL at %d:"
                      " Creation failed with status %d, expected status %d",
                      __LINE__, rv, SK_CIRCBUF_ERR_BAD_PARAM);
        exit(EXIT_FAILURE);
    }

#if 0
    /* should fail due to item_size too large */
    rv = sk_circbuf_create_const_itemsize(&cbuf, INT32_MAX, 3);
    if (SK_CIRCBUF_OK == rv) {
        skAppPrintErr("FAIL at %d:"
                      " Creation succeeded; expected failure",
                      __LINE__);
        exit(EXIT_FAILURE);
    }
    if (SK_CIRCBUF_ERR_BAD_PARAM != rv) {
        skAppPrintErr("FAIL at %d:"
                      " Creation failed with status %d, expected status %d",
                      __LINE__, rv, SK_CIRCBUF_ERR_BAD_PARAM);
        exit(EXIT_FAILURE);
    }
#endif  /* 0 */

    /* should succeed */
    rv = sk_circbuf_create_const_itemsize(&cbuf, ITEM_COUNT, ITEM_COUNT);
    if (SK_CIRCBUF_OK != rv) {
        skAppPrintErr("FAIL at %d:"
                      " Creation failed with status %d, expected success",
                      __LINE__, rv);
        exit(EXIT_FAILURE);
    }

    pthread_mutex_lock(&shutdown_mutex);

    pthread_create(&read_thrd, NULL, &reader, cbuf);

    pthread_create(&write_thrd, NULL, &writer, cbuf);

    pthread_cond_wait(&shutdown_ok, &shutdown_mutex);
    pthread_mutex_unlock(&shutdown_mutex);

    sk_circbuf_stop(cbuf);

    sk_circbuf_print_stats(cbuf, NULL, skAppPrintErr);

    pthread_join(write_thrd, NULL);
    pthread_join(read_thrd, NULL);

    sk_circbuf_destroy(cbuf);

    /* set level to "emerg" to avoid the "Stopped logging" message */
    sklogSetLevel("emerg");
    sklogTeardown();

    skAppUnregister();

    return 0;
}


/*
** Local Variables:
** mode:c
** indent-tabs-mode:nil
** c-basic-offset:4
** End:
*/
