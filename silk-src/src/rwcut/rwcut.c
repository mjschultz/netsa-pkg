/*
** Copyright (C) 2001-2017 by Carnegie Mellon University.
**
** @OPENSOURCE_LICENSE_START@
** See license information in ../../LICENSE.txt
** @OPENSOURCE_LICENSE_END@
*/

/*
** rwcut.c
**
**      cut fields/records from the given input file(s) using field
**      specifications from here, record filter specifications from
**      module libfilter
**
** 1/15/2002
**
** Suresh L. Konda
**
*/

#include <silk/silk.h>

RCSIDENT("$SiLK: rwcut.c efd886457770 2017-06-21 18:43:23Z mthomas $");

#include "rwcut.h"


/* TYPEDEFS AND MACROS */

/* When --copy-input is active but the required 'num_recs' records
 * have been printed, skStreamSkipRecords() is used to read data from
 * all remaining input streams.  This specifies the record-count
 * parameter to pass to that function. */
#define CUT_SKIP_COUNT 65536


/* EXPORTED VARIABLES */

/* The object to convert the record to text */
sk_formatter_t *fmtr;

/* handle input streams */
sk_options_ctx_t *optctx = NULL;
sk_flow_iter_t *flowiter = NULL;

/* number records to print */
uint64_t num_recs = 0;

/* number of records to skip before printing */
uint64_t skip_recs = 0;

/* number of records to "tail" */
uint64_t tail_recs = 0;

/* buffer used for storing 'tail_recs' records */
rwRec *tail_buf = NULL;

/* The output stream: where to print the records */
sk_fileptr_t output;

lua_State *L = NULL;


/* LOCAL VARIABLES */

/* current position in the 'tail_buf' */
static rwRec *tail_buf_cur;

/* whether we read more than 'tail_recs' records. 1==yes */
static int tail_buf_full = 0;


/* FUNCTION DEFINITIONS */

/*
 *  status = tailFiles();
 *
 *    Read SiLK flow records from all input streams and store the
 *    most recent 'tail_recs' number of records in the 'tail_buf'
 *    buffer.
 *
 *    Return -1 on read error, or 0 otherwise.
 */
static int
tailFiles(
    void)
{
    ssize_t rv;

    assert(tail_buf_cur);

    while ((rv = sk_flow_iter_get_next_rec(flowiter, tail_buf_cur)) == 0) {
        ++tail_buf_cur;
        if (tail_buf_cur == &tail_buf[tail_recs]) {
            tail_buf_cur = tail_buf;
            tail_buf_full = 1;
        }
    }

    return ((SKSTREAM_ERR_EOF == rv) ? 0 : -1);
}


/*
 *  printTailBuffer();
 *
 *    Print the SiLK Flow records that are in the global 'tail_buf'
 *    buffer.
 */
static void
printTailBuffer(
    void)
{
    uint64_t avail_recs;
    char *fmtr_buf;
    size_t len;

    /* determine the number of records available for printing and
     * position 'tail_buf_cur' on the first record to print */
    if (tail_buf_full) {
        /* the number of records is the size of the buffer and
         * 'tail_buf_cur' is already sitting on the first record */
        avail_recs = tail_recs;
    } else {
        /* compute number of records seen, then move 'tail_buf_cur' to
         * the first record in the buffer */
        avail_recs = tail_buf_cur - tail_buf;
        tail_buf_cur = tail_buf;
    }

    /* determine number of records to print */
    if (0 == num_recs) {
        num_recs = avail_recs;
    } else if (avail_recs < num_recs) {
        num_recs = avail_recs;
    }

    printTitle();

    while (num_recs) {
        addPluginFields(tail_buf_cur);
        len = sk_formatter_record_to_string(fmtr, tail_buf_cur, &fmtr_buf);
        if (!fwrite(fmtr_buf, len, 1, output.of_fp)) {
            skAppPrintErr("Could not write record");
            exit(EXIT_FAILURE);
        }
        rwRecReset(tail_buf_cur);
        --num_recs;
        ++tail_buf_cur;
        if (tail_buf_cur == &tail_buf[tail_recs]) {
            tail_buf_cur = tail_buf;
        }
    }
}


/*
 *  status = cutFiles(rwrec);
 *
 *    Process the initial SiLK Flow record in 'rwrec' then read all
 *    remaining SiLK flow records from the inputs streams and maybe
 *    print them according the values in 'skip_recs' and 'num_recs'.
 *
 *    Return -1 on error and 0 on success.
 */
static int
cutFiles(
    rwRec              *rwrec)
{
    ssize_t rv = SKSTREAM_OK;
    char *fmtr_buf = NULL;
    size_t len = 0;

    if (skip_recs) {
        /* account for record passed into the function */
        --skip_recs;

        rv = sk_flow_iter_skip_records(flowiter, skip_recs, NULL);
        if (rv != SKSTREAM_OK && rv != SKSTREAM_ERR_EOF) {
            return -1;
        }

        /* read a record to print */
        rv = sk_flow_iter_get_next_rec(flowiter, rwrec);
        if (rv) {
            if (SKSTREAM_ERR_EOF == rv) {
                return 0;
            }
            return -1;
        }
    }

    if (0 == num_recs) {
        /* print all records */
        do {
            addPluginFields(rwrec);

            len = sk_formatter_record_to_string(fmtr, rwrec, &fmtr_buf);
            if (!fwrite(fmtr_buf, len, 1, output.of_fp)) {
                skAppPrintErr("Could not write record");
                exit(EXIT_FAILURE);
            }
        } while ((rv = sk_flow_iter_get_next_rec(flowiter, rwrec)) == 0);
        if (SKSTREAM_ERR_EOF != rv) {
            return -1;
        }
        return 0;
    }

    do {
        addPluginFields(rwrec);

        len = sk_formatter_record_to_string(fmtr, rwrec, &fmtr_buf);
        if (!fwrite(fmtr_buf, len, 1, output.of_fp)) {
            skAppPrintErr("Could not write record");
            exit(EXIT_FAILURE);
        }
        --num_recs;
    } while (num_recs
             && ((rv = sk_flow_iter_get_next_rec(flowiter, rwrec)) == 0));

    if (0 == skOptionsCtxCopyStreamIsActive(optctx)) {
        /* we're done */
        return 0;
    }

    rv = sk_flow_iter_skip_remaining_records(flowiter);
    return ((SKSTREAM_ERR_EOF == rv || SKSTREAM_OK == rv) ? 0 : -1);
}


int main(int argc, char **argv)
{
    rwRec rwrec;
    ssize_t rv = 0;

    appSetup(argc, argv);                 /* never returns on error */

    if (tail_buf) {
        assert(tail_recs);
        tail_buf_cur = tail_buf;

        /* Process the files from command line or stdin */
        rv = tailFiles();
        if (rv < 0) {
            exit(EXIT_FAILURE);
        }
        printTailBuffer();
    } else {
        /* Process the files on command line or records from stdin */
        rwRecInitialize(&rwrec, L);

        /* get first record */
        rv = sk_flow_iter_get_next_rec(flowiter, &rwrec);
        if (SKSTREAM_OK != rv && SKSTREAM_ERR_EOF != rv) {
            exit(EXIT_FAILURE);
        }

        /* print title line */
        printTitle();

        if (SKSTREAM_ERR_EOF == rv) {
            /* no records so nothing else to do */
            appTeardown();
            return 0;
        }

        rv = cutFiles(&rwrec);
        if (rv < 0) {
            exit(EXIT_FAILURE);
        }
    }

    /* done */
    appTeardown();

    return 0;
}

/*
** Local Variables:
** mode:c
** indent-tabs-mode:nil
** c-basic-offset:4
** End:
*/
