/*
** Copyright (C) 2014-2017 by Carnegie Mellon University.
**
** @OPENSOURCE_LICENSE_START@
** See license information in ../../LICENSE.txt
** @OPENSOURCE_LICENSE_END@
*/

/*
 *  skcmdline.c
 *
 *    Structures and functions for handling the command line.
 *
 *    Mark Thomas
 *    November 2014
 *
 */

#include <silk/silk.h>

RCSIDENT("$SiLK: skcmdline.c efd886457770 2017-06-21 18:43:23Z mthomas $");

#include <silk/skcmdline.h>
#include <silk/skredblack.h>
#include <silk/skfixstream.h>
#include <silk/skschema.h>
#include <silk/skstream.h>
#include <silk/skvector.h>


/* LOCAL DEFINES AND TYPEDEFS */

/*
 *    skcli_input_fields_t is the caller's handle to the structure
 *    that is manipulated by the functions in this file.
 */
struct skcli_input_fields_st {
    /* a red-black tree of sk_field_t pointers */
    sk_rbtree_t                        *rb_fields;
    /* a vector of sk_schema_t pointers found in the input streams */
    sk_vector_t                        *v_schemas;
    /* the number of input streams processed */
    size_t                              stream_count;
    /* the caller's callback function to invoke on each new schema */
    skcli_input_fields_schema_cb_fn_t   cb_schema;
    /* a data parameter for the 'cb_schema' function */
    void                               *cb_data;
};
/* skcli_input_fields_t */


/*
 *    skcli_input_fields_iter_t is the structure to iterate over the
 *    sk_field_t objects in the red-black tree of the
 *    skcli_input_fields_t.
 */
struct skcli_input_fields_iter_st {
    const skcli_input_fields_t *input_fields;
    sk_rbtree_iter_t           *rb_fields_iter;
    const sk_field_t           *next_field;
};
/* skcli_input_fields_iter_t */


/* FUNCTION DEFINITIONS */

/*
 *    Comparison function for the sk_field_t object in the red-black
 *    treee.
 */
static int
input_fields_rbtree_cmp(
    const void         *v_a,
    const void         *v_b,
    const void  UNUSED(*v))
{
    sk_field_ident_t id_a = sk_field_get_ident((const sk_field_t*)v_a);
    sk_field_ident_t id_b = sk_field_get_ident((const sk_field_t*)v_b);

    return ((id_a < id_b) ? -1 : (id_a > id_b));
}

/*
 *    Callback invoked for each schema seen during processing of the
 *    streams by skcli_input_fields_populate().
 */
static void
input_fields_schema_callback(
    sk_schema_t        *schema,
    uint16_t            tid,
    void               *v_input_fields)
{
    skcli_input_fields_t *input_fields = (skcli_input_fields_t*)v_input_fields;

    if (input_fields->cb_schema) {
        input_fields->cb_schema(schema, tid, input_fields->cb_data);
    }
    sk_schema_clone(schema);
    sk_vector_append_value(input_fields->v_schemas, &schema);
}

size_t
skcli_input_fields_count_fields(
    skcli_input_fields_t   *input_fields)
{
    assert(input_fields);
    return sk_rbtree_size(input_fields->rb_fields);
}

size_t
skcli_input_fields_count_streams(
    skcli_input_fields_t   *input_fields)
{
    assert(input_fields);
    return input_fields->stream_count;
}

size_t
skcli_input_fields_count_templates(
    skcli_input_fields_t   *input_fields)
{
    assert(input_fields);
    return sk_vector_get_count(input_fields->v_schemas);
}

int
skcli_input_fields_create(
    skcli_input_fields_t  **input_fields)
{
    skcli_input_fields_t *in_fields;

    assert(input_fields);

    in_fields = sk_alloc(skcli_input_fields_t);
    sk_rbtree_create(
        &in_fields->rb_fields, &input_fields_rbtree_cmp, NULL, NULL);
    in_fields->v_schemas = sk_vector_create(sizeof(sk_schema_t*));

    *input_fields = in_fields;
    return 0;
}

void
skcli_input_fields_destroy(
    skcli_input_fields_t   *input_fields)
{
    sk_schema_t *schema;
    size_t i;

    if (input_fields) {
        sk_rbtree_destroy(&input_fields->rb_fields);
        for (i = 0; i < sk_vector_get_count(input_fields->v_schemas); ++i) {
            sk_vector_get_value(input_fields->v_schemas, i, &schema);
            sk_schema_destroy(schema);
        }
        sk_vector_destroy(input_fields->v_schemas);
        free(input_fields);
    }
}

int
skcli_input_fields_populate(
    skcli_input_fields_t   *input_fields,
    sk_options_ctx_t       *options_ctx,
    fbInfoModel_t          *info_model)
{
    const sk_schema_t *schema;
    const sk_field_t *field;
    sk_fixstream_t *fixstream;
    const sk_fixrec_t *rec;
    char path[PATH_MAX];
    size_t i, j;
    ssize_t rv;

    assert(input_fields);
    assert(options_ctx);

    /* Process all input streams; the template callback fills the
     * vector 'input_fields->v_schemas' with the schemas. */
    input_fields->stream_count = 0;
    while ((rv = skOptionsCtxNextArgument(options_ctx, path, sizeof(path)))==0)
    {
        if ((rv = sk_fixstream_create(&fixstream))
            || (rv = sk_fixstream_bind(fixstream, path, SK_IO_READ))
            || (rv = sk_fixstream_set_info_model(fixstream, info_model))
            || (rv = sk_fixstream_set_schema_cb(
                    fixstream, &input_fields_schema_callback,
                    (void*)input_fields))
            || (rv = sk_fixstream_open(fixstream)))
        {
            skAppPrintErr("%s", sk_fixstream_strerror(fixstream));
            sk_fixstream_destroy(&fixstream);
            continue;
        }
        ++input_fields->stream_count;
        while ((rv = sk_fixstream_read_record(fixstream, &rec)) == SKSTREAM_OK)
            ;                   /* empty */
        if (SKSTREAM_ERR_EOF != rv) {
            skAppPrintErr("%s", sk_fixstream_strerror(fixstream));
        }
        sk_fixstream_destroy(&fixstream);
    }

    /* For each schema, add its fields to the redblack tree */
    for (i = 0; i < sk_vector_get_count(input_fields->v_schemas); ++i) {
        sk_vector_get_value(input_fields->v_schemas, i, &schema);
        for (j = 0; j < sk_schema_get_count(schema); ++j) {
            field = sk_schema_get_field(schema, j);
            /* skip the padding element and list elements */
            switch (sk_field_get_ident(field)) {
              case SK_FIELD_IDENT_CREATE(0, 210):
              case SK_FIELD_IDENT_CREATE(0, FB_IE_BASIC_LIST):
              case SK_FIELD_IDENT_CREATE(0, FB_IE_SUBTEMPLATE_LIST):
              case SK_FIELD_IDENT_CREATE(0, FB_IE_SUBTEMPLATE_MULTILIST):
                break;
              default:
                sk_rbtree_insert(input_fields->rb_fields, field, NULL);
                break;
            }
        }
    }

    return 0;
}

int
skcli_input_fields_iter_create(
    skcli_input_fields_iter_t **out_iter,
    const skcli_input_fields_t *input_fields)
{
    skcli_input_fields_iter_t *iter;

    assert(input_fields);
    assert(out_iter);

    iter = sk_alloc(skcli_input_fields_iter_t);
    iter->input_fields = input_fields;
    iter->rb_fields_iter = sk_rbtree_iter_create();
    iter->next_field = (const sk_field_t*)sk_rbtree_iter_bind_first(
        iter->rb_fields_iter, input_fields->rb_fields);
    *out_iter = iter;
    return 0;
}

void
skcli_input_fields_iter_destroy(
    skcli_input_fields_iter_t  *iter)
{
    if (iter) {
        sk_rbtree_iter_free(iter->rb_fields_iter);
        free(iter);
    }
}

const sk_field_t *
skcli_input_fields_iter_next(
    skcli_input_fields_iter_t  *iter)
{
    const sk_field_t *field;

    assert(iter);
    field = iter->next_field;
    iter->next_field = ((const sk_field_t*)
                        sk_rbtree_iter_next(iter->rb_fields_iter));
    return field;
}

int
skcli_input_fields_set_schema_callback(
    skcli_input_fields_t               *input_fields,
    skcli_input_fields_schema_cb_fn_t   cb_func,
    void                               *cb_data)
{
    assert(input_fields);

    input_fields->cb_schema = cb_func;
    input_fields->cb_data = cb_data;

    return 0;
}


/*
** Local Variables:
** mode:c
** indent-tabs-mode:nil
** c-basic-offset:4
** End:
*/
