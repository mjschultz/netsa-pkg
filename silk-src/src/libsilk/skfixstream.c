/*
** Copyright (C) 2014-2017 by Carnegie Mellon University.
**
** @OPENSOURCE_LICENSE_START@
** See license information in ../../LICENSE.txt
** @OPENSOURCE_LICENSE_END@
*/

/*
 *  skfixstream.c
 *
 *    A wrapper over skstream_t that supports reading and writing
 *    streams of IPFIX records.
 */

#include <silk/silk.h>

RCSIDENT("$SiLK: skfixstream.c efd886457770 2017-06-21 18:43:23Z mthomas $");

#include <silk/skfixstream.h>
#include <silk/skipfixcert.h>
#include <silk/skredblack.h>
#include <silk/skschema.h>
#include <silk/skstream.h>
#include <silk/skvector.h>
#include <silk/utils.h>

#ifdef SKFIXSTREAM_TRACE_LEVEL
#define TRACEMSG_LEVEL SKFIXSTREAM_TRACE_LEVEL
#endif
#define TRACEMSG(msg) TRACEMSG_TO_TRACEMSGLVL(1, msg)
#include <silk/sktracemsg.h>


/* LOCAL DEFINES AND TYPEDEFS */

/*
 *    Whether to export automatically export IPFIX elements.  This
 *    macro is currently off because the code it wraps needs to be
 *    updated to work with skstream.
 */
#ifndef SK_FIXSTREAM_EXPORT_ELEMENTS
#  define SK_FIXSTREAM_EXPORT_ELEMENTS 0
#endif

/*
 *    First two bytes of an IPFIX file and any IPFIX block.
 */
#define STREAM_MAGIC_NUMBER_IPFIX  0x000a

/*
 *    Octet-length required to check magic numbers
 */
#define STREAM_CHECK_MAGIC_BUFSIZE  sizeof(uint16_t)


/* Values for the flowEndReason. this first set is defined by the
 * IPFIX spec */
#define STREAM_END_IDLE            1
#define STREAM_END_ACTIVE          2
#define STREAM_END_CLOSED          3
#define STREAM_END_FORCED          4
#define STREAM_END_RESOURCE        5

/* Mask for the values of flowEndReason: want to ignore the next bit */
#define STREAM_END_MASK            0x1f

/* Bits from flowEndReason: whether flow is a continuation */
#define STREAM_END_ISCONT          0x80

/* Bits from flowAttributes */
#define STREAM_FLOW_ATTRIBUTE_UNIFORM_PACKET_SIZE 0x01


/*
 *    Return SKSTREAM_ERR_NULL_ARGUMENT when 'srin_stream' is NULL.
 */
#define STREAM_RETURN_IF_NULL(srin_stream) \
    if (NULL == (srin_stream)) { return SKSTREAM_ERR_NULL_ARGUMENT; }


struct sk_fixstream_st {
    /* raw message read from disk; the fBuf_t uses this data */
    uint8_t                     msgbuf[1 + UINT16_MAX];
    /* please to start an error message */
    char                        errbuf[2048];
    /* the fixbuf object to read msgbuf[] or write to the exporter */
    fBuf_t                     *fbuf;
    /* the file handle */
    FILE                       *fp;
    /* error handle */
    GError                     *gerr;
    /* a structure to use when writing that knows which external
     * template IDs have been used */
    sk_rbtree_t                *ext_tmpl;
    /* when writing, most recently used internal and external
     * schema */
    const sk_schema_t          *prev_schema[2];
    /* callback function to invoke when a new schema is read */
    sk_fixstream_schema_cb_fn_t schema_cb_fn;
    /* user's context object to pass to the new-schema callback */
    const void                 *schema_cb_data;
    /* observation domain for output */
    uint32_t                    domain;
    /* true if the definitions of the IEs should be exported */
    unsigned                    export_ies       : 1;

    skstream_t                 *stream;
    int                         pipe[2];
    int                         errnum;
    ssize_t                     err_info;
    ssize_t                     last_rv;
    sk_fixrec_t                 cur_fixrec;
    uint64_t                    rec_count;
    fbInfoModel_t              *info_model;
    skstream_mode_t             io_mode;
    unsigned                    is_eof :1;
    unsigned                    is_callers_model :1;
};
/* typedef struct sk_fixstream_st sk_fixstream_t; // silk_types.h */


/*
 *    fixstream_tmpl_rec_t is used when reading IPFIX files.
 *
 *    There is one of these objects for every IPFIX template we read.
 *    This object holds the record most recently read that matches the
 *    template.  The object is stored on the template itself.
 *
 *    The object is created by fixstream_template_cb()
 *    and freed by fixstream_template_cb_free().
 */
struct fixstream_tmpl_rec_st {
    sk_fixrec_t             rec;
    size_t                  len;
    uint16_t                tid;
};
typedef struct fixstream_tmpl_rec_st fixstream_tmpl_rec_t;


/*
 *    Whether template is being added externally or internally.
 *
 *    These values are used as indexes to arrays that keep track of
 *    which template ID to specify when writing IPFIX flows.  They are
 *    also used as arguments to some libfixbuf functions.
 */
#define  EXT_TMPL  0
#define  INT_TMPL  1


/*
 *    fixstream_ext_tmpl_elem_t is used when writing IPFIX files.
 *
 *    There is one of these objects for every IPFIX template we write.
 *    The objects are stored in the 'ext_tmpl' member of the
 *    sk_fixstream_t structure.
 *
 *    The object is used to keep track of the external templates that
 *    have been used.  The key is the tid and tmpl.  The schema member
 *    is here to ensure the 'tmpl' member does not get freed while it
 *    is in the 'ext_tmpl' structure.
 */
struct fixstream_ext_tmpl_elem_st {
    const sk_schema_t  *schema;
    fbTemplate_t       *tmpl;
    uint16_t            tid;
};
typedef struct fixstream_ext_tmpl_elem_st fixstream_ext_tmpl_elem_t;



/* LOCAL FUNCTION PROTOTYPES */

static int
fixstream_export_schema(
    sk_fixstream_t     *stream,
    const sk_schema_t  *schema);
static void
fixstream_template_cb_free(
    void               *v_tmpl_ctx,
    void               *app_ctx);
static int
fixstream_write_from_pipe(
    sk_fixstream_t     *stream);


/* FUNCTION DEFINITIONS */

/*
 *  status = fixstream_export_list_schemas(stream, schema, rec);
 *
 *    Update 'stream' so its fixbuf session knows about all the
 *    sub-schemas in use on 'rec', whose schema is 'schema'.
 *
 *    The template used by 'schema' is NOT added to the session.  It
 *    is the caller's responsibility to ensure that the stream's
 *    session knows about template used by 'schema'.
 */
static int
fixstream_export_list_schemas(
    sk_fixstream_t     *stream,
    const sk_schema_t  *schema,
    const sk_fixrec_t  *rec)
{
    sk_fixlist_t *list;
    const sk_fixrec_t *list_rec;
    const sk_schema_t *list_schema;
    const sk_field_t *list_field;
    const sk_field_t *field;
    sk_schema_err_t err;
    int visit_recs;
    size_t i, j, k;
    int rv;

    assert(sk_fixrec_get_schema(rec) == schema);

    /*
     *    Check for any list fields in the schema for 'rec'.  If any
     *    that exist, create an iterator to visit all elements in the
     *    list.
     *
     *    For a basicList, recursively call this function for each
     *    record if the list's IE is itself a list.
     *
     *    For a subTemplateList, add the list's schema to the stream's
     *    session then recursively call this function for each record.
     *
     *    For a subTemplateMultiList, recursively call this function
     *    for each record.
     */

    for (i = 0; i < sk_schema_get_count(schema); ++i) {
        field = sk_schema_get_field(schema, i);
        TRACEMSG(("%s:%d: WriterAddListSchemas procesing field %s of schema %p",
                  __FILE__, __LINE__, sk_field_get_name(field),(void*)schema));
        switch (sk_field_get_type(field)) {
          case FB_BASIC_LIST:
            err = sk_fixrec_get_list(rec, field, &list);
            if (err) {
                skAppPrintErr("Unable to get basicList: %s",
                              sk_schema_strerror(err));
                return SKSTREAM_ERR_SCHEMA;
            }
            if (0 == sk_fixlist_count_elements(list)) {
                /* list is empty; nothing to do */
                sk_fixlist_destroy(list);
                break;
            }
            list_schema = sk_fixlist_get_schema(list, 0);
            visit_recs = 0;
            switch (sk_field_get_type(sk_schema_get_field(list_schema, 0))){
              case FB_BASIC_LIST:
              case FB_SUB_TMPL_LIST:
              case FB_SUB_TMPL_MULTI_LIST:
                visit_recs = 1;
                break;
            }
            if (0 == visit_recs) {
                sk_fixlist_destroy(list);
                TRACEMSG(("%s:%d: No need to visit elements of basicList",
                          __FILE__, __LINE__));
                break;
            }
            TRACEMSG(("%s:%d: Visiting %u elements of basicList %p",
                      __FILE__, __LINE__, sk_fixlist_count_elements(list),
                      (void*)list));
            while (sk_fixlist_next_element(list, &list_rec) == SK_ITERATOR_OK){
                rv = fixstream_export_list_schemas(stream, list_schema,
                                                     list_rec);
                if (rv) {
                    sk_fixlist_destroy(list);
                    return rv;
                }
            }
            TRACEMSG(("%s:%d: Finished visiting elements of basicList %p",
                      __FILE__, __LINE__, (void*)list));
            sk_fixlist_destroy(list);
            break;

          case FB_SUB_TMPL_LIST:
            err = sk_fixrec_get_list(rec, field, &list);
            if (err) {
                skAppPrintErr("Unable to get subTemplateList: %s",
                              sk_schema_strerror(err));
                return SKSTREAM_ERR_SCHEMA;
            }
            /* add the list's schema to the stream's session */
            list_schema = sk_fixlist_get_schema(list, 0);
            if (NULL == list_schema) {
                skAppPrintErr("Unable to get subTemplateList's schema");
                sk_fixlist_destroy(list);
                return SKSTREAM_ERR_SCHEMA;
            }
            TRACEMSG(("%s:%d: Exporting schema %p of subTemplateList %p",
                      __FILE__, __LINE__, (void*)list_schema, (void*)list));
            rv = fixstream_export_schema(stream, list_schema);
            if (rv) {
                sk_fixlist_destroy(list);
                return rv;
            }
            visit_recs = 0;
            if (sk_fixlist_count_elements(list)) {
                /* if the STL's schema contains list elements, we need
                 * to visit each record in the list */
                for (k = 0;
                     ((0 == visit_recs)
                      && (list_field = sk_schema_get_field(list_schema, k)));
                     ++k)
                {
                    switch (sk_field_get_type(list_field)) {
                      case FB_BASIC_LIST:
                      case FB_SUB_TMPL_LIST:
                      case FB_SUB_TMPL_MULTI_LIST:
                        visit_recs = 1;
                        break;
                    }
                }
            }
            if (0 == visit_recs) {
                sk_fixlist_destroy(list);
                TRACEMSG(("%s:%d: No need to visit elements of subTemplateList",
                          __FILE__, __LINE__));
                break;
            }
            TRACEMSG(("%s:%d: Visiting %u elements of subTemplateList %p",
                      __FILE__, __LINE__, sk_fixlist_count_elements(list),
                      (void*)list));
            while (sk_fixlist_next_element(list, &list_rec) == SK_ITERATOR_OK){
                rv = fixstream_export_list_schemas(stream, list_schema,
                                                     list_rec);
                if (rv) {
                    sk_fixlist_destroy(list);
                    return rv;
                }
            }
            TRACEMSG(("%s:%d: Finished visiting elements of subTemplateList %p",
                      __FILE__, __LINE__, (void*)list));
            sk_fixlist_destroy(list);
            break;

          case FB_SUB_TMPL_MULTI_LIST:
            err = sk_fixrec_get_list(rec, field, &list);
            if (err) {
                skAppPrintErr("Unable to get subTemplateMultiList: %s",
                              sk_schema_strerror(err));
                return SKSTREAM_ERR_SCHEMA;
            }
            /* add the list's schemas to the stream's session */
            for (j = 0; (list_schema = sk_fixlist_get_schema(list, j)); ++j) {
                TRACEMSG((("%s:%d: Exporting schema #%" SK_PRIuZ
                           " %p of subTemplateMultiList %p"),
                          __FILE__, __LINE__, j, (void*)list_schema,
                          (void*)list));
                rv = fixstream_export_schema(stream, list_schema);
                if (rv) {
                    sk_fixlist_destroy(list);
                    return rv;
                }
            }
            TRACEMSG(("%s:%d: Checking %u elements of subTemplateMultiList %p",
                      __FILE__, __LINE__, sk_fixlist_count_elements(list),
                      (void*)list));
            list_schema = NULL;
            visit_recs = 0;
            while (sk_fixlist_next_element(list, &list_rec) == SK_ITERATOR_OK){
                if (list_schema != sk_fixrec_get_schema(list_rec)) {
                    visit_recs = 0;
                    list_schema = sk_fixrec_get_schema(list_rec);
                    /* if the STML's schema contains list elements, we
                     * need to visit each record in the list */
                    for (k = 0;
                         ((0 == visit_recs)
                          && (list_field=sk_schema_get_field(list_schema, k)));
                         ++k)
                    {
                        switch (sk_field_get_type(list_field)) {
                          case FB_BASIC_LIST:
                          case FB_SUB_TMPL_LIST:
                          case FB_SUB_TMPL_MULTI_LIST:
                            visit_recs = 1;
                            break;
                        }
                    }
                }
                if (0 == visit_recs) {
                    TRACEMSG((("%s:%d: No need to visit elements of"
                               " subTemplateMultiList that use schema %p"),
                              __FILE__, __LINE__, (void*)schema));
                    continue;
                }
                TRACEMSG((("%s:%d: Visiting elements of "
                           "subTemplateMultiList %p that use schema %p"),
                          __FILE__, __LINE__, (void*)list, (void*)schema));
                rv = fixstream_export_list_schemas(stream, list_schema,
                                                     list_rec);
                if (rv) {
                    sk_fixlist_destroy(list);
                    return rv;
                }
                TRACEMSG((("%s:%d: Finished visiting elements of"
                           " subTemplateMultiList %p that use schema %p"),
                          __FILE__, __LINE__, (void*)list, (void*)schema));
            }
            TRACEMSG(("%s:%d: Finished checking elements of"
                      " subTemplateMultiList %p",
                      __FILE__, __LINE__, (void*)list));
            sk_fixlist_destroy(list);
            break;

          default:
            break;
        }
    }

    return SKSTREAM_OK;
}


static int
fixstream_export_schema(
    sk_fixstream_t     *stream,
    const sk_schema_t  *schema)
{
    fixstream_ext_tmpl_elem_t *elem;
    fixstream_ext_tmpl_elem_t key;
    sk_rbtree_iter_t *iter;
    sk_vector_t *to_remove;
    fbTemplate_t *cur_tmpl;
    sk_schema_err_t err;
    fbInfoElement_t *ie1;
    fbInfoElement_t *ie2;
    int rb_status;
    int same_tmpl;
    size_t count;
    size_t i;

    /* get schema's template object and template ID */
    err = sk_schema_get_template(schema, &key.tmpl, &key.tid);
    if (err) {
        skAppPrintErr("Unable to get schema's template: %s",
                      sk_schema_strerror(err));
        return SKSTREAM_ERR_SCHEMA;
    }

    TRACEMSG(("%s:%d: WriterExportSchema has template %p 0x%04x for schema %p",
              __FILE__, __LINE__, (void*)key.tmpl, key.tid, (void*)schema));

    /* have we seen this template object/ID pair before? */
    elem = ((fixstream_ext_tmpl_elem_t*)
            sk_rbtree_find(stream->ext_tmpl, &key));
    if (elem) {
        /* yes, we have */
        TRACEMSG(("%s:%d: RBtree contains matching template %p 0x%04x",
                  __FILE__, __LINE__, (void*)key.tmpl, key.tid));
        return SKSTREAM_OK;
    }

    /* check for that template ID in the stream's session */
    cur_tmpl = fbSessionGetTemplate(fBufGetSession(stream->fbuf), EXT_TMPL,
                                    key.tid, NULL);
    if (cur_tmpl == key.tmpl) {
        /* found the ID and the objects match; add the template
         * object/ID pair to the red-black tree */
        TRACEMSG(("%s:%d: Stream's session already contains template %p 0x%04x",
                  __FILE__, __LINE__, (void*)key.tmpl, key.tid));
        elem = sk_alloc(fixstream_ext_tmpl_elem_t);
        *elem = key;
        elem->schema = sk_schema_clone(schema);
        rb_status = sk_rbtree_insert(stream->ext_tmpl, elem, NULL);
        if (SK_RBTREE_OK != rb_status) {
            TRACEMSG(("%s:%d: Failed to add template %p 0x%04x to RBtree: %d",
                      __FILE__, __LINE__, (void*)key.tmpl, key.tid,
                      rb_status));
            /* FIXME */
            return SKSTREAM_OK;
        }
        TRACEMSG(("%s:%d: Added template %p 0x%04x to RBtree",
                  __FILE__, __LINE__, (void*)key.tmpl, key.tid));
        return SKSTREAM_OK;
    }
    if (cur_tmpl) {
        /* template objects differ; see if this schema's template has
         * the same structure as the one already in the session */
        TRACEMSG((("%s:%d: Stream's session contains template 0x%04x but"
                   " object is %p instead of %p"),
                  __FILE__, __LINE__, key.tid, (void*)cur_tmpl,
                  (void*)key.tmpl));
        count = fbTemplateCountElements(key.tmpl);
        if (fbTemplateCountElements(cur_tmpl) != count) {
            same_tmpl = 0;
        } else {
            same_tmpl = 1;
            for (i = 0; i < count; ++i) {
                ie1 = fbTemplateGetIndexedIE(key.tmpl, i);
                ie2 = fbTemplateGetIndexedIE(cur_tmpl, i);
                if (ie1->ref.canon != ie2->ref.canon
                    || ie1->len != ie2->len)
                {
                    same_tmpl = 0;
                    break;
                }
            }
        }

        if (same_tmpl) {
            /* we different template objects that use the same TID and
             * have the same strucure; should be no need to modify the
             * session; add an entry to the cache */
            TRACEMSG(("%s:%d: Templates %p and %p have the same structure",
                      __FILE__, __LINE__, (void*)cur_tmpl, (void*)key.tmpl));
            elem = sk_alloc(fixstream_ext_tmpl_elem_t);
            *elem = key;
            elem->schema = sk_schema_clone(schema);
            rb_status = sk_rbtree_insert(stream->ext_tmpl, elem, NULL);
            if (SK_RBTREE_OK != rb_status) {
                TRACEMSG((("%s:%d: Failed to add template %p 0x%04x"
                           " to RBtree: %d"),
                          __FILE__, __LINE__, (void*)key.tmpl, key.tid,
                          rb_status));
                /* FIXME */
                return SKSTREAM_OK;
            }
            TRACEMSG(("%s:%d: Added template %p 0x%04x to RBtree",
                      __FILE__, __LINE__, (void*)key.tmpl, key.tid));
            return SKSTREAM_OK;
        }

        /* the templates are different; below we replace the template
         * object that uses this template ID; but first, remove any
         * entries that use that template ID from our cache */
        to_remove = sk_vector_create(sizeof(fixstream_ext_tmpl_elem_t*));

        iter = sk_rbtree_iter_create();
        elem = ((fixstream_ext_tmpl_elem_t*)
                sk_rbtree_iter_bind_first(iter, stream->ext_tmpl));
        while (elem && elem->tid < key.tid) {
            elem = (fixstream_ext_tmpl_elem_t*)sk_rbtree_iter_next(iter);
        }
        while (elem && elem->tid == key.tid) {
            TRACEMSG(("%s:%d: Removing template %p 0x%04x from RBtree",
                      __FILE__, __LINE__, (void*)elem->tmpl, elem->tid));
            sk_vector_append_value(to_remove, elem);
            elem = (fixstream_ext_tmpl_elem_t*)sk_rbtree_iter_next(iter);
        }
        sk_rbtree_iter_free(iter);

        for (i = sk_vector_get_count(to_remove); i > 0; --i) {
            sk_vector_get_value(to_remove, i, (void*)&elem);
            sk_rbtree_remove(stream->ext_tmpl, elem, NULL);
        }
        sk_vector_destroy(to_remove);

        /* add new element to the tree */
        elem = sk_alloc(fixstream_ext_tmpl_elem_t);
        *elem = key;
        elem->schema = sk_schema_clone(schema);
        rb_status = sk_rbtree_insert(stream->ext_tmpl, elem, NULL);
        if (SK_RBTREE_OK != rb_status) {
            TRACEMSG(("%s:%d: Failed to add template %p 0x%04x to RBtree: %d",
                      __FILE__, __LINE__, (void*)key.tmpl, key.tid,
                      rb_status));
        } else {
            TRACEMSG(("%s:%d: Added template %p 0x%04x to RBtree",
                      __FILE__, __LINE__, (void*)key.tmpl, key.tid));
        }
    }

    TRACEMSG((("%s:%d: Adding external template %p 0x%04x to session %p on '%s'"
               " (replacing %p)"),
              __FILE__, __LINE__, (void*)key.tmpl, key.tid,
              (void*)fBufGetSession(stream->fbuf),
              skStreamGetPathname(stream->stream),
              (void*)cur_tmpl));
    if (fbSessionAddTemplate(fBufGetSession(stream->fbuf), EXT_TMPL, key.tid,
                             key.tmpl, &stream->gerr))
    {
        return SKSTREAM_OK;
    }
    TRACEMSG(("%s:%d: Unable to add template %p 0x%04x to session %p: %s",
              __FILE__, __LINE__, (void*)key.tmpl, key.tid,
              (void*)fBufGetSession(stream->fbuf), stream->gerr->message));
    return SKSTREAM_ERR_GERROR;
}


/*
 *    Free the fBuf_t that 'stream' uses.  If 'stream' is writing
 *    IPFIX records, emit any pending records and flush the skstream_t
 *    that 'stream' wraps.
 */
static int
fixstream_fbuf_free(
    sk_fixstream_t     *stream)
{
    ssize_t rv = SKSTREAM_OK;
    ssize_t rv2;

    assert(stream);
    assert(stream->fbuf);

    if (SK_IO_READ == stream->io_mode) {
        fBufFree(stream->fbuf);
    } else {
        if (!fBufEmit(stream->fbuf, &stream->gerr)) {
            rv = SKSTREAM_ERR_GERROR;
        }
        rv2 = fixstream_write_from_pipe(stream);
        if (rv2 && !rv) {
            rv = rv2;
        }
        fBufFree(stream->fbuf);
        rv2 = fclose(stream->fp);
        if (EOF == rv2 && !rv) {
            stream->errnum = errno;
            rv = SKSTREAM_ERR_WRITE;
        }
        rv2 = fixstream_write_from_pipe(stream);
        if (rv2 && !rv) {
            rv = rv2;
        }
        rv2 = skStreamFlush(stream->stream);
        if (rv2 && !rv) {
            rv = rv2;
        }
        close(stream->pipe[0]);
        stream->fp = NULL;
        stream->pipe[0] = stream->pipe[1] = -1;
    }
    stream->fbuf = NULL;

    return rv;
}


/*
 *  fixstream_template_cb(session, tid, tmpl, stream, &cur_rec, free_fn);
 *
 *    Create an object to hold the current record that matches the
 *    template 'tmpl' having the template ID 'tid' owned by 'session'.
 *
 *    The object is returned to the caller in the memory referenced by
 *    'cur_rec'.  The 'free_fn' is the function to deallocate that
 *    structure.
 *
 *    This function is called by libfixbuf when a new template is
 *    noticed.  The function is registered with fixbuf by
 *    fbSessionAddTemplateCtxCallback2().
 */
static void
fixstream_template_cb(
    fbSession_t            *session,
    uint16_t                etid,
    fbTemplate_t           *tmpl,
    void                   *v_stream,
    void                  **v_ctx,
    fbTemplateCtxFree2_fn  *ctx_free_fn)
{
#ifndef NDEBUG
    uint16_t int_tid;
#endif
    fixstream_tmpl_rec_t *tmpl_ctx;
    sk_fixstream_t *stream;
    sk_schema_t *schema;
    fbTemplate_t *schema_tmpl;
    GError *gerr = NULL;

    /* ignore this template if it is for sending custom IPFIX
     * elements */
    if (fbInfoModelTypeInfoRecord(tmpl)) {
        *v_ctx = NULL;
        *ctx_free_fn = NULL;
        return;
    }

    tmpl_ctx = sk_alloc(fixstream_tmpl_rec_t);

    if (sk_schema_create_from_template(
            &schema, fbSessionGetInfoModel(session), tmpl))
    {
        skAppPrintErr("Cannot create schema from template. Abort");
        skAbort();
    }
    sk_schema_set_tid(schema, etid);

    stream = (sk_fixstream_t*)v_stream;
    assert(session == fBufGetSession(stream->fbuf));

    /* call the new-schema callback function if it is set */
    if (stream->schema_cb_fn) {
        stream->schema_cb_fn(
            schema, etid, (void*)stream->schema_cb_data);
    }

    if (sk_schema_freeze(schema)) {
        sk_schema_destroy(schema);
        skAppPrintErr("Unable to freeze the schema. Abort");
        skAbort();
    }

    sk_schema_get_template(schema, &schema_tmpl, &tmpl_ctx->tid);

    TRACEMSG((("%s:%d: TemplateCallbackHandler creating schema=%p"
               " from template=%p, TID=0x%04x, ctx=%p, schema_tmpl=%p,"
               " schema_tid=0x%04x on '%s'"),
              __FILE__, __LINE__, (void*)schema, (void*)tmpl, etid,
              (void*)tmpl_ctx, (void*)schema_tmpl, tmpl_ctx->tid,
              skStreamGetPathname(stream->stream)));

    /* add internal template */
    if (!fbSessionAddTemplate(session, INT_TMPL, tmpl_ctx->tid,
                              schema_tmpl, &gerr))
    {
        g_clear_error(&gerr);
        sk_schema_destroy(schema);
        skAppPrintErr("Unable to add template to session. Abort");
        skAbort();
    }
    assert((sk_schema_get_template(schema, NULL, &int_tid), int_tid == etid));

    /* tell fixbuf to decode this template when it occurs in a list */
    fbSessionAddTemplatePair(session, etid, etid);

    tmpl_ctx->len = sk_schema_get_record_length(schema);
    sk_fixrec_init(&tmpl_ctx->rec, schema);
    /* the record owns the schema */
    sk_schema_destroy(schema);

    *v_ctx = (void*)tmpl_ctx;
    *ctx_free_fn = fixstream_template_cb_free;
}


/*
 *  fixstream_template_cb_free(cur_rec, stream);
 *
 *    Free the structure that holds the current record.
 *
 *    This function is called by libfixbuf when a template is
 *    destroyed.  This function is set by
 *    fixstream_template_cb(), which is the callback
 *    registered with fixbuf by fbSessionAddTemplateCtxCallback2().
 */
static void
fixstream_template_cb_free(
    void               *v_tmpl_ctx,
    void        UNUSED(*app_ctx))
{
    fixstream_tmpl_rec_t *tmpl_ctx = (fixstream_tmpl_rec_t*)v_tmpl_ctx;

    if (tmpl_ctx) {
        sk_fixrec_destroy(&tmpl_ctx->rec);
        memset(tmpl_ctx, 0, sizeof(*tmpl_ctx));
        free(tmpl_ctx);
        tmpl_ctx = NULL;
    }
}


/*
 *    For interfaces that can only write to a FILE*, this function is
 *    used to read from a pipe(2)---where the other end is the
 *    FILE*---and feed the data to the deflate() method for
 *    compression.
 */
static int
fixstream_write_from_pipe(
    sk_fixstream_t     *stream)
{
    ssize_t rv;
    ssize_t len;

    assert(stream);
    assert(-1 != stream->pipe[0]);

    for (;;) {
        len = read(stream->pipe[0], stream->msgbuf, sizeof(stream->msgbuf));
        if (-1 == len) {
            if (EWOULDBLOCK == errno) {
                return SKSTREAM_OK;
            }
            if (EINTR == errno) {
                continue;
            }
            stream->errnum = errno;
            stream->err_info = SKSTREAM_ERR_READ;
            return -1;
        }
        if (0 == len) {
            return SKSTREAM_OK;
        }
        rv = skStreamWrite(stream->stream, stream->msgbuf, len);
        if (rv != len) {
        }
    }
}


/*
 *    A callback function used by the 'ipfix.ext_tmpl' red-black tree.
 *    That tree is created when writing an IPFIX stream; the tree
 *    manages the external templates.
 *
 *    When inserting an element into the tree, the tree calls this
 *    function to compare the elements it contains.  The elements in
 *    the tree use the tid and tmpl members as their key.
 */
static int
fixstream_writer_schema_cmp(
    const void         *v_fixstream_ext_tmpl_elem_a,
    const void         *v_fixstream_ext_tmpl_elem_b,
    const void  UNUSED(*v_ctx_data))
{
    const fixstream_ext_tmpl_elem_t *a;
    const fixstream_ext_tmpl_elem_t *b;

    a = (const fixstream_ext_tmpl_elem_t*)v_fixstream_ext_tmpl_elem_a;
    b = (const fixstream_ext_tmpl_elem_t*)v_fixstream_ext_tmpl_elem_b;

    if (a->tid < b->tid) {
        return -1;
    }
    if (a->tid > b->tid) {
        return 1;
    }
    return ((a->tmpl < b->tmpl) ? -1 : (a->tmpl > b->tmpl));
}


/*
 *    A callback function used by the 'ipfix.ext_tmpl' red-black
 *    tree.  That tree is created when writing an IPFIX stream.
 *
 *    When the tree is being destroyed, the tree calls this function
 *    on each element in contains to destroy the element's contents.
 */
static void
fixstream_writer_schema_free(
    void               *v_fixstream_ext_tmpl_elem)
{
    fixstream_ext_tmpl_elem_t *e;

    e = (fixstream_ext_tmpl_elem_t*)v_fixstream_ext_tmpl_elem;
    sk_schema_destroy(e->schema);
    free(e);
}


/*
 *  status = fixstream_writer_schema_update(stream, rwrec, ext_schema);
 *
 *    Set the exporting fBuf_t on 'stream' to use the internal and
 *    external template IDs associated with those in 'schema[]'.  If
 *    'depth' is 0, the schemas are used by top-level records; if
 *    'depth' is greater than 0, the schemas refer to schemas used by
 *    lists and a template-pair is created.
 *
 *    This is a helper function for sk_fixstream_write_record() that is
 *    called whenever the schema for the record being written does not
 *    match the previous record's schema.
 *
 *    This function checks to see if the schemas have been seen
 *    before; if so, the template IDs are re-used and the fBuf_t is
 *    updated to use those templates.  If not, a new template ID for the
 *    internal and/or external template is generated, that information
 *    is stored on the stream for later look-up, the template(s)
 *    is(are) added to the session, and fBuf_t is told to use those
 *    template IDs.
 */
static int
fixstream_writer_schema_update(
    sk_fixstream_t     *stream,
    const sk_fixrec_t  *rec,
    const sk_schema_t  *ext_schema)
{
#if TRACEMSG_LEVEL > 0
    const char *ex_in[] = {"external", "internal"};
#endif
    const sk_schema_t *schema[2];
    fbTemplate_t *tmpl[2];
    uint16_t tid[2];
    sk_schema_err_t err;
    int rv;

    assert(stream);
    assert(rec);
    assert(ext_schema);

    schema[EXT_TMPL] = ext_schema;
    schema[INT_TMPL] = sk_fixrec_get_schema(rec);

    /* This is a complete mess..... */

    /* Ensure the schemas (templates) used by any list elements in the
     * record are available in the stream's session */
    rv = fixstream_export_list_schemas(stream, schema[INT_TMPL], rec);
    if (rv) { return rv; }

    if (schema[EXT_TMPL] != stream->prev_schema[EXT_TMPL]) {
        /* Add the external schema to the stream's session */
        err = sk_schema_get_template(schema[EXT_TMPL],
                                     &tmpl[EXT_TMPL], &tid[EXT_TMPL]);
        if (err) {
            skAppPrintErr("Unable to get schema's template: %s",
                          sk_schema_strerror(err));
            return SKSTREAM_ERR_SCHEMA;
        }

        /* add external template to session */
        rv = fixstream_export_schema(stream, schema[EXT_TMPL]);
        if (rv) { return rv; }

        /* set external template */
        TRACEMSG(("%s:%d: Setting %s template to %p 0x%04x on '%s'",
                  __FILE__, __LINE__, ex_in[EXT_TMPL], (void*)tmpl[EXT_TMPL],
                  tid[EXT_TMPL], skStreamGetPathname(stream->stream)));
        if (!fBufSetExportTemplate(
                stream->fbuf, tid[EXT_TMPL], &stream->gerr))
        {
            TRACEMSG(
                ("%s:%d: Unable to set %s template to %p 0x%04x on '%s': %s",
                 __FILE__, __LINE__, ex_in[EXT_TMPL], tmpl[EXT_TMPL],
                 tid[EXT_TMPL], skStreamGetPathname(stream->stream),
                 stream->gerr->message));
            return SKSTREAM_ERR_GERROR;
        }
        stream->prev_schema[EXT_TMPL] = schema[EXT_TMPL];
    }

    if (schema[INT_TMPL] != stream->prev_schema[INT_TMPL]) {
        err = sk_schema_get_template(schema[INT_TMPL],
                                     &tmpl[INT_TMPL], &tid[INT_TMPL]);
        if (err) {
            skAppPrintErr("Unable to get schema's template: %s",
                          sk_schema_strerror(err));
            return SKSTREAM_ERR_SCHEMA;
        }

        /* add internal template to session; FIXME: cache these too? */
        TRACEMSG(("%s:%d: Adding %s template %p 0x%04x to session %p on '%s'",
                  __FILE__, __LINE__, ex_in[INT_TMPL], (void*)tmpl[INT_TMPL],
                  tid[INT_TMPL], (void *)fBufGetSession(stream->fbuf),
                  skStreamGetPathname(stream->stream)));
        if (!fbSessionAddTemplate(fBufGetSession(stream->fbuf), INT_TMPL,
                                  tid[INT_TMPL], tmpl[INT_TMPL],
                                  &stream->gerr))
        {
            TRACEMSG(
                ("%s:%d: Unable to add template %p 0x%04x to session %p: %s",
                 __FILE__, __LINE__, (void*)tmpl[INT_TMPL], tid[INT_TMPL],
                 (void*)fBufGetSession(stream->fbuf), stream->gerr->message));
            return SKSTREAM_ERR_GERROR;
        }

        /* set internal template */
        TRACEMSG(("%s:%d: Setting %s template to %p 0x%04x on '%s'",
                  __FILE__, __LINE__, ex_in[INT_TMPL], (void*)tmpl[INT_TMPL],
                  tid[INT_TMPL], skStreamGetPathname(stream->stream)));
        if (!fBufSetInternalTemplate(
                stream->fbuf, tid[INT_TMPL], &stream->gerr))
        {
            TRACEMSG(
                ("%s:%d: Unable to set %s template to %p 0x%04x on '%s': %s",
                 __FILE__, __LINE__, ex_in[INT_TMPL], tmpl[INT_TMPL],
                 tid[INT_TMPL], skStreamGetPathname(stream->stream),
                 stream->gerr->message));
            return SKSTREAM_ERR_GERROR;
        }
        stream->prev_schema[INT_TMPL] = schema[INT_TMPL];
    }

    /* FIXME: add code to export elements of new external template */
#if SK_FIXSTREAM_EXPORT_ELEMENTS
    /* Add the element type option template */
    type_tmpl = fbInfoElementAllocTypeTemplate(model, &stream->gerr);
    if (!fbSessionAddTemplate(fBufGetSession(stream->fbuf), TRUE, SKI_ELEMENT_TYPE_TID,
                              type_tmpl, stream->gerr))
    {
        TRACEMSG(("%s:%d: fbSessionAddTemplate() error is %s on '%s'",
                  __FILE__, __LINE__, stream->gerr->message,
                  skStreamGetPathname(stream->stream)));
        return SKSTREAM_ERR_GERROR;
    }
    if (!fbSessionAddTemplate(
            fBufGetSession(stream->fbuf), FALSE, SKI_ELEMENT_TYPE_TID,
            type_tmpl, &stream->gerr))
    {
        TRACEMSG(("%s:%d: fbSessionAddTemplate() error is %s on '%s'",
                  __FILE__, __LINE__, stream->gerr->message,
                  skStreamGetPathname(stream->stream)));
        return SKSTREAM_ERR_GERROR;
    }
#endif  /* SK_FIXSTREAM_EXPORT_ELEMENTS */
#if SK_FIXSTREAM_EXPORT_ELEMENTS
    {
        fbInfoElement_t *ie;
        uint32_t i;

        /* Export all elements with private enterprise numbers */
        for (i = 0; i < fbTemplateCountElements(tmpl); ++i) {
            ie = fbTemplateGetIndexedIE(tmpl, i);
            if (ie->ent && !fbInfoElementWriteOptionsRecord(
                    stream->fbuf, ie->ref.canon,
                    SKI_ELEMENT_TYPE_TID, &stream->gerr))
            {
                return SKSTREAM_ERR_GERROR;
            }
        }
    }
#endif  /* SK_FIXSTREAM_EXPORT_ELEMENTS */

    return SKSTREAM_OK;
}


/*
 * *********************************
 * PUBLIC / EXPORTED FUNCTIONS
 * *********************************
 */


int
sk_fixstream_bind(
    sk_fixstream_t     *fixstream,
    const char         *pathname,
    skstream_mode_t     read_write_append)
{
    ssize_t rv;

    STREAM_RETURN_IF_NULL(fixstream);

    if (NULL == pathname) {
        return (fixstream->last_rv = SKSTREAM_ERR_NULL_ARGUMENT);
    }
    if (fixstream->stream) {
        return (fixstream->last_rv = SKSTREAM_ERR_PREV_DATA);
    }

    if ((rv = skStreamCreate(&fixstream->stream, read_write_append,
                             SK_CONTENT_OTHERBINARY))
        || (rv = skStreamBind(fixstream->stream, pathname)))
    {
        skStreamDestroy(&fixstream->stream);
    }

    return (fixstream->last_rv = rv);
}


int
sk_fixstream_close(
    sk_fixstream_t     *fixstream)
{
    ssize_t rv;
    ssize_t rv2;

    STREAM_RETURN_IF_NULL(fixstream);

    if (!fixstream->stream) {
        rv = SKSTREAM_ERR_NOT_OPEN;
    } else {
        rv = SKSTREAM_OK;
        if (fixstream->fbuf) {
            rv = fixstream_fbuf_free(fixstream);
        }
        rv2 = skStreamClose(fixstream->stream);
        if (rv2 && !rv) {
            rv = rv2;
        }
    }

    return (fixstream->last_rv = rv);
}


int
sk_fixstream_create(
    sk_fixstream_t    **stream_ptr)
{
    sk_fixstream_t *stream;

    if (stream_ptr == NULL) {
        return SKSTREAM_ERR_NULL_ARGUMENT;
    }

    stream = *stream_ptr = sk_alloc(sk_fixstream_t);

    sk_fixrec_init(&stream->cur_fixrec, NULL);

    /* Set prev_schema to invalid value */
    stream->prev_schema[EXT_TMPL] = ((sk_schema_t*)-1);
    stream->prev_schema[INT_TMPL] = ((sk_schema_t*)-1);

    return (stream->last_rv = SKSTREAM_OK);
}


void
sk_fixstream_destroy(
    sk_fixstream_t    **stream_ptr)
{
    sk_fixstream_t *stream;

    if ((NULL == stream_ptr) || (NULL == *stream_ptr)) {
        return;
    }
    stream = *stream_ptr;
    *stream_ptr = NULL;

    if (stream->fbuf) {
        sk_fixstream_remove_stream(stream, NULL);
    } else {
        skStreamDestroy(&stream->stream);
    }

    sk_rbtree_destroy(&stream->ext_tmpl);
    stream->ext_tmpl = NULL;

    if (!stream->is_callers_model && stream->info_model) {
        skipfix_information_model_destroy(stream->info_model);
        stream->info_model = NULL;
    }

    g_clear_error(&stream->gerr);
    sk_fixrec_destroy(&stream->cur_fixrec);
    free(stream);
}


int
sk_fixstream_flush(
    sk_fixstream_t     *stream)
{
    ssize_t rv = SKSTREAM_OK;
    ssize_t rv2;

    STREAM_RETURN_IF_NULL(stream);

    if (SK_IO_READ == stream->io_mode) {
        goto END;
    }
    if (!stream->stream) {
        rv = SKSTREAM_ERR_NOT_BOUND;
        goto END;
    }
    if (!stream->fbuf) {
        rv = SKSTREAM_ERR_NOT_OPEN;
        goto END;
    }
    if (!fBufEmit(stream->fbuf, &stream->gerr)) {
        rv = SKSTREAM_ERR_GERROR;
    }
    rv2 = fixstream_write_from_pipe(stream);
    if (rv2 && !rv) {
        rv = rv2;
    }
    rv2 = fflush(stream->fp);
    if (EOF == rv2 && !rv) {
        stream->errnum = errno;
        rv = SKSTREAM_ERR_WRITE;
    }
    rv2 = fixstream_write_from_pipe(stream);
    if (rv2 && !rv) {
        rv = rv2;
    }
    rv2 = skStreamFlush(stream->stream);
    if (rv2 && !rv) {
        rv = rv2;
    }
  END:
    return (stream->last_rv = rv);
}


fbInfoModel_t *
sk_fixstream_get_info_model(
    const sk_fixstream_t   *stream)
{
    if (NULL == stream) {
        return NULL;
    }
    return stream->info_model;
}


sktime_t
sk_fixstream_get_last_export_time(
    const sk_fixstream_t   *stream)
{
    if (stream && stream->fbuf) {
        return sktimeCreate(fBufGetExportTime(stream->fbuf), 0);
    }
    return -1;
}


#if 0
/* FIXME: Add this */
uint32_t
sk_fixstream_get_observation_domain(
    const sk_fixstream_t   *fixstream)
{
    /* Must either call fbCollectorGetObservationDomain() or
     * fbSessionGetObservationDomain() but it is unclear that we have
     * a collector since we "bring our own" and also unclear that the
     * fbSession_t's observation domain is updated as new records are
     * read. */
}
#endif  /* 0 */


uint64_t
sk_fixstream_get_record_count(
    const sk_fixstream_t   *stream)
{
    assert(stream);
    return stream->rec_count;
}


skstream_t *
sk_fixstream_get_stream(
    const sk_fixstream_t   *fixstream)
{
    if (NULL == fixstream) {
        return NULL;
    }
    return fixstream->stream;
}


int
sk_fixstream_open(
    sk_fixstream_t     *stream)
{
    fbSession_t *session = NULL;
    int rv = SKSTREAM_OK;
    int flags;

    STREAM_RETURN_IF_NULL(stream);

    if (!stream->stream) {
        return (stream->last_rv = SKSTREAM_ERR_NOT_BOUND);
    }
    rv = skStreamOpen(stream->stream);
    if (rv != SKSTREAM_OK && rv != SKSTREAM_ERR_PREV_OPEN) {
        return (stream->last_rv = rv);
    }
    if (stream->fbuf) {
        return SKSTREAM_OK;
    }

    if (skStreamGetContentType(stream->stream) != SK_CONTENT_OTHERBINARY) {
        return SKSTREAM_ERR_UNSUPPORT_CONTENT;
    }
    stream->io_mode = skStreamGetMode(stream->stream);

    /* create the info model and the session */
    if (!stream->info_model) {
        stream->info_model = skipfix_information_model_create(0);
    }
    session = fbSessionAlloc(stream->info_model);

    if (SK_IO_READ == stream->io_mode) {
        /* set the new-template callback on the session */
        fbSessionAddTemplateCtxCallback2(
            session, fixstream_template_cb, (void *)stream);

        /* create and initialize the fBuf_t */
        stream->fbuf = fBufAllocForCollection(session, NULL);
        /* turn off automatic mode */
        fBufSetAutomaticMode(stream->fbuf, FALSE);
#if 0
        /* automatically add any alien information elements to the
         * information model */
        if (!fBufSetAutomaticInsert(stream->fbuf, &stream->gerr)) {
            return SKSTREAM_ERR_GERROR;
        }
#endif
        return SKSTREAM_OK;
    }

    /* fixbuf requires a FILE*; create a pipe(2), where one end
     * becomes the FILE* for fixbuf and fixstream_write_from_pipe()
     * reads from the other end and writes to stream; ensure reading
     * side of pipe is nonblocking */

    rv = pipe(stream->pipe);
    if (rv) {
        stream->errnum = errno;
        return SKSTREAM_ERR_SYS_FDOPEN;
    }

    /* the reader */
    flags = fcntl(stream->pipe[0], F_GETFL, 0);
    rv = fcntl(stream->pipe[0], F_SETFL, flags | O_NONBLOCK);

    /* the writer */
    stream->fp = fdopen(stream->pipe[1], "w");
    if (NULL == stream->fp) {
        stream->errnum = errno;
        return SKSTREAM_ERR_SYS_FDOPEN;
    }

    fbSessionSetDomain(session, stream->domain);
    /* create the exporter */
    stream->fbuf = fBufAllocForExport(session, fbExporterAllocFP(stream->fp));
    /* create the schema structure */
    sk_rbtree_create(&stream->ext_tmpl, fixstream_writer_schema_cmp,
                     fixstream_writer_schema_free, NULL);

#if SK_FIXSTREAM_EXPORT_ELEMENTS
    /* Add the element type option template */
    type_tmpl = fbInfoElementAllocTypeTemplate(model, err);
    if (!fbSessionAddTemplate(session, TRUE, SKI_ELEMENT_TYPE_TID,
                              type_tmpl, err))
    {
        goto ERROR;
    }
    if (!fbSessionAddTemplate(session, FALSE, SKI_ELEMENT_TYPE_TID,
                              type_tmpl, err))
    {
        goto ERROR;
    }
#endif  /* SK_FIXSTREAM_EXPORT_ELEMENTS */

#if SK_FIXSTREAM_EXPORT_ELEMENTS
    {
        fbInfoElement_t *ie;
        uint32_t i;

        /* Export all elements with private enterprise numbers */
        for (i = 0; i < fbTemplateCountElements(tmpl); ++i) {
            ie = fbTemplateGetIndexedIE(tmpl, i);
            if (ie->ent && !fbInfoElementWriteOptionsRecord(
                    fbuf, ie->ref.canon, SKI_ELEMENT_TYPE_TID, err))
            {
                goto ERROR;
            }
        }
    }
#endif  /* SK_FIXSTREAM_EXPORT_ELEMENTS */

    return SKSTREAM_OK;

}


int
sk_fixstream_read_record(
    sk_fixstream_t     *stream,
    const sk_fixrec_t **fixrec_out)
{
    uint16_t etid;
    fixstream_tmpl_rec_t *tmpl_ctx;
    fbTemplate_t *tmpl;
    size_t len;
    uint16_t vers;
    ssize_t saw;
    uint8_t *bp;
    int rv = SKSTREAM_OK;

    STREAM_RETURN_IF_NULL(stream);

    if (!stream->fbuf) {
        if (!stream->stream) {
            rv = SKSTREAM_ERR_NOT_BOUND;
        } else {
            rv = SKSTREAM_ERR_NOT_OPEN;
        }
        goto END;
    }

    /* loop until we get a record; if we reach end-of-file, go to the
     * label 'END' */
    for (;;) {
        g_clear_error(&stream->gerr);
        /* get next record's template */
        tmpl = fBufNextCollectionTemplate(stream->fbuf, &etid, &stream->gerr);
        if (tmpl) {
            tmpl_ctx = (fixstream_tmpl_rec_t*)fbTemplateGetContext(tmpl);
            if (NULL == tmpl_ctx) {
                skAppPrintErr(
                    "Template context for template=%p in '%s' is NULL",
                    (void*)tmpl, skStreamGetPathname(stream->stream));
                skAbort();
            }
            TRACEMSG((("%s:%d: Got next collection template %p TID=0x%04x,"
                       " ctx=%p on '%s'"),
                      __FILE__, __LINE__, (void*)tmpl, etid, (void*)tmpl_ctx,
                      skStreamGetPathname(stream->stream)));

            sk_fixrec_clear(&tmpl_ctx->rec);
            if (tmpl_ctx->rec.schema != stream->prev_schema[INT_TMPL]) {
                TRACEMSG((("%s:%d: Changing internal schema from %p"
                           " to %p TID=0x%04x on '%s'"),
                          __FILE__, __LINE__,
                          (void*)stream->prev_schema[INT_TMPL],
                          (void*)tmpl_ctx->rec.schema, tmpl_ctx->tid,
                          skStreamGetPathname(stream->stream)));
                if (!fBufSetInternalTemplate(
                        stream->fbuf, tmpl_ctx->tid, &stream->gerr))
                {
                    TRACEMSG((("%s:%d: fBufSetInternalTemplate() error"
                               " is %s on '%s'"),
                              __FILE__, __LINE__, stream->gerr->message,
                              skStreamGetPathname(stream->stream)));
                    rv = SKSTREAM_ERR_GERROR;
                    goto END;
                }
                stream->prev_schema[INT_TMPL] = tmpl_ctx->rec.schema;
            }

            /* get the record */
            len = tmpl_ctx->len;
            if (fBufNext(
                    stream->fbuf, tmpl_ctx->rec.data, &len, &stream->gerr))
            {
                break;
            }
        }

        /* error from fBufNextCollectionTemplate() or fBufNext() */
        if (!stream->gerr) {
            skAppPrintErr(
                "%s:%d: fBufNext%s() on '%s' gave error and GError is NULL",
                __FILE__, __LINE__, (tmpl ? "" : "CollectionTemplate"),
                skStreamGetPathname(stream->stream));
            skAbort();
        }
        TRACEMSG(("%s:%d: fBufNext%s() error is %s on '%s'",
                  __FILE__, __LINE__, (tmpl ? "" : "CollectionTemplate"),
                  stream->gerr->message, skStreamGetPathname(stream->stream)));
        if (!g_error_matches(stream->gerr, FB_ERROR_DOMAIN, FB_ERROR_EOM)
            && !g_error_matches(stream->gerr, FB_ERROR_DOMAIN, FB_ERROR_BUFSZ))
        {
            rv = SKSTREAM_ERR_GERROR;
            goto END;
        }
        g_clear_error(&stream->gerr);

        /* read bytes from the stream */
        bp = stream->msgbuf;
        saw = skStreamRead(stream->stream, bp, 4);
        if (saw < 4) {
            /* EOF or error */
            stream->is_eof = 1;
            if (0 == saw) {
                rv = SKSTREAM_ERR_EOF;
                goto END;
            }
            if (-1 == saw) {
                rv = SKSTREAM_ERR_READ;
                goto END;
            }
            rv = SKSTREAM_ERR_READ_SHORT;
            goto END;
        }
        vers = ntohs(*((uint16_t*)(bp)));
        len = ntohs(*((uint16_t*)(bp + 2)));
        if (STREAM_MAGIC_NUMBER_IPFIX != vers) {
            /* Not IPFIX version */
            rv = SKSTREAM_ERR_BAD_MAGIC;  /* FIXME */
            goto END;
        }
        if (len <= 4) {
            /* Bad IPFIX length */
            rv = SKSTREAM_ERR_BAD_MAGIC;  /* FIXME */
            goto END;
        }
        bp += 4;
        len -= 4;
        saw = skStreamRead(stream->stream, bp, len);
        if (saw != (ssize_t)len) {
            /* EOF or error */
            stream->is_eof = 1;
            if (-1 == saw) {
                rv = SKSTREAM_ERR_READ;
                goto END;
            }
            rv = SKSTREAM_ERR_READ_SHORT;
            goto END;
        }
        fBufSetBuffer(stream->fbuf, stream->msgbuf, 4+len);
    }

    /* if here, we have a record */
    tmpl_ctx->rec.flags |= SK_FIXREC_FIXBUF_VARDATA;

    /* add any templates used by lists in the record to the session
     * owned by the schema */
    sk_fixrec_copy_list_templates(&tmpl_ctx->rec);

    /* invoke callback functions to handle any plug-in fields */
    sk_fixrec_update_computed(&tmpl_ctx->rec);

    /* got a record */
    ++stream->rec_count;
    *fixrec_out = &tmpl_ctx->rec;

    assert(SKSTREAM_OK == rv);
  END:
    return (stream->last_rv = rv);
}


int
sk_fixstream_remove_stream(
    sk_fixstream_t     *fixstream,
    skstream_t        **stream)
{
    ssize_t rv = SKSTREAM_OK;
    ssize_t rv2;

    STREAM_RETURN_IF_NULL(fixstream);

    if (fixstream->fbuf) {
        rv = fixstream_fbuf_free(fixstream);
    }
    if (stream) {
        *stream = fixstream->stream;
    } else if (fixstream->stream) {
        rv2 = skStreamClose(fixstream->stream);
        if (rv2 && !rv) {
            rv = rv2;
        }
        skStreamDestroy(&fixstream->stream);
    }
    fixstream->stream = NULL;

    return (fixstream->last_rv = rv);
}


int
sk_fixstream_set_info_model(
    sk_fixstream_t     *stream,
    fbInfoModel_t      *info_model)
{
    STREAM_RETURN_IF_NULL(stream);

    if (NULL == info_model) {
        return (stream->last_rv = SKSTREAM_ERR_NULL_ARGUMENT);
    }
    if (stream->fbuf) {
        return (stream->last_rv = SKSTREAM_ERR_PREV_DATA);
    }
    if (!stream->is_callers_model && stream->info_model) {
        skipfix_information_model_destroy(stream->info_model);
    }
    stream->info_model = info_model;
    stream->is_callers_model = 1;

    return (stream->last_rv = SKSTREAM_OK);
}


int
sk_fixstream_set_observation_domain(
    sk_fixstream_t     *stream,
    uint32_t            domain)
{
    int rv = SKSTREAM_OK;

    STREAM_RETURN_IF_NULL(stream);

    if (SK_IO_READ == stream->io_mode) {
        goto END;
    }

    stream->domain = domain;
    if (stream->fbuf) {
        rv = sk_fixstream_flush(stream);
        fbSessionSetDomain(fBufGetSession(stream->fbuf), domain);
    }

  END:
    return (stream->last_rv = rv);
}


int
sk_fixstream_set_schema_cb(
    sk_fixstream_t                 *stream,
    sk_fixstream_schema_cb_fn_t     new_schema_cb,
    const void                     *callback_data)
{
    STREAM_RETURN_IF_NULL(stream);

    stream->schema_cb_fn = new_schema_cb;
    stream->schema_cb_data = callback_data;

    return (stream->last_rv = SKSTREAM_OK);
}


int
sk_fixstream_set_stream(
    sk_fixstream_t     *fixstream,
    skstream_t         *stream)
{
    STREAM_RETURN_IF_NULL(stream);

    if (fixstream->stream) {
        return (fixstream->last_rv = SKSTREAM_ERR_PREV_DATA);
    }
    fixstream->stream = stream;

    return (fixstream->last_rv = SKSTREAM_OK);
}


const char *
sk_fixstream_strerror(
    const sk_fixstream_t   *stream)
{
    if (NULL == stream) {
        return "Unknown error";
    }
    skStreamLastErrMessage(stream->stream, stream->last_rv,
                           (char *)stream->errbuf, sizeof(stream->errbuf));
    return stream->errbuf;
}


int
sk_fixstream_write_record(
    sk_fixstream_t     *stream,
    const sk_fixrec_t  *fixrec,
    const sk_schema_t  *schema)
{
    int rv;

    STREAM_RETURN_IF_NULL(stream);

    if (!stream->fbuf) {
        if (!stream->stream) {
            rv = SKSTREAM_ERR_NOT_BOUND;
        } else {
            rv = SKSTREAM_ERR_NOT_OPEN;
        }
        goto END;
    }
    if (SK_IO_READ == stream->io_mode) {
        rv = SKSTREAM_ERR_UNSUPPORT_IOMODE;
        goto END;
    }

    g_clear_error(&stream->gerr);

    assert(fixrec->schema);

    rv = fixstream_writer_schema_update(stream, fixrec,
                                        (schema ? schema : fixrec->schema));
    if (rv) { goto END; }

    if (!fBufAppend(stream->fbuf, fixrec->data,
                    sk_schema_get_record_length(fixrec->schema),
                    &stream->gerr))
    {
        rv = SKSTREAM_ERR_GERROR;
        goto END;
    }
    rv = fixstream_write_from_pipe(stream);
    if (rv) { goto END; }

    ++stream->rec_count;

    assert(SKSTREAM_OK == rv);
  END:
    return (stream->last_rv = rv);
}


/*
** Local Variables:
** mode:c
** indent-tabs-mode:nil
** c-basic-offset:4
** End:
*/
