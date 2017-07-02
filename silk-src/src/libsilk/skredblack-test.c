/*
** Copyright (C) 2014-2017 by Carnegie Mellon University.
**
** @OPENSOURCE_LICENSE_START@
** See license information in ../../LICENSE.txt
** @OPENSOURCE_LICENSE_END@
*/

/*
 *  File to test skredblack.c
 *
 */

#include <silk/silk.h>

RCSIDENT("$SiLK: skredblack-test.c efd886457770 2017-06-21 18:43:23Z mthomas $");

#include <silk/utils.h>
#include <silk/skredblack.h>


/* LOCAL DEFINES AND TYPEDEFS */

#define NUM_INSERTS  20


/* FUNCTION DEFINITIONS */

static void
printer(
    FILE               *fp,
    const void         *data)
{
    fprintf(fp, "%d", *(int*)data);
}


static int
compare(
    const void         *pa,
    const void         *pb,
    const void         *config)
{
    (void)config;

    if (*(int *)pa < *(int *)pb) {
        return -1;
    }
    if (*(int *)pa > *(int *)pb) {
        return 1;
    }
    return 0;
}


int main(int UNUSED(argc), char **argv)
{
    size_t i;
    int *ptr;
    void *val;
    sk_rbtree_t *rb;
    sk_rbtree_iter_t *rblist;
    int rv;
    pid_t pid = getpid();
    size_t len;
    int data[NUM_INSERTS];

    /* pid = 94878; */

    skAppRegister(argv[0]);

    srand(pid);

    fprintf(stderr, "pid is %ld\n", (unsigned long)pid);

    if (sk_rbtree_create(&rb, compare, free, NULL)) {
        skAppPrintErr("insufficient memory");
        exit(1);
    }

    rv = 20000;
    ptr = &rv;
    rv = sk_rbtree_remove(rb, ptr, NULL);
    fprintf(stdout, "remove from empty tree gives %d\n", rv);

    i = 0;
    for (i = 0; i < NUM_INSERTS; ++i) {
        sk_rbtree_debug_print(rb, stdout, printer);

        ptr = sk_alloc(int);
        data[i] = rand() & 0xff;
        *ptr = data[i];
        rv = sk_rbtree_insert(rb, (void*)ptr, NULL);
        fprintf(stdout, ("%4" SK_PRIuZ " insert of %d returns %d\n"),
                i, *ptr, rv);
        if (SK_RBTREE_ERR_DUPLICATE == rv) {
            free(ptr);
        }

        if (i % 5 == 1) {
            sk_rbtree_debug_print(rb, stdout, printer);

            ptr = sk_alloc(int);
            *ptr = data[i];
            rv = sk_rbtree_insert(rb, (void*)ptr, NULL);
            fprintf(stdout, ("%4" SK_PRIuZ " re-insert of %d returns %d\n"),
                    i, *ptr, rv);
            if (SK_RBTREE_ERR_DUPLICATE == rv) {
                free(ptr);
            }
        }
    }

    sk_rbtree_debug_print(rb, stdout, printer);

    rblist = sk_rbtree_iter_create();

    for (val = sk_rbtree_iter_bind_first(rblist, rb);
         val != NULL;
         val = sk_rbtree_iter_next(rblist))
    {
        printf("%6d\n", *(int *)val);
    }

    sk_rbtree_iter_free(rblist);

    len = NUM_INSERTS;
    while (len > 0) {
        i = rand() % len;
        --len;
        rv = sk_rbtree_remove(rb, &data[i], NULL);
        fprintf(stdout, ("%4" SK_PRIuZ " removal of data[%" SK_PRIuZ
                         "] = %d returns %d\n"),
                len, i, data[i], rv);

        sk_rbtree_debug_print(rb, stdout, printer);

        if (len % 5 == 3) {
            rv = sk_rbtree_remove(rb, &data[i], NULL);
            fprintf(stdout, ("%4" SK_PRIuZ " re-removal of data[%"
                             SK_PRIuZ "] = %d returns %d\n"),
                    len, i, data[i], rv);

            sk_rbtree_debug_print(rb, stdout, printer);
        }

        data[i] = data[len];
    }

    rv = 20000;
    ptr = &rv;
    rv = sk_rbtree_remove(rb, ptr, NULL);
    fprintf(stdout, "remove from empty tree gives %d\n", rv);

    sk_rbtree_destroy(&rb);

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
