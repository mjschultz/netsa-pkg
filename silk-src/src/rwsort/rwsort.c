/*
** Copyright (C) 2001-2017 by Carnegie Mellon University.
**
** @OPENSOURCE_LICENSE_START@
** See license information in ../../LICENSE.txt
** @OPENSOURCE_LICENSE_END@
*/

/*
**  rwsort reads SiLK Flow Records from the standard input or from
**  named files and sorts them on one or more user-specified fields.
**
**  rwsort attempts to sort the records in RAM using a buffer whose
**  maximum size is DEFAULT_SORT_BUFFER_SIZE bytes.  The user may
**  choose a different maximum size with the --sort-buffer-size
**  switch.  The buffer rwsort initially allocates is
**  1/SORT_NUM_CHUNKS of this size; when it is full, the buffer is
**  reallocated and grown by another 1/SORT_NUM_CHUNKS.  This
**  continues until all records are read, a realloc() fails, or the
**  maximum buffer size is reached.
**
**  The purpose of gradually increasing the buffer size is twofold:
**  1. So we don't use more memory than we actually need.  2. When
**  allocating a large buffer during start-up, the OS would give us
**  the memory, but if we attempted to use the buffer the OS would
**  kill the rwsort process.
**
**  Records are read and stored in this buffer; if the input ends
**  before the buffer is filled, the records are sorted and printed to
**  standard out or to the named output file.
**
**  However, if the buffer fills before the input is completely read,
**  the records in the buffer are sorted and written to a temporary
**  file on disk; the buffer is cleared, and reading of the input
**  resumes, repeating the process as necessary until all records are
**  read.  We then do an N-way merge-sort on the temporary files,
**  where N is either all the temporary files, MAX_MERGE_FILES, or the
**  maximum number that we can open before running out of file descriptors
**  (EMFILE) or memory.  If we cannot open all temporary files, we
**  merge the N files into a new temporary file, then add it to the
**  list of files to merge.
**
**  When the temporary files are written to the same volume (file
**  system) as the final output, the maximum disk usage will be
**  2-times the number of records read (times the size per record);
**  when different volumes are used, the disk space required for the
**  temporary files will be between 1 and 1.5 times the number of
**  records.
**
*/

#include <silk/silk.h>

RCSIDENT("$SiLK: rwsort.c efd886457770 2017-06-21 18:43:23Z mthomas $");

#include "rwsort.h"
#include <silk/skheap.h>


/* TYPEDEFS */


/* EXPORTED VARIABLES */

/* number of key fields to sort over; skStringMapParse() sets this */
uint32_t num_fields = 0;

/* the fields that make up the sort key */
key_field_t *key_fields = NULL;

/* for looping over the input streams */
sk_flow_iter_t *flowiter = NULL;

/* output stream */
skstream_t *out_stream = NULL;

/* sidecar to write to the output file */
sk_sidecar_t *out_sidecar = NULL;

/* temp file context */
sk_tempfilectx_t *tmpctx;

/* whether the user wants to reverse the sort order */
int reverse = 0;

/* whether to treat the input files as already sorted */
int presorted_input = 0;

/* maximum amount of RAM to attempt to allocate */
size_t sort_buffer_size;

lua_State *L = NULL;



/* FUNCTION DEFINITIONS */

/* How to sort the flows: forward or reverse? */
#define RETURN_SORT_ORDER(val, cleanup)         \
    cleanup ;                                   \
    return (reverse ? -(val) : (val))

/* Define our raw sorting functions */
#define RETURN_IF_SORTED(func, rec_a, rec_b, cleanup)                   \
    if (func((rwRec *)(rec_a)) == func((rwRec *)(rec_b))) {             \
        /* no-op */                                                     \
    } else if (func((rwRec *)(rec_a)) < func((rwRec *)(rec_b))) {       \
        RETURN_SORT_ORDER(-1, cleanup);                                 \
    } else {                                                            \
        RETURN_SORT_ORDER(1, cleanup);                                  \
    }

#define RETURN_IF_SORTED_IPS(func, rec_a, rec_b, cleanup)       \
    {                                                           \
        skipaddr_t ipa, ipb;                                    \
        int cmp;                                                \
        func((rwRec *)(rec_a), &ipa);                           \
        func((rwRec *)(rec_b), &ipb);                           \
        cmp = skipaddrCompare(&ipa, &ipb);                      \
        if (cmp != 0) {                                         \
            RETURN_SORT_ORDER(cmp, cleanup);                    \
        }                                                       \
    }


static uint8_t
getIcmpType(
    const void         *rec)
{
    if (rwRecIsICMP((const rwRec *)rec)) {
        return rwRecGetIcmpType((const rwRec *)rec);
    }
    return 0;
}

static uint8_t
getIcmpCode(
    const void         *rec)
{
    if (rwRecIsICMP((const rwRec *)rec)) {
        return rwRecGetIcmpCode((const rwRec *)rec);
    }
    return 0;
}


/*
 *  rwrecCompare(a, b);
 *
 *     Returns an ordering on the recs pointed to `a' and `b' by
 *     comparing the fields listed in the sort_fields[] array.
 *
 *     Note that `a' and `b' must be rwRec*.
 */
static int
rwrecCompare(
    const void         *a,
    const void         *b)
{
    key_field_t *key;
    uint32_t i;
    int rv;

    /* number of items to pop from Lua stack */
    int depth = 0;

    /* index into Lua stack of the sidecar for records a and b */
    int sc_a = 0;
    int sc_b = 0;

    /* type of the specific sidecar item */
    int type_a;
    int type_b;

    int have_sidecar = 0;

    for (i = 0, key = key_fields; i < num_fields; ++i, ++key) {
        switch (key->kf_id) {
          case RWREC_FIELD_SIP:
            RETURN_IF_SORTED_IPS(rwRecMemGetSIP, a, b, lua_pop(L, depth));
            break;

          case RWREC_FIELD_DIP:
            RETURN_IF_SORTED_IPS(rwRecMemGetDIP, a, b, lua_pop(L, depth));
            break;

          case RWREC_FIELD_NHIP:
            RETURN_IF_SORTED_IPS(rwRecMemGetNhIP, a, b, lua_pop(L, depth));
            break;

          case RWREC_FIELD_SPORT:
            RETURN_IF_SORTED(rwRecGetSPort, a, b, lua_pop(L, depth));
            break;

          case RWREC_FIELD_DPORT:
            RETURN_IF_SORTED(rwRecGetDPort, a, b, lua_pop(L, depth));
            break;

          case RWREC_FIELD_PROTO:
            RETURN_IF_SORTED(rwRecGetProto, a, b, lua_pop(L, depth));
            break;

          case RWREC_FIELD_PKTS:
            RETURN_IF_SORTED(rwRecGetPkts, a, b, lua_pop(L, depth));
            break;

          case RWREC_FIELD_BYTES:
            RETURN_IF_SORTED(rwRecGetBytes, a, b, lua_pop(L, depth));
            break;

          case RWREC_FIELD_FLAGS:
            RETURN_IF_SORTED(rwRecGetFlags, a, b, lua_pop(L, depth));
            break;

          case RWREC_FIELD_STIME:
            RETURN_IF_SORTED(rwRecGetStartTime, a, b, lua_pop(L, depth));
            break;

          case RWREC_FIELD_ELAPSED:
            RETURN_IF_SORTED(rwRecGetElapsed, a, b, lua_pop(L, depth));
            break;

          case RWREC_FIELD_ETIME:
            RETURN_IF_SORTED(rwRecGetEndTime, a, b, lua_pop(L, depth));
            break;

          case RWREC_FIELD_SID:
            RETURN_IF_SORTED(rwRecGetSensor, a, b, lua_pop(L, depth));
            break;

          case RWREC_FIELD_INPUT:
            RETURN_IF_SORTED(rwRecGetInput, a, b, lua_pop(L, depth));
            break;

          case RWREC_FIELD_OUTPUT:
            RETURN_IF_SORTED(rwRecGetOutput, a, b, lua_pop(L, depth));
            break;

          case RWREC_FIELD_INIT_FLAGS:
            RETURN_IF_SORTED(rwRecGetInitFlags, a, b, lua_pop(L, depth));
            break;

          case RWREC_FIELD_REST_FLAGS:
            RETURN_IF_SORTED(rwRecGetRestFlags, a, b, lua_pop(L, depth));
            break;

          case RWREC_FIELD_TCP_STATE:
            RETURN_IF_SORTED(rwRecGetTcpState, a, b, lua_pop(L, depth));
            break;

          case RWREC_FIELD_APPLICATION:
            RETURN_IF_SORTED(rwRecGetApplication, a, b, lua_pop(L, depth));
            break;

          case RWREC_FIELD_FTYPE_CLASS:
          case RWREC_FIELD_FTYPE_TYPE:
            RETURN_IF_SORTED(rwRecGetFlowType, a, b, lua_pop(L, depth));
            break;

          case RWREC_FIELD_ICMP_TYPE:
            RETURN_IF_SORTED(getIcmpType, a, b, lua_pop(L, depth));
            break;

          case RWREC_FIELD_ICMP_CODE:
            RETURN_IF_SORTED(getIcmpCode, a, b, lua_pop(L, depth));
            break;

          default:
            if (1 != have_sidecar) {
                if (-1 == have_sidecar) {
                    /* missing from both records */
                    break;
                }
                /* get the sidecar table for each record */
                type_a = lua_rawgeti(L, LUA_REGISTRYINDEX,
                                     rwRecGetSidecar((rwRec *)a));
                sc_a = lua_gettop(L);
                type_b = lua_rawgeti(L, LUA_REGISTRYINDEX,
                                     rwRecGetSidecar((rwRec *)b));
                sc_b = lua_gettop(L);
                depth += 2;
                /* ensure sidecar data on both objects */
                if (LUA_TTABLE != type_a) {
                    if (LUA_TTABLE != type_b) {
                        /* missing from both */
                        have_sidecar = -1;
                        lua_pop(L, 2);
                        depth -= 2;
                        break;
                    } else {
                        /* missing on a but not on b; treat a < b */
                        RETURN_SORT_ORDER(-1, lua_pop(L, depth));
                    }
                } else if (LUA_TTABLE != type_b) {
                    /* missing on b but not on a; treat a > b */
                    RETURN_SORT_ORDER(1, lua_pop(L, depth));
                }
            }
            type_a = lua_getfield(L, sc_a, key->kf_name);
            type_b = lua_getfield(L, sc_b, key->kf_name);
            depth += 2;
            if (PLUGIN_FIELD_BIT & key->kf_id) {
                /* we go through the fields in the same way they were
                 * added, and 'key' should always be an index to the
                 * current plugin. */
                uint8_t *data_a, *data_b;
                skplugin_err_t err;

                if (LUA_TSTRING != type_a || LUA_TSTRING != type_b) {
                    skAppPrintErr("Type of sidecar element for plugin item"
                                  " is not string");
                    skAbort();
                }
                data_a = (uint8_t *)lua_tostring(L, -2);
                data_b = (uint8_t *)lua_tostring(L, -1);
                err = skPluginFieldRunBinCompareFn(
                    key->kf_pi_handle, &rv, data_a, data_b);
                if (err != SKPLUGIN_OK) {
                    const char **name;
                    skPluginFieldName(key->kf_pi_handle, &name);
                    skAppPrintErr(("Plugin-based field %s failed "
                                   "comparing binary values "
                                   "with error code %d"), name[0], err);
                    exit(EXIT_FAILURE);
                }
                if (rv != 0) {
                    RETURN_SORT_ORDER(rv, lua_pop(L, depth));
                }
            } else if (type_a != type_b) {
                /* when the types are different, sort based on the
                 * type */
                rv = type_a - type_b;
                RETURN_SORT_ORDER(rv, lua_pop(L, depth));
            } else if (LUA_TNIL == type_a) {
                lua_pop(L, 2);
                depth -= 2;
                break;
            } else {
                assert(SIDECAR_FIELD_BIT & key->kf_id);
                switch (key->kf_type) {
                  case SK_SIDECAR_UINT8:
                  case SK_SIDECAR_UINT16:
                  case SK_SIDECAR_UINT32:
                  case SK_SIDECAR_UINT64:
                    {
                        lua_Integer n_a, n_b;

                        n_a = lua_tointeger(L, -2);
                        n_b = lua_tointeger(L, -1);
                        if (n_a != n_b) {
                            RETURN_SORT_ORDER((n_a - n_b), lua_pop(L, depth));
                        }
                        break;
                    }
                  case SK_SIDECAR_DOUBLE:
                    {
                        lua_Number n_a, n_b;

                        n_a = lua_tonumber(L, -2);
                        n_b = lua_tonumber(L, -1);
                        if (n_a != n_b) {
                            RETURN_SORT_ORDER((n_a - n_b), lua_pop(L, depth));
                        }
                        break;
                    }
                  case SK_SIDECAR_STRING:
                  case SK_SIDECAR_BINARY:
                    {
                        const char *str_a, *str_b;
                        size_t len_a = 0;
                        size_t len_b = 0;

                        str_a = lua_tolstring(L, -2, &len_a);
                        str_b = lua_tolstring(L, -1, &len_b);
                        if (!str_a) {
                            rv = (str_b) ? -1 : 0;
                        } else if (!str_b) {
                            rv = 1;
                        } else if (len_a < len_b) {
                            rv = memcmp(str_a, str_b, len_a);
                            if (0 == rv) { rv = -1; }
                        } else {
                            rv = memcmp(str_a, str_b, len_b);
                            if (0 == rv) {
                                rv = len_a > len_b;
                            }
                        }
                        if (rv) {
                            RETURN_SORT_ORDER(rv, lua_pop(L, depth));
                        }
                        break;
                    }
                  case SK_SIDECAR_ADDR_IP4:
                  case SK_SIDECAR_ADDR_IP6:
                    {
                        skipaddr_t *addr_a, *addr_b;

                        addr_a = sk_lua_toipaddr(L, -2);
                        addr_b = sk_lua_toipaddr(L, -1);
                        if (addr_a && addr_b) {
                            rv = skipaddrCompare(addr_a, addr_b);
                            if (rv) {
                                RETURN_SORT_ORDER(rv, lua_pop(L, depth));
                            }
                        }
                        break;
                    }
                  case SK_SIDECAR_DATETIME:
                    {
                        sktime_t *t_a, *t_b;

                        t_a = sk_lua_todatetime(L, -2);
                        t_b = sk_lua_todatetime(L, -1);
                        if (!t_a || !t_b) {
                        } else if (t_a != t_b) {
                            RETURN_SORT_ORDER((t_a - t_b), lua_pop(L, depth));
                        }
                        break;
                    }
                  case SK_SIDECAR_BOOLEAN:
                    {
                        int bool_a, bool_b;

                        bool_a = lua_toboolean(L, -2);
                        bool_b = lua_toboolean(L, -1);
                        if (bool_a != bool_b) {
                            RETURN_SORT_ORDER(bool_a - bool_b,
                                              lua_pop(L, depth));
                        }
                        break;
                    }
                    break;
                  case SK_SIDECAR_EMPTY:
                    break;
                  case SK_SIDECAR_LIST:
                  case SK_SIDECAR_TABLE:
                    break;
                  case SK_SIDECAR_UNKNOWN:
                    break;
                }
            }
            lua_pop(L, 2);
            depth -= 2;
            break;
        }
    }

    lua_pop(L, depth);
    return 0;
}


/*
 *  status = compHeapNodes(b, a, v_recs);
 *
 *    Callback function used by the heap two compare two heapnodes.
 *    The arguments 'b' and 'a' are the indexes into an array of
 *    rwRec, where 'v_recs' is the array of rwRec.
 *
 *    Note the order of arguments is 'b', 'a'.
 */
static int
compHeapNodes(
    const skheapnode_t  b,
    const skheapnode_t  a,
    void               *v_recs)
{
    const rwRec *recs = (rwRec *)v_recs;
    const rwRec *rec_a = &recs[*(uint16_t*)a];
    const rwRec *rec_b = &recs[*(uint16_t*)b];

    return rwrecCompare(rec_a, rec_b);
}


/*
 *    Create and return a new temporary file for storing SiLK Flow
 *    records, putting the index of the file in 'temp_idx'.  Exit the
 *    application on failure.
 */
static skstream_t *
sortTempCreate(
    int                *temp_idx)
{
    FILE *fp;
    skstream_t *stream = NULL;
    char *path;
    ssize_t rv;
    int fd;

    fp = skTempFileCreate(tmpctx, temp_idx, &path);
    if (fp == NULL) {
        skAppPrintSyserror("Error creating new temporary file");
        appExit(EXIT_FAILURE);
    }
    fd = dup(fileno(fp));
    fclose(fp);
    if (-1 == fd) {
        skAppPrintSyserror("Error duplicating temporary file pointer");
        appExit(EXIT_FAILURE);
    }
    if ((rv = skStreamCreate(&stream, SK_IO_WRITE, SK_CONTENT_SILK_FLOW))
        || (rv = skStreamBind(stream, path))
        || (rv = skHeaderSetCompressionMethod(skStreamGetSilkHeader(stream),
                                              SK_COMPMETHOD_BEST))
        || (rv = skStreamFDOpen(stream, fd)))
    {
        skAppPrintErr("Error creating stream wrapper for new temporary file");
        skStreamPrintLastErr(stream, rv, &skAppPrintErr);
        skStreamDestroy(&stream);
        appExit(EXIT_FAILURE);
    }
    if (sk_sidecar_count_elements(out_sidecar)) {
        rv = skStreamSetSidecar(stream, out_sidecar);
        if (rv) {
            skStreamPrintLastErr(stream, rv, &skAppPrintErr);
            skStreamDestroy(&stream);
            appExit(EXIT_FAILURE);
        }
    }

    return stream;
}

/*
 *    Re-open the existing temporary file indexed by 'temp_idx'.
 *    Return the new stream.  Return NULL if we could not open the
 *    stream due to out-of-memory or out-of-file-handles error.  Exit
 *    the application on any other error.
 */
static skstream_t *
sortTempReopen(
    int                 temp_idx)
{
    skstream_t *stream = NULL;
    const char *path;
    ssize_t rv;
    int errnum;

    path = skTempFileGetName(tmpctx, temp_idx);
    if ((rv = skStreamCreate(&stream, SK_IO_READ, SK_CONTENT_SILK_FLOW))
        || (rv = skStreamBind(stream, path)))
    {
        if (SKSTREAM_ERR_ALLOC == rv) {
            skStreamDestroy(&stream);
            return NULL;
        }
        skStreamPrintLastErr(stream, rv, skAppPrintErr);
        skStreamDestroy(&stream);
        appExit(EXIT_FAILURE);
    }

    rv = skStreamOpen(stream);
    if (rv) {
        errnum = skStreamGetLastErrno(stream);
        if ((errnum == EMFILE) || (errnum == ENOMEM)) {
            skStreamDestroy(&stream);
            return NULL;
        }
        skStreamPrintLastErr(stream, rv, skAppPrintErr);
        skStreamDestroy(&stream);
        appExit(EXIT_FAILURE);
    }
    return stream;
}

/*
 *    Close a temporary file.  Exit the application if stream was open
 *    for write and closing fails.
 */
static void
sortTempClose(
    skstream_t         *stream)
{
    char errbuf[2 * PATH_MAX];
    ssize_t rv;

    rv = skStreamClose(stream);
    switch (rv) {
      case SKSTREAM_OK:
      case SKSTREAM_ERR_NOT_OPEN:
      case SKSTREAM_ERR_CLOSED:
        skStreamDestroy(&stream);
        return;
      case SKSTREAM_ERR_NULL_ARGUMENT:
        return;
    }

    skStreamLastErrMessage(stream, rv, errbuf, sizeof(errbuf));
    skAppPrintErr("Error closing temporary file: %s", errbuf);
    if (skStreamGetMode(stream) == SK_IO_WRITE) {
        appExit(EXIT_FAILURE);
    }
    skStreamDestroy(&stream);
}

/*
 *    Read a SiLK Flow record from 'str_stream' into 'str_rec'.
 *    Return 1 if a record was read, and return or 0 for other
 *    condition (end-of-file, short read, error).
 */
#define sortTempRead(str_stream, str_rec)                       \
    sortTempReadHelper(str_stream, str_rec, __FILE__, __LINE__)

static ssize_t
sortTempReadHelper(
    skstream_t         *stream,
    rwRec              *rwrec,
    const char         *file_name,
    int                 file_line)
{
    ssize_t rv;

    rv = skStreamReadRecord(stream, rwrec);
#if TRACEMSG_LEVEL == 0
    (void)file_name;
    (void)file_line;

    return (SKSTREAM_OK == rv);
#else
    if (SKSTREAM_OK == rv) {
        return 1;
    } else {
        char errbuf[2 * PATH_MAX];

        skStreamLastErrMessage(stream, rv, errbuf, sizeof(errbuf));
        TRACEMSG(("%s:%d: Failed to read record: %s",
                  file_name, file_line, errbuf));
    }
    return 0;
#endif  /* #else of #if TRACEMSG_LEVEL == 0 */
}


/*
 *    Write the SiLK Flow record 'stw_rec' to 'stw_stream'.  Return on
 *    success and exit the appliation on error or short write.
 */
#define sortTempWrite(stw_stream, stw_rec)                    \
    sortTempWriteHelper(stw_stream, stw_rec, __FILE__, __LINE__)

static void
sortTempWriteHelper(
    skstream_t         *stream,
    const rwRec        *rwrec,
    const char         *file_name,
    int                 file_line)
{
    char errbuf[2 * PATH_MAX];
    ssize_t rv;

    rv = skStreamWriteRecord(stream, rwrec);
    if (0 == rv) {
        return;
    }
    skStreamLastErrMessage(stream, rv, errbuf, sizeof(errbuf));

#if TRACEMSG_LEVEL == 0
    (void)file_name;
    (void)file_line;
#else
    TRACEMSG(("%s:%d: Failed to write record: %s",
              file_name, file_line, errbuf));
#endif

    skAppPrintErr("Error writing to temporary file: %s", errbuf);
    appExit(EXIT_FAILURE);
}


/*
 *  mergeFiles(temp_file_idx)
 *
 *    Merge the temporary files numbered from 0 to 'temp_file_idx'
 *    inclusive into either the final output file or another temporary
 *    file, maintaining sorted order.
 *
 *    Exit the application if an error occurs.
 */
static void
mergeFiles(
    int                 temp_file_idx)
{
    skstream_t *fps[MAX_MERGE_FILES];
    rwRec recs[MAX_MERGE_FILES];
    int j;
    uint16_t open_count;
    uint16_t i;
    uint16_t *top_heap;
    uint16_t lowest;
    int tmp_idx_a;
    int tmp_idx_b;
    skstream_t *fp_intermediate = NULL;
    int tmp_idx_intermediate;
    int opened_all_temps = 0;
    skheap_t *heap;
    uint32_t heap_count;
    int rv;

    TRACEMSG(("Merging #%d through #%d into '%s'",
              0, temp_file_idx, skStreamGetPathname(out_stream)));

    heap = skHeapCreate2(compHeapNodes, MAX_MERGE_FILES, sizeof(uint16_t),
                         NULL, recs);
    if (NULL == heap) {
        skAppPrintOutOfMemory("heap");
        appExit(EXIT_FAILURE);
    }

    /* initialize the recs[] array */
    rwRecInitializeArray(recs, L, MAX_MERGE_FILES);

    /* the index of the first temp file to the merge */
    tmp_idx_a = 0;

    /* This loop repeats as long as we haven't read all of the temp
     * files generated in the sorting stage. */
    do {
        assert(SKHEAP_ERR_EMPTY==skHeapPeekTop(heap,(skheapnode_t*)&top_heap));

        /* the index of the last temp file to merge */
        tmp_idx_b = temp_file_idx;

        /* open an intermediate temp file.  The merge-sort will have
         * to write records here if there are not enough file handles
         * available to open all the existing tempoary files. */
        fp_intermediate = sortTempCreate(&tmp_idx_intermediate);

        /* count number of files we open */
        open_count = 0;

        TRACEMSG(("Attempting to reopen files #%d through #%d...",
                  tmp_idx_a, tmp_idx_b));

        /* Attempt to open up to MAX_MERGE_FILES, though we an open
         * may fail due to lack of resources (EMFILE or ENOMEM) */
        for (j = tmp_idx_a; j <= tmp_idx_b; ++j) {
            fps[open_count] = sortTempReopen(j);
            if (NULL == fps[open_count]) {
                if (0 == open_count) {
                    skAppPrintErr("Unable to open any temporary files");
                    appExit(EXIT_FAILURE);
                }
                /* We cannot open any more files.  Rewind counter by
                 * one to catch this file on the next merge. */
                assert(j > 0);
                tmp_idx_b = j - 1;
                TRACEMSG(
                    ("EMFILE limit hit--merging #%d through #%d into #%d: %s",
                     tmp_idx_a, tmp_idx_b, tmp_idx_intermediate,
                     strerror(errno)));
                break;
            }

            /* read the first record */
            if (sortTempRead(fps[open_count], &recs[open_count])) {
                /* insert the file index into the heap */
                skHeapInsert(heap, &open_count);
                ++open_count;
                if (open_count == MAX_MERGE_FILES) {
                    /* We've reached the limit for this pass.  Set
                     * tmp_idx_b to the file we just opened. */
                    tmp_idx_b = j;
                    TRACEMSG((("MAX_MERGE_FILES limit hit--"
                               "merging #%d through #%d into #%d"),
                              tmp_idx_a, tmp_idx_b, tmp_idx_intermediate));
                    break;
                }
            } else {
                if (skStreamGetLastReturnValue(fps[open_count]) != 0) {
                    skAppPrintSyserror(("Error reading first record from"
                                        " temporary file '%s'"),
                                       skTempFileGetName(tmpctx, j));
                    appExit(EXIT_FAILURE);
                }
                TRACEMSG(("Ignoring empty temporary file #%d '%s'",
                          j, skTempFileGetName(tmpctx, j)));
                skStreamDestroy(&fps[open_count]);
            }
        }

        /* Here, we check to see if we've opened all temp files.  If
         * so, set a flag so we write data to final destination and
         * break out of the loop after we're done. */
        if (tmp_idx_b == temp_file_idx) {
            opened_all_temps = 1;
            /* no longer need the intermediate temp file */
            sortTempClose(fp_intermediate);
            fp_intermediate = NULL;
        } else {
            /* we could not open all temp files, so merge all opened
             * temp files into the intermediate file.  Add the
             * intermediate file to the list of files to merge */
            temp_file_idx = tmp_idx_intermediate;
        }

        TRACEMSG((("Merging %" PRIu16 " temporary files (#%d through #%d)"),
                  open_count, tmp_idx_a, tmp_idx_b));

        heap_count = skHeapGetNumberEntries(heap);
        assert(heap_count == open_count);

        /* exit this while() once we are only processing a single
         * file */
        while (heap_count > 1) {
            /* entry at the top of the heap has the lowest key */
            skHeapPeekTop(heap, (skheapnode_t*)&top_heap);
            lowest = *top_heap;

            /* write the lowest record */
            if (fp_intermediate) {
                /* write record to intermediate tmp file */
                sortTempWrite(fp_intermediate, &recs[lowest]);
            } else {
                /* we successfully opened all (remaining) temp files,
                 * write to record to the final destination */
                rv = skStreamWriteRecord(out_stream, &recs[lowest]);
                if (0 != rv) {
                    skStreamPrintLastErr(out_stream, rv, &skAppPrintErr);
                    if (SKSTREAM_ERROR_IS_FATAL(rv)) {
                        appExit(EXIT_FAILURE);
                    }
                }
            }

            /* replace the record we just wrote */
            if (sortTempRead(fps[lowest], &recs[lowest])) {
                /* read was successful.  "insert" the new entry into
                 * the heap (which has same value as old entry). */
                skHeapReplaceTop(heap, &lowest, NULL);
            } else {
                /* no more data for this file; remove it from the
                 * heap */
                skHeapExtractTop(heap, NULL);
                --heap_count;
                TRACEMSG(("Finished reading file #%u; %u files remain",
                          tmp_idx_a + lowest, heap_count));
            }
        }

        /* get index of the remaining file */
        skHeapExtractTop(heap, &lowest);
        assert(SKHEAP_ERR_EMPTY==skHeapPeekTop(heap,(skheapnode_t*)&top_heap));

        /* read records from the remaining file */
        if (fp_intermediate) {
            do {
                sortTempWrite(fp_intermediate, &recs[lowest]);
            } while (sortTempRead(fps[lowest], &recs[lowest]));
        } else {
            do {
                rv = skStreamWriteRecord(out_stream, &recs[lowest]);
                if (0 != rv) {
                    skStreamPrintLastErr(out_stream, rv, &skAppPrintErr);
                    if (SKSTREAM_ERROR_IS_FATAL(rv)) {
                        appExit(EXIT_FAILURE);
                    }
                }
            } while (sortTempRead(fps[lowest], &recs[lowest]));
        }

        TRACEMSG(("Finished reading file #%u; 0 files remain",
                  tmp_idx_a + lowest));
        TRACEMSG((("Finished processing #%d through #%d"),
                  tmp_idx_a, tmp_idx_b));

        /* Close all open temp files */
        for (i = 0; i < open_count; ++i) {
            sortTempClose(fps[i]);
        }
        /* Delete all temp files we opened (or attempted to open) this
         * time */
        for (j = tmp_idx_a; j <= tmp_idx_b; ++j) {
            skTempFileRemove(tmpctx, j);
        }

        /* Close the intermediate temp file. */
        if (fp_intermediate) {
            sortTempClose(fp_intermediate);
            fp_intermediate = NULL;
        }

        /* Start the next merge with the next input temp file */
        tmp_idx_a = tmp_idx_b + 1;

    } while (!opened_all_temps);

    skHeapFree(heap);
}


/*
 *    If file cannot be opened due to no file handles, return an error
 *    code that causes the flow iterator to retry the stream.  If
 *    there is a different error, report the error and return that
 *    same error code.
 *
 *    This is a callback function for the flow iterator when reading
 *    presorted input.
 */
static ssize_t
open_error_callback(
    sk_flow_iter_t     *f_iter,
    skstream_t         *stream,
    ssize_t             err_code,
    void               *cb_data)
{
    (void)f_iter;
    (void)cb_data;

    if (EMFILE == errno || ENOMEM == errno) {
        return SKSTREAM_ERR_NOT_OPEN;
    }
    skStreamPrintLastErr(stream, err_code, &skAppPrintErr);
    return err_code;
}


/*
 *  temp_file_idx = sortPresorted(argc, argv);
 *
 *    Assume all input files have been sorted using the exact same
 *    --fields value as those we are using, and simply merge sort
 *    them.
 *
 *    This function is still fairly complicated, because we have to
 *    handle running out of memory or file descriptors as we process
 *    the inputs.  When that happens, we write the records to
 *    temporary files and then use mergeFiles() above to sort those
 *    files.
 *
 *    Exits the application if an error occurs.  On success, this
 *    function returns the index of the final temporary file to use
 *    for the mergeSort().  A return value less than 0 is considered
 *    successful and indicates that no merge-sort is required.
 */
static int
sortPresorted(
    int                 argc,
    char              **argv)
{
    skstream_t *stream[MAX_MERGE_FILES];
    rwRec recs[MAX_MERGE_FILES];
    uint16_t i;
    uint16_t open_count;
    uint16_t *top_heap;
    uint16_t lowest;
    skstream_t *fp_intermediate = NULL;
    int temp_file_idx = -1;
    int opened_all_inputs = 0;
    skheap_t *heap;
    uint32_t heap_count;
    int rv;

    memset(stream, 0, sizeof(stream));

    heap = skHeapCreate2(compHeapNodes, MAX_MERGE_FILES, sizeof(uint16_t),
                         NULL, recs);
    if (NULL == heap) {
        skAppPrintOutOfMemory("heap");
        appExit(EXIT_FAILURE);
    }

    /* initialize the recs[] array */
    rwRecInitializeArray(recs, L, MAX_MERGE_FILES);

    /* set a callback that is used when an error occurs that checks
     * whether we are out of file handles. */
    sk_flow_iter_set_stream_error_cb(flowiter, SK_FLOW_ITER_CB_ERROR_OPEN,
                                     open_error_callback, NULL);

    /* This loop repeats as long as we haven't read all of input
     * files */
    do {
        /* open an intermediate temp file.  The merge-sort will have
         * to write records here if there are not enough file handles
         * available to open all the input files. */
        fp_intermediate = sortTempCreate(&temp_file_idx);

        TRACEMSG(("Attempting to open %u presorted files...",MAX_MERGE_FILES));

        /* Attempt to open up to MAX_MERGE_FILES, though we an open
         * may fail due to lack of resources (EMFILE or ENOMEM) */
        for (open_count = 0; open_count < MAX_MERGE_FILES; ++open_count) {
            rv = sk_flow_iter_get_next_stream(flowiter, &stream[open_count]);
            if (SKSTREAM_OK == rv) {
                continue;
            }
            if (SKSTREAM_ERR_EOF == rv) {
                /* no more input.  add final information to header */
                sk_file_header_t *hdr;
                hdr = skStreamGetSilkHeader(out_stream);
                if ((rv = skHeaderAddInvocation(hdr, 1, argc, argv))
                    || (rv = skOptionsNotesAddToStream(out_stream)))
                {
                    skStreamPrintLastErr(out_stream, rv, &skAppPrintErr);
                }
                /* add sidecar to stream */
                if (sk_sidecar_count_elements(out_sidecar)) {
                    rv = skStreamSetSidecar(out_stream, out_sidecar);
                    if (rv) {
                        skStreamPrintLastErr(out_stream, rv, &skAppPrintErr);
                    }
                }
                rv = 1;
            } else if (errno == EMFILE || errno == ENOMEM) {
                rv = -2;
            } else {
                /* skStreamPrintLastErr(stream, rv, skAppPrintErr); */
                rv = -1;
            }
            break;
        }
        switch (rv) {
          case 1:
            /* successfully opened all (remaining) input files */
            opened_all_inputs = 1;
            if (temp_file_idx > 0) {
                TRACEMSG(("Opened all remaining inputs"));
            } else {
                /* we opened all the input files in a single pass.  we
                 * no longer need the intermediate temp file */
                TRACEMSG(("Opened all inputs in a single pass"));
                sortTempClose(fp_intermediate);
                fp_intermediate = NULL;
                temp_file_idx = -1;
            }
            break;
          case -1:
            /* unexpected error opening a file */
            appExit(EXIT_FAILURE);
          case -2:
            /* ran out of memory or file descriptors */
            TRACEMSG((("Unable to open all inputs---"
                       "out of memory or file handles")));
            break;
          case 0:
            if (open_count == MAX_MERGE_FILES) {
                /* ran out of pointers for this run */
                TRACEMSG((("Unable to open all inputs---"
                           "MAX_MERGE_FILES limit reached")));
                break;
            }
            /* no other way that rv == 0 */
            TRACEMSG(("rv == 0 but open_count is %d. Abort.",
                      open_count));
            skAbort();
          default:
            /* unexpected error */
            TRACEMSG(("Got unexpected rv value = %d", rv));
            skAbortBadCase(rv);
        }

        /* Read the first record from each file into the work buffer */
        for (i = 0; i < open_count; ++i) {
            if (fillRecordAndKey(stream[i], &recs[i])) {
                /* insert the file index into the heap */
                skHeapInsert(heap, &i);
            }
        }

        heap_count = skHeapGetNumberEntries(heap);

        TRACEMSG((("Merging %" PRIu32 " of %" PRIu16 " open presorted files"),
                  heap_count, open_count));

        /* exit this while() once we are only processing a single
         * file */
        while (heap_count > 1) {
            /* entry at the top of the heap has the lowest key */
            skHeapPeekTop(heap, (skheapnode_t*)&top_heap);
            lowest = *top_heap;

            /* write the lowest record */
            if (fp_intermediate) {
                /* we are using the intermediate temp file, so
                 * write the record there. */
                sortTempWrite(fp_intermediate, &recs[lowest]);
            } else {
                /* we are not using any temp files, write the
                 * record to the final destination */
                rv = skStreamWriteRecord(out_stream, &recs[lowest]);
                if (0 != rv) {
                    skStreamPrintLastErr(out_stream, rv, &skAppPrintErr);
                    if (SKSTREAM_ERROR_IS_FATAL(rv)) {
                        appExit(EXIT_FAILURE);
                    }
                }
            }

            /* replace the record we just wrote */
            if (fillRecordAndKey(stream[lowest], &recs[lowest])) {
                /* read was successful.  "insert" the new entry into
                 * the heap (which has same value as old entry). */
                skHeapReplaceTop(heap, &lowest, NULL);
            } else {
                /* no more data for this file; remove it from the
                 * heap */
                skHeapExtractTop(heap, NULL);
                --heap_count;
                TRACEMSG(("Finished reading records from file #%u;"
                          " %" PRIu32 " files remain",
                          lowest, heap_count));
            }
        }

        /* read records from the remaining file */
        if (SKHEAP_OK == skHeapExtractTop(heap, &lowest)) {
            if (fp_intermediate) {
                do {
                    sortTempWrite(fp_intermediate, &recs[lowest]);
                } while (fillRecordAndKey(stream[lowest], &recs[lowest]));
            } else {
                do {
                    rv = skStreamWriteRecord(out_stream, &recs[lowest]);
                    if (0 != rv) {
                        skStreamPrintLastErr(out_stream, rv, &skAppPrintErr);
                        if (SKSTREAM_ERROR_IS_FATAL(rv)) {
                            appExit(EXIT_FAILURE);
                        }
                    }
                } while (fillRecordAndKey(stream[lowest], &recs[lowest]));
            }
            TRACEMSG(("Finished reading records from file #%u; 0 files remain",
                      lowest));
        }

        /* Close the input files that we processed this time. */
        for (i = 0; i < open_count; ++i) {
            sk_flow_iter_close_stream(flowiter, stream[i]);
        }

        /* Close the intermediate temp file. */
        if (fp_intermediate) {
            sortTempClose(fp_intermediate);
            fp_intermediate = NULL;
        }
    } while (!opened_all_inputs);

    skHeapFree(heap);

    /* If any temporary files were written, we now have to merge-sort
     * them */
    return temp_file_idx;
}


/*
 *  int = sortRandom(argc, argv);
 *
 *    Don't make any assumptions about the input.  Store the input
 *    records in a large buffer, and sort those in-core records once
 *    all records are processed or the buffer is full.  If the buffer
 *    fills up, store the sorted records into temporary files.  Once
 *    all records are read, use mergeFiles() above to merge-sort the
 *    temporary files.
 *
 *    Exits the application if an error occurs.  On success, this
 *    function returns the index of the final temporary file to use
 *    for the mergeSort().  A return value less than 0 is considered
 *    successful and indicates that no merge-sort is required.
 */
static int
sortRandom(
    int                 argc,
    char              **argv)
{
    /* index of last used temporary file; -1 == not used */
    int temp_file_idx = -1;
    /* region of memory for records */
    rwRec *record_buffer = NULL;
    /* current size of 'record_buffer' (as a number of recs) */
    size_t buffer_recs;
    /* maximum allowed size of 'recprd_buffer' (number of recs) */
    size_t buffer_max_recs;
    /* step size to take to get to buffer_max_recs (num recs) */
    size_t buffer_chunk_recs;
    /* how quickly to grow to 'buffer_max_recs' */
    size_t num_chunks;
    /* current positon in the current 'record_buffer' */
    rwRec *cur_rec;
    /* number of records currently in memory */
    size_t record_count;
    /* pointer to record held by stream */
    skstream_t *fp;
    size_t i;
    ssize_t in_rv;
    ssize_t rv;

    /* Determine the maximum number of records that will fit into the
     * buffer if it grows the maximum size */
    buffer_max_recs = sort_buffer_size / NODE_SIZE;
    TRACEMSG((("sort_buffer_size = %" SK_PRIuZ
               "\nnode_size = %" SK_PRIuZ
               "\nbuffer_max_recs = %" SK_PRIuZ),
              sort_buffer_size, NODE_SIZE, buffer_max_recs));

    /* We will grow to the maximum size in chunks; do not allocate
     * more than MAX_CHUNK_SIZE at any time */
    num_chunks = NUM_CHUNKS;
    if (num_chunks < 1) {
        num_chunks = 1;
    }
    if (sort_buffer_size / num_chunks > MAX_CHUNK_SIZE) {
        num_chunks = sort_buffer_size / MAX_CHUNK_SIZE;
    }

    /* Attempt to allocate the initial chunk.  If we fail, increment
     * the number of chunks---which will decrease the amount we
     * attempt to allocate at once---and try again. */
    for (;;) {
        buffer_chunk_recs = buffer_max_recs / num_chunks;
        TRACEMSG((("num_chunks = %" SK_PRIuZ
                   "\nbuffer_chunk_recs = %" SK_PRIuZ),
                  num_chunks, buffer_chunk_recs));

        record_buffer = (rwRec *)malloc(buffer_chunk_recs * NODE_SIZE);
        if (record_buffer) {
            /* malloc was successful */
            break;
        } else if (buffer_chunk_recs < MIN_IN_CORE_RECORDS) {
            /* give up at this point */
            skAppPrintErr("Error allocating space for %d records",
                          MIN_IN_CORE_RECORDS);
            appExit(EXIT_FAILURE);
        } else {
            /* reduce the amount we allocate at once by increasing the
             * number of chunks and try again */
            TRACEMSG(("malloc() failed"));
            ++num_chunks;
        }
    }

    buffer_recs = buffer_chunk_recs;
    TRACEMSG((("buffer_recs = %" SK_PRIuZ), buffer_recs));

    rwRecInitializeArray(record_buffer, L, buffer_recs);
    cur_rec = record_buffer;
    record_count = 0;

    while ((in_rv = sk_flow_iter_get_next_rec(flowiter, cur_rec)) == 0) {
        addPluginFields(cur_rec);
        ++record_count;
        ++cur_rec;
        if (record_count == buffer_recs) {
             /* no room for the record in the buffer */
            if (buffer_recs < buffer_max_recs) {
                /* buffer is not at its max size, see if we can grow
                 * it */
                rwRec *old_buf = record_buffer;

                /* add a chunk of records.  if we are near the max,
                 * set the size to the max */
                buffer_recs += buffer_chunk_recs;
                if (buffer_recs + buffer_chunk_recs > buffer_max_recs) {
                    buffer_recs = buffer_max_recs;
                }
                TRACEMSG((("Buffer full--attempt to grow to "
                           "%" SK_PRIuZ " records, %" SK_PRIuZ " octets"),
                          buffer_recs, NODE_SIZE * buffer_recs));

                /* attempt to grow */
                record_buffer = (rwRec *)realloc(record_buffer,
                                                 NODE_SIZE * buffer_recs);
                if (record_buffer) {
                    /* Success, initialize new records and make
                     * certain cur_rec points into the new buffer */
                    cur_rec = record_buffer + record_count;
                    rwRecInitializeArray(cur_rec, L,
                                         (buffer_recs - record_count));
                } else {
                    /* Unable to grow it */
                    TRACEMSG(("realloc() failed"));
                    record_buffer = old_buf;
                    buffer_max_recs = buffer_recs = record_count;
                }
            }

            if (record_count == buffer_max_recs) {
                /* Either buffer at maximum size or attempt to grow it
                 * failed. */
                /* Sort */
                TRACEMSG(("Sorting %" SK_PRIuZ " records...", record_count));
                skQSort(record_buffer, record_count, NODE_SIZE, &rwrecCompare);
                TRACEMSG(("Sorting %" SK_PRIuZ " records...done",
                          record_count));

                fp = sortTempCreate(&temp_file_idx);
                TRACEMSG(("Writing %" SK_PRIuZ " records to %s",
                          record_count, skStreamGetPathname(fp)));
                for (i = 0; i < record_count; ++i) {
                    sortTempWrite(fp, &record_buffer[i]);
                    rwRecReset(&record_buffer[i]);
                }
                sortTempClose(fp);
                /* Reset everything */
                record_count = 0;
                cur_rec = record_buffer;
            }
        }
    }
    if (SKSTREAM_ERR_EOF != in_rv) {
        free(record_buffer);
        appExit(EXIT_FAILURE);
    }

    /* Sort (and maybe store) last batch of records */
    if (record_count > 0) {
        TRACEMSG(("Sorting %" SK_PRIuZ " records...", record_count));
        skQSort(record_buffer, record_count, NODE_SIZE, &rwrecCompare);
        TRACEMSG(("Sorting %" SK_PRIuZ " records...done", record_count));
        if (temp_file_idx >= 0) {
            /* Write last batch to temp file */
            fp = sortTempCreate(&temp_file_idx);
            TRACEMSG(("Writing %" SK_PRIuZ " records to %s",
                      record_count, skStreamGetPathname(fp)));
            for (i = 0; i < record_count; ++i) {
                sortTempWrite(fp, &record_buffer[i]);
                rwRecReset(&record_buffer[i]);
            }
            sortTempClose(fp);
        }
    }

    /* no more input.  add final information to header */
    if ((rv = skHeaderAddInvocation(skStreamGetSilkHeader(out_stream),
                                    1, argc, argv))
        || (rv = skOptionsNotesAddToStream(out_stream)))
    {
        skStreamPrintLastErr(out_stream, rv, &skAppPrintErr);
    }
    /* add sidecar to stream */
    if (sk_sidecar_count_elements(out_sidecar)) {
        rv = skStreamSetSidecar(out_stream, out_sidecar);
        if (rv) {
            skStreamPrintLastErr(out_stream, rv, &skAppPrintErr);
        }
    }

    /* Generate the output */

    if (record_count > 0 && temp_file_idx == -1) {
        /* No temp files written, just output batch of records */
        TRACEMSG((("Writing %" SK_PRIuZ " records to '%s'"),
                  record_count, skStreamGetPathname(out_stream)));
        for (i = 0; i < record_count; ++i) {
            rv = skStreamWriteRecord(out_stream, &record_buffer[i]);
            rwRecReset(&record_buffer[i]);
            if (0 != rv) {
                skStreamPrintLastErr(out_stream, rv, &skAppPrintErr);
                if (SKSTREAM_ERROR_IS_FATAL(rv)) {
                    free(record_buffer);
                    appExit(EXIT_FAILURE);
                }
            }
        }
    }
    /* else a merge sort is required; which gets invoked from main() */

    free(record_buffer);

    return temp_file_idx;
}


int main(int argc, char **argv)
{
    int temp_idx = -1;
    int rv;

    appSetup(argc, argv);                 /* never returns on error */

    if (presorted_input) {
        temp_idx = sortPresorted(argc, argv);
    } else {
        temp_idx = sortRandom(argc, argv);
    }
    if (temp_idx >= 0) {
        mergeFiles(temp_idx);
    }

    if (skStreamGetRecordCount(out_stream) == 0) {
        /* No records were read at all; write the header to the output
         * file */
        rv = skStreamWriteSilkHeader(out_stream);
        if (0 != rv) {
            skStreamPrintLastErr(out_stream, rv, &skAppPrintErr);
        }
    }

    /* close the file */
    if ((rv = skStreamClose(out_stream))
        || (rv = skStreamDestroy(&out_stream)))
    {
        skStreamPrintLastErr(out_stream, rv, &skAppPrintErr);
        appExit(EXIT_FAILURE);
    }
    out_stream = NULL;

    return 0;
}


/*
** Local Variables:
** mode:c
** indent-tabs-mode:nil
** c-basic-offset:4
** End:
*/
