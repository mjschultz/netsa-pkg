/*
** Copyright (C) 2014-2017 by Carnegie Mellon University.
**
** @OPENSOURCE_LICENSE_START@
** See license information in ../../LICENSE.txt
** @OPENSOURCE_LICENSE_END@
*/

/*
**  Schema binding for lua
**
*/

#include <silk/silk.h>

RCSIDENT("$SiLK: sklua-schema.c efd886457770 2017-06-21 18:43:23Z mthomas $");

#include <silk/utils.h>
#include <silk/skfixstream.h>
#include <silk/skipfixcert.h>
#include <silk/sklua.h>
#include <silk/skplugin.h>
#include <silk/skschema.h>
#include <silk/skstream.h>
#include <silk/skvector.h>


/* LOCAL DEFINES AND TYPEDEFS */

#define SILK_FLOW_TYPE   SK_FIELD_IDENT_CREATE(IPFIX_CERT_PEN, 30)
#define SILK_FLOW_SENSOR SK_FIELD_IDENT_CREATE(IPFIX_CERT_PEN, 31)

/* Type string for stream userdata */
#define SK_LUA_STREAM       "silk.stream"

/* Type string for the schema userdata */
#define SK_LUA_SCHEMA        "silk.schema"

/* Type string for the field userdata */
#define SK_LUA_FIELD         "silk.field"

/* Type string for the fixlist userdata */
#define SK_LUA_FIXLIST       "silk.fixlist"

/* Registry name for the information model pointer. */
#define SK_LUA_INFOMODEL     "sk_lua_infomodel"

/*
 *    Registry name for the dependency table.
 *
 *    The dependency table is a table with weak keys and normal
 *    values.  If a userdata object, A, depends on the existence of
 *    another userdata object, B, (because the underlying C datatype
 *    of the latter owns the memory of the former), the dependent, A,
 *    is added as a key, with the object it depends on, B, as the
 *    value.  This table ensures that the dependency, B, is not
 *    garbage collected until the dependent, A, is.
 */
#define SK_LUA_DEPENDENCIES  "sk_lua_dependencies"

/*
 *    Registry name for the schema look-up table.
 *
 *    This table maps sk_schema_t pointers to schema userdata objects.
 *    The value is weak in this table, allowing entries to be garbage
 *    collected if the schema is no longer referenced anywhere else.
 */
#define SK_LUA_SCHEMA_LOOKUP "sk_lua_schema_lookup"

/*
 *    Registry name for the schemamap (schema copy-plan) cache.
 *
 *    The cache maps two keys to a single value, and it does this by
 *    being a table of tables.  The outer table table has weak keys,
 *    while the inner table has weak keys and values.
 *
 *    The key in this outer table (the cache) is the schema userdata
 *    of the source schema.  This key's value is a table where the key
 *    is the destination schema userdata and the value is the
 *    schemamap userdata object.
 */
#define SK_LUA_SCHEMAMAP_CACHE "sk_lua_schemamap_cache"

/*
 *    These IDs specify indexes into the callback table in the Lua
 *    registry for each plug-in field.  The TABLE_SIZE value must be
 *    the maximum of all these values, and it is used for setting the
 *    size of the Lua table.
 */
#define SKLUAPIN_CBDATA_UPDATE          1
#define SKLUAPIN_CBDATA_FIELDS          2
#define SKLUAPIN_CBDATA_INITIALIZE      3
#define SKLUAPIN_CBDATA_CLEANUP         4
#define SKLUAPIN_CBDATA_TABLE_SIZE      4

/**
 *    Check whether the function argument 'arg' is an sk_lua_stream_t
 *    userdata and return the argument cast to that type.  Raise an
 *    error if not.
 */
#define sk_lua_checkstream(check_L, check_arg)                          \
    SKLUA_CHECK_TYPE(check_L, check_arg, SK_LUA_STREAM, sk_lua_stream_t *)

/**
 *    Check whether the function argument 'arg' is an sk_field_t
 *    userdata and return the argument cast to that type.  Raise an
 *    error if not.
 */
#define sk_lua_checkfield(check_L, check_arg)                           \
    SKLUA_CHECK_TYPE(check_L, check_arg, SK_LUA_FIELD, sk_field_t **)

/**
 *    Check whether the function argument 'arg' is an sk_field_t
 *    userdata and return the argument cast to that type.  Return NULL
 *    if not.
 */
#define sk_lua_tofield(to_L, to_arg)                                    \
    SKLUA_TEST_TYPE(to_L, to_arg, SK_LUA_FIELD, sk_field_t **)

/**
 *    Check whether the function argument 'arg' is an sk_schema_t
 *    userdata and return the argument cast to that type.  Raise an
 *    error if not.
 */
#define sk_lua_checkschema(check_L, check_arg)                          \
    SKLUA_CHECK_TYPE(check_L, check_arg, SK_LUA_SCHEMA, sk_schema_t **)

/**
 *    Check whether the function argument 'arg' is an sk_schema_t
 *    userdata and return the argument cast to that type.  Return NULL
 *    if not.
 */
#define sk_lua_toschema(to_L, to_arg)                                   \
    SKLUA_TEST_TYPE(to_L, to_arg, SK_LUA_SCHEMA, sk_schema_t **)

/**
 *    Check whether the function argument 'arg' is an sk_fixlist_t
 *    userdata and return the argument cast to that type.  Raise an
 *    error if not.
 */
#define sk_lua_checkfixlist(check_L, check_arg)                         \
    SKLUA_CHECK_TYPE(check_L, check_arg, SK_LUA_FIXLIST, sk_fixlist_t **)

/**
 *    Check whether the function argument 'arg' is an sk_fixlist_t
 *    userdata and return the argument cast to that type.  Return NULL
 *    if not.
 */
#define sk_lua_tofixlist(to_L, to_arg)                                  \
    SKLUA_TEST_TYPE(to_L, to_arg, SK_LUA_FIXLIST, sk_fixlist_t **)


struct sk_lua_stream_st {
    unsigned            is_ipfix;
    union stream_un {
        sk_fixstream_t *fix;
        skstream_t     *rw;
    }                   stream;
};
typedef struct sk_lua_stream_st sk_lua_stream_t;


/* array indices of data in the schema uservalue */
enum schema_data_en {
    /* table that contains the fields doubly indexed: numeric keys
     * that map to field values in position order in the schema, and
     * also IE-names that map to the fields */
    SKLUA_SCHEMA_UVAL_FIELDS = 1,
    /* table that maps from sk_field_ident_t ((pen<<32)|id) to field
     * object */
    SKLUA_SCHEMA_UVAL_IDENT = 2,
    /* table that maps from sk_field_t to field userdata; this table
     * is created only when the schema is used by the plug-in code,
     * and it allows the C callback to get the field-userdata
     * object. */
    SKLUA_SCHEMA_UVAL_PLUGIN = 3,
    /* how large to create the schema uservalue table */
    SKLUA_SCHEMA_UVAL_TABLE_LEN = 4
};

/* array indices of data in the fixrec uservalue */
enum fixrec_data_en {
    /* schema table */
    SKLUA_FIXREC_UVAL_SCHEMA = 1,
    /* nil if gc-able */
    SKLUA_FIXREC_UVAL_NO_GC = 2,
    SKLUA_FIXREC_UVAL_TABLE_LEN = 2
};

/* array indices of data in the fixlist uservalue */
enum fixlist_data_en {
    /* fixrec that contains this fixlist */
    SKLUA_FIXLIST_UVAL_FIXREC = 1,
    /* schema for the elements in this list, or the number 0 if the
     * list is a subTemplateMultiList */
    SKLUA_FIXLIST_UVAL_SCHEMA = 2,
    SKLUA_FIXLIST_UVAL_TABLE_LEN = 2
};

/* A lua fixrec pointer that includes the fixrec to which it points */
typedef struct sk_lua_silk_fixrec_st {
    sk_fixrec_t *recp;
    sk_fixrec_t  rec;
} sk_lua_silk_fixrec_t;

/* A datatype that can hold any field value */
typedef union anytype_un {
    uint8_t       u8;
    int8_t        i8;
    uint16_t      u16;
    int16_t       i16;
    uint32_t      u32;
    int32_t       i32;
    uint64_t      u64;
    int64_t       i64;
    float         f;
    double        d;
    int           i;
    sktime_t      t;
    sk_ntp_time_t ntp;
    char          str[UINT16_MAX];
    uint8_t       buf[UINT16_MAX];
} anytype_t;

/*
 *    sk_lua_stream_newschema_t supports the Lua function
 *    stream_new_schema_callback().  The structure holds the Lua
 *    state and a reference to the user's callback function which is
 *    stored in the Lua registry.
 */
typedef struct sk_lua_stream_newschema_st {
    lua_State          *L;
    /* reference in Lua registry to the table that holds the Lua
     * callback function. */
    int                 ref;
} sk_lua_stream_newschema_t;

/*
 *    For skplugin support in Lua, this structure is passed as the
 *    callback context to the C functions that implement the init(),
 *    cleanup(), and update() functions.
 */
struct skluapin_callback_data_st {
    lua_State   *L;
    /* reference in Lua registry to the table that holds the Lua
     * callback functions. */
    int          ref;
};
typedef struct skluapin_callback_data_st skluapin_callback_data_t;


/* LOCAL VARIABLE DEFINITIONS */

/* Lua initialization code; this is binary code compiled from
 * lua/silk-schema.lua */
static const uint8_t sk_lua_init_blob[] = {
#include "lua/silk-schema.i"
};

/* For each of the following variables, the variable's address indexes
 * a function in the Lua registry.  The functions are loaded from Lua
 * source files silk-site.lua or silk-schema.lua.  Remove the leading
 * "fn_" to get the function name. */
static int fn_index_ies = 0;
static int fn_normalize_ie = 0;
static int fn_get_plugin_fields = 0;
static int fn_sensor_id = 0;
static int fn_flowtype_id = 0;
static int fn_fixlist_append_normalize = 0;


/*
 *    IPFIX IE type names.
 *
 *    These should match IPFIX Information Element Data Types values
 *    (RFC5610).  See also the fbInfoElementDataType_t enum in
 *    fixbuf/public.h and
 *    http://www.iana.org/assignments/ipfix/ipfix.xhtml#ipfix-information-element-data-types
 *
 *    This is used for name to enum matching in Lua when verifying or
 *    getting the dataType value.
 */
static const char *sk_lua_ie_type_names[] = {
    "octetArray",               /* 0 */
    "unsigned8",                /* 1 */
    "unsigned16",               /* 2 */
    "unsigned32",               /* 3 */
    "unsigned64",               /* 4 */
    "signed8",                  /* 5 */
    "signed16",                 /* 6 */
    "signed32",                 /* 7 */
    "signed64",                 /* 8 */
    "float32",                  /* 9 */
    "float64",                  /* 10 */
    "boolean",                  /* 11 */
    "macAddress",               /* 12 */
    "string",                   /* 13 */
    "dateTimeSeconds",          /* 14 */
    "dateTimeMilliseconds",     /* 15 */
    "dateTimeMicroseconds",     /* 16 */
    "dateTimeNanoseconds",      /* 17 */
    "ipv4Address",              /* 18 */
    "ipv6Address",              /* 19 */
    "basicList",                /* 20 */
    "subTemplateList",          /* 21 */
    "subTemplateMultiList",     /* 22 */
    NULL
};

static const size_t sk_lua_ie_type_names_count =
    sizeof(sk_lua_ie_type_names)/sizeof(sk_lua_ie_type_names[0])-1u;

/*
 *    Indexes from the sk_lua_ie_type_names[] array of types that
 *    should have the FB_IE_F_ENDIAN flag set.
 */
static const unsigned sk_lua_ie_endian_typed_names[] = {
     1,                         /* unsigned8            */
     2,                         /* unsigned16           */
     3,                         /* unsigned32           */
     4,                         /* unsigned64           */
     5,                         /* signed8              */
     6,                         /* signed16             */
     7,                         /* signed32             */
     8,                         /* signed64             */
     9,                         /* float32              */
    10,                         /* float64              */
    14,                         /* dateTimeSeconds      */
    15,                         /* dateTimeMilliseconds */
    16,                         /* dateTimeMicroseconds */
    17,                         /* dateTimeNanoseconds  */
    18                          /* ipv4Address          */
};

static const size_t sk_lua_ie_endian_typed_names_count =
    (sizeof(sk_lua_ie_endian_typed_names)
     / sizeof(sk_lua_ie_endian_typed_names[0]));


/*
 *    IPFIX IE semantic unit names.
 *
 *    These should match IPFIX Information Element Units values
 *    (RFC5610).  See also the FB_UNITS_ macros in fixbuf/public.h and
 *    http://www.iana.org/assignments/ipfix/ipfix.xhtml#ipfix-information-element-units
 *
 *    This is used for name to enum matching in Lua when verifying or
 *    getting the units value.
 */
static const char *sk_lua_ie_semantic_units[] = {
    "none",                     /*  0 */
    "bits",                     /*  1 */
    "octets",                   /*  2 */
    "packets",                  /*  3 */
    "flows",                    /*  4 */
    "seconds",                  /*  5 */
    "milliseconds",             /*  6 */
    "microseconds",             /*  7 */
    "nanoseconds",              /*  8 */
    "fourOctetWords",           /*  9  RFC5610's "4-octet words" !! */
    "messages",                 /* 10 */
    "hops",                     /* 11 */
    "entries",                  /* 12 */
    "frames",                   /* 13 */
    NULL
};

static const size_t sk_lua_ie_semantic_units_count =
    sizeof(sk_lua_ie_semantic_units)/sizeof(sk_lua_ie_semantic_units[0])-1u;

/*
 *    IPFIX IE semantic names.
 *
 *    These should match IPFIX Information Element Semantics values
 *    (RFC5610).  See also the FB_IE_ macros defined in
 *    fixbuf/public.h and
 *    http://www.iana.org/assignments/ipfix/ipfix.xhtml#ipfix-information-element-semantics
 *
 *    This is used for name to enum matching in Lua when verifying or
 *    getting the dataTypeSemantics value.
 */
static const char *sk_lua_ie_semantic_names[] = {
    "default",                  /* 0 */
    "quantity",                 /* 1 */
    "totalCounter",             /* 2 */
    "deltaCounter",             /* 3 */
    "identifier",               /* 4 */
    "flags",                    /* 5 */
    "list",                     /* 6 */
    NULL
};

static const size_t sk_lua_ie_semantic_names_count =
    sizeof(sk_lua_ie_semantic_names)/sizeof(sk_lua_ie_semantic_names[0])-1u;


#define SKLUA_LISTTYPE_BL       0 /* basicList */
#define SKLUA_LISTTYPE_STL      1 /* subTemplateList */
#define SKLUA_LISTTYPE_STML     2 /* subTemplateMultiList*/

/*
 *    Types of Structured Data Elements (Lists)
 *
 *    This is used for name to enum matching in Lua when getting the
 *    type of list.
 */
static const char *sk_lua_list_type_names[] = {
    "basicList",                /* 0 */
    "subTemplateList",          /* 1 */
    "subTemplateMultiList",     /* 2 */
    NULL
};

/*
 *    IPFIX Semantic Names for Structured Data Types (Lists)
 *
 *    These should match IPFIX Information Element Semantics values
 *    (RFC6313).  See also the FB_IE_ macros defined in
 *    fixbuf/public.h and
 *    http://www.iana.org/assignments/ipfix/ipfix.xhtml#ipfix-structured-data-types-semantics
 *
 *    This is used for name to enum matching in Lua.
 *
 *    NOTE: Position in this list is one greater than the value.
 */
static const char *sk_lua_list_semantic_names[] = {
    "undefined",                /* 0xFF */
    "noneOf",                   /* 0 */
    "exactlyOneOf",             /* 1 */
    "oneOrMoreOf",              /* 2 */
    "allOf",                    /* 3 */
    "ordered",                  /* 4 */
    NULL
};

static const size_t sk_lua_list_semantic_names_count =
    ((sizeof(sk_lua_list_semantic_names)
      / sizeof(sk_lua_list_semantic_names[0])) - 1u);

/*
 *    How to find an IE when registering a plug-in field.  Be certain
 *    to keep this in sync with the definition of
 *    sk_field_computed_lookup_t in skschema.h.
 *
 *    This is used for name to enum matching in Lua when verifying the
 *    plug-in lookup types.
 */
static const char *sk_lua_field_computed_lookup_names[] = {
    "create",                   /* SK_FIELD_COMPUTED_CREATE */
    "ident",                    /* SK_FIELD_COMPUTED_LOOKUP_BY_IDENT */
    "name",                     /* SK_FIELD_COMPUTED_LOOKUP_BY_NAME */
    NULL
};

static const size_t sk_lua_field_computed_lookup_names_count =
    ((sizeof(sk_lua_field_computed_lookup_names)
      / sizeof(sk_lua_field_computed_lookup_names[0])) - 1u);


/* FUNCTION DECLARATIONS */

static void
sk_lua_push_fixlist(
    lua_State          *L,
    sk_fixlist_t       *fixlist,
    int                 rec_idx,
    int                 schema_idx);


/* FUNCTION DEFINITIONS */

/*
 * Call the function called 'name' in the globals table on the top
 * 'nargs' of the stack, and accept 'nresults' return values.
 */
static void
sk_lua_call_global(
    lua_State          *L,
    void               *addr,
    int                 nargs,
    int                 nresults)
{
    int idx = lua_gettop(L);
    lua_rawgetp(L, LUA_REGISTRYINDEX, addr);
    lua_insert(L, idx - nargs + 1);
    lua_call(L, nargs, nresults);
}

/*
 *    Helper function for the sk_lua_make_table_FOO() functions below
 *    that implement the internal.make_table_FOO() functions.
 *
 *    Given one of the sk_lua_FOO[] arrays of names above (e.g.,
 *    sk_lua_ie_type_names), puts onto the Lua stack a table where the
 *    key contains the name and value is the position (1-based) in the
 *    list.
 */
static int
sk_lua_make_table(
    lua_State          *L,
    const char         *names[],
    size_t              count)
{
    size_t i;

    lua_createtable(L, 0, count);
    for (i = 0; i < count; ++i) {
        assert(names[i]);
        lua_pushinteger(L, 1+i);
        lua_setfield(L, -2, names[i]);
    }
    assert(NULL == names[i]);
    return 1;
}

/*
 *    Implementation of internal.make_table_ie_type_names().
 */
static int
sk_lua_make_table_ie_type_names(
    lua_State          *L)
{
    return sk_lua_make_table(L, sk_lua_ie_type_names,
                             sk_lua_ie_type_names_count);
}

/*
 *    Implementation of internal.make_table_ie_semantic_units().
 */
static int
sk_lua_make_table_ie_semantic_units(
    lua_State          *L)
{
    return sk_lua_make_table(L, sk_lua_ie_semantic_units,
                             sk_lua_ie_semantic_units_count);
}

/*
 *    Implementation of internal.make_table_ie_semantic_names().
 */
static int
sk_lua_make_table_ie_semantic_names(
    lua_State          *L)
{
    return sk_lua_make_table(L, sk_lua_ie_semantic_names,
                             sk_lua_ie_semantic_names_count);
}

/*
 *    Implementation of internal.make_table_list_semantic_names().
 */
static int
sk_lua_make_table_list_semantic_names(
    lua_State          *L)
{
    return sk_lua_make_table(L, sk_lua_list_semantic_names,
                             sk_lua_list_semantic_names_count);
}

/*
 *    Implementation of
 *    internal.make_table_field_computed_lookup_names().
 */
static int
sk_lua_make_table_field_computed_lookup_names(
    lua_State          *L)
{
    return sk_lua_make_table(L, sk_lua_field_computed_lookup_names,
                             sk_lua_field_computed_lookup_names_count);
}

/*
 *    Implementation of internal.make_table_ie_endian_typed_names().
 *
 *    Put onto the Lua stack a table where the key is the name of a
 *    type that should have the endian flag set and the value is the
 *    name's position in the list.
 *
 *    Uses contents of sk_lua_ie_endian_typed_names[] as indexes into
 *    sk_lua_ie_type_names[].
 */
static int
sk_lua_make_table_ie_endian_typed_names(
    lua_State          *L)
{
    size_t i;

    lua_createtable(L, 0, sk_lua_ie_endian_typed_names_count);

    for (i = 0; i < sk_lua_ie_endian_typed_names_count; ++i) {
        assert(sk_lua_ie_type_names[sk_lua_ie_endian_typed_names[i]]);
        lua_pushinteger(L, 1+i);
        lua_setfield(
            L, -2, sk_lua_ie_type_names[sk_lua_ie_endian_typed_names[i]]);
    }
    return 1;
}


/*
 *    Push the information model stored in the Lua registry onto the
 *    stack and return a pointer to the model.
 *
 *    If the model is not in the Lua registry, raise an error unless
 *    'no_error' is non-zero.  When no_error is non-zero and the model
 *    is not present, push nil onto the stack and return NULL.
 */
static fbInfoModel_t *
sk_lua_get_info_model(
    lua_State          *L,
    int                 no_error)
{
    if (lua_getfield(L, LUA_REGISTRYINDEX, SK_LUA_INFOMODEL) != LUA_TNIL) {
        return (fbInfoModel_t *)lua_touserdata(L, -1);
    }
    if (!no_error) {
        luaL_error(L, "No information model in Lua registry");
    }
    return NULL;
}


/*
 * field_get_attribute() and field __index metamethod (field_index)
 *
 * =pod
 *
 * =item I<field>B<[> I<attribute> B<]>
 *
 * Return the specified attribute of I<field>.  As always in Lua,
 * I<field>.I<attribute> works as well.  An alias for
 * L<silk.B<field_get_attribute()>|/"silk.B<field_get_attribute(>I<field>, I<attribute>B<)>">.
 *
 * =item silk.B<field_get_attribute(>I<field>, I<attribute>B<)>
 *
 * Return the specified attribute of I<field>.  The list of valid
 * attributes are:
 *
 * =over 4
 *
 * =item name
 *
 * The name, a string
 *
 * =item elementId
 *
 * The integer identifier
 *
 * =item enterpriseId
 *
 * The Private Enterprise Number (PEN), an integer or B<nil> if the
 * field is from the standard information model
 *
 * =item length
 *
 * The length in octets, an integer or the string C<varlen> to
 * indicate a variable-length element
 *
 * =item dataType
 *
 * A string specifying the data type (IPFIX Information Element Data
 * Types values [RFC5610]).  One of C<octetArray>, C<unsigned8>,
 * C<unsigned16>, C<unsigned32>, C<unsigned64>, C<signed8>,
 * C<signed16>, C<signed32>, C<signed64>, C<float32>, C<float64>,
 * C<boolean>, C<macAddress>, C<string>, C<dateTimeSeconds>,
 * C<dateTimeMilliseconds>, C<dateTimeMicroseconds>,
 * C<dateTimeNanoseconds>, C<ipv4Address>, C<ipv6Address>,
 * C<basicList>, C<subTemplateList>, C<subTemplateMultiList>
 *
 * =item dataTypeSemantics
 *
 * A string specifying the data type semantics (IPFIX Information
 * Element Semantics values [RFC5610]).  One of C<default>,
 * C<quantity>, C<totalCounter>, C<deltaCounter>, C<identifier>,
 * C<flags>, C<list>
 *
 * =item units
 *
 * A string specifying the units (IPFIX Information Element Units
 * values [RFC5610]).  One of C<none>, C<bits>, C<octets>, C<packets>,
 * C<flows>, C<seconds>, C<milliseconds>, C<microseconds>,
 * C<nanoseconds>, C<fourOctetWords> (RFC5610's C<4-octet words>),
 * C<messages>, C<hops>, C<entries>, C<frames>
 *
 * =item rangemin
 *
 * The minimum legal value for an integer field or B<nil> if none
 *
 * =item rangemax
 *
 * The maximum legal value for an integer field or B<nil> if none
 *
 * =item description
 *
 * The description, a string or B<nil> if none
 *
 * =item schema
 *
 * The L<schema|/Schema> that owns this field.
 *
 * =back
 *
 * =cut
 */
static int
sk_lua_field_get_attribute(
    lua_State          *L)
{
    sk_lua_checkfield(L, 1);          /* field*/
    lua_getuservalue(L, 1);           /* (infotable field) */
    lua_pushvalue(L, 2);              /* index */
    lua_gettable(L, -2);              /* (get (infotable field) index) */
    return 1;
}

/*
 *  field_get_info_table(field);
 *
 *    Return the field's information table (ie_spec).
 *
 *    This is an "internal" lua function that is used by the exported
 *    __pairs(field) function in silk-schema.lua.
 */
static int
sk_lua_field_get_info_table(
    lua_State          *L)
{
    sk_lua_checkfield(L, 1);
    lua_getuservalue(L, 1);
    return 1;
}

/*
 *    Wrap a field pointer as a Lua field userdata and push it onto
 *    the Lua stack, where 'schema' is the index of the field's
 *    schema-userdata on the stack.  The field-userdata is the field
 *    pointer.  The uservalue is the information table for the field.
 */
static void
sk_lua_push_field(
    lua_State          *L,
    const sk_field_t   *field,
    int                 schema)
{
    const sk_field_t **f;
    uint16_t len;
    int idx;

    /* Create the userdata object */
    f = sk_lua_newuserdata(L, const sk_field_t *);
    /* Set the userdata pointer to the field */
    *f = field;

    /* Create the field info table---this is its uservalue */
    lua_createtable(L, 0, 11);

    /* Add schema */
    lua_pushvalue(L, schema);
    lua_setfield(L, -2, "schema");

    /* Add name */
    lua_pushstring(L, sk_field_get_name(field));
    lua_setfield(L, -2, "name");

    /* Add elementId */
    lua_pushinteger(L, sk_field_get_id(field));
    lua_setfield(L, -2, "elementId");

    /* Add enterpriseId (zero if not set) */
    if (sk_field_get_pen(field)) {
        lua_pushinteger(L, sk_field_get_pen(field));
        lua_setfield(L, -2, "enterpriseId");
    }

    /* Add length */
    len = sk_field_get_length(field);
    if (len == FB_IE_VARLEN) {
        lua_pushliteral(L, "varlen");
    } else {
        lua_pushinteger(L, len);
    }
    lua_setfield(L, -2, "length");

    /* Add dataType */
    if ((idx = sk_field_get_type(field)) >= sk_lua_ie_type_names_count) {
        luaL_error(L, "field type to name mapping out of range %d", idx);
    }
    lua_pushstring(L, sk_lua_ie_type_names[idx]);
    lua_setfield(L, -2, "dataType");

    /* Add dataTypeSemantics */
    if ((idx = sk_field_get_semantics(field)) >=sk_lua_ie_semantic_names_count)
    {
        luaL_error(L, "field semantics to name mapping out of range %d", idx);
    }
    lua_pushstring(L, sk_lua_ie_semantic_names[idx]);
    lua_setfield(L, -2, "dataTypeSemantics");

    /* Add units */
    if ((idx = sk_field_get_units(field)) >= sk_lua_ie_semantic_units_count) {
        luaL_error(L, "field units to name mapping out of range %d", idx);
    }
    lua_pushstring(L, sk_lua_ie_semantic_units[idx]);
    lua_setfield(L, -2, "units");

    /* Add description */
    lua_pushstring(L, sk_field_get_description(field));
    lua_setfield(L, -2, "description");

    /* Add rangemin and rangemax; IETF says range is unsigned, so only
     * need to check for max != 0 */
    if (sk_field_get_max(field) != 0) {
        lua_pushinteger(L, sk_field_get_min(field));
        lua_setfield(L, -2, "rangemin");
        lua_pushinteger(L, sk_field_get_max(field));
        lua_setfield(L, -2, "rangemax");
    }

    /* Set the field info table as the field userdata's uservalue */
    lua_setuservalue(L, -2);

    /* Set the field userdata's type and metatable */
    luaL_setmetatable(L, SK_LUA_FIELD);
}

/*
 * Garbage collect a schema
 */
static int
sk_lua_schema_gc(
    lua_State          *L)
{
    sk_schema_t **schema = (sk_schema_t **)lua_touserdata(L, 1);
    sk_schema_destroy(*schema);
    return 0;
}

/*
 * schema_count_fields() and schema __len metamethod
 *
 * =pod
 *
 * =item B<#>I<schema>
 *
 * An alias for
 * L<silk.B<schema_count_fields()>|/"silk.B<schema_count_fields(>I<schema>B<)>">.
 *
 * =item silk.B<schema_count_fields(>I<schema>B<)>
 *
 * Return the number of fields (IEs) in I<schema>.
 *
 * =cut
 */
static int
sk_lua_schema_count_fields(
    lua_State          *L)
{
    sk_schema_t **schema;

    schema = sk_lua_checkschema(L, 1);
    lua_pushinteger(L, sk_schema_get_count(*schema));
    return 1;
}

/*
 * =pod
 *
 * =item silk.B<schema_get_fields(>I<schema>B<)>
 *
 * Return a table that contains information about the information
 * elements in the schema.  This table is doubly-indexed: once by
 * position (integer), and once by name.  The values are
 * L<field|/Field> objects.
 *
 * =cut
 */
static int
sk_lua_schema_get_fields(
    lua_State          *L)
{
    size_t len;

    /* copy the uservalue table of schema */
    sk_lua_checkschema(L, 1);
    lua_getuservalue(L, 1);
    lua_rawgeti(L, -1, SKLUA_SCHEMA_UVAL_FIELDS);
    len = lua_rawlen(L, -1);
    lua_createtable(L, len, len);
    /* when lua_next() is called the stack contains schema_fields at
     * -3, new_table at -2, previous_key at -1 */
    for (lua_pushnil(L); lua_next(L, -3) != 0; lua_pop(L, 1)) {
        /* repeat key and value and then add to new_table; must
         * maintain the key for call to lua_next() */
        lua_pushvalue(L, -2);
        lua_pushvalue(L, -2);
        lua_settable(L, -5);
    }

    return 1;
}

/*
 * schema_get_field() and schema __index metamethod (schema_index)
 *
 * =pod
 *
 * =item I<schema>B<[> I<field> | I<position> | I<name> | I<table> B<]>
 *
 * Get a L<field|/Field> object from a schema.  An alias for
 * L<silk.B<schema_get_field()>|/"silk.B<schema_get_field(>I<schema>, {I<field> | I<position> | I<name> | I<table>}B<)>">
 *
 *
 * =item silk.B<schema_get_field(>I<schema>, {I<field> | I<position> | I<name> | I<table>}B<)>
 *
 * Get a L<field|/Field> from a schema.
 *
 * If the argument is a field object, return I<field> if it from
 * I<schema> or B<nil> if it is not from I<schema>.
 *
 * If the argument is numeric, treat it as a positional index into
 * I<schema> where 1 is the first position.  Return B<nil> if
 * I<position> is out of range.
 *
 * If the argument is a string, assume it is the name of an IE and
 * search for an IE with I<name> on I<schema>.  When an IE appears
 * multiple times on I<schema>, I<name> may include a suffix of
 * C<_I<number>> to return the I<number>th IE whose name is I<name>.
 * I<name> without the C<_I<number>> suffix is an alias for
 * C<I<name>_1>.  Return B<nil> if I<name> does not match any field on
 * I<schema>.
 *
 * If the argument is a table, it must have an C<elementId> key whose
 * value is the elementId of the field to return.  The elementId is
 * assumed to be in the standard information model unless the table
 * contains an C<enterpriseId> key whose value is not B<nil> or 0.
 * Raise an error when an C<elementId> key is not present, the value
 * for C<elementId> or is not a number, or the value for
 * C<enterpriseId> is not a number and not B<nil>.  Return B<nil> when
 * the table is valid and the desired field is not present on
 * I<schema>.
 *
 * If the argument is any other type of object, return B<nil>.
 *
 * =cut
 */
static int
sk_lua_schema_get_field(
    lua_State          *L)
{
    sk_schema_t **schema;
    void *field;

    schema = sk_lua_checkschema(L, 1);

    switch (lua_type(L, 2)) {
      case LUA_TTABLE:
        /* look up by ident (pen and id number) */
        lua_getuservalue(L, 1);
        lua_rawgeti(L, -1, SKLUA_SCHEMA_UVAL_IDENT);
        if (lua_getfield(L, 2, "elementId") != LUA_TNUMBER) {
            return luaL_error(L, "expected numeric 'elementId' key, got %s",
                              sk_lua_typename(L, -1));
        }
        switch (lua_getfield(L, 2, "enterpriseId")) {
          case LUA_TNIL:
            lua_pop(L, 1);
            break;
          case LUA_TNUMBER:
            lua_pushinteger(L, 32);
            lua_arith(L, LUA_OPSHL); /* pen << 32 */
            lua_arith(L, LUA_OPBOR); /* (pen << 32) | id */
            break;
          default:
            return luaL_error(
                L, "expected numeric or nil 'enterpriseId' key, got %s",
                sk_lua_typename(L, -1));
        }
        /* get ident_table */
        lua_rawget(L, -2);
        return 1;

      case LUA_TUSERDATA:
        /* is the field identifier a field-userdata? */
        field = luaL_testudata(L, 2, SK_LUA_FIELD);
        if (field) {
            /* if so, verify the 'schema' entry in its uservalue table
             * is the schema at stack position 1. */
            lua_getuservalue(L, 2);
            lua_getfield(L, -1, "schema");
            if (lua_touserdata(L, -1) == schema) {
                lua_pop(L, 2);
                return 1;
            }
            lua_pushnil(L);
            return 1;
        }
        /* if not a field, do a general lookup */
        /* FALLTHROUGH */

      default:
        /* look up by name (or whatever object is at stack 2) */
        lua_getuservalue(L, 1);
        lua_rawgeti(L, -1, SKLUA_SCHEMA_UVAL_FIELDS);
        lua_pushvalue(L, 2);
        lua_rawget(L, -2);
        return 1;
    }
}


/* An iterator function over a schema's IEs.  upvalue(1) is the field
 * table, upvalue(2) is the current index.  */
static int
sk_lua_schema_iter_func(
    lua_State          *L)
{
    lua_Integer c;
    /* get index upvalue */
    lua_pushvalue(L, lua_upvalueindex(2));

    /* Add one and push new index value */
    c = lua_tointeger(L, -1);
    lua_pushinteger(L, c + 1);

    /* Replace index upvalue with new index */
    lua_replace(L, lua_upvalueindex(2));

    /* Get and return the original index-th value from the IE table */
    lua_gettable(L, lua_upvalueindex(1));
    return 1;
}

/*
 * =pod
 *
 * =item silk.B<schema_iter(>I<schema>B<)>
 *
 * Return an iterator over a schema's L<field|/Field> objects.
 *
 * =cut
 */
static int
sk_lua_schema_iter(
    lua_State          *L)
{
    sk_lua_checkschema(L, 1);

    /* nil is returned for the state and initial value.  The state is
     * carried in the function's upvalues instead. */

    lua_getuservalue(L, 1);     /* uservalue */
    lua_rawgeti(L, -1, SKLUA_SCHEMA_UVAL_FIELDS); /* Field table upvalue */
    lua_pushinteger(L, 1);      /* index upvalue (starts at 1) */
    lua_pushcclosure(L, sk_lua_schema_iter_func, 2); /* Make a closure */
    lua_pushnil(L);
    lua_pushnil(L);
    return 3;                   /* return closure, nil, nil */
}

/*
 * =pod
 *
 * =item silk.B<schema_get_template_id(>I<schema>B<)>
 *
 * Return the numeric template id of I<schema>.
 *
 * =cut
 */
static int
sk_lua_schema_get_template_id(
    lua_State          *L)
{
    sk_schema_t *schema;
    uint16_t tid;

    schema = *sk_lua_checkschema(L, 1);
    sk_schema_get_template(schema, NULL, &tid);
    lua_pushinteger(L, tid);
    return 1;
}

/*
 *    Given a schema in C (an sk_schema_t pointer) push a schema
 *    userdata object representing that schema onto the Lua stack.
 *    This function should be called with a clone of the schema
 *    pointer.
 *
 *    If the schema userdata for the schema pointer already exists,
 *    decrement the schema's reference count and return that existing
 *    userdata.
 *
 *    Otherwise, create a new userdata.  A table of field userdata
 *    objects is created as the schema-userdata's uservalue.  This
 *    table can be indexed as an array (as ordered in the schema) or
 *    as a dictionary (by the IE name).  If the same IE appears
 *    multiple times in the schema, the entries are named "name_1",
 *    "name_2", ... and "name" is an alias for "name_1".
 */
static void
sk_lua_push_schema(
    lua_State          *L,
    const sk_schema_t  *schema)
{
    const sk_field_t *f;
    uint16_t num_elements;
    const sk_schema_t **s;
    int idx, top;
    uint16_t i;

    top = lua_gettop(L);
    lua_getfield(L, LUA_REGISTRYINDEX, SK_LUA_SCHEMA_LOOKUP);
    lua_pushlightuserdata(L, (void *)schema);
    if (lua_rawget(L, -2) != LUA_TNIL) {
        lua_remove(L, -2);
        sk_schema_destroy(schema);
        return;
    }
    lua_pop(L, 1);

    num_elements = sk_schema_get_count(schema);

    /* Create the userdata, and set its value to the schema pointer */
    s = sk_lua_newuserdata(L, const sk_schema_t *);
    *s = schema;
    idx = top + 2;        /* The index of the schema userdata */
    assert(idx == lua_gettop(L));

    /* Create the uservalue table */
    lua_createtable(L, SKLUA_SCHEMA_UVAL_TABLE_LEN, 0);   /* idx + 1 */

    /* Create the IE table */
    lua_createtable(L, num_elements, num_elements); /* idx + 2 */

    for (i = 0; i < num_elements; ++i) {
        f = sk_schema_get_field(schema, i);

        /* Create the field (IE) userdata */
        sk_lua_push_field(L, f, idx);

        /* Add to dependency table */
        lua_getfield(L, LUA_REGISTRYINDEX, SK_LUA_DEPENDENCIES);
        lua_pushvalue(L, -2);   /* field userdata */
        lua_pushvalue(L, idx);  /* schema userdata*/
        lua_settable(L, -3); /* Add to dependency table */
        lua_pop(L, 1);

        /* Add the field userdata as the (i + 1)-th value */
        lua_rawseti(L, idx + 2, i + 1);
    }

    /* Call the index_ies(fields) function from the Lua code  */
    sk_lua_call_global(L, &fn_index_ies, 1, 2);
    lua_rawseti(L, idx + 1, SKLUA_SCHEMA_UVAL_IDENT);
    lua_rawseti(L, idx + 1, SKLUA_SCHEMA_UVAL_FIELDS);

    /* set uservalue table */
    lua_setuservalue(L, idx);

    /* set type and metadata */
    luaL_setmetatable(L, SK_LUA_SCHEMA);

    /* Add to lookup table */
    lua_insert(L, -2);
    lua_pushlightuserdata(L, (void *)schema);
    lua_pushvalue(L, -3);
    lua_settable(L, -3);
    lua_pop(L, 1);

    assert(lua_gettop(L) == top + 1);
}

/*
 *    bool = internal.schemas_match(s1, s2)
 *
 *    Return true if schemas s1 and s2 match.  Return false otherwise.
 *
 *    This is an "internal" lua function that is used by the helper
 *    function export.fixlist_append_normalize() in silk-schema.lua.
 */
static int
sk_lua_schemas_match(
    lua_State          *L)
{
    const sk_schema_t *s[2];

    s[0] = *sk_lua_checkschema(L, 1);
    s[1] = *sk_lua_checkschema(L, 2);
    lua_pushboolean(L, sk_schema_matches_schema(s[0], s[1], NULL));
    return 1;
}


/* Convert the argument at 'index' into a table that represents an IE
 * identifier. */
static void
convert_argument_to_ie_table(
    lua_State          *L,
    int                 index)
{
    index = lua_absindex(L, index);
    switch (lua_type(L, index)) {
      case LUA_TTABLE:
        break;
      case LUA_TSTRING:
        lua_createtable(L, 0, 1);
        lua_pushvalue(L, index);
        lua_setfield(L, -2, "name");
        lua_replace(L, index);
        break;
      case LUA_TNUMBER:
        lua_createtable(L, 0, 1);
        lua_pushvalue(L, index);
        lua_setfield(L, -2, "elementId");
        lua_replace(L, index);
        break;
      case LUA_TUSERDATA:
        if (luaL_testudata(L, index, SK_LUA_FIELD)) {
            lua_createtable(L, 0, 1);
            lua_pushvalue(L, index);
            lua_setfield(L, -2, "field");
            lua_replace(L, index);
            break;
        }
      default:
        sk_lua_argerror(
            L, index,
            "expected string, number, field, or table as IE identifer, got %s",
            sk_lua_typename(L, index));
    }
}

/* Fill 'found_ie' with an element represented by the IE identifier
 * table at 'index' */
static void
convert_ie_table_to_ie(
    lua_State          *L,
    int                 index,
    fbInfoModel_t      *model,
    fbInfoElement_t    *found_ie)
{
    const fbInfoElement_t *iep;
    fbInfoElement_t ie;
    const char *key;
    lua_Unsigned num;
    uint16_t len = 0;

    iep = NULL;
    index = lua_absindex(L, index);
    found_ie->ref.name = NULL;
    lua_pushnil(L);
    while (lua_next(L, index) != 0) {
        if (lua_type(L, -2) != LUA_TSTRING) {
            sk_lua_argerror(L, index, "expected string key, got %s",
                            sk_lua_typename(L, -2));
        }
        key = lua_tostring(L, -2);
        if (strcmp("name", key) == 0) {
            /* Handle "name" element */
            const char *name = lua_tostring(L, -1);
            iep = fbInfoModelGetElementByName(model, name);
            if (iep == NULL) {
                sk_lua_argerror(L, index, "not a known IE name '%s'", name);
            }
        } else if (strcmp("enterpriseId", key) == 0) {
            /* Cannot have "enterpriseId" without "elementId" */
            if (lua_getfield(L, index, "elementId") == LUA_TNIL) {
                sk_lua_argerror(L, index,
                                "Found enterpriseId without elementId");
            }
            lua_pop(L, 2);
            continue;
        } else if (strcmp("elementId", key) == 0) {
            /* Handle a "enterpriseId"/"elementId" pair.
             * ("enterpriseId" may be nil) */
            if (lua_type(L, -1) != LUA_TNUMBER) {
                sk_lua_argerror(L, index,
                                "expected numeric elementId, got %s",
                                sk_lua_typename(L, -1));
            }
            num = (lua_Unsigned)lua_tointeger(L, -1);
            luaL_argcheck(L, num <= INT16_MAX, index,"elementId is too large");
            ie.num = num;
            switch (lua_getfield(L, index, "enterpriseId")) {
              case LUA_TNIL:
                ie.ent = 0;
                break;
              case LUA_TNUMBER:
                num = (lua_Unsigned)lua_tointeger(L, -1);
                luaL_argcheck(L, num <= UINT32_MAX, index,
                              "enterpriseId is too large");
                ie.ent = num;
                break;
              default:
                sk_lua_argerror(L, index,
                                "expected numeric enterpriseId, got %s",
                                sk_lua_typename(L, -1));
            }
            iep = fbInfoModelGetElementByID(model, ie.num, ie.ent);
            if (iep == NULL) {
                if (0 == ie.ent) {
                    sk_lua_argerror(L, index, "not a known IE elementId %d",
                                    ie.num);
                }
                sk_lua_argerror(L, index,
                                "not a known IE enterpriseId/elementId %d/%d",
                                ie.ent, ie.num);
            }
            lua_pop(L, 1);  /* pop the pen */
        } else if (strcmp("field", key) == 0) {
            /* Handle a "field" argument */
            sk_field_t **fp = sk_lua_tofield(L, -1);
            if (fp == NULL) {
                sk_lua_argerror(
                    L, index, "field key does not contain a field object");
            }
            ie = *sk_field_get_ie(*fp);
            if (len == 0) {
                len = sk_field_get_length(*fp);
            }
            iep = &ie;
        } else if (strcmp("length", key) == 0) {
            /* Handle a "length" argument */
            switch (lua_type(L, -1)) {
              case LUA_TNUMBER:
                num = (lua_Unsigned)lua_tointeger(L, -1);
                luaL_argcheck(L, num <= 0xfffe, index, "length is too long");
                len = num;
                break;
              case LUA_TSTRING:
                {
                    const char *varlen = lua_tostring(L, -1);
                    if (strcmp(varlen, "varlen") != 0) {
                        sk_lua_argerror(L, index,
                                        "invalid length value '%s'", varlen);
                    }
                    len = FB_IE_VARLEN;
                }
                break;
              default:
                sk_lua_argerror(
                    L, index, "expected length to be number or string, got %s",
                    sk_lua_typename(L, -1));
            }
            iep = NULL;
        } else {
            /* Error on unknown keys */
            sk_lua_argerror(L, index, "illegal key '%s'", key);
        }
        if (iep) {
            if (found_ie->ref.name && (iep->ent != found_ie->ent
                                       || iep->num != found_ie->num))
            {
                sk_lua_argerror(
                    L, index, "IE specification resolves to multiple IEs");
            } else {
                /* Save the IE */
                *found_ie = *iep;
            }
        }
        lua_pop(L, 1);
    }
    if (!found_ie->ref.name) {
        sk_lua_argerror(L, index, "Empty specification");
    }
    if (len) {
        found_ie->len = len;
    }
}

/*
 * =pod
 *
 * =item silk.B<schema(>[I<elem>[, ...]]B<)>
 *
 * Create a schema consisting of the given information elements.  The
 * type of each element must be one of the following:
 *
 * =over 4
 *
 * =item a string
 *
 * The string should be the name of the information element.
 *
 * =item an integer
 *
 * The value should be the elementId of a standard information element.
 *
 * =item field object
 *
 * A L<field|/Field> object.
 *
 * =item table
 *
 * A table of I<key = value> pairs describing an information element.
 * The possible keys and their expected values are:
 *
 * =over 4
 *
 * =item name
 *
 * A string containing the name of the information element.
 *
 * =item enterpriseId
 *
 * The Private Enterprise Number (PEN) of the information element.
 * This key requires a corresponding C<elementId> key.
 *
 * =item elementId
 *
 * The number of the information element in the Private Enterprise
 * specified in C<enterpriseId> or a standard information element if
 * C<enterpriseId> is not specified.
 *
 * =item field
 *
 * The L<field|/Field> object.
 *
 * =item length
 *
 * The length of the field as an integer, or the string C<varlen> to
 * indicate a variable-length field.  When not specified, the base
 * length of the element is used.
 *
 * =back
 *
 * =back
 *
 * =cut
 */
int
sk_lua_schema_create(
    lua_State          *L)
{
    fbInfoModel_t *model;
    int i;
    int top;
    sk_schema_t *schema;
    sk_schema_t **tmp_schema;
    const char *err_msg = NULL;
    sk_field_t *f;

    top = lua_gettop(L);
#if 0
    if (top < 1) {
        luaL_error(L, "may not create a schema with no information elements");
    }
#endif

    /* Create a temporary schema object that will destroy the schema
     * if necessary */
    tmp_schema = sk_lua_newuserdata(L, sk_schema_t *);
    *tmp_schema = NULL;
    lua_newtable(L);
    lua_setuservalue(L, -2);
    luaL_setmetatable(L, SK_LUA_SCHEMA);

    model = sk_lua_get_info_model(L, 0);
    lua_pop(L, 1);
    if (sk_schema_create(&schema, model, NULL, 0)) {
        return luaL_error(L, "Error creating empty schema");
    }
    *tmp_schema = schema;

    /* For each argument... */
    for (i = 1; i <= top; ++i) {
        fbInfoElement_t ie;

        /* Convert argument to table argument */
        convert_argument_to_ie_table(L, i);
        convert_ie_table_to_ie(L, i, model, &ie);

        /* Add the IE to the schema */
        if (sk_schema_insert_field_by_id(
                &f, schema, ie.ent, ie.num, NULL, NULL))
        {
            if (ie.ent) {
                err_msg = lua_pushfstring(
                    L, ("Could not add element %d/%d to schema"),
                    ie.ent, ie.num);
            } else {
                err_msg = lua_pushfstring(
                    L, "Could not add element %d to schema", ie.num);
            }
            return luaL_error(L, err_msg);
        }

        /* Adjust the length */
        sk_field_set_length(f, ie.len);
    }

    /* freeze the schema */
    if (sk_schema_freeze(schema)) {
        return luaL_error(L, "Error initializing schema");
    }

    /* Create the lua schema object from the pointer */
    sk_lua_push_schema(L, schema);

    /* Unprotect the schema pointer */
    *tmp_schema = NULL;

    return 1;
}

static int
sk_lua_fixrec_gc(
    lua_State          *L)
{
    sk_fixrec_t *fixrec;

    lua_getuservalue(L, 1);
    if (lua_rawgeti(L, -1, SKLUA_FIXREC_UVAL_NO_GC) == LUA_TNIL) {
        fixrec = *sk_lua_checkfixrec(L, 1);
        sk_fixrec_destroy(fixrec);
    }
    lua_pop(L, 2);
    return 0;
}

/*
 * =pod
 *
 * =item silk.B<fixrec_get_schema(>I<fixrec>B<)>
 *
 * Return the L<schema|/Schema> associated with I<fixrec>.
 *
 * =cut
 */
int
sk_lua_fixrec_get_schema(
    lua_State          *L)
{
    sk_lua_checkfixrec(L, 1);
    lua_getuservalue(L, 1);
    lua_rawgeti(L, -1, SKLUA_FIXREC_UVAL_SCHEMA);
    lua_remove(L, -2);
    return 1;
}


/*
 *    Given the indexes on the Lua stack of a fixrec and of a "field
 *    identifier", return the field as an sk_field_t.  Return NULL if
 *    the field does not exist on fixrec.  The function leaves the
 *    stack as it found it except on error (detailed below).
 *
 *    A "field identifier" is either a field-userdata, an IE name, a
 *    position, or a Lua table containing an elementId key with value
 *    and an optional enterpriseId key and value.  Specifically, if
 *    the "field identifier" is not a field object, schema_get_field()
 *    is called to get a field object.
 *
 *    For most errors, when the field does not exist on the fixrec the
 *    stack is left the same as when the function was called with the
 *    addition of an error message.  However, if the table passed to
 *    schema_get_field() is not of the correct form, that function
 *    raises an error and this function's cleanup is ignored.
 */
static sk_field_t *
sk_lua_rec_canonicalize_field(
    lua_State          *L,
    int                 fixrec,
    int                 field)
{
    sk_field_t **f;
    void *schema;
    int success;

    /* See if the argument is a field-userdata */
    f = sk_lua_tofield(L, field);
    if (f) {
        /* If so, verify the 'schema' entry in its uservalue table is
         * the same as the fixrec's schema */
        lua_getuservalue(L, field);
        lua_getfield(L, -1, "schema");
        schema = lua_touserdata(L, -1);
        assert(schema);
        lua_pushcfunction(L, sk_lua_fixrec_get_schema);
        lua_pushvalue(L, fixrec);
        lua_call(L, 1, 1);
        success = (lua_touserdata(L, -1) == schema);
        lua_pop(L, 3);
    } else {
        /* If not, find the field associated with the argument:
         * schema_get_field(fixrec_get_schema(REC), FIELD) */
        lua_pushcfunction(L, sk_lua_schema_get_field);
        lua_pushcfunction(L, sk_lua_fixrec_get_schema);
        lua_pushvalue(L, fixrec);
        lua_call(L, 1, 1);
        lua_pushvalue(L, field);
        lua_call(L, 2, 1);
        success = !lua_isnil(L, -1);
        if (success) {
            f = sk_lua_checkfield(L, -1);
        }
        lua_pop(L, 1);
    }

    if (success) {
        return *f;
    }
    lua_pushfstring(L, "Not a valid field for fixrec %p: %s",
                    lua_touserdata(L, fixrec), luaL_tolstring(L, field, NULL));
    return NULL;
}

/*
 * fixrec_get_value() and fixrec __index metamethod (fixrec_index)
 *
 * =pod
 *
 * =item I<fixrec>B<[> I<field> | I<position> | I<name> | I<table> B<]>
 *
 * Get the value for a field from I<fixrec>.  An alias for the two
 * argument form of
 * L<silk.B<fixrec_get_value()>|/"silk.B<fixrec_get_value(>I<fixrec>, {I<field> | I<position> | I<name> | I<table>}[, I<notfound>]B<)>">.
 *
 * =item silk.B<fixrec_get_value(>I<fixrec>, {I<field> | I<position> | I<name> | I<table>}[, I<notfound>]B<)>
 *
 * Get the value for a field from I<fixrec>.  When the second argument
 * is not a L<field|/Field>, the function uses
 * L<silk.B<schema_get_field()>|/"silk.B<schema_get_field(>I<schema>, {I<field> | I<position> | I<name> | I<table>}B<)>">
 * to get the field object from I<fixrec>'s L<schema|/Schema>.
 *
 * When only two arguments are provided, the behavior when the field
 * is not present on I<fixrec>'s schema depends on the type of the
 * second argument.  When the second argument is a position (i.e.,
 * numeric), the function returns B<nil>.  Otherwise, the function
 * raises an error.
 *
 * Usually when the field is not present on I<fixrec>'s schema and a
 * third argument is provided (shown as I<notfound> above), that value
 * is returned.  However, when B<schema_get_field()> raises an error
 * because the form of the I<table> argument is invalid, any
 * I<notfound> argument is ignored.
 *
 * =cut
 */
static int
sk_lua_fixrec_get_value(
    lua_State          *L)
{
    anytype_t data;
    uint16_t len;
    sk_fixrec_t *fixrec;
    sk_field_t *field;
    sk_schema_err_t err = 0;
    int argc;

    /* get number of arguments and the fixrec */
    argc = lua_gettop(L);
    fixrec = *sk_lua_checkfixrec(L, 1);

    /* get the field */
    field = sk_lua_rec_canonicalize_field(L, 1, 2);
    if (NULL == field) {
        /* field not on fixrec */
        if (3 == argc) {
            /* a third argument was given, use it as return value */
            lua_pushvalue(L, 3);
            return 1;
        }
        /* ignore error and push nil when key is integer */
        if (lua_isinteger(L, 2)) {
            lua_pushnil(L);
            return 1;
        }
        return lua_error(L);
    }

    /* Push a default return value */
    if (3 == argc) {
        lua_pushvalue(L, 3);
    } else {
        lua_pushnil(L);
    }

    /* Push the data associated with the given field  */
    switch (sk_field_get_type(field)) {
      case FB_UINT_8:
        if (!(err = sk_fixrec_get_unsigned8(fixrec, field, &data.u8))) {
            lua_pushinteger(L, (lua_Integer)data.u8);
        }
        break;
      case FB_UINT_16:
        if (!(err = sk_fixrec_get_unsigned16(fixrec, field, &data.u16))) {
            lua_pushinteger(L, (lua_Integer)data.u16);
        }
        break;
      case FB_UINT_32:
        if (!(err = sk_fixrec_get_unsigned32(fixrec, field, &data.u32))) {
            lua_pushinteger(L, (lua_Integer)data.u32);
        }
        break;
      case FB_UINT_64:
        if (!(err = sk_fixrec_get_unsigned64(fixrec, field, &data.u64))) {
            lua_pushinteger(L, (lua_Integer)data.u64);
        }
        break;
      case FB_INT_8:
        if (!(err = sk_fixrec_get_signed8(fixrec, field, &data.i8))) {
            lua_pushinteger(L, data.i8);
        }
        break;
      case FB_INT_16:
        if (!(err = sk_fixrec_get_signed16(fixrec, field, &data.i16))) {
            lua_pushinteger(L, data.i16);
        }
        break;
      case FB_INT_32:
        if (!(err = sk_fixrec_get_signed32(fixrec, field, &data.i32))) {
            lua_pushinteger(L, data.i32);
        }
        break;
      case FB_INT_64:
        if (!(err = sk_fixrec_get_signed64(fixrec, field, &data.i64))) {
            lua_pushinteger(L, data.i64);
        }
        break;
      case FB_FLOAT_32:
        if (!(err = sk_fixrec_get_float32(fixrec, field, &data.f))) {
            lua_pushnumber(L, data.f);
        }
        break;
      case FB_FLOAT_64:
        if (!(err = sk_fixrec_get_float64(fixrec, field, &data.d))) {
            lua_pushnumber(L, data.d);
        }
        break;
      case FB_BOOL:
        if (!(err = sk_fixrec_get_boolean(fixrec, field, &data.i))) {
            lua_pushboolean(L, data.i != 0);
        }
        break;
      case FB_MAC_ADDR:
        if (!(err = sk_fixrec_get_mac_address(fixrec, field, data.buf))) {
            lua_pushlstring(L, data.str, 6);
        }
        break;
      case FB_OCTET_ARRAY:
        len = sizeof(data.buf);
        if (!(err = sk_fixrec_get_octet_array(fixrec, field, data.buf, &len)))
        {
            lua_pushlstring(L, data.str, len);
        }
        break;
      case FB_STRING:
        len = sizeof(data.buf);
        if (!(err = sk_fixrec_get_string(fixrec, field, data.str, &len)))
        {
            lua_pushlstring(L, data.str, len);
        }
        break;
      case FB_IP4_ADDR:
      case FB_IP6_ADDR:
        {
            skipaddr_t *addr = sk_lua_push_ipaddr(L);
            err = sk_fixrec_get_ip_address(fixrec, field, addr);
        }
        break;
      case FB_DT_SEC:
      case FB_DT_MILSEC:
      case FB_DT_MICROSEC:
      case FB_DT_NANOSEC:
        if (!(err = sk_fixrec_get_datetime(fixrec, field, &data.t))) {
            sktime_t *t = sk_lua_push_datetime(L);
            *t = data.t;
        }
        break;
      case FB_BASIC_LIST:
      case FB_SUB_TMPL_LIST:
      case FB_SUB_TMPL_MULTI_LIST:
        {
            sk_fixlist_t *fixlist;
            if (!(err = sk_fixrec_get_list(fixrec, field, &fixlist))) {
                sk_lua_push_fixlist(L, fixlist, 1, 0);
            }
        }
        break;
    }
    if (err) {
        return luaL_error(L, "Unable to get field %s on fixrec: %s",
                          sk_field_get_name(field), sk_schema_strerror(err));
    }

    return 1;
}

/*
 * fixrec_set_value() and fixrec __newindex metamethod (fixrec_newindex)
 *
 * =pod
 *
 * =item I<fixrec>B<[> I<field> | I<position> | I<name> | I<table> B<] => I<value>
 *
 * Set the value for a field in I<fixrec> to I<value>.  An alias for
 * L<silk.B<fixrec_set_value()>|/"silk.B<fixrec_set_value(>I<fixrec>, {I<field> | I<position> | I<name> | I<table>}, I<value>B<)>">.
 *
 * =item silk.B<fixrec_set_value(>I<fixrec>, {I<field> | I<position> | I<name> | I<table>}, I<value>B<)>
 *
 * Set the value for a field in I<fixrec> to I<value>.  When the
 * second argument is not a L<field|/Field>, the function uses
 * L<silk.B<schema_get_field()>|/"silk.B<schema_get_field(>I<schema>, {I<field> | I<position> | I<name> | I<table>}B<)>">
 * to get the field object from I<fixrec>'s L<schema|/Schema>.
 *
 * Raise an error if field if not present on I<fixrec>'s schema or if
 * there is an issue setting the field to I<value>.
 *
 * =cut
 */
static int
sk_lua_fixrec_set_value(
    lua_State          *L)
{
    sk_fixrec_t *fixrec;
    sk_field_t *field;
    sk_schema_err_t err = -1;
    int succeeded;
    size_t len;
    const char *expected = NULL;
    const char *str;

    fixrec = *sk_lua_checkfixrec(L, 1);

    /* get the field; error if field not on fixrec */
    field = sk_lua_rec_canonicalize_field(L, 1, 2);
    if (NULL == field) {
        return lua_error(L);
    }

    /* Handle string entries for some specific numeric fields */
    if (lua_type(L, 3) == LUA_TSTRING) {
        void *fn = NULL;
        switch(sk_field_get_ident(field)) {
          case SILK_FLOW_TYPE:
            fn = &fn_flowtype_id;
            break;
          case SILK_FLOW_SENSOR:
            fn = &fn_sensor_id;
            break;
          default:
            break;
        }
        if (fn) {
            lua_pushvalue(L, 3);
            sk_lua_call_global(L, fn, 1, 1);
            if (lua_isinteger(L, -1)) {
                lua_replace(L, 3);
            } else {
                lua_pop(L, 1);
            }
        }
    }

    /* Push the data associated with the given field  */
    switch (sk_field_get_type(field)) {
      case FB_BOOL:
        err = sk_fixrec_set_boolean(fixrec, field, lua_toboolean(L, 3));
        break;
      case FB_UINT_8:
      case FB_UINT_16:
      case FB_UINT_32:
      case FB_UINT_64:
        {
            uint64_t u64 = (uint64_t)lua_tointegerx(L, 3, &succeeded);
            if (!succeeded) {
                expected = "number";
            } else {
                err = sk_fixrec_set_unsigned(fixrec, field, u64);
            }
        }
        break;
      case FB_INT_8:
      case FB_INT_16:
      case FB_INT_32:
      case FB_INT_64:
        {
            int64_t i64 = lua_tointegerx(L, 3, &succeeded);
            if (!succeeded) {
                expected = "number";
            } else {
                err = sk_fixrec_set_signed(fixrec, field, i64);
            }
        }
        break;
      case FB_FLOAT_32:
      case FB_FLOAT_64:
        {
            double d = lua_tonumberx(L, 3, &succeeded);
            if (!succeeded) {
                expected = "number";
            } else {
                err = sk_fixrec_set_float(fixrec, field, d);
            }
        }
        break;
      case FB_MAC_ADDR:
        {
            str = lua_tolstring(L, 3, &len);
            if (NULL == str) {
                expected = "string";
            } else if (len != 6) {
                return luaL_error(
                    L, ("Unable to set field %s on fixrec %p:"
                        " string of length 6 expected, string has length %I"),
                    sk_field_get_name(field), lua_touserdata(L, 1),
                    (lua_Integer)len);
            } else {
                err = sk_fixrec_set_mac_address(fixrec, field, (uint8_t *)str);
            }
        }
        break;
      case FB_OCTET_ARRAY:
        {
            str = lua_tolstring(L, 3, &len);
            if (NULL == str) {
                expected = "string";
            } else {
                err = sk_fixrec_set_octet_array(
                    fixrec, field, (uint8_t *)str, len);
            }
        }
        break;
      case FB_STRING:
        {
            str = lua_tolstring(L, 3, &len);
            if (NULL == str) {
                expected = "string";
            } else {
                err = sk_fixrec_set_string(fixrec, field, str);
            }
        }
        break;
      case FB_IP4_ADDR:
      case FB_IP6_ADDR:
        {
            const skipaddr_t *addr = sk_lua_toipaddr(L, 3);
            if (NULL == addr) {
                expected = "silk.ipaddr";
            } else {
                err = sk_fixrec_set_ip_address(fixrec, field, addr);
                if (err == SK_SCHEMA_ERR_NOT_IPV4) {
                    return luaL_error(
                        L, ("Unable to set field %s on fixrec %p:"
                            " ipv4 address expected, got ipv6 address"),
                        sk_field_get_name(field), lua_touserdata(L, 1));
                }
            }
        }
        break;
      case FB_DT_SEC:
      case FB_DT_MILSEC:
      case FB_DT_MICROSEC:
      case FB_DT_NANOSEC:
        {
            sktime_t *t = sk_lua_todatetime(L, 3);
            if (NULL == t) {
                expected = "silk.time";
            } else {
                err = sk_fixrec_set_datetime(fixrec, field, *t);
            }
        }
        break;
      case FB_BASIC_LIST:
      case FB_SUB_TMPL_LIST:
      case FB_SUB_TMPL_MULTI_LIST:
        {
            sk_fixlist_t **fixlist;

            fixlist = sk_lua_tofixlist(L, 3);
            if (NULL == fixlist) {
                expected = "silk.fixlist";
            } else {
                err = sk_fixrec_set_list(fixrec, field, *fixlist);
            }
        }
        break;
      default:
        break;
    }
    if (expected) {
        return luaL_error(L, ("Unable to set field %s on fixrec %p:"
                              " %s expected, got %s"),
                          sk_field_get_name(field), lua_touserdata(L, 1),
                          expected, sk_lua_typename(L, 3));
    }
    if (err) {
        return luaL_error(L, "Unable to set field %s on fixrec %p: %s",
                          sk_field_get_name(field), lua_touserdata(L, 1),
                          sk_schema_strerror(err));
    }

    return 0;
}

/*
 *    Push existing fixrec onto the stack and disable its garbage
 *    collection.
 */
void
sk_lua_push_fixrec(
    lua_State          *L,
    sk_fixrec_t        *rec)
{
    sk_fixrec_t **lrec;

    assert(rec);
    assert(sk_fixrec_get_schema(rec));

    lrec = sk_lua_newuserdata(L, sk_fixrec_t *);
    *lrec = rec;
    luaL_setmetatable(L, SK_LUA_FIXREC);

    /* uservalue table */
    lua_createtable(L, 2, 0);
    sk_lua_push_schema(L, sk_schema_clone(sk_fixrec_get_schema(rec)));
    lua_rawseti(L, -2, SKLUA_FIXREC_UVAL_SCHEMA);
    lua_pushboolean(L, 1);
    lua_rawseti(L, -2, SKLUA_FIXREC_UVAL_NO_GC);
    lua_setuservalue(L, -2);
}

/*
 *    Create a new sk_lua_silk_fixrec_t object, initialize the
 *    sk_fixrec_t it contains using the provided 'schema' (which may
 *    be NULL), set the schema entry in the fixrec's uservalue table
 *    to the schema-userdata on the Lua stack at 'schema_idx', and
 *    return the sk_fixrec_t pointer.
 */
static sk_fixrec_t *
sk_lua_fixrec_create_helper(
    lua_State          *L,
    const sk_schema_t  *schema,
    int                 schema_idx)
{
    sk_lua_silk_fixrec_t *lrec;

    schema_idx = lua_absindex(L, schema_idx);
    lrec = sk_lua_newuserdata(L, sk_lua_silk_fixrec_t);
    lrec->recp = &lrec->rec;
    sk_fixrec_init(lrec->recp, schema);
    luaL_setmetatable(L, SK_LUA_FIXREC);

    /* uservalue table */
    lua_createtable(L, 1, 0);
    lua_pushvalue(L, schema_idx);
    lua_rawseti(L, -2, SKLUA_FIXREC_UVAL_SCHEMA);
    lua_setuservalue(L, -2);

    return lrec->recp;
}

/*
 * =pod
 *
 * =item silk.B<fixrec(>I<schema>[, I<table>]B<)>
 *
 * Create a zeroed-out fixrec with the L<schema|/Schema> given in
 * I<schema>.
 *
 * When the optional I<table> argument is provided, for each
 * I<key>,I<value> pair in the table call
 * B<fixrec_set_value(>I<fixrec>,I<key>,I<value>B<)>.  Destroy the
 * fixrec and raise an error if any field on the fixrec cannot be set.
 *
 * =cut
 */
int
sk_lua_fixrec_create(
    lua_State          *L)
{
    sk_schema_t **schema;
    int rec_index;
    int have_table;

    /* very arguments and check for the second argument */
    schema = sk_lua_checkschema(L, 1);
    have_table = !lua_isnoneornil(L, 2);
    if (have_table) {
        luaL_checktype(L, 2, LUA_TTABLE);
    }

    /* Create a zeroed-out fixrec using the schema */
    sk_lua_fixrec_create_helper(L, *schema, 1);
    if (!have_table) {
        return 1;
    }
    rec_index = lua_gettop(L);

    /* optional second argument is a table of field/value pairs to set
     * on the fixrec */
    for (lua_pushnil(L); lua_next(L, 2) != 0; lua_pop(L, 1)) {
        /* key and value are on the stack.  push fixrec_set_value(),
         * the fixrec, the key, and the value */
        lua_pushcfunction(L, sk_lua_fixrec_set_value);
        lua_pushvalue(L, rec_index);
        lua_pushvalue(L, -4);
        lua_pushvalue(L, -4);
        lua_call(L, 3, 0);
    }

    return 1;
}

static int
sk_lua_schemamap_gc(
    lua_State          *L)
{
    sk_schemamap_t **map = (sk_schemamap_t **)lua_touserdata(L, 1);
    sk_schemamap_destroy(*map);
    return 0;
}

/*
 *    Push a Lua representation of the sk_schemamap_t 'map' onto the
 *    stack.
 */
static void
sk_lua_push_schemamap(
    lua_State          *L,
    sk_schemamap_t     *map)
{
    sk_schemamap_t **map_ptr = sk_lua_newuserdata(L, sk_schemamap_t *);
    *map_ptr = map;
    lua_createtable(L, 0, 1);
    lua_pushcfunction(L, sk_lua_schemamap_gc);
    lua_setfield(L, -2, "__gc");
    lua_setmetatable(L, -2);
}


/*
 *    Expect three arguments on the Lua stack (key1, key2, value) and
 *    take as a parameter the index of a table on the stack.  Add an
 *    entry to the table that maps the keys to the value, and remove
 *    all three elements from the stack.
 *
 *    The cache is implemented as a hash-table of hash-tables.  The
 *    outer hash table is the table at 'cache_index', and it is
 *    indexed by key1.  The inner hash tables are created as required;
 *    its key is 'key2'.
 */
static void
sk_lua_dcache_put(
    lua_State          *L,
    int                 cache_index)
{
    int idx = lua_gettop(L) - 2;
    cache_index = lua_absindex(L, cache_index);
    /* Check for key1 in 'cache_index' */
    lua_pushvalue(L, idx);
    if (lua_gettable(L, cache_index) == LUA_TNIL) {
        /* Create an inner table */
        lua_pop(L, 1);
        sk_lua_create_weaktable(L, "kv");
        /* Add key1->table entry to cache_index */
        lua_pushvalue(L, idx);
        lua_pushvalue(L, -2);
        lua_settable(L, cache_index);
    }
    /* Add key2->value to inner table */
    lua_pushvalue(L, idx + 1);
    lua_pushvalue(L, idx + 2);
    lua_settable(L, -3);
    /* Clear the stack */
    lua_settop(L, idx - 1);
}

/*
 *    Expect two arguments on the Lua stack (key1, key2) and take as a
 *    parameter the index of a table on the statck.  Remove the keys
 *    from the stack and push onto the stack the value in the table
 *    that is indexed by the two keys.  Push nil if no value exists.
 *
 *    The cache is a hash-table of hash-tables.  See
 *    sk_lua_dcache_put().
 */
static void
sk_lua_dcache_get(
    lua_State          *L,
    int                 cache_index)
{
    int idx = lua_gettop(L) - 1;
    cache_index = lua_absindex(L, cache_index);
    /* Check for key1 in 'cache_index' */
    lua_pushvalue(L, idx);
    if (lua_gettable(L, cache_index) != LUA_TNIL) {
        /* Check for key2 in the inner hash table */
        lua_pushvalue(L, idx + 1);
        lua_gettable(L, -2);
    }
    /* Put result an only entry on the stack */
    lua_replace(L, idx);
    lua_settop(L, idx);
}

/*
 * =pod
 *
 * =item silk.B<fixrec_copy(>I<fixrec>[, I<schema> | I<dest_fixrec>]B<)>
 *
 * Return a copy of I<fixrec>.
 *
 * When only one argument is present, a new fixrec is created that
 * conforms to I<fixrec>'s L<schema|/Schema>, the data from I<fixrec>
 * is copied into the new fixrec, and the new fixrec is returned.
 *
 * With the optional I<schema> argument, a new fixrec is created that
 * conforms to that L<schema|/Schema> object, the data is copied
 * between the two fixrecs, and the new fixrec is returned.
 *
 * With the optional I<dest_fixrec> argument, which may have a
 * different underlying schema than I<fixrec>, the data from I<fixrec>
 * is copied into I<dest_fixrec>, and I<dest_fixrec> is returned.
 *
 * When copying across different schemas, the function takes each
 * field in the destination schema and attempts to find a matching
 * field in I<fixrec>'s schema.  If such a field is found, the value
 * of that field in I<fixrec> is copied into the destination fixrec.
 * If there are multiple instances of a field, the first field in
 * I<fixrec> is copied to the first destination field, the second to
 * the second, et cetera.  Fields in I<dest_fixrec> that are not found
 * in I<fixrec> are left untouched.  If any destination field is
 * smaller than the matching source field, the value is truncated.
 *
 * =cut
 */
static int
sk_lua_fixrec_copy(
    lua_State          *L)
{
    sk_fixrec_t *rec;
    sk_fixrec_t *dest;
    int dest_schema_idx;
    sk_schema_t **dest_schema;
    sk_schemamap_t **map;
    sk_schemamap_t *dest_map;

    rec = *sk_lua_checkfixrec(L, 1);
    if (lua_isnoneornil(L, 2)) {
        /* No destination fixrec or schema: copy the fixrec by getting
         * the source's schema-userdata, creating a new fixrec, and
         * copying the data from source to the destination. */
        lua_getuservalue(L, 1);
        lua_rawgeti(L, -1, SKLUA_FIXREC_UVAL_SCHEMA);
        dest = sk_lua_fixrec_create_helper(L, NULL, 3);
        sk_fixrec_copy_into(dest, rec);
        return 1;
    }
    /* Else a schema or fixrec was supplied as a second argument */

    dest = NULL;
    dest_schema = sk_lua_toschema(L, 2);
    if (NULL != dest_schema) {
        /* Second argument is schema */
        dest_schema_idx = 2;
    } else {
        /* Second argument must be a fixrec */
        sk_fixrec_t **dest_udata;
        dest_udata = sk_lua_tofixrec(L, 2);
        if (NULL == dest_udata) {
            return sk_lua_argerror(
                L, 2, "silk.schema, silk.fixrec, or nil expected, got %s",
                sk_lua_typename(L, 2));
        }
        dest = *dest_udata;
        lua_pushcfunction(L, sk_lua_fixrec_get_schema);
        lua_pushvalue(L, 2);
        lua_call(L, 1, 1);
        dest_schema = (sk_schema_t **)lua_touserdata(L, -1);
        dest_schema_idx = lua_gettop(L);
    }
    lua_getfield(L, LUA_REGISTRYINDEX, SK_LUA_SCHEMAMAP_CACHE);
    /* Call "fixrec_get_schema(rec)" on the source fixrec */
    lua_pushcfunction(L, sk_lua_fixrec_get_schema);
    lua_pushvalue(L, 1);
    lua_call(L, 1, 1);
    /* Push another reference to the source schema and a reference to
     * the destination schema, check for an entry in the
     * schemamap-cache */
    lua_pushvalue(L, -1);
    lua_pushvalue(L, dest_schema_idx);
    sk_lua_dcache_get(L, -4);
    if (!lua_isnil(L, -1)) {
        /* Use the cached map */
        map = (sk_schemamap_t **)lua_touserdata(L, -1);
        lua_pop(L, 3);
    } else {
        /* Create a new map and cache it */
        lua_pop(L, 1);
        lua_pushvalue(L, dest_schema_idx); /* to schema*/
        sk_schemamap_create_across_schemas(
            &dest_map, *dest_schema, sk_fixrec_get_schema(rec));
        map = &dest_map;
        sk_lua_push_schemamap(L, dest_map);
        sk_lua_dcache_put(L, -4);
        lua_pop(L, 1);
    }
    if (NULL != dest) {
        /* If we have a destination fixrec, push it*/
        lua_pushvalue(L, 2);
    } else {
        /* Create a destination fixrec using the schema that was
         * provided as the second argument. */
        dest = sk_lua_fixrec_create_helper(L, *dest_schema, 2);
    }
    sk_schemamap_apply(*map, dest, rec);
    return 1;
}

/**
 *    map = internal.schemamap_create(table, src_schema, dest_schema)
 *
 *    Create a schemamap based on a dest -> src table, where each dest
 *    and src are an IE identifier as used by the schema()
 *    constructor.
 *
 *    This is an "internal" lua function that is used by the exported
 *    fixrec_copier() function in silk-schema.lua.
 */
static int
sk_lua_schemamap_create(
    lua_State          *L)
{
    const sk_schema_t *to_schema, *from_schema;
    const sk_field_t *to_field, *from_field;
    fbInfoModel_t *model;
    sk_schemamap_t *map;
    sk_vector_t *vec;
    sk_schema_err_t rv;

    luaL_checktype(L, 1, LUA_TTABLE);
    to_schema = *sk_lua_checkschema(L, 2);
    from_schema = *sk_lua_checkschema(L, 3);
    vec = sk_vector_create(sizeof(const sk_field_t *));
    /* fprintf(stderr, ("%s:%d: pushing protected pointer %p" */
    /*                  " and free_fn %p\n"), */
    /*         __FILE__, __LINE__, (void *)vec, (void *)&sk_vector_destroy); */
    sk_lua_push_protected_pointer(
        L, vec, (sk_lua_free_fn_t)sk_vector_destroy);

    model = sk_lua_get_info_model(L, 0);
    lua_pop(L, 1);

    lua_pushnil(L);
    while (lua_next(L, 1) != 0) {
        fbInfoElement_t ie_key;
        fbInfoElement_t ie_value;
        lua_pushvalue(L, -2);
        convert_argument_to_ie_table(L, -2);
        convert_argument_to_ie_table(L, -1);
        convert_ie_table_to_ie(L, -2, model, &ie_value);
        convert_ie_table_to_ie(L, -1, model, &ie_key);

        if ((to_field = sk_schema_get_field_by_ident(
                 to_schema, SK_FIELD_IDENT_CREATE(ie_key.ent, ie_key.num),
                 NULL)))
        {
            if ((from_field = sk_schema_get_field_by_ident(
                     from_schema, SK_FIELD_IDENT_CREATE(
                         ie_value.ent, ie_value.num), NULL)))
            {
                sk_vector_append_value(vec, &from_field);
                sk_vector_append_value(vec, &to_field);
            }
        }
        lua_pop(L, 2);
    }
    rv = sk_schemamap_create_across_fields(&map, vec);
    if (rv != 0 && rv != SK_SCHEMA_ERR_TRUNCATED) {
        lua_pushnil(L);
        lua_pushliteral(L, "Invalid schema mapping");
        return 2;
    }
    sk_lua_push_schemamap(L, map);
    if (rv == SK_SCHEMA_ERR_TRUNCATED) {
        lua_pushliteral(L, "At least one field in the map may be truncated");
        return 2;
    }
    return 1;
}

/**
 *    internal.schemamap_apply(map, from_rec, to_rec)
 *
 *    Apply a schemamap created by schemamap_create() to the given
 *    fixrecs.  The map must have been created using the same schemas
 *    used by from_rec and to_rec.
 *
 *    This is an "internal" lua function that is used by the exported
 *    fixrec_copier() function in silk-schema.lua.
 */
static int
sk_lua_schemamap_apply(
    lua_State          *L)
{
    const sk_schemamap_t **map;
    const sk_fixrec_t *from;
    sk_fixrec_t *to;
    sk_schema_err_t rv;

    map = (const sk_schemamap_t **)lua_touserdata(L, 1);
    if (NULL == map) {
        return sk_lua_argerror(L, 1, "silk.schemamap expected, got %s",
                               sk_lua_typename(L, 1));
    }
    from = *sk_lua_checkfixrec(L, 2);
    to = *sk_lua_checkfixrec(L, 3);
    rv = sk_schemamap_apply(*map, to, from);
    if (rv != 0) {
        return luaL_error(L, "could not apply schemamap: %s",
                          sk_schema_strerror(rv));
    }
    return 1;
}

/**
 *    internal.field_to_name(field)
 *
 *    Given an ie identifer (as in the schema() constructor), return
 *    the ie name.
 *
 *    This is an "internal" lua function that is used by the exported
 *    fixrec_copier() function in silk-schema.lua.
 */
static int
sk_lua_field_to_name(
    lua_State          *L)
{
    fbInfoModel_t *model;
    fbInfoElement_t ie;

    model = sk_lua_get_info_model(L, 0);
    lua_pop(L, 1);

    luaL_checkany(L, 1);
    convert_argument_to_ie_table(L, 1);
    convert_ie_table_to_ie(L, 1, model, &ie);
    lua_pushstring(L, ie.ref.name);
    return 1;
}

/*
 * =pod
 *
 * =item silk.B<infomodel_augment(>I<ie_sequence>B<)>
 *
 * Modify the global information model to include the information
 * elements (IEs) described in I<ie_sequence>.
 *
 * Each element in the sequence should be a table, and each table may
 * contain the following keys and values (the B<name>, B<elementId>,
 * B<length>, and B<dataType> keys are required):
 *
 * =over 4
 *
 * =item name
 *
 * The canonical name of the IE.  Required.
 *
 * =item elementId
 *
 * The element identifier as an integer between 1 and 61439 inclusive.
 * Required.
 *
 * =item enterpriseId
 *
 * The Private Enterprise Number (PEN) as an integer between 0 and
 * 4294967295 inclusive.  A value of 0 or B<nil> is equivalent to
 * having no PEN---that is, the standard information model.  Defaults
 * to 0.
 *
 * =item length
 *
 * The length of this element as a number of octets or the string
 * C<varlen> for a variable sized element.  Required.
 *
 * =item dataType
 *
 * The data type of the IE.  Required.  Must be one of the following
 * strings:
 *
 * =over 4
 *
 * =item *
 *
 * octetArray
 *
 * =item *
 *
 * unsigned8
 *
 * =item *
 *
 * unsigned16
 *
 * =item *
 *
 * unsigned32
 *
 * =item *
 *
 * unsigned64
 *
 * =item *
 *
 * signed8
 *
 * =item *
 *
 * signed16
 *
 * =item *
 *
 * signed32
 *
 * =item *
 *
 * signed64
 *
 * =item *
 *
 * float32
 *
 * =item *
 *
 * float64
 *
 * =item *
 *
 * boolean
 *
 * =item *
 *
 * macAddress
 *
 * =item *
 *
 * string
 *
 * =item *
 *
 * dateTimeSeconds
 *
 * =item *
 *
 * dateTimeMilliseconds
 *
 * =item *
 *
 * dateTimeMicroseconds
 *
 * =item *
 *
 * dateTimeNanoseconds
 *
 * =item *
 *
 * ipv4Address
 *
 * =item *
 *
 * ipv6Address
 *
 * =item *
 *
 * basicList
 *
 * =item *
 *
 * subTemplateList
 *
 * =item *
 *
 * subTemplateMultiList
 *
 * =back
 *
 * =item description
 *
 * A textual description of the IE.  Defaults to no description.
 *
 * =item dataTypeSemantics
 *
 * The data type semantic value for the IE.  Defaults to having no
 * semantic value (C<default>).  If specified, must be one of the
 * following strings:
 *
 * =over 4
 *
 * =item *
 *
 * default
 *
 * =item *
 *
 * quantity
 *
 * =item *
 *
 * totalCounter
 *
 * =item *
 *
 * deltaCounter
 *
 * =item *
 *
 * identifier
 *
 * =item *
 *
 * flags
 *
 * =item *
 *
 * list
 *
 * =back
 *
 * =item units
 *
 * The units that the given quantity or counter represents.  Defaults
 * to not having a units value (C<none>).  If specified, must be one
 * of the following strings:
 *
 * =over 4
 *
 * =item *
 *
 * none
 *
 * =item *
 *
 * bits
 *
 * =item *
 *
 * octets
 *
 * =item *
 *
 * packets
 *
 * =item *
 *
 * flows
 *
 * =item *
 *
 * seconds
 *
 * =item *
 *
 * milliseconds
 *
 * =item *
 *
 * microseconds
 *
 * =item *
 *
 * nanoseconds
 *
 * =item *
 *
 * fourOctetWords (``4-octet words'' in RFC5610)
 *
 * =item *
 *
 * messages
 *
 * =item *
 *
 * hops
 *
 * =item *
 *
 * entries
 *
 * =item *
 *
 * frames
 *
 * =back
 *
 * =item rangemin
 *
 * The minimum numeric value for this element.  Defaults to 0.  Not
 * useful for non-numeric elements or without a corresponding non-zero
 * rangemax item.
 *
 * =item rangemax
 *
 * The maximum numeric value for this element.  Defaults to 0.  Not
 * useful for non-numeric elements.
 *
 * =item endian
 *
 * Whether endianness needs to be considered when transcoding this
 * element.  This should be either B<true> or B<false>.  Defaults to
 * an appropriate value based on the dataType.
 *
 * =item reversible
 *
 * Whether this element can have a reverse value (as in a bi-flow).
 * This should be either B<true> or B<false>.  Defaults to B<false>.
 *
 * =back
 *
 * =cut
 */
static int
sk_lua_infomodel_augment(
    lua_State          *L)
{
    int i;
    int limit;
    fbInfoModel_t *model = NULL;
    fbInfoElement_t ie;
    uint32_t num;

    model = sk_lua_get_info_model(L, 0);
    luaL_checktype(L, 1, LUA_TTABLE);
    limit = luaL_len(L, 1);
    if (0 == limit) {
        /* check whether the user provided a single table instead of a
         * sequence of tables */
        lua_pushnil(L);
        if (lua_next(L, 1) != 0) {
            /* the argument that should be a sequence is a table
             * containing key,value pairs */
            luaL_error(L, "expected a sequence of tables");
        }
        /* the sequence argument is empty */
    }
    for (i = 1; i <= limit; ++i) {
        memset(&ie, 0, sizeof(fbInfoElement_t));

        lua_pushinteger(L, i);
        lua_gettable(L, 1);
        luaL_checktype(L, -1, LUA_TTABLE);
        sk_lua_call_global(L, &fn_normalize_ie, 1, 1);

        ie.flags = 0;

        /* Name */
        lua_getfield(L, -1, "name");
        ie.ref.name = lua_tostring(L, -1);
        lua_pop(L, 1);

        /* elementId */
        lua_getfield(L, -1, "elementId");
        ie.num = (lua_Unsigned)lua_tointeger(L, -1);
        lua_pop(L, 1);

        /* enterpriseId */
        lua_getfield(L, -1, "enterpriseId");
        ie.ent = (lua_Unsigned)lua_tointeger(L, -1);
        lua_pop(L, 1);

        /* description */
        lua_getfield(L, -1, "description");
        ie.description = lua_tostring(L, -1);
        lua_pop(L, 1);

        /* dataType */
        lua_getfield(L, -1, "dataType");
        ie.type = luaL_checkoption(L, -1, NULL, sk_lua_ie_type_names);
        lua_pop(L, 1);

        /* dataTypeSemantics */
        lua_getfield(L, -1, "dataTypeSemantics");
        num = luaL_checkoption(L, -1, "default", sk_lua_ie_semantic_names);
        ie.flags |= num << 8;
        lua_pop(L, 1);

        /* units */
        lua_getfield(L, -1, "units");
        num = luaL_checkoption(L, -1, "none", sk_lua_ie_semantic_units);
        ie.flags |= num << 16;
        lua_pop(L, 1);

        /* rangemin */
        lua_getfield(L, -1, "rangemin");
        ie.min = lua_tonumber(L, -1);
        lua_pop(L, 1);

        /* rangemax */
        lua_getfield(L, -1, "rangemax");
        ie.max = lua_tonumber(L, -1);
        lua_pop(L, 1);

        /* endian */
        lua_getfield(L, -1, "endian");
        if (lua_toboolean(L, -1)) {
            ie.flags |= FB_IE_F_ENDIAN;
        }
        lua_pop(L, 1);

        /* reversible */
        lua_getfield(L, -1, "reversible");
        if (lua_toboolean(L, -1)) {
            ie.flags |= FB_IE_F_REVERSIBLE;
        }
        lua_pop(L, 1);

        /* length */
        lua_getfield(L, -1, "length");
        ie.len = (lua_Unsigned)lua_tointeger(L, -1);
        lua_pop(L, 1);

        fbInfoModelAddElement(model, &ie);
    }
    return 0;
}


/*
 *  ******************************************************************
 *  Fixrec List (fixlist)
 *  ******************************************************************
 */

/*
 * This function is automatically generated from within Lua, so
 * document it here.
 *
 * =pod
 *
 * =item silk.B<fixlist_to_string(>I<fixlist>B<)>
 *
 * Return a unique string designating I<fixlist>.
 *
 * =cut
 */

/*
 *    Garbage collection function for fixlist userdata.
 */
static int
sk_lua_fixlist_gc(
    lua_State          *L)
{
    sk_fixlist_t **fixlist = (sk_fixlist_t **)lua_touserdata(L, 1);
    sk_fixlist_destroy(*fixlist);
    return 0;
}

/*
 *    Create a new Lua representation of the sk_fixlist_t 'fixlist'
 *    and push it onto the stack.
 *
 *    The values at 'rec_idx' and 'schema_idx' are stored on the
 *    uservalue for the fixlist userdata.
 *
 *    'rec_idx' is the index in the Lua stack of the fixrec that
 *    contains 'fixlist', or 0 if none---that is, when the user is
 *    creating a new fixlist from within Lua.
 *
 *    'schema_idx' is the index in the Lua stack of the schema used by
 *    'fixlist'.
 *
 *    When 'schema_idx' is 0 and 'rec_idx' is non-zero, a
 *    schema-userdata is created for the fixlist's schema and stored
 *    in the fixlists's uservalue.
 *
 *    When 'schema_idx' is 0 and 'rec_idx' is 0, the code assumes the
 *    user is creating a fixlist that represents a
 *    subTemplateMultiList.  When a fixlist-userdata represents a
 *    STML, the fixlist stores a zero in the schema entry of its
 *    uservalue.
 */
static void
sk_lua_push_fixlist(
    lua_State          *L,
    sk_fixlist_t       *fixlist,
    int                 rec_idx,
    int                 schema_idx)
{
    sk_fixlist_t **fixlist_udata;
    const sk_schema_t *schema;
    int pushed_schema = 0;

    assert(fixlist);
    fixlist_udata = sk_lua_newuserdata(L, sk_fixlist_t *);
    *fixlist_udata = fixlist;
    luaL_setmetatable(L, SK_LUA_FIXLIST);
    /* create uservalue table */
    lua_createtable(L, SKLUA_FIXLIST_UVAL_TABLE_LEN, 0);
    if (rec_idx) {
        assert(rec_idx > 0);
        lua_pushvalue(L, rec_idx);
        lua_rawseti(L, -2, SKLUA_FIXLIST_UVAL_FIXREC);
        if (0 == schema_idx
            && sk_fixlist_get_type(fixlist) != FB_SUB_TMPL_MULTI_LIST)
        {
            schema = sk_fixlist_get_schema(fixlist, 0);
            assert(schema);
            sk_lua_push_schema(L, sk_schema_clone(schema));
            schema_idx = lua_gettop(L);
            pushed_schema = 1;
        }
    }
    if (!schema_idx) {
        /* this is a subTemplateMultiList; push a zero onto the stack
         * as its schema */
        assert(sk_fixlist_get_type(fixlist) == FB_SUB_TMPL_MULTI_LIST);
        lua_pushinteger(L, 0);
    } else if (!pushed_schema) {
        /* push the schema_idx passed into the function */
        assert(schema_idx > 0);
        lua_pushvalue(L, schema_idx);
    }
    lua_rawseti(L, -2, SKLUA_FIXLIST_UVAL_SCHEMA);
    lua_setuservalue(L, -2);
}

/*
 *    Helper function for sk_lua_fixlist_create().
 *
 *    Take the argument list of elements and create a schema from them
 *    by replacing the list-type argument (at position 1) in the stack
 *    with schema_create() and calling it.
 */
static sk_schema_t **
sk_lua_fixlist_create_make_schema(
    lua_State          *L)
{
    sk_schema_t **schema;

    lua_pushcfunction(L, sk_lua_schema_create);
    lua_replace(L, 1);
    lua_call(L, lua_gettop(L)-1, 1);
    schema = sk_lua_toschema(L, -1);
    if (!schema) {
        luaL_error(L, "silk.schema expected, got %s", sk_lua_typename(L, -1));
    }
    return schema;
}

/*
 * =pod
 *
 * =item silk.B<fixlist(>I<list_type>[, I<schema> | I<elem>, I<elem>...]B<)>
 *
 * Create a fixlist (a fixrec list).  The I<list_type> argument is a
 * string specifying the type of list to create, and must be one of
 *
 * =over 4
 *
 * =item subTemplateMultiList
 *
 * Create a list where the elements in the list may have different
 * schemas.  No additional arguments are permitted.
 *
 * =item subTemplateList
 *
 * Create a list where all elements in the list use the same schema.  One
 * or more arguments are required.
 *
 * =item basicList
 *
 * Create a list where all elements are the same type of information
 * element (IE).  One additional argument is required.
 *
 * =back
 *
 * When creating a subTemplateList, the schema to be used by the elements
 * in the list must be specified by providing either a single
 * L<schema|/Schema> argument or the elements needed to create a new
 * schema.
 *
 * When creating a basicList, the IE of the elements in the list must be
 * specified by providing either a single I<elem> argument or a schema
 * argument that contains a single IE.
 *
 * The form of each I<elem> argument is the same as that used by
 * L<silk.B<schema()>|/"silk.B<schema(>[I<elem>[, ...]]B<)>">.
 *
 * =cut
 */
static int
sk_lua_fixlist_create(
    lua_State          *L)
{
    sk_fixlist_t *fixlist;
    sk_schema_t **schema;
    sk_schema_err_t err;
    fbInfoModel_t *model;
    int schema_idx;
    int list_type;

    list_type = luaL_checkoption(L, 1, NULL, sk_lua_list_type_names);
    switch (list_type) {
      case SKLUA_LISTTYPE_STML:
        /* subTemplateMultiList */
        if (lua_gettop(L) != 1) {
            return luaL_error(
                L, "must specify only the list type when creating a %s",
                sk_lua_list_type_names[list_type]);
        }
        /* get the information model */
        model = sk_lua_get_info_model(L, 0);
        lua_pop(L, 1);
        err = sk_fixlist_create_subtemplatemultilist(&fixlist, model);
        if (err) {
            return luaL_error(
                L, "error creating %s: %s",
                sk_lua_list_type_names[list_type], sk_schema_strerror(err));
        }
        sk_lua_push_fixlist(L, fixlist, 0, 0);
        break;

      case SKLUA_LISTTYPE_STL:
        /* subTemplateList */
        if (lua_gettop(L) == 1) {
            /* protect against creating a subTemplateList that uses an
             * empty schema */
            return luaL_error(
                L, "must specify at least two arguments when creating a %s",
                sk_lua_list_type_names[list_type]);
        }
        schema_idx = 2;
        schema = sk_lua_toschema(L, schema_idx);
        if (!schema) {
            /* make a schema from args 2..N */
            schema = sk_lua_fixlist_create_make_schema(L);
            assert(1 == lua_gettop(L));
            schema_idx = 1;
        }
        err = sk_fixlist_create_subtemplatelist(&fixlist, *schema);
        if (err) {
            return luaL_error(
                L, "error creating %s: %s",
                sk_lua_list_type_names[list_type], sk_schema_strerror(err));
        }
        sk_lua_push_fixlist(L, fixlist, 0, schema_idx);
        break;

      case SKLUA_LISTTYPE_BL:
        /* basicList */
        if (lua_gettop(L) != 2) {
            return luaL_error(
                L, "must specify exactly two arguments when creating a %s",
                sk_lua_list_type_names[list_type]);
        }
        schema = sk_lua_toschema(L, 2);
        if (!schema) {
            /* FIXME: avoid creating this temporary schema */
            /* make a schema from arg 2 */
            schema = sk_lua_fixlist_create_make_schema(L);
        } else if (1 != sk_schema_get_count(*schema)) {
            return sk_lua_argerror(
                L, 2,
                "schema must have a single element when creating a basicList");
        }
        err = (sk_fixlist_create_basiclist_from_ident(
                   &fixlist, sk_schema_get_infomodel(*schema),
                   sk_field_get_ident(sk_schema_get_field(*schema, 0))));
        if (err) {
            return luaL_error(
                L, "error creating %s: %s",
                sk_lua_list_type_names[list_type], sk_schema_strerror(err));
        }
        /* create a schema-userdata from the "fake" schema generated
         * from the basic list itself */
        sk_lua_push_schema(
            L, sk_schema_clone(sk_fixlist_get_schema(fixlist, 0)));
        sk_lua_push_fixlist(L, fixlist, 0, lua_gettop(L));
        break;

      default:
        skAbortBadCase(list_type);
    }
    return 1;
}

/*
 *
 * =pod
 *
 * =item silk.B<fixlist_append(>I<fixlist>, I<obj>[, I<obj>...]B<)>
 *
 * Append data to the fixrec list I<fixlist>.  For all list types each
 * I<obj> argument may be a L<fixrec|/Fixrec>.  When I<fixlist> is a
 * subTemplateList or a basicList, the function raises an error when the
 * L<schema|/Schema> of the fixrec I<obj> does not match the schema of
 * I<fixlist>.  Additional types for I<obj> are acceptable depending on
 * the type of I<fixlist>.
 *
 * When appending to a basicList, each I<obj> may be a fixrec or it may
 * be a value to append to the basicList.  That is, for each I<obj> that
 * is not a fixrec the function does the equivalent of creating a fixrec
 * from I<fixlist>'s schema and setting the value of the first field in
 * that fixrec to I<obj>:
 * silk.fixrec(fixlist_get_schema(I<fixlist>),{I<obj>})
 *
 * When appending to a subTemplateList, each I<obj> may be a fixrec or it
 * may be a table of key,value pairs to use when creating a fixrec based
 * on I<fixlist>'s schema.  That is, the function does the equivalent of:
 * silk.fixrec(fixlist_get_schema(I<fixlist>),I<obj>)
 *
 * When appending to a subTemplateMultiList, each I<obj> may be a fixrec
 * or it may be a sequence whose first element is a sequence of
 * information elements and whose remaining elements are tables.  The
 * first item in each I<obj> sequence is used to create a schema, and the
 * remaining tables in each I<obj> sequence are used to create fixrecs
 * based on that schema.  That is, for each I<obj>, the code does the
 * equivalent of:
 *
 *  local s = silk.schema(table.unpack(obj[1]))
 *  for i = 2, #obj do
 *    silk.fixlist_append(fixlist, silk.fixrec(s, obj[i]))
 *  end
 *
 * =cut
 */
static int
sk_lua_fixlist_append(
    lua_State          *L)
{
    sk_fixlist_t *fixlist;
    sk_fixrec_t **rec;
    size_t argc;
    size_t i;
    sk_schema_err_t err;

    /* call export.fixlist_append_normalize() to convert the arguments
     * to fixrecs and to ensure the fixrecs' schema match that of the
     * list */
    sk_lua_call_global(L, &fn_fixlist_append_normalize, lua_gettop(L), 2);

    fixlist = *sk_lua_checkfixlist(L, 1);
    luaL_checktype(L, 2, LUA_TTABLE);
    argc = lua_rawlen(L, 2);
    for (i = 1; i <= argc; ++i, lua_pop(L, 1)) {
        lua_rawgeti(L, 2, i);
        rec = sk_lua_tofixrec(L, -1);
        if (!rec) {
            return luaL_error(L, ("programmer error in table at index %d"
                                  "; silk.fixrec expected, found %s"),
                              i, sk_lua_typename(L, -1));
        }
        err = sk_fixlist_append_fixrec(fixlist, *rec);
        if (err) {
            return luaL_error(L, "unable to append %s to %s: %s",
                              luaL_tolstring(L, -1, NULL),
                              luaL_tolstring(L, 1, NULL),
                              sk_schema_strerror(err));
        }
    }
    return 0;
}

/*
 * fixlist_count_elements() and fixlist __len metamethod
 *
 * =pod
 *
 * =item B<#>I<fixlist>
 *
 * Return the number of elements in I<fixlist>.  An alias for
 * B<fixlist_count_elements()>.
 *
 * =item silk.B<fixlist_count_elements(>I<fixlist>B<)>
 *
 * Return the number of elements in I<fixlist>.
 *
 * =cut
 */
static int
sk_lua_fixlist_count_elements(
    lua_State          *L)
{
    sk_fixlist_t *fixlist;

    fixlist = *sk_lua_checkfixlist(L, 1);
    lua_pushinteger(L, sk_fixlist_count_elements(fixlist));
    return 1;
}

/*
 * =pod
 *
 * =item silk.B<fixlist_get_schema(>I<fixlist>B<)>
 *
 * Return the schema used by the elements in I<fixlist> or return
 * B<nil> if the type of I<fixlist> is C<subTemplateMultiList>.
 *
 * For a basicList, this function returns an invalid schema that
 * contains a single information element.  The schema is invalid
 * because it uses a reserved template identifier (0xFF).
 *
 * =cut
 */
static int
sk_lua_fixlist_get_schema(
    lua_State          *L)
{
#ifndef NDEBUG
    sk_fixlist_t *fixlist = *
#endif
        sk_lua_checkfixlist(L, 1);

    lua_getuservalue(L, 1);
    switch (lua_rawgeti(L, -1, SKLUA_FIXLIST_UVAL_SCHEMA)) {
      case LUA_TUSERDATA:
        lua_remove(L, -2);
        break;
      case LUA_TNUMBER:
        assert(FB_SUB_TMPL_MULTI_LIST == sk_fixlist_get_type(fixlist));
        lua_pop(L, 2);
        lua_pushnil(L);
        break;
      default:
        lua_pop(L, 1);
        skAbortBadCase(lua_rawgeti(L, -1, SKLUA_FIXLIST_UVAL_SCHEMA));
    }
    return 1;
}

/*
 * =pod
 *
 * =item silk.B<fixlist_get_semantic(>I<fixlist>B<)>
 *
 * Return a string describing the relationship among the list elements
 * in the structured data fixrec I<fixlist>.  (See RFC6313.)  The
 * returned value is one of
 *
 * =over 4
 *
 * =item noneOf
 *
 * Specifies that none of the elements are actual properties of the
 * Data Fixrec.
 *
 * =item exactlyOneOf
 *
 * Specifies that only a single element from the structured data is an
 * actual property of the Data Fixrec. This is equivalent to a logical
 * XOR operation.
 *
 * =item oneOrMoreOf
 *
 * Specifies that one or more elements from the list in the structured
 * data are actual properties of the Data Fixrec. This is equivalent
 * to a logical OR operation.
 *
 * =item allOf
 *
 * Specifies that all of the list elements from the structured data
 * are actual properties of the Data Fixrec.
 *
 * =item ordered
 *
 * Specifies that elements from the list in the structured data are
 * ordered.
 *
 * =item undefined
 *
 * Specifies that the semantic of the list elements is not specified
 * and that, if a semantic exists, then it is up to the Collecting
 * Process to draw its own conclusions. The "undefined" structured
 * data type semantic is the default structured data type semantic.
 *
 * =back
 *
 * =cut
 */
static int
sk_lua_fixlist_get_semantic(
    lua_State          *L)
{
    sk_fixlist_t *fixlist;
    uint8_t semantic;

    fixlist = *sk_lua_checkfixlist(L, 1);
    semantic = sk_fixlist_get_semantic(fixlist);

    /* names in the array are one position ahead of the semantic
     * value and "undefined" is in position 0. */
    if (FB_LIST_SEM_UNDEFINED == semantic) {
        lua_pushstring(L, sk_lua_list_semantic_names[0]);
    } else {
        ++semantic;
        if (semantic >= sk_lua_list_semantic_names_count) {
            luaL_error(
                L, "list semantics to name mapping out of range %d", semantic);
        }
        lua_pushstring(L, sk_lua_list_semantic_names[semantic]);
    }
    return 1;
}

/*
 * =pod
 *
 * =item silk.B<fixlist_set_semantic(>I<fixlist>, I<semantic>B<)>
 *
 * Specify the relationship among the list elements in the structured
 * data fixrec I<fixlist>.  The valid values for I<semantic> are given
 * in the description of B<fixlist_get_semantic()>.
 *
 * =cut
 */
static int
sk_lua_fixlist_set_semantic(
    lua_State          *L)
{
    sk_fixlist_t *fixlist;
    int semantic;

    fixlist = *sk_lua_checkfixlist(L, 1);
    semantic = luaL_checkoption(L, 2, NULL, sk_lua_list_semantic_names);

    /* names in the array are one position ahead of the semantic
     * value and "undefined" is in position 0. */
    --semantic;
    if (semantic < 0) {
        sk_fixlist_set_semantic(fixlist, FB_LIST_SEM_UNDEFINED);
    } else {
        sk_fixlist_set_semantic(fixlist, semantic);
    }
    return 0;
}

/*
 * =pod
 *
 * =item silk.B<fixlist_get_type(>I<fixlist>B<)>
 *
 * Return a string describing the type structured data in I<fixlist>.
 * The returned value is one of
 *
 * =over 4
 *
 * =item basicList
 *
 * A list where the elements in the list are a single information
 * element.
 *
 * =item subTemplateList
 *
 * A list where the elements in the list all have the same schema.
 *
 * =item subTemplateMultiList
 *
 * A list where the elements in the list may have different schemas.
 * A subTemplateMultiList may be thought of as a basicList of
 * subTemplateLists.
 *
 * =back
 *
 * =cut
 */
static int
sk_lua_fixlist_get_type(
    lua_State          *L)
{
    sk_fixlist_t *fixlist;

    fixlist = *sk_lua_checkfixlist(L, 1);
    switch (sk_fixlist_get_type(fixlist)) {
      case FB_BASIC_LIST:
        lua_pushstring(L, sk_lua_list_type_names[SKLUA_LISTTYPE_BL]);
        break;
      case FB_SUB_TMPL_LIST:
        lua_pushstring(L, sk_lua_list_type_names[SKLUA_LISTTYPE_STL]);
        break;
      case FB_SUB_TMPL_MULTI_LIST:
        lua_pushstring(L, sk_lua_list_type_names[SKLUA_LISTTYPE_STML]);
        break;
      default:
        skAbortBadCase(sk_fixlist_get_type(fixlist));
    }
    return 1;
}

/*
 *
 * =pod
 *
 * =item I<fixlist>B<[> I<position> B<]>
 *
 * Return the fixrec in the fixrec list I<fixlist> at index
 * I<position>.  An alias for
 * L<B<fixlist_get_element()>|/"silk.B<fixlist_get_element(>I<fixlist>, I<position>B<)>">.
 *
 * =item silk.B<fixlist_get_element(>I<fixlist>, I<position>B<)>
 *
 * Return the fixrec in the fixrec list I<fixlist> at index
 * I<position>, where 1 is the first element.  Return B<nil> if
 * I<position> is greater than the number of elements in I<fixlist>
 *
 * =cut
 *
 */
static int
sk_lua_fixlist_get_element(
    lua_State          *L)
{
    const sk_fixrec_t *fixrec;
    sk_fixrec_t *copy;
    sk_fixlist_t *fixlist;
    sk_schema_err_t err;
    lua_Integer pos;

    fixlist = *sk_lua_checkfixlist(L, 1);
    pos = luaL_checkinteger(L, 2);
    if (pos < 1 || pos > UINT16_MAX) {
        lua_pushnil(L);
        return 1;
    }
    err = sk_fixlist_get_element(fixlist, pos-1, &fixrec);
    if (err) {
        lua_pushnil(L);
        return 1;
    }

    sk_lua_push_schema(L, sk_schema_clone(fixrec->schema));
    copy = sk_lua_fixrec_create_helper(L, NULL, lua_gettop(L));
    sk_fixrec_copy_into(copy, fixrec);
    return 1;
}

#if 0
static int
sk_lua_fixlist_set_element(
    lua_State          *L)
{
    return 0;
}
#endif  /* 0 */

/*
 * =pod
 *
 * =item silk.B<fixlist_reset_iter(>I<fixlist>B<)>
 *
 * Reset the fixrec list I<fixlist> so that B<fixlist_next_element()>
 * returns the first element in the list.
 *
 * =cut
 */
static int
sk_lua_fixlist_reset_iter(
    lua_State          *L)
{
    sk_fixlist_t *fixlist;

    fixlist = *sk_lua_checkfixlist(L, 1);
    sk_fixlist_reset_iter(fixlist);
    return 0;
}

/*
 *    A helper function for fixlist_iter() and fixlist_next_element().
 *
 *    The iterator function that returns fixrecs from a fixlist
 *    userdata.  The input values are the state (i.e., the fixlist
 *    userdata object), and a value which is ignored.
 *
 *    The function uses one upvalue, which is either the fixrec to
 *    clear and fill with the new data or nil (in which case a new
 *    fixrec is created).
 */
static int
sk_lua_fixlist_iter_func(
    lua_State          *L)
{
    sk_fixlist_t **fixlist;
    const sk_fixrec_t *rec;
    sk_fixrec_t *copy;

    fixlist = (sk_fixlist_t **)lua_touserdata(L, 1); /* 1 */
    lua_settop(L, 1);
    assert(fixlist);

    /* Get the next fixrec */
    if (sk_fixlist_next_element(*fixlist, &rec) == SK_ITERATOR_NO_MORE_ENTRIES)
    {
        /* at end of list, return nil */
        lua_pushnil(L);
        return 1;
    }

    /* Push schema */
    sk_lua_push_schema(L, sk_schema_clone(rec->schema));

    /* If the upvalue is nil, create a fixrec-userdata; otherwise
     * update the schema reference on the upvalue fixrec. Copy the
     * fixrec from the stream into the fixrec-userdata. */
    if (lua_type(L, lua_upvalueindex(1)) == LUA_TNIL) {
        copy = sk_lua_fixrec_create_helper(L, NULL, lua_gettop(L));
    } else {
        lua_pushvalue(L, lua_upvalueindex(1));
        copy = *(sk_fixrec_t **)lua_touserdata(L, -1);
        lua_getuservalue(L, -1);
        lua_pushvalue(L, -3);   /* schema */
        lua_rawseti(L, -2, SKLUA_FIXREC_UVAL_SCHEMA);
        lua_pop(L, 1);
    }
    assert(copy);
    sk_fixrec_copy_into(copy, rec);

    /* Return fixrec */
    return 1;
}


/*
 *    A helper function for fixlist_iter() and fixlist_next_element().
 *
 *    Take a fixlist userdata object at stack position 1 and an
 *    optional fixrec at stack position 2.  Set the stack so that the
 *    iterator closure is at position 1 and fixlist at 2.  The
 *    optional fixrec is used as an upvalue for the closure.
 */
static void
sk_lua_fixlist_make_iter_closure(
    lua_State          *L)
{
    sk_lua_checkfixlist(L, 1);
    if (!lua_isnoneornil(L, 2)) {
        sk_lua_checkfixrec(L, 2);
        lua_pushvalue(L, 2);
    } else {
        lua_pushnil(L);
    }
    lua_pushcclosure(L, sk_lua_fixlist_iter_func, 1);
    lua_pushvalue(L, 1);
}

/*
 * =pod
 *
 * =item silk.B<fixlist_iter(>I<fixlist>[, I<fixrec>]B<)>
 *
 * Return an iterator over the L<fixrec|/Fixrec> objects in the
 * structure data I<fixlist>.  If the optional I<fixrec> is provided,
 * the iterator clears that fixrec, fills it with the new data
 * (ignoring I<fixrec>'s previous L<schema|/Schema>), and returns it
 * each time rather than creating a new fixrec.
 *
 * =cut
 */
static int
sk_lua_fixlist_iter(
    lua_State          *L)
{
    sk_lua_fixlist_reset_iter(L);
    sk_lua_fixlist_make_iter_closure(L);
    lua_pushnil(L);
    return 3;
}

/*
 * =pod
 *
 * =item silk.B<fixlist_next_element(>I<fixlist>[, I<fixrec>]B<)>
 *
 * Return the next element from the structured data I<fixlist> as a
 * L<fixrec|/Fixrec>, or return B<nil> if all elements from I<fixlist>
 * have been seen.  If the optional I<fixrec> is specified, the
 * function clears that fixrec (ignoring I<fixrec>'s previous
 * L<schema|/Schema>), fills it with the new data, and returns it.
 * Use B<fixlist_reset_iter()> to process the elements in I<fixlist>
 * again.
 *
 * =cut
 */
static int
sk_lua_fixlist_next_element(
    lua_State          *L)
{
    /* create a closure over sk_lua_fixlist_iter_func() and then call
     * it */
    sk_lua_fixlist_make_iter_closure(L);
    lua_call(L, 1, 1);
    return 1;
}


/*
 *  ******************************************************************
 *  Stream
 *  ******************************************************************
 */

/*
 * This function is automatically generated from within Lua, so
 * document it here.
 *
 * =pod
 *
 * =item silk.B<stream_to_string(>I<stream>B<)>
 *
 * Return a unique string designating I<stream>.
 *
 * =cut
 */


/* Garbage collection function for a stream userdata value. */
static int
sk_lua_stream_gc(
    lua_State          *L)
{
    sk_lua_stream_t *stream;
    sk_lua_stream_newschema_t *state;

    /* if the stream has a uservalue, remove the value it references
     * from the Lua registry */
    if (lua_getuservalue(L, 1) == LUA_TLIGHTUSERDATA
        && (state = (sk_lua_stream_newschema_t*)lua_touserdata(L, -1)) != NULL)
    {
        luaL_unref(L, LUA_REGISTRYINDEX, state->ref);
        free(state);
    }
    lua_pop(L, 1);
    stream = (sk_lua_stream_t *)lua_touserdata(L, 1);
    if (stream) {
        if (stream->is_ipfix) {
            sk_fixstream_destroy(&stream->stream.fix);
        } else {
            skStreamDestroy(&stream->stream.rw);
        }
    }
    return 0;
}

static int
sk_lua_stream_error(
    lua_State          *L,
    sk_lua_stream_t    *stream,
    int                 errcode,
    const char         *msg)
{
    char errbuf[2 * PATH_MAX];

    if (NULL == msg) {
        msg = "Stream error";
    }
    if (stream->is_ipfix
        && stream->stream.fix)
    {
        return luaL_error(L, "%s: %s",
                          msg, sk_fixstream_strerror(stream->stream.fix));
    }
    skStreamLastErrMessage(stream->stream.rw, errcode, errbuf, sizeof(errbuf));
    return luaL_error(L, "%s: %s", msg, errbuf);
}


/*
 *    Check that the object at position arg on the Lua stack is a
 *    stream and that its mode matches that in 'mode'.  Return the
 *    stream on succcess; raise an error if not.
 */
static sk_lua_stream_t *
sk_lua_stream_check_mode(
    lua_State          *L,
    int                 arg,
    skstream_mode_t     mode)
{
    sk_lua_stream_t *stream;
    skstream_t *s;

    stream = sk_lua_checkstream(L, arg);
    s = ((stream->is_ipfix)
         ? sk_fixstream_get_stream(stream->stream.fix)
         : stream->stream.rw);
    if (mode != skStreamGetMode(s)) {
        sk_lua_argerror(L, arg, "expected stream open for %s",
                        (SK_IO_WRITE == mode) ? "writing" : "reading");
        return NULL;
    }
    return stream;
}



/*
 *    The iterator function that returns fixrecs from a stream
 *    userdata.  The input values are the state (which is the stream
 *    userdata object), and a value which is ignored.
 *
 *    The function uses one upvalue, which is either the fixrec to
 *    clear and fill with the new data or nil (in which case a new
 *    fixrec is created).
 *
 *    See also sk_lua_stream_iter_func_rwrec().
 */
static int
sk_lua_stream_iter_func_fixrec(
    lua_State          *L)
{
    sktime_t export_time;
    sktime_t *t;
    sk_lua_stream_t *stream;
    const sk_fixrec_t *rec;
    sk_fixrec_t *copy;
    int rv;

    stream = (sk_lua_stream_t *)lua_touserdata(L, 1);
    lua_settop(L, 1);

    /* Read the fixrec from the stream*/
    rv = sk_fixstream_read_record(stream->stream.fix, &rec);
    if (rv) {
        /* Handle error conditions */
        if (rv == SKSTREAM_ERR_EOF) {
            /* If EOF, return nil */
            lua_pushnil(L);
            return  1;
        }
        return sk_lua_stream_error(L, stream, rv, "Stream read error");
    }

    /* Push schema */
    sk_lua_push_schema(L, sk_schema_clone(rec->schema));

    /* If the upvalue is nil, create a fixrec-userdata; otherwise
     * update the schema reference on the upvalue fixrec. Copy the
     * fixrec from the stream into the fixrec-userdata. */
    if (lua_type(L, lua_upvalueindex(1)) == LUA_TNIL) {
        copy = sk_lua_fixrec_create_helper(L, NULL, lua_gettop(L));
    } else {
        lua_pushvalue(L, lua_upvalueindex(1));
        copy = *(sk_fixrec_t **)lua_touserdata(L, -1);
        lua_getuservalue(L, -1);
        lua_pushvalue(L, -3);   /* schema */
        lua_rawseti(L, -2, SKLUA_FIXREC_UVAL_SCHEMA);
        lua_pop(L, 1);
    }
    assert(copy);
    sk_fixrec_copy_into(copy, rec);

    export_time = sk_fixstream_get_last_export_time(stream->stream.fix);
    if (export_time == -1) {
        /* Return fixrec */
        return 1;
    }
    t = sk_lua_push_datetime(L);
    *t = export_time;

    /* Return fixrec and export time */
    return 2;
}

/*
 *    Similar to sk_lua_stream_iter_func_fixrec(), but works on rwrec
 *    instead of fixrec.
 */
static int
sk_lua_stream_iter_func_rwrec(
    lua_State          *L)
{
    sk_lua_stream_t *stream;
    rwRec *rwrec;
    int rv;

    stream = (sk_lua_stream_t *)lua_touserdata(L, 1);
    lua_settop(L, 1);

    /* If the upvalue is nil, create an rwRec-userdata; otherwise
     * update the schema reference on the upvalue rwRec. Copy the
     * rwRec from the stream into the rwRec-userdata. */
    if (lua_type(L, lua_upvalueindex(1)) == LUA_TNIL) {
        rwrec = sk_lua_push_rwrec(L, NULL);
    } else {
        lua_pushvalue(L, lua_upvalueindex(1));
        rwrec = (rwRec *)lua_touserdata(L, -1);
    }

    /* Read the rwRec */
    rv = skStreamReadRecord(stream->stream.rw, rwrec);
    if (rv) {
        /* Handle error conditions */
        if (rv == SKSTREAM_ERR_EOF) {
            /* If EOF, return nil */
            lua_pushnil(L);
            return  1;
        }
        return sk_lua_stream_error(L, stream, rv, "Stream read error");
    }

    /* Return record */
    return 1;
}


/*
 *    Helper function for stream_iter() and stream_read().
 *
 *    Assume Lua stack has a stream at position 1 and an optional
 *    argument at 2, where the second argument is one of the types
 *    described for sk_lua_stream_iter().
 *
 *    Modify the stack so that the appropriate C-function read closure
 *    (sk_lua_stream_iter_func_{fixrec,rwrec}) is at position 1 and
 *    the stream is at 2.  If the second argument was a record, that
 *    becomes the upvalue for the closure; otherwise nil is used as
 *    the upvalue.
 */
static void
sk_lua_stream_read_helper(
    lua_State          *L)
{
    const char *opt_strings[] = {"ipfix", "silk", NULL};
    sk_lua_stream_t *stream;
    lua_CFunction iter_func = NULL;

    stream = sk_lua_stream_check_mode(L, 1, SK_IO_READ);
    if (0 == stream->is_ipfix) {
        switch (lua_type(L, 2)) {
          case LUA_TNONE:
          case LUA_TNIL:
            iter_func = sk_lua_stream_iter_func_rwrec;
            lua_pushnil(L);
            break;

          case LUA_TSTRING:
            if (0 == luaL_checkoption(L, 2, NULL, opt_strings)) {
                iter_func = sk_lua_stream_iter_func_fixrec;
            } else {
                iter_func = sk_lua_stream_iter_func_rwrec;
            }
            lua_pushnil(L);
            break;

          case LUA_TUSERDATA:
            if (sk_lua_tofixrec(L, 2)) {
                iter_func = sk_lua_stream_iter_func_fixrec;
                lua_pushvalue(L, 2);
            }
            if (sk_lua_torwrec(L, 2)) {
                iter_func = sk_lua_stream_iter_func_rwrec;
                lua_pushvalue(L, 2);
            }
            break;

          default:
            break;
        }

    } else {
        switch (lua_type(L, 2)) {
          case LUA_TNONE:
          case LUA_TNIL:
            iter_func = sk_lua_stream_iter_func_fixrec;
            lua_pushnil(L);
            break;

          case LUA_TSTRING:
            if (1 == luaL_checkoption(L, 2, NULL, opt_strings)) {
                luaL_error(L, ("Stream read error: Cannot read rwrec"
                               " from an IPFIX stream"));
                return;
            }
            iter_func = sk_lua_stream_iter_func_fixrec;
            lua_pushnil(L);
            break;

          case LUA_TUSERDATA:
            if (sk_lua_tofixrec(L, 2)) {
                iter_func = sk_lua_stream_iter_func_fixrec;
                lua_pushvalue(L, 2);
            }
            if (sk_lua_torwrec(L, 2)) {
                luaL_error(L, ("Stream read error: Cannot read rwrec"
                               " from an IPFIX stream"));
                return;
            }
            break;

          default:
            break;
        }
    }

    if (NULL == iter_func) {
        sk_lua_argerror(L, 2, "%s, %s, string, or nil expected, got %s",
                        SK_LUA_FIXREC, SK_LUA_RWREC, sk_lua_typename(L,2));
    }

    /* create closure and push stream */
    lua_pushcclosure(L, iter_func, 1);
    lua_pushvalue(L, 1);
}


/*
 * =pod
 *
 * =item silk.B<stream_iter(>I<stream>[, I<arg>]B<)>
 *
 * Return an iterator over the records in I<stream>.  I<stream> must
 * be open for reading.  Using the iterator consumes the stream.
 *
 * On each iteration, a single record is read from I<stream> and two
 * values are returned.  The first returned value is the record or
 * B<nil> when I<stream> contains no more records.  The second
 * returned value depends on the type of records in I<stream>: it is
 * the export time (as a L<datetime|/Datetime>) of the record when
 * reading from an IPFIX stream or B<nil> when reading from a SiLK
 * Flow stream.
 *
 * When I<arg> is not specified, a new record is created and returned
 * on each iteration.  The type of the record is an L<rwrec|/RWRec>
 * when I<stream> is reading from a SiLK Flow stream or a
 * L<fixrec|/Fixrec> when I<stream> is reading from an IPFIX stream.
 *
 * The type of record to be returned may be specified by giving the
 * string C<silk> or C<ipfix> as the second argument.  A new record is
 * created and returned on each iteration.
 *
 * If the second argument is an rwrec, that record is cleared and
 * filled on each iteration.  When reading from an IPFIX stream, the
 * IPFIX record is converted to an rwrec.
 *
 * If the second argument is a L<fixrec|/Fixrec>, the function clears
 * that fixrec, fills it with the new data (ignoring I<fixrec>'s
 * previous L<schema|/Schema>), and returns it on each iteration.
 * When reading from a SiLK Flow stream, the SiLK record is converted
 * to IPFIX.
 *
 * =cut
 */
static int
sk_lua_stream_iter(
    lua_State          *L)
{
    sk_lua_stream_read_helper(L);
    lua_pushnil(L);
    return 3;
}

/*
 * =pod
 *
 * =item silk.B<stream_read(>I<stream>[, I<arg>]B<)>
 *
 * Read a single record from I<stream>.  The optional second argument
 * and the return values are the same as those described for a single
 * iteration of the stream iterator.  See
 * L<stream_iter()|/"silk.B<stream_iter(>I<stream>[, I<arg>]B<)>">.
 *
 * =cut
 */
int
sk_lua_stream_read(
    lua_State          *L)
{
    sk_lua_stream_read_helper(L);
    lua_call(L, 1, 2);
    return 2;
}


/*
 * =pod
 *
 * =item silk.B<stream_open_reader(>I<filename>[, I<type>]B<)>
 *
 * Create a stream that reads records from the file named I<filename>.
 * Specify I<filename> as C<-> or C<stdin> to read from the standard
 * input.
 *
 * When the I<type> argument is specified and is not B<nil>, it must
 * be a string designating the expected type of records in
 * I<filename>, either C<ipfix> for IPFIX files or C<silk> for SiLK
 * flow files.  The function raises an error if the actual type does
 * not match the expected type.  The function also raises an error if
 * it encounters end-of-file or another read error while trying to
 * determine the type of records in I<filename.  When I<type> is not
 * supplied or is B<nil>, either type of record is allowed.
 *
 * =cut
 */
int
sk_lua_stream_open_reader(
    lua_State          *L)
{
    const char *opt_strings[] = {"ipfix", "silk", "any", NULL};
    const char *filename;
    sk_lua_stream_t *stream;
    int content;
    /* skcontent_t file_content; */
    fbInfoModel_t *model = NULL;
    int rv;

    filename = sk_lua_checkstring(L, 1);
    content = luaL_checkoption(L, 2, "any", opt_strings);

    stream = sk_lua_newuserdata(L, sk_lua_stream_t);
    memset(stream, 0, sizeof(*stream));
    luaL_setmetatable(L, SK_LUA_STREAM);
    if (1 == content) {
        /* SiLK rwRec stream */
        stream->is_ipfix = 0;
        rv = skStreamOpenSilkFlow(&stream->stream.rw, filename, SK_IO_READ);
        if (rv) { goto ERROR; }
    } else if (0 == content) {
        /* IPFIX stream */
        model = sk_lua_get_info_model(L, 1);
        lua_pop(L, 1);
        stream->is_ipfix = 1;
        if ((rv = sk_fixstream_create(&stream->stream.fix))
            || (rv = sk_fixstream_bind(stream->stream.fix,
                                       filename, SK_IO_READ))
            || (rv = sk_fixstream_set_info_model(stream->stream.fix, model))
            || (rv = sk_fixstream_open(stream->stream.fix)))
        {
            goto ERROR;
        }
    } else {
        return luaL_error(L, "type 'any' is not implemented yet");
    }

#if 0

    /* Check content type */
/*
**    if (content != SK_CONTENT_UNKNOWN_FLOW) {
**        rv = skStreamDetermineContentType(*stream, &file_content);
**        if (rv) {
**            goto ERROR;
**        }
*/
        if (content != file_content) {
            return luaL_error(L, ("Stream open error: actual content type '%s'"
                                  " does not match desired type '%s'"),
                              opt_strings[SK_CONTENT_SILK_FLOW == file_content],
                              opt_strings[SK_CONTENT_SILK_FLOW == content]);
        }
/*
**    }
*/
    luaL_setmetatable(L, SK_LUA_STREAM);
#endif

    /* create a table as the stream's uservalue */
    lua_newtable(L);
    lua_setuservalue(L, -2);

    return 1;

  ERROR:
    return sk_lua_stream_error(L, stream, rv, "Stream open error");
}


/*
 * =pod
 *
 * =item silk.B<stream_close(>I<stream>B<)>
 *
 * Flush and close I<stream>.
 *
 * =cut
 */
static int
sk_lua_stream_close(
    lua_State          *L)
{
    sk_lua_stream_t *s = sk_lua_checkstream(L, 1);
    ssize_t rv;

    if (NULL == s->stream.rw) {
        rv = 0;
    } else if (1 == s->is_ipfix) {
        rv = sk_fixstream_close(s->stream.fix);
    } else {
        rv = skStreamWriteSilkHeader(s->stream.rw);
        switch (rv) {
          case SKSTREAM_OK:
          case SKSTREAM_ERR_PREV_DATA:
          case SKSTREAM_ERR_UNSUPPORT_IOMODE:
          case SKSTREAM_ERR_UNSUPPORT_CONTENT:
          case SKSTREAM_ERR_NOT_OPEN:
          case SKSTREAM_ERR_CLOSED:
            break;
          default:
            break;
        }
        rv = skStreamClose(s->stream.rw);
    }
    if (rv) {
        return sk_lua_stream_error(L, s, rv, "Stream close error");
    }

    return 0;
}

/*
 * =pod
 *
 * =item silk.B<stream_get_name(>I<stream>B<)>
 *
 * Return the filename for I<stream>.
 *
 * =cut
 */
static int
sk_lua_stream_get_name(
    lua_State          *L)
{
    sk_lua_stream_t *stream;
    skstream_t *s;

    stream = sk_lua_checkstream(L, 1);
    s = ((stream->is_ipfix)
         ? sk_fixstream_get_stream(stream->stream.fix)
         : stream->stream.rw);
    lua_pushstring(L, skStreamGetPathname(s));
    return 1;
}


/*
 * =pod
 *
 * =item silk.B<stream_get_sidecar(>I<stream>B<)>
 *
 * Return the sidecar description object that exists on the SiLK Flow
 * record stream I<stream>.  Return B<nil> if no sidecar description
 * exists.  Raise an error if I<stream> is an IPFIX stream.
 *
 * =cut
 *
 *    Create a Lua sidecar from the sidecar description read from the
 *    header of an skstream.
 */
static int
sk_lua_stream_get_sidecar(
    lua_State          *L)
{
    sk_lua_stream_t *stream;
    const sk_sidecar_t *sc;
    sk_sidecar_t *sidecar;

    stream = sk_lua_checkstream(L, 1);
    if (stream->is_ipfix) {
        return luaL_error(L, "cannot get sidecar from an ipfix stream");
    }
    sc = skStreamGetSidecar(stream->stream.rw);
    if (sc) {
        sk_sidecar_copy(&sidecar, sc);
    } else {
        sidecar = NULL;
    }
    sk_lua_push_sidecar(L, sidecar, 1);

    return 1;
}

/*
 * =pod
 *
 * =item silk.B<stream_set_sidecar(>I<stream>, I<sidecar>B<)>
 *
 * Set the sidecar description object on the SiLK Flow record stream
 * I<stream> to I<sidecar>.  Raise an error if I<stream> is not open
 * for writing, if I<stream> is an IPFIX stream, or if I<sidecar> is
 * not frozen.
 *
 * =cut
 *    Add a sidecar description to an skstream.
 */
static int
sk_lua_stream_set_sidecar(
    lua_State          *L)
{
    sk_lua_stream_t *stream;
    const sk_sidecar_t *sidecar;
    ssize_t rv;

    stream = sk_lua_stream_check_mode(L, 1, SK_IO_WRITE);
    sidecar = *sk_lua_checksidecar(L, 2);

    if (stream->is_ipfix) {
        return luaL_error(L, "cannot set sidecar on an ipfix stream");
    }

    /* ensure the sidecar is frozen */
    lua_pushcfunction(L, sk_lua_sidecar_is_frozen);
    lua_pushvalue(L, 2);
    lua_call(L, 1, 1);
    if (!lua_toboolean(L, -1)) {
        return luaL_error(
            L, "error setting sidecar on stream: sidecar is not frozen");
    }
    lua_pop(L, 1);

    rv = skStreamSetSidecar(stream->stream.rw, sidecar);
    if (rv) {
        return sk_lua_stream_error(L, stream, rv,
                                   "error setting sidecar on stream");
    }
    return 0;
}


/*
 *    A helper function to implement the Lua function
 *    stream_new_schema_callback().
 *
 *    This is the C function that sk_fixstream_set_schema_cb()
 *    invokes.  This callback invokes the user's Lua function.
 *
 *    The signature of this function is sk_fixstream_schema_cb_fn_t.
 */
static void
sk_lua_stream_newschema_callback(
    sk_schema_t        *schema,
    uint16_t     UNUSED(tid),
    void               *v_state)
{
    sk_lua_stream_newschema_t *state = (sk_lua_stream_newschema_t *)v_state;
    int rv;
#ifndef NDEBUG
    int top = lua_gettop(state->L);
#endif

    /* get the table containing the stream object and callback
     * function from the Lua registry. */
    if (lua_rawgeti(state->L, LUA_REGISTRYINDEX, state->ref) != LUA_TTABLE) {
        skAppPrintErr("expect table but type is %d [%s]",
                      lua_type(state->L, -1), sk_lua_typename(state->L, -1));
        lua_pop(state->L, 1);
        assert(top == lua_gettop(state->L));
        return;
    }
    /* push the callback function */
    if (lua_rawgeti(state->L, -1, 1) != LUA_TFUNCTION) {
        skAppPrintErr("expect function but type is %d [%s]",
                      lua_type(state->L, -1), sk_lua_typename(state->L, -1));
        lua_pop(state->L, 2);
        assert(top == lua_gettop(state->L));
        return;
    }
    /* push the stream object */
    if (lua_rawgeti(state->L, -2, 2) != LUA_TUSERDATA) {
        skAppPrintErr("expect userdata but type is %d [%s]",
                      lua_type(state->L, -1), sk_lua_typename(state->L, -1));
        lua_pop(state->L, 3);
        assert(top == lua_gettop(state->L));
        return;
    }
    /* push the schema object, perhaps creating a user-data */
    sk_lua_push_schema(state->L, sk_schema_clone(schema));
    rv = lua_pcall(state->L, 2, 0, 0);
    if (LUA_OK != rv) {
        skAppPrintErr("error in new schema callback: %s",
                      lua_tostring(state->L, -1));
        lua_pop(state->L, 1);
    }
    lua_pop(state->L, 1);
    assert(lua_gettop(state->L) == top);
}

/*
 * =pod
 *
 * =item silk.B<stream_new_schema_callback(>I<stream>, I<schema_cb>B<)>
 *
 * Add to I<stream> a callback function I<schema_cb> that is invoked
 * each time I<stream> sees a new schema.  I<stream> must be an open
 * for reading IPFIX data.  The I<schema_cb> function is called with
 * two parameters, the I<stream> and a Schema object representing the
 * new schema.  The return value of I<schema_cb> is ignored.
 *
 * =cut
 *
 *    To implement this, the stream userdata and function that are
 *    passed into this function are stored in a Lua table, and that
 *    table is added to the Lua registry.
 *
 *    We allocate a C structure to hold the reference into the Lua
 *    registry and the Lua state.  This struct becomes the callback
 *    data parameter for sk_fixstream_set_schema_cb().
 *
 *    To ensure the C struct is properly freed when the stream is
 *    destroyed, store the struct in Lua as a lightuserdata and make
 *    the lightuserdata the uservalue for the stream userdata.
 *
 *    When the stream sees a new schema, the skstream code invokes the
 *    sk_lua_stream_newschema_callback() function above.  That C
 *    function gets the table from the Lua registry and invokes the
 *    Lua callback function with a schema userdata created from the
 *    new schema.
 */
static int
sk_lua_stream_new_schema_callback(
    lua_State          *L)
{
    sk_lua_stream_t *s;
    sk_lua_stream_newschema_t *state;

    /* ensure we have a stream and a callback funciton */
    s = sk_lua_stream_check_mode(L, 1, SK_IO_READ);
    luaL_checktype(L, 2, LUA_TFUNCTION);

    if (!s->is_ipfix) {
        return luaL_error(L, "cannot set schema callback on rwRec stream");
    }

    /* check for an existing uservalue on the stream userdata */
    if (lua_getuservalue(L, 1) == LUA_TLIGHTUSERDATA
        && (state = (sk_lua_stream_newschema_t*)lua_touserdata(L, -1)) != NULL)
    {
        luaL_unref(L, LUA_REGISTRYINDEX, state->ref);
        assert(L == state->L);
        state->ref = LUA_NOREF;
    } else {
        /* create the state object */
        state = sk_alloc(sk_lua_stream_newschema_t);
        state->L = L;
        state->ref = LUA_NOREF;

        /* Add the state to the stream as its uservalue */
        lua_pushlightuserdata(L, (void *)state);
        lua_setuservalue(L, 1);
    }
    lua_pop(L, 1);

    /* create a Lua table, and store the function at index 1 and
     * the stream at index 2 */
    lua_createtable(L, 2, 1);
    lua_pushvalue(L, 2);
    lua_rawseti(L, 3, 1);
    lua_pushvalue(L, 1);
    lua_rawseti(L, 3, 2);
    /* store table in the lua registry */
    state->ref = luaL_ref(L, LUA_REGISTRYINDEX);

    sk_fixstream_set_schema_cb(
        s->stream.fix, sk_lua_stream_newschema_callback, state);

    return 0;
}


/*
 * =pod
 *
 * =item silk.B<stream_open_writer(>I<filename>, I<type>B<)>
 *
 * Create a stream object that writes records to I<filename>.  Specify
 * I<filename> as C<-> or C<stdout> to write to the standard output.
 *
 * The I<type> argument must be a string designating how to represent
 * records in I<filename>, either C<ipfix> for IPFIX records or
 * C<silk> for SiLK flow records.
 *
 * Use B<stream_write()> to write records to I<stream>.
 * B<stream_write()> accepts either IPFIX or SiLK Flow records; if
 * necessary, the record is converted to the type specified by
 * I<type> prior to being written to the stream.
 *
 * =cut
 */
int
sk_lua_stream_open_writer(
    lua_State          *L)
{
    const char *opt_strings[] = {"ipfix", "silk", NULL};
    const char *filename;
    sk_lua_stream_t *stream;
    int content;
    fbInfoModel_t *model = NULL;
    int rv;

    filename = sk_lua_checkstring(L, 1);
    content = luaL_checkoption(L, 2, NULL, opt_strings);

    stream = sk_lua_newuserdata(L, sk_lua_stream_t);
    memset(stream, 0, sizeof(*stream));
    luaL_setmetatable(L, SK_LUA_STREAM);
    if (1 == content) {
        /* SiLK rwRec stream */
        stream->is_ipfix = 0;
        rv = skStreamOpenSilkFlow(&stream->stream.rw, filename, SK_IO_WRITE);
        if (rv) { goto ERROR; }
    } else {
        model = sk_lua_get_info_model(L, 1);
        lua_pop(L, 1);
        stream->is_ipfix = 1;
        if ((rv = sk_fixstream_create(&stream->stream.fix))
            || (rv = sk_fixstream_bind(stream->stream.fix,
                                       filename, SK_IO_WRITE))
            || (rv = sk_fixstream_set_info_model(stream->stream.fix, model))
            || (rv = sk_fixstream_open(stream->stream.fix)))
        {
            goto ERROR;
        }
    }

    return 1;

  ERROR:
    return sk_lua_stream_error(L, stream, rv, "Stream open error");
}

/*
 * =pod
 *
 * =item silk.B<stream_write(>I<stream>, I<fixrec>[, I<schema>]B<)>
 *
 * Write the L<fixrec|/Fixrec> I<fixrec> to I<stream>.  I<stream> must
 * be open for writing.  If I<schema> is supplied, the fixrec is
 * written using that L<schema|/Schema> object.  If the type C<silk>
 * was specified when I<stream> was opened, a SiLK Flow record
 * approximation of I<fixrec> is written to I<stream>.
 *
 * =item silk.B<stream_write(>I<stream>, I<rwrec>B<)>
 *
 * Write the RWRec I<rwrec> to I<stream>.  I<stream> must be open for
 * writing.  If the type C<ipfix> was specified when I<stream> was
 * opened, an IPFIX record approximation of I<rwrec> is written to
 * I<stream>.
 *
 * =cut
 */
int
sk_lua_stream_write(
    lua_State          *L)
{
    sk_lua_stream_t *stream;
    sk_schema_t *schema = NULL;
    rwRec *rwrec;
    sk_fixrec_t **rec;
    int rv;

    stream = sk_lua_stream_check_mode(L, 1, SK_IO_WRITE);
    if ((rec = sk_lua_tofixrec(L, 2)) != NULL) {
        if (!lua_isnoneornil(L, 3)) {
            schema = *sk_lua_checkschema(L, 3);
        }
        rv = sk_fixstream_write_record(stream->stream.fix, *rec, schema);
    } else if ((rwrec = sk_lua_torwrec(L, 2)) != NULL) {
        if (lua_gettop(L) != 2) {
            return luaL_error(L, ("Only 2 arguments allowed when writing"
                                  " a %s, got %d"),
                              SK_LUA_RWREC, lua_gettop(L));
        }
        rv = skStreamWriteRecord(stream->stream.rw, rwrec);
    } else {
        return sk_lua_argerror(L, 2, "%s or %s expected, got %s",
                               SK_LUA_RWREC, SK_LUA_FIXLIST,
                               sk_lua_typename(L, 2));
    }

    if (rv) {
        return sk_lua_stream_error(L, stream, rv, "Stream write error");
    }
    return 0;
}


/*
 *  ******************************************************************
 *  Support for plug-in fields defined in Lua
 *  ******************************************************************
 */

/*
 *    Helper function for skluapin_field_initialize() and
 *    skluapin_field_cleanup() where 'cbfunc_pos' indicates the index
 *    of the callback function in the cbdata_table.
 */
static skplugin_err_t
skluapin_simple_callback(
    void               *v_cbdata,
    int                 cbfunc_pos)
{
#ifndef NDEBUG
    int top = lua_gettop(((skluapin_callback_data_t*)v_cbdata)->L);
#endif
    skluapin_callback_data_t *cbdata;
    lua_State *L;
    int rv;

    cbdata = (skluapin_callback_data_t*)v_cbdata;
    L = cbdata->L;

    /* get the table containing the initialization function for this
     * plug-in field from the Lua registry. */
    if (lua_rawgeti(L, LUA_REGISTRYINDEX, cbdata->ref) != LUA_TTABLE) {
        const char *s = luaL_tolstring(L, -1, NULL);
        skAppPrintErr("expect table but type is %d [%s]", lua_type(L, -1), s);
        lua_pop(L, 1);
        assert(top == lua_gettop(L));
        return SKPLUGIN_ERR;
    }

    /* push the update callback function.  Call it if it is a
     * function; otherwise do nothing. */
    if (lua_rawgeti(L, -1, cbfunc_pos) != LUA_TFUNCTION) {
        lua_pop(L, 2);
        assert(top == lua_gettop(L));
        return SKPLUGIN_OK;
    }
    rv = lua_pcall(L, 0, 1, 0);
    if (LUA_OK == rv) {
        rv = lua_tointeger(L, -1);
        lua_pop(L, 2);
        assert(lua_gettop(L) == top);
        return ((0 == rv) ? SKPLUGIN_OK : SKPLUGIN_ERR);
    }
    lua_pop(L, 2);
    assert(lua_gettop(L) == top);

    return SKPLUGIN_ERR;
}

static int
skluapin_field_initialize(
    void               *v_cbdata)
{
    return skluapin_simple_callback(v_cbdata, SKLUAPIN_CBDATA_INITIALIZE);
}

static int
skluapin_field_cleanup(
    void               *v_cbdata)
{
    return skluapin_simple_callback(v_cbdata, SKLUAPIN_CBDATA_CLEANUP);
}


/*
 *    Callback function invoked by sk_fixrec_update_computed() to
 *    update the value of the computed field on 'rec'.
 *
 *    This function must have the signature defined by
 *    sk_field_computed_update_fn_t.
 */
static sk_schema_err_t
skluapin_field_compute(
    sk_fixrec_t                    *rec,
    const sk_field_computed_data_t *field_data)
{
#ifndef NDEBUG
    int top
        = lua_gettop(((skluapin_callback_data_t*)field_data->caller_ctx)->L);
#endif
    skluapin_callback_data_t *cbdata;
    const sk_schema_t *schema;
    lua_State *L;
    int cbdata_idx;
    size_t i;
    size_t schema_len;
    int rv;
    int rec_idx;
    int field_map_idx;

    cbdata = (skluapin_callback_data_t*)field_data->caller_ctx;
    L = cbdata->L;

    /* get the table containing the update function and field sequence
     * for this plug-in field from the Lua registry. */
    if (LUA_TTABLE != lua_rawgeti(L, LUA_REGISTRYINDEX, cbdata->ref)) {
        const char *x = lua_tostring(L, -1);
        skAppPrintErr("type is %d ['%s']", lua_type(L, -1), x);
        lua_pop(L, 1);
        assert(lua_gettop(L) == top);
        return SK_SCHEMA_ERR_UNSPECIFIED;
    }
    cbdata_idx = lua_gettop(L);

    /* create a Lua fixrec-userdata from the fixrec; get the fixrec's
     * schema; get the field to field-userdata map located on the
     * schema's uservalue table */
    sk_lua_push_fixrec(L, rec);
    rec_idx = lua_gettop(L);
    lua_pushcfunction(L, sk_lua_fixrec_get_schema);
    lua_pushvalue(L, rec_idx);
    lua_call(L, 1, 1);
    lua_getuservalue(L, -1);
    if (lua_rawgeti(L, -1, SKLUA_SCHEMA_UVAL_PLUGIN) != LUA_TTABLE) {
        /* schema is at -3, schema-uservalue at -2, nil at -1; replace
         * nil with a new table; loop through the schema's fields
         * table create a field->field_userdata map in the new
         * table. add that new table to the schema's uservalue */
        schema = *(sk_schema_t**)lua_touserdata(L, -3);
        schema_len = sk_schema_get_count(schema);
        lua_pop(L, 1);
        lua_createtable(L, 0, schema_len);
        lua_rawgeti(L, -2, SKLUA_SCHEMA_UVAL_FIELDS);
        for (i = 0; i < schema_len; ++i) {
            lua_pushlightuserdata(L, (void*)sk_schema_get_field(schema, i));
            lua_rawgeti(L, -2, i+1);
            assert(sk_schema_get_field(schema, i)
                   == *(sk_field_t**)lua_touserdata(L, -1));
            lua_rawset(L, -4);
        }
        /* done with the fields table; push the field map table onto a
         * second time so a copy remains on the stack */
        lua_pop(L, 1);
        lua_pushvalue(L, -1);
        lua_rawseti(L, -3, SKLUA_SCHEMA_UVAL_PLUGIN);
    }
    field_map_idx = lua_gettop(L);

    /*
     * prepare to call: update(rec, field, sequence_of_prerequisites)
     *
     * for 'field' and each field in the prerequisites, map the
     * sk_field_t to the field-userdata via table at field_map_idx.
     *
     * the table for the prerequisites already exists and it has the
     * correct size---it was created when the plug-in field was
     * registered.
     */
    lua_rawgeti(L, cbdata_idx, SKLUAPIN_CBDATA_UPDATE);
    assert(lua_isfunction(L, -1));
    lua_pushvalue(L, rec_idx);
    lua_pushlightuserdata(L, (void*)field_data->dest);
    lua_rawget(L, field_map_idx);

    if (0 == field_data->entries) {
        /* call update(rec, field) */
        rv = lua_pcall(L, 2, 0, 0);
    } else {
        lua_rawgeti(L, cbdata_idx, SKLUAPIN_CBDATA_FIELDS);
        for (i = 0; i < field_data->entries; ++i) {
            if (NULL == field_data->fields[i]) {
                lua_pushnil(L);
            } else {
                lua_pushlightuserdata(L, (void*)field_data->fields[i]);
                lua_rawget(L, field_map_idx);
            }
            lua_rawseti(L, -2, 1+i);
        }
        /* call update(rec, field, sequence_of_prerequisites) */
        rv = lua_pcall(L, 3, 0, 0);
    }
    if (LUA_OK == rv) {
        lua_pop(L, 5);
        assert(lua_gettop(L) == top);
        return SK_SCHEMA_ERR_SUCCESS;
    }
    /* five objects plus an error  */
    /* skAppPrintErr("%s", lua_tostring(L, -1)); */
    lua_pop(L, 6);
    assert(lua_gettop(L) == top);
    return SK_SCHEMA_ERR_UNSPECIFIED;
}


/*
 *    Lua callback function to process a sequence containing tables,
 *    where each table is an "ie_spec" that is used to create a
 *    plug-in field.
 *
 *    This function is invoked from the following function by
 *    lua_pcall().
 */
static int
skluapin_register_fields(
    lua_State          *L)
{
    const char *field_names[1024];
    char namebuf[2048];
    skplugin_schema_callbacks_t regdata;
    skluapin_callback_data_t *cbdata;
    int rv;
    int gc_ref;
    int t;
    int i;
    int j;
    int len;
    int num_fields;
    int type;
    int ie_specs_seq;
    int cbdata_table;

    /* There should be a table on the stack, which is the sequence of
     * plug-in field specifiers (ie_spec) */
    luaL_argcheck(L, 1, LUA_TTABLE, "table expected");
    ie_specs_seq = lua_gettop(L);

    /* Create table for freeing the callback structures. */
    gc_ref = sk_lua_create_gc_table(L);

    /* Visit each ie_spec in the sequence */
    len = luaL_len(L, ie_specs_seq);
    for (i = 1; i <= len; ++i) {
        lua_pushinteger(L, i);
        lua_gettable(L, ie_specs_seq);

        /* this is an ie_spec table; if it is not a table, the Lua
         * code has messed up */
        t = lua_gettop(L);
        if (!lua_istable(L, t)) {
            return luaL_error(L, ("Sequence returned by get_registered_fields"
                                  " contains non-table (%s)"),
                              sk_lua_typename(L, t));
        }

        /* initalize values for this plug-in field */
        num_fields = 0;
        memset(&regdata, 0, sizeof(regdata));
        regdata.desc.field_names = field_names;

        /* Create and initialize the callback data structure used by
         * skplugin; arrange for it to be freed by Lua. */
        cbdata = sk_alloc(skluapin_callback_data_t);
        /* fprintf(stderr, ("%s:%d: Calling sk_lua_gc_protect_pointer" */
        /*                  " on table %d, object %p, free_fn %p\n"), */
        /*         __FILE__, __LINE__, gc_ref, (void *)cbdata, (void *)free);*/
        sk_lua_gc_protect_pointer(L, gc_ref, cbdata, free);
        cbdata->L = L;
        cbdata->ref = LUA_NOREF;
        regdata.desc.caller_ctx = cbdata;
        regdata.desc.update = &skluapin_field_compute;
        regdata.init = &skluapin_field_initialize;
        regdata.cleanup = &skluapin_field_cleanup;

        /* Process the entries in this ie_spec table.  The following
         * assumes error checking has already occurred within the Lua
         * code that built the ie_spec sequence. */

        /* lookup */
        lua_getfield(L, t, "lookup");
        regdata.desc.lookup =
            ((sk_field_computed_lookup_t)
             luaL_checkoption(L, -1, NULL,sk_lua_field_computed_lookup_names));
        lua_pop(L, 1);

        /* name */
        if (lua_getfield(L, t, "name") != LUA_TNIL) {
            regdata.desc.name = lua_tostring(L, -1);
        }
        lua_pop(L, 1);

        /* elementId */
        lua_getfield(L, t, "elementId");
        regdata.desc.ident = lua_tointeger(L, -1);
        lua_pop(L, 1);

        /* enterpriseId */
        lua_getfield(L, t, "enterpriseId");
        regdata.desc.ident = (lua_tointeger(L, -1) << 32) | regdata.desc.ident;
        lua_pop(L, 1);

        /* dataType */
        if (lua_getfield(L, t, "dataType") != LUA_TNIL) {
            regdata.desc.datatype
                = luaL_checkoption(L, -1, NULL, sk_lua_ie_type_names);
        }
        lua_pop(L, 1);

        /* dataTypeSemantics */
        lua_getfield(L, t, "dataTypeSemantics");
        regdata.desc.semantics
            = luaL_checkoption(L, -1, "default", sk_lua_ie_semantic_names);
        lua_pop(L, 1);

        /* units */
        lua_getfield(L, t, "units");
        regdata.desc.units
            = luaL_checkoption(L, -1, "none", sk_lua_ie_semantic_units);
        lua_pop(L, 1);

        /* rangemin */
        lua_getfield(L, t, "rangemin");
        regdata.desc.min = lua_tonumber(L, -1);
        lua_pop(L, 1);

        /* rangemax */
        lua_getfield(L, t, "rangemax");
        regdata.desc.max = lua_tonumber(L, -1);
        lua_pop(L, 1);

        /* length */
        lua_getfield(L, t, "length");
        regdata.desc.len = lua_tointeger(L, -1);
        lua_pop(L, 1);

        /* This table is used to cache things required by the plug-in
         * callback functions.  It gets added to the registry once we
         * fill it. */
        lua_createtable(L, SKLUAPIN_CBDATA_TABLE_SIZE, 0);
        cbdata_table = lua_gettop(L);

        /* update; which we add the to the table we created above */
        lua_getfield(L, t, "update");
        lua_rawseti(L, cbdata_table, SKLUAPIN_CBDATA_UPDATE);

        /* initialize; which we add the to the table we created above */
        lua_getfield(L, t, "initialize");
        lua_rawseti(L, cbdata_table, SKLUAPIN_CBDATA_INITIALIZE);

        /* cleanup; which we add the to the table we created above */
        lua_getfield(L, t, "cleanup");
        lua_rawseti(L, cbdata_table, SKLUAPIN_CBDATA_CLEANUP);

        /* prerequisite fields used when computing this value */
        type = lua_getfield(L, t, "prerequisite");
        if (LUA_TTABLE == type && (num_fields = luaL_len(L, -1)) > 0) {
            if (num_fields > (int)(sizeof(field_names)/sizeof(field_names[0])))
            {
                return luaL_error(L, ("Plugin field '%s' uses more fields"
                                      " than are supported (max = %d)"),
                                  regdata.desc.name,
                                  sizeof(field_names)/sizeof(field_names[0]));
            }
            /* add a table having 'num_fields' elements to the context
             * table; the C code above that supports the 'update'
             * callback fills this table with the fields. */
            lua_createtable(L, num_fields, 0);
            lua_rawseti(L, cbdata_table, SKLUAPIN_CBDATA_FIELDS);
            /* store the field names into a C array for calling
             * skpinRegSchemaField() */
            for (j = 1; j <= num_fields; ++j) {
                lua_pushinteger(L, j);
                lua_gettable(L, -2);
                field_names[j-1] = lua_tostring(L, -1);
                lua_pop(L, 1);
            }
        }
        /* since we have handles to the strings in the prerequisite
         * table, do not pop it from the stack until after calling
         * skpinRegSchemaField() */
        regdata.desc.field_names_len = num_fields;

        /* add the table to the registry */
        lua_pushvalue(L, cbdata_table);
        cbdata->ref = luaL_ref(L, LUA_REGISTRYINDEX);

        if (snprintf(namebuf, sizeof(namebuf), "plugin.%s", regdata.desc.name)
            >= (ssize_t)sizeof(namebuf))
        {
            return luaL_error(L, ("Plug-in field '%s' has name longer"
                                  " than maximum supported (max = %d)"),
                              regdata.desc.name, sizeof(namebuf));
        }

        rv = skpinRegSchemaField(namebuf, &regdata, cbdata);
        if (rv) {
            return luaL_error(L, "Error adding field '%s'", namebuf);
        }

        /* Remove everything from the stack except the sequence of
         * ie_specs */
        lua_settop(L, ie_specs_seq);
    }
    assert(lua_gettop(L) == ie_specs_seq);
    lua_pop(L, 1);

    return 0;
}


/*
 *    HACKTASTIC!!!!
 *
 *    This static Lua state is used to communicate the state between
 *    the sk_lua_plugin_register_fields() and the skluapin_setup()
 *    functions.
 */
static lua_State *static_L;

/*
 *    Pretends to be a setup function for skplugin.h.  Invoked by the
 *    function immediately below.
 */
static int
skluapin_setup(
    void)
{
    lua_State *L = static_L;
    int rv;

    assert(L);

    /* function that processes the entries in the ie_specs sequence */
    lua_pushcfunction(L, &skluapin_register_fields);

    /* Call get_plugin_fields() to get the sequence containing the
     * ie_specs of fields to register.  That function is implemented
     * in silk-schema.lua. */
    sk_lua_call_global(L, &fn_get_plugin_fields, 0, 1);
    if (!lua_istable(L, -1)) {
        lua_pop(L, 2);
        return SKPLUGIN_OK;
    }

    /* call the skluapin_register_fields() function */
    rv = lua_pcall(L, 1, 0, 0);
    if (LUA_OK == rv) {
        return SKPLUGIN_OK;
    }
    skAppPrintErr("Error creating plug-in fields: %s",
                  lua_tostring(L, -1));
    return SKPLUGIN_ERR;
}


/*
 *    Register plug-in fields defined in Lua.
 */
int
sk_lua_plugin_register_fields(
    lua_State          *L)
{
    /*
     *  This function only exists as a wrapper around the
     *  skluapin_setup() function above.
     *
     *  The problem is that skluapin_setup() calls the
     *  skpinRegSchemaField() function from skplugin, and that
     *  function expects to be called from the context of a call to
     *  skPluginLoadPlugin() or skPluginAddAsPlugin().  (Actually, the
     *  only dependency that skpinRegSchemaField() seems to have on
     *  being called in that context is an assert that
     *  skp_in_plugin_init is true.)
     *
     *  There is also the complication that there is no way to hand
     *  state to the plug-in setup function except via global
     *  variables.  Thus, there is a static variable shared between
     *  skluapin_setup() and this function.
     */
    static_L = L;

    return skPluginSchemaAddAsPlugin("sklua-plugin", &skluapin_setup);
}



/*
 *  ******************************************************************
 *  Variables for the schema module
 *  ******************************************************************
 */

static const luaL_Reg sk_lua_field_metatable[] = {
    {"__index", sk_lua_field_get_attribute},
    {NULL, NULL}
};

static const luaL_Reg sk_lua_field_methods[] = {
    {"get_attribute", sk_lua_field_get_attribute},
    {NULL, NULL}
};

static const luaL_Reg sk_lua_schema_metatable[] = {
    {"__gc",      sk_lua_schema_gc},
    {"__index",   sk_lua_schema_get_field},
    {"__len",     sk_lua_schema_count_fields},
    {NULL, NULL}
};

static const luaL_Reg sk_lua_schema_methods[] = {
    {"count_fields",    sk_lua_schema_count_fields},
    {"get_field",       sk_lua_schema_get_field},
    {"get_fields",      sk_lua_schema_get_fields},
    {"get_template_id", sk_lua_schema_get_template_id},
    {"iter",            sk_lua_schema_iter},
    {NULL, NULL}
};

static const luaL_Reg sk_lua_fixrec_metatable[] = {
    {"__gc",        sk_lua_fixrec_gc},
    {"__index",     sk_lua_fixrec_get_value},
    {"__newindex",  sk_lua_fixrec_set_value},
    {NULL, NULL}
};

static const luaL_Reg sk_lua_fixrec_methods[] = {
    {"copy",        sk_lua_fixrec_copy},
    {"get_schema",  sk_lua_fixrec_get_schema},
    {"get_value",   sk_lua_fixrec_get_value},
    {"set_value",   sk_lua_fixrec_set_value},
    {NULL, NULL}
};

static const luaL_Reg sk_lua_fixlist_metatable[] = {
    {"__gc",        sk_lua_fixlist_gc},
    {"__len",       sk_lua_fixlist_count_elements},
    {"__index",     sk_lua_fixlist_get_element},
    /*{"__newindex",  sk_lua_fixlist_set_element},*/
    {NULL, NULL}
};

static const luaL_Reg sk_lua_fixlist_methods[] = {
    {"append",          sk_lua_fixlist_append},
    {"count_elements",  sk_lua_fixlist_count_elements},
    {"get_schema",      sk_lua_fixlist_get_schema},
    {"get_element",     sk_lua_fixlist_get_element},
    /*{"set_element",     sk_lua_fixlist_set_element},*/
    {"get_semantic",    sk_lua_fixlist_get_semantic},
    {"set_semantic",    sk_lua_fixlist_set_semantic},
    {"get_type",        sk_lua_fixlist_get_type},
    {"iter",            sk_lua_fixlist_iter},
    {"next_element",    sk_lua_fixlist_next_element},
    {"reset_iter",      sk_lua_fixlist_reset_iter},
    {NULL, NULL}
};

static const luaL_Reg sk_lua_stream_metatable[] = {
    {"__gc", sk_lua_stream_gc},
    {NULL, NULL}
};

static const luaL_Reg sk_lua_stream_methods[] = {
    {"iter",                sk_lua_stream_iter},
    {"read",                sk_lua_stream_read},
    {"close",               sk_lua_stream_close},
    {"get_sidecar",         sk_lua_stream_get_sidecar},
    {"get_name",            sk_lua_stream_get_name},
    {"new_schema_callback", sk_lua_stream_new_schema_callback},
    {"open_reader",         sk_lua_stream_open_reader},
    {"open_writer",         sk_lua_stream_open_writer},
    {"set_sidecar",         sk_lua_stream_set_sidecar},
    {"write",               sk_lua_stream_write},
    {NULL, NULL}
};

static const luaL_Reg sk_lua_schema_functions[] = {
    {"infomodel_augment", sk_lua_infomodel_augment},
    {NULL, NULL}
};

/* Functions that are exported to silk-schema.lua, but are not meant
 * to be exported to "users" */
static const luaL_Reg sk_lua_schema_internal_fns[] = {
    {"schemas_match", sk_lua_schemas_match},
    {"schemamap_create", sk_lua_schemamap_create},
    {"schemamap_apply", sk_lua_schemamap_apply},
    {"field_to_name", sk_lua_field_to_name},
    {"field_get_info_table", sk_lua_field_get_info_table},
    {"make_table_ie_type_names", sk_lua_make_table_ie_type_names},
    {"make_table_ie_semantic_units", sk_lua_make_table_ie_semantic_units},
    {"make_table_ie_semantic_names", sk_lua_make_table_ie_semantic_names},
    {"make_table_list_semantic_names", sk_lua_make_table_list_semantic_names},
    {"make_table_field_computed_lookup_names",
     sk_lua_make_table_field_computed_lookup_names},
    {"make_table_ie_endian_typed_names",
     sk_lua_make_table_ie_endian_typed_names},
    {NULL, NULL}
};

static const sk_lua_object_t objects[] = {
    {"field", SK_LUA_FIELD, NULL, sk_lua_field_metatable,
     sk_lua_field_methods, NULL},
    {"schema", SK_LUA_SCHEMA, sk_lua_schema_create, sk_lua_schema_metatable,
     sk_lua_schema_methods, NULL},
    {"fixrec", SK_LUA_FIXREC, sk_lua_fixrec_create, sk_lua_fixrec_metatable,
     sk_lua_fixrec_methods, NULL},
    {"stream", SK_LUA_STREAM, NULL,
     sk_lua_stream_metatable, sk_lua_stream_methods, NULL},
    {"fixlist", SK_LUA_FIXLIST, sk_lua_fixlist_create,
     sk_lua_fixlist_metatable, sk_lua_fixlist_methods, NULL},
    SK_LUA_OBJECT_SENTINEL
};


/* FIXME: No need for this to be public? */
void
sk_lua_schema_init(
    lua_State          *L)
{
    int rv;
    fbInfoModel_t *model;

    /* return if it appears we've been called before */
    if (lua_getfield(L, LUA_REGISTRYINDEX, SK_LUA_DEPENDENCIES) != LUA_TNIL) {
        lua_pop(L, 1);
        return;
    }
    lua_pop(L, 1);

    /* Create the gc dependency table (key depends on value).
     * Specifically, the value will not be garbage collected until all
     * keys to that value are collected. */
    sk_lua_create_weaktable(L, "k");
    lua_setfield(L, LUA_REGISTRYINDEX, SK_LUA_DEPENDENCIES);

    /* Create the schema look-up table (schema pointer -> schema) */
    sk_lua_create_weaktable(L, "v");
    lua_setfield(L, LUA_REGISTRYINDEX, SK_LUA_SCHEMA_LOOKUP);

    /* Create the schema copy-plan (schemamap) cache */
    sk_lua_create_weaktable(L, "k");
    lua_setfield(L, LUA_REGISTRYINDEX, SK_LUA_SCHEMAMAP_CACHE);

    /* Create and protect the information model */
    rv = sk_lua_create_gc_table(L);
    model = skipfix_information_model_create(SK_INFOMODEL_UNIQUE);
    /* fprintf(stderr, ("%s:%d: Calling sk_lua_gc_protect_pointer on the" */
    /*                  " info model table %d, object %p, free_fn %p\n"), */
    /*         __FILE__, __LINE__, rv, (void *)model, */
    /*         (void *)skipfix_information_model_destroy); */
    sk_lua_gc_protect_pointer(
        L, rv, (void *)model,
        (sk_lua_free_fn_t)skipfix_information_model_destroy);
    lua_pushlightuserdata(L, model);
    lua_setfield(L, LUA_REGISTRYINDEX, SK_LUA_INFOMODEL);
}


/* lua module registration function for the schema module.   */
int
luaopen_schema(
    lua_State          *L)
{
    int inittable = lua_istable(L, 1);

    /* Check lua versions */
    luaL_checkversion(L);

    skipfix_initialize(0);

    /* Initialize */
    sk_lua_schema_init(L);

    /* Add pointers to a couple of lua site functions */
    lua_pushcfunction(L, luaopen_silk_site);
    lua_call(L, 0, 1);
    lua_getfield(L, -1, "sensor_id");
    lua_rawsetp(L, LUA_REGISTRYINDEX, &fn_sensor_id);
    lua_getfield(L, -1, "flowtype_id");
    lua_rawsetp(L, LUA_REGISTRYINDEX, &fn_flowtype_id);
    lua_pop(L, 1);

    /* Run the make_schema_module function to create the given
     * module */

    /* Load the lua portion; it gets 4 arguments: the objects, global
     * functions for export, internal functions, and the silk module.
     * It returns two values: the silk module and a table of functions
     * for internal use by the C code */
    lua_newtable(L);
    sk_lua_add_to_object_table(L, -1, objects);
    luaL_newlib(L, sk_lua_schema_functions);
    luaL_newlib(L, sk_lua_schema_internal_fns);
    if (inittable) {
        lua_pushvalue(L, 1);
    } else {
        lua_pushnil(L);
    }
    sk_lua_load_lua_blob(L, sk_lua_init_blob, sizeof(sk_lua_init_blob),
                         "silk-schema.lua", 4, 2);
    /* for each internal function defined in lua blob, get it from the
     * table (that is confusingly named "export") and add it to the
     * lua registry */
    lua_getfield(L, -1, "index_ies");
    lua_rawsetp(L, LUA_REGISTRYINDEX, &fn_index_ies);
    lua_getfield(L, -1, "normalize_ie");
    lua_rawsetp(L, LUA_REGISTRYINDEX, &fn_normalize_ie);
    lua_getfield(L, -1, "get_plugin_fields");
    lua_rawsetp(L, LUA_REGISTRYINDEX, &fn_get_plugin_fields);
    lua_getfield(L, -1, "fixlist_append_normalize");
    lua_rawsetp(L, LUA_REGISTRYINDEX, &fn_fixlist_append_normalize);
    /* pop the internal table */
    lua_pop(L, 1);

    /* Return the module */
    return 1;
}


/*
** Local Variables:
** mode:c
** indent-tabs-mode:nil
** c-basic-offset:4
** End:
*/
