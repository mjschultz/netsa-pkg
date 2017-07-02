/*
** Copyright (C) 2014-2017 by Carnegie Mellon University.
**
** @OPENSOURCE_LICENSE_START@
** See license information in ../../LICENSE.txt
** @OPENSOURCE_LICENSE_END@
*/

/*
 *  SiLK plug-in implementation for IPFIX fields.
 *
 */

#include <silk/silk.h>

RCSIDENT("$SiLK: skfixplugin.c efd886457770 2017-06-21 18:43:23Z mthomas $");

#include <silk/skcountry.h>
#include <silk/skdllist.h>
#include <silk/sklua.h>
#include <silk/skipaddr.h>
#include <silk/skplugin.h>
#include <silk/skschema.h>


/* LOCAL DEFINES AND TYPEDEFS */

/* print a message and exit the application if memory for the object
 * 'x' is NULL */
#define CHECK_MEM(x)                                            \
    if (x) { /* no-op */ } else {                               \
        skAppPrintErr(("skplugin: unable to allocate memory"    \
                       " for object %s at %s:%d"),              \
                      #x, __FILE__, __LINE__);                  \
        abort();                                                \
    }


struct skp_schema_field_st {
    skplugin_schema_callback_fn_t       init;
    skplugin_schema_callback_fn_t       cleanup;
    sk_field_computed_description_t     desc;
    const char                         *name;
    void                               *cbdata;
    unsigned                            init_called :1;
};
typedef struct skp_schema_field_st skp_schema_field_t;


/* LOCAL VARIABLE DEFINITIONS */

/* The list of schema fields */
static sk_dllist_t *skp_schema_field_list = NULL;

static int skp_initialized = 0;

static int skp_debug = 0;

static int skp_in_plugin_init = 0;


/* FUNCTION DEFINITIONS */

/* Destroy an skp_schema_field_t object */
static void
skp_schema_field_destroy(
    void               *vschema_field)
{
    skp_schema_field_t *field = (skp_schema_field_t*)vschema_field;
    int i;

    for (i = 0; i < field->desc.field_names_len; ++i) {
        free((void*)field->desc.field_names[i]);
    }
    free((void*)field->desc.field_names);
    free((void*)field->name);
    free(field);
}



void
skPluginSchemaFieldSetup(
    void)
{
    const char *env_value;

    skp_initialized = 1;

    /* Check for debugging */
    env_value = getenv(SKPLUGIN_DEBUG_ENVAR);
    if ((env_value != NULL) && (env_value[0] != '\0')) {
        skp_debug = 1;
    }

    skp_schema_field_list = skDLListCreate(skp_schema_field_destroy);
    CHECK_MEM(skp_schema_field_list);
}


void
skPluginSchemaFieldTeardown(
    void)
{
    skDLListDestroy(skp_schema_field_list);
    skp_initialized = 0;
}


int
skpinRegSchemaField(
    const char                         *name,
    const skplugin_schema_callbacks_t  *regdata,
    void                               *cbdata)
{
    skp_schema_field_t *field;
    size_t field_names_len;
    size_t i;

    assert(skp_initialized);
    assert(skp_in_plugin_init);
    assert(skp_schema_field_list);

    if (!name) {
        if (skp_debug) {
            skAppPrintErr(SKPLUGIN_DEBUG_ENVAR
                          ": ignoring schema field due to NULL name");
        }
        return (int)SKPLUGIN_ERR;
    }
    if (!regdata) {
        if (skp_debug) {
            skAppPrintErr(
                (SKPLUGIN_DEBUG_ENVAR
                 ": ignoring schema field '%s' due to NULL regdata"),
                name);
        }
        return (int)SKPLUGIN_ERR;
    }
    if (NULL == regdata->desc.update) {
        if (skp_debug) {
            skAppPrintErr((SKPLUGIN_DEBUG_ENVAR
                           ": ignoring schema field due to"
                           " NULL update() callback"));
        }
        return (int)SKPLUGIN_ERR;
    }

    field = sk_alloc(skp_schema_field_t);
    field->name = sk_alloc_strdup(name);
    field->cbdata = cbdata;
    field->init = regdata->init;
    field->cleanup = regdata->cleanup;
    /* shallow copy the description; then "deep copy" the strings */
    field->desc = regdata->desc;
    if (0 == regdata->desc.field_names_len
        || NULL == regdata->desc.field_names)
    {
        field->desc.field_names_len = 0;
        field->desc.field_names = NULL;
    } else {
        /* determine the number of strings in field_names[] */
        if (regdata->desc.field_names_len < 0) {
            field_names_len = SIZE_MAX;
        } else {
            field_names_len = regdata->desc.field_names_len;
        }
        if (field_names_len) {
            for (i = 0;
                 i < field_names_len && NULL != regdata->desc.field_names[i];
                 ++i)
                ; /* empty */
            field_names_len = i;
        }

        /* copy the names */
        if (field_names_len) {
            field->desc.field_names_len = field_names_len;
            field->desc.field_names
                = (const char**)sk_alloc_array(char *, field_names_len);
            for (i = 0; i < field_names_len; ++i) {
                field->desc.field_names[i]
                    = sk_alloc_strdup(regdata->desc.field_names[i]);
            }
        }
    }

    CHECK_MEM(0 == skDLListPushTail(skp_schema_field_list, field));

    return (int)SKPLUGIN_OK;
}



/*
 *    Fill 'field' with the plugin schema field associated with
 *    'name'.  Return SKPLUGIN_ERR if there is no such field.
 *
 *    In addition, run the initialization function for the field if it
 *    has not yet been run and return the status of that function.
 */
static int
skPluginSchemaFieldFind(
    skp_schema_field_t    **field,
    const char             *name)
{
    sk_dll_iter_t iter;
    int err = (int)SKPLUGIN_OK;
    int rv;

    assert(field);
    assert(name);
    assert(skp_initialized);
    /* assert(skp_handle_type(SKPLUGIN_FN_SCHEMA_FIELD)); */

    /* find the field by name in the list of fields */
    skDLLAssignIter(&iter, skp_schema_field_list);
    while ((rv = skDLLIterForward(&iter, (void **)field)) == 0) {
        if (strcmp(name, (*field)->name) == 0) {
            break;
        }
    }
    if (rv != 0) {
        return (int)SKPLUGIN_ERR;
    }

    if (!(*field)->init_called) {
        (*field)->init_called = 1;
        if ((*field)->init) {
            skp_in_plugin_init = 1;
            err = (*field)->init((*field)->cbdata);
            skp_in_plugin_init = 0;
            if (err == (int)SKPLUGIN_ERR_FATAL) {
                skAppPrintErr("Fatal error in initializing schema field code");
                exit(EXIT_FAILURE);
            }
            if (err == (int)SKPLUGIN_FILTER_IGNORE) {
                /* FIXME */
                return err;
            }
            return err;
        }
    }

    return (int)SKPLUGIN_OK;
}


/* Find the callback associated with 'name' and invoke it to create
 * the field. */
int
skPluginSchemaFieldAdd(
    const sk_field_t  **field,
    const char         *name,
    sk_schema_t        *schema,
    const sk_field_t   *before)
{
    sk_field_computed_description_t *desc;
    skp_schema_field_t *schema_field;
    const sk_field_t *field_local;
    int err;

    /* FIXME!! ADD SUPPORT FOR init() and cleanup() */

    if (NULL == field) {
        field = &field_local;
    }

    assert(schema);
    assert(skp_initialized);
    assert(!skp_in_plugin_init);
    /* assert(skp_handle_type(SKPLUGIN_FN_SCHEMA_FIELD)); */

    err = skPluginSchemaFieldFind(&schema_field, name);
    if (err) {
        return err;
    }

    desc = &schema_field->desc;

    /* Add the new field to the schema */
    if (sk_schema_insert_computed_field(field, schema, desc, before)) {
        return (int)SKPLUGIN_ERR;
    }

    /* Save the ident in the description so future IE creation in
     * other InforModels will use the same ident */
    if (0 == desc->ident) {
        assert(*field != NULL);
        desc->ident = sk_field_get_ident(*field);
    }
    return (int)SKPLUGIN_OK;
}


/* Find the callback associated with 'name' and invoke it to get the
 * IE. */
int
skPluginSchemaFieldGetIE(
    const fbInfoElement_t **ie,
    fbInfoModel_t          *model,
    const char             *name)
{
    sk_field_computed_description_t *desc;
    skp_schema_field_t *schema_field;
    int err = (int)SKPLUGIN_OK;
    const fbInfoElement_t *ie_local;

    /* FIXME!! ADD SUPPORT FOR init() and cleanup() */

    if (NULL == ie) {
        ie = &ie_local;
    }

    assert(model);
    assert(name);
    assert(skp_initialized);
    assert(!skp_in_plugin_init);
    /* assert(skp_handle_type(SKPLUGIN_FN_SCHEMA_FIELD)); */

    err = skPluginSchemaFieldFind(&schema_field, name);
    if (err) {
        return err;
    }

    desc = &schema_field->desc;

    /* Return the IE associated with this field */
    *ie = sk_schema_get_ie_from_computed_description(desc, model);
    if (NULL == *ie) {
        return (int)SKPLUGIN_ERR;
    }

    /* Save the ident in the description so future IE creation in
     * other InfoModels will use the same ident */
    if (0 == desc->ident) {
        desc->ident = SK_FIELD_IDENT_CREATE((*ie)->ent, (*ie)->num);
    }
    return (int)SKPLUGIN_OK;
}


/* Binds an iterator around all schema fields */
int
skPluginSchemaFieldIteratorBind(
    skplugin_schema_field_iter_t  *iter)
{
    assert(skp_initialized);
    assert(!skp_in_plugin_init);
    assert(iter);

    skDLLAssignIter(iter, skp_schema_field_list);
    return (int)SKPLUGIN_OK;
}


/* Retrieves the name for the next schema field, returns 1 on success,
 * 0 on failure. */
int
skPluginSchemaFieldIteratorNext(
    skplugin_schema_field_iter_t  *iter,
    const char                   **name)
{
    skp_schema_field_t *field;

    assert(skp_initialized);
    assert(!skp_in_plugin_init);
    assert(iter);

    if (skDLLIterForward(iter, (void **)&field) != 0) {
        return 0;
    }
    *name = field->name;
    return 1;
}


int
skPluginSchemaAddAsPlugin(
    const char             *name,
    int                   (*setup_fn)(void))
{
    int err;

    assert(setup_fn);

    (void)name;
    skp_in_plugin_init = 1;
    err = setup_fn();
    skp_in_plugin_init = 0;
    return err;
}


/*  ********************************************************************  */
/*  COUNTRY CODE  */
/*  ********************************************************************  */

/* support for IPFIX fields */
#define CC_SCHEMA_FIELD_SRC  "sourceCountryCode"
#define CC_SCHEMA_FIELD_DST  "destinationCountryCode"

/* number of fields in the cc_schema_fields[i].field_names[] array. */
#define FIELD_NAMES_LEN 2

/*static sk_field_computed_description_t cc_schema_fields[FIELD_NAMES_LEN]; */
static const char *required_fields[][FIELD_NAMES_LEN] = {
    {"sourceIPv4Address", "sourceIPv6Address"},
    {"destinationIPv4Address", "destinationIPv6Address"}
};


/**
 *    Callback function invoked by sk_fixrec_update_computed().
 *
 *    This function must have the signature defind by
 *    sk_field_computed_update_fn_t.
 *
 *    Compute and fill in the country code 'dest' on record 'rec',
 *    based on the address fields in 'data'.
 *
 *    This is a callback function invoked to update the value of the
 *    country code.
 */
static sk_schema_err_t
compute_cc(
    sk_fixrec_t                    *rec,
    const sk_field_computed_data_t *data)
{
    skipaddr_t addr;
    char code[3];

    /* data->fields[0] is ipv4 address */
    /* data->fields[1] is ipv6 address */

    if (data->fields[1]) {
        sk_fixrec_get_ip_address(rec, data->fields[1], &addr);
        if (SK_IPV6_IS_ZERO(&addr) && data->fields[0]) {
            /* use the IPv4 address if IPv6 address is zero */
            sk_fixrec_get_ip_address(rec, data->fields[0], &addr);
        }
    } else if (data->fields[0]) {
        sk_fixrec_get_ip_address(rec, data->fields[0], &addr);
    } else {
        /* No IP, set country code to empty string */
        sk_fixrec_set_string(rec, data->dest, "");
        return 0;
    }
    skCountryLookupName(&addr, code, sizeof(code));
    sk_fixrec_set_string(rec, data->dest, code);
    return 0;
}


#if 0
/**
 *    Callback invoked by skPluginSchemaFieldGetIE().
 *
 *    This function must have the signature defind by
 *    skplugin_get_model_ie_fn_t.
 *
 *    Add the computed field described by 'v_desc' to the information
 *    model 'model'.
 */
static int
cc_get_model_ie(
    const fbInfoElement_t **ie,
    fbInfoModel_t          *model,
    void                   *v_desc)
{
    sk_field_computed_description_t *desc;

    desc = (sk_field_computed_description_t*)v_desc;
    assert(desc == &cc_schema_fields[0] || desc == &cc_schema_fields[1]);

    /* Country code initialization */
    if (ccInit(NULL)) {
        return (int)SKPLUGIN_ERR;
    }

    /* Return the IE associated with this field */
    *ie = sk_schema_get_ie_from_computed_description(desc, model);
    if (*ie == NULL) {
        return (int)SKPLUGIN_ERR;
    }
    /* Save the ident in the description so future IE creation in
     * other InfoModels will use the same ident */
    desc->ident = SK_FIELD_IDENT_CREATE((*ie)->ent, (*ie)->num);
    return (int)SKPLUGIN_OK;
}


/**
 *    Callback invoked by skPluginSchemaFieldAdd().
 *
 *    This function must have the signature defined by
 *    skplugin_add_to_schema_fn_t.
 *
 *    Add the computed field described by 'v_desc' to the schema
 *    'schema'.
 */
static int
cc_add_to_schema(
    const sk_field_t      **field,
    sk_schema_t            *schema,
    const sk_field_t       *before,
    void                   *v_desc)
{
    sk_field_computed_description_t *desc;
    const sk_field_t *f;

    desc = (sk_field_computed_description_t*)v_desc;
    assert(desc == &cc_schema_fields[0] || desc == &cc_schema_fields[1]);

    /* Country code initialization */
    if (ccInit(NULL)) {
        return (int)SKPLUGIN_ERR;
    }

    if (field == NULL) {
        field = &f;
    }

    /* Add the new field to the schema */
    if (sk_schema_insert_computed_field(
            field, schema, desc, before))
    {
        return (int)SKPLUGIN_ERR;
    }

    /* Save the ident in the description so future IE creation in
     * other InforModels will use the same ident */
    if (0 == desc->ident) {
        assert(*field != NULL);
        desc->ident = sk_field_get_ident(*field);
    }
    return (int)SKPLUGIN_OK;
}
#endif  /* 0 */


/*
 *  status = ccInit(data);
 *
 *    The initialization code for this plugin.  This is called by the
 *    plugin initialization code after option parsing and before data
 *    processing.
 */
static int
ccInit(
    void        UNUSED(*x))
{
    /* Read in the data file */
    if (skCountrySetup(NULL, &skAppPrintErr)) {
        return (int)SKPLUGIN_ERR;
    }

    return (int)SKPLUGIN_OK;
}


/*
 *   status = ccCleanup(data);
 *
 *     Called by plugin interface code to tear down this plugin.
 */
static int
ccCleanup(
    void        UNUSED(*x))
{
    skCountryTeardown();
    return (int)SKPLUGIN_OK;
}


int
skCountryAddSchemaFields(
    void)
{
    int rv = (int)SKPLUGIN_OK;
    skplugin_schema_callbacks_t schema_regdata;
    char namebuf[1024];

    memset(&schema_regdata, 0, sizeof(schema_regdata));
    schema_regdata.init          = ccInit;
    schema_regdata.cleanup       = ccCleanup;
    schema_regdata.desc.lookup   = SK_FIELD_COMPUTED_CREATE;
    schema_regdata.desc.datatype = FB_STRING;
    schema_regdata.desc.len      = 2;
    schema_regdata.desc.update   = compute_cc;
    schema_regdata.desc.field_names_len = FIELD_NAMES_LEN;

    /* add source */
    schema_regdata.desc.field_names = required_fields[0];
    schema_regdata.desc.name = CC_SCHEMA_FIELD_SRC;
    snprintf(namebuf, sizeof(namebuf), "plugin.%s",
             schema_regdata.desc.name);
    rv = skpinRegSchemaField(namebuf, &schema_regdata, NULL);
    if ((int)SKPLUGIN_OK != rv) {
        goto END;
    }

    /* add destination */
    schema_regdata.desc.field_names = required_fields[1];
    schema_regdata.desc.name = CC_SCHEMA_FIELD_DST;
    snprintf(namebuf, sizeof(namebuf), "plugin.%s",
             schema_regdata.desc.name);
    rv = skpinRegSchemaField(namebuf, &schema_regdata, NULL);
    if ((int)SKPLUGIN_OK != rv) {
        goto END;
    }

  END:
    return rv;
}
