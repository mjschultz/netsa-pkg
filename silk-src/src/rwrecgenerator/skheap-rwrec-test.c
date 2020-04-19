/*
** Copyright (C) 2011-2020 by Carnegie Mellon University.
**
** @OPENSOURCE_LICENSE_START@
** See license information in ../../LICENSE.txt
** @OPENSOURCE_LICENSE_END@
*/

/*
**  skheap-rwrec-test.c
**
**     Test the skheap-rwrec code.
**
**  Michael Duggan
**  May 2011
*/


#include <silk/silk.h>

RCSIDENT("$SiLK: skheap-rwrec-test.c ef14e54179be 2020-04-14 21:57:45Z mthomas $");

#include <silk/skstream.h>
#include <silk/utils.h>
#include "skheap-rwrec.h"

int main(int UNUSED(argc), char **argv)
{
#define DATA_SIZE 30
    skrwrecheap_t *heap;
    int data[DATA_SIZE] = {
        201, 34, 202, 56, 203,  2,
        204, 65, 205,  3, 206,  5,
        207,  8, 208, 74, 209, 32,
        210, 78, 211, 79, 212, 80,
        213,  5, 214,  5, 215,  1};
    rwRec recs[DATA_SIZE];
    const rwRec *last, *cur;
    int rv;
    int i;

    /* register the application */
    skAppRegister(argv[0]);

    memset(recs, 0, sizeof(recs));
    for (i = 0; i < DATA_SIZE; i++) {
        rwRecSetElapsed(&recs[i], data[i]);
        rwRecSetProto(&recs[i], data[i]);
    }

    heap = skRwrecHeapCreate(1);
    if (heap == NULL) {
        skAppPrintErr("Failed to create heap");
        exit(EXIT_FAILURE);
    }

    for (i = 0; i < DATA_SIZE; i++) {
        rv = skRwrecHeapInsert(heap, &recs[i]);
        if (rv != 0) {
            skAppPrintErr("Failed to insert element");
            exit(EXIT_FAILURE);
        }
    }

    last = skRwrecHeapPeek(heap);
    if (last == NULL) {
        skAppPrintErr("Heap unexpectedly empty");
        exit(EXIT_FAILURE);
    }
    for (i = 0; i < DATA_SIZE; i++) {
        cur = skRwrecHeapPop(heap);
        if (cur == NULL) {
            skAppPrintErr("Heap unexpectedly empty");
            exit(EXIT_FAILURE);
        }
        if (i != 0 && cur == last) {
            skAppPrintErr("Unexpected duplicate");
            exit(EXIT_FAILURE);
        }
        if (rwRecGetProto(cur) < rwRecGetProto(last)) {
            skAppPrintErr("Incorrect ordering");
            exit(EXIT_FAILURE);
        }
        printf("%" PRIu32 "\n", rwRecGetProto(cur));
        last = cur;
    }
    if (skRwrecHeapPeek(heap) != NULL) {
        skAppPrintErr("Heap unexpectedly non-empty");
        exit(EXIT_FAILURE);
    }
    if (skRwrecHeapPop(heap) != NULL) {
        skAppPrintErr("Heap unexpectedly non-empty");
        exit(EXIT_FAILURE);
    }

    printf("Success!\n");

    return EXIT_SUCCESS;
}


/*
** Local Variables:
** mode:c
** indent-tabs-mode:nil
** c-basic-offset:4
** End:
*/
