/*
** Copyright (C) 2014-2017 by Carnegie Mellon University.
**
** @OPENSOURCE_LICENSE_START@
** See license information in ../../LICENSE.txt
** @OPENSOURCE_LICENSE_END@
*/
#ifndef _SKLUA_H
#define _SKLUA_H
#ifdef __cplusplus
extern "C" {
#endif

#include <silk/silk.h>

RCSIDENTVAR(rcsID_SKLUA_H, "$SiLK: sklua.h efd886457770 2017-06-21 18:43:23Z mthomas $");

/*
**  sklua.h
**
**  Lua routines for silk
**
*/

/* include lua headers with the default linkage */
#ifndef __cplusplus

#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>

#else

extern "C++" {
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
}

#endif

#include <silk/skipaddr.h>
#include <silk/skredblack.h>


/*** LUA type names ***/

/* Wrapper for skipaddr_t */
#define SK_LUA_IPADDR       "silk.ipaddr"

/* Wrapper for skIPWildcard_t */
#define SK_LUA_IPWILDCARD   "silk.ipwildcard"

/* Wrapper for skipset_t */
#define SK_LUA_IPSET        "silk.ipset"

/* Wrapper for skPrefixMap_t */
#define SK_LUA_PMAP         "silk.prefixmap"

/* Wrapper for sk_bitmap_t */
#define SK_LUA_BITMAP       "silk.bitmap"

/* Wrapper for sktime_t */
#define SK_LUA_DATETIME     "silk.time"

/* Wrapper for sk_fixrec_t* */
#define SK_LUA_FIXREC       "silk.fixrec"

/* Wrapper for rwRec */
#define SK_LUA_RWREC        "silk.rwrec"

/* Wrapper for sidecar userdata */
#define SK_LUA_SIDECAR      "silk.sidecar"


/*******************/
/*** sklua-utils ***/
/*******************/

/**
 *    Helper macro to define the sk_lua_checkTYPE() macros below.
 *
 *    Check whether the object at position 'chk_arg' on the Lua statck
 *    is a 'chk_lua_type' userdata and, if so, return the object cast
 *    to the 'chk_c_type'.  Raise an error if not.
 */
#define SKLUA_CHECK_TYPE(chk_L, chk_arg, chk_lua_type, chk_c_type)      \
    ((chk_c_type)luaL_checkudata((chk_L), (chk_arg), chk_lua_type))

/**
 *    Helper macro to define the sk_lua_toTYPE() macros below.
 *
 *    Check whether the object at position 'chk_arg' on the Lua statck
 *    is a 'chk_lua_type' userdata and, if so, return the object cast
 *    to the 'chk_c_type'.  Return NULL if not.
 */
#define SKLUA_TEST_TYPE(chk_L, chk_arg, chk_lua_type, chk_c_type) \
    ((chk_c_type)luaL_testudata((chk_L), (chk_arg), chk_lua_type))


/**
 *    Return the string referenced by argument narg, returning its
 *    length in len if not NULL.  Raises an error if the argument is
 *    not a string.  Unlike luaL_checkstring(), this will not
 *    auto-convert numbers to strings.
 */
const char *
sk_lua_checklstring(
    lua_State          *L,
    int                 narg,
    size_t             *len);

/**
 *    A version of sk_lua_checklstring that doesn't return the length
 *    of the returned string.
 */
#define sk_lua_checkstring(L, narg) sk_lua_checklstring((L), (narg), NULL)

/**
 *    Return the integer referenced by argument narg cast to an
 *    lua_Unsigned value.  Raises an error if the argument is not an
 *    number or if the value is signed.  Like lua_tointegerx(), doubles
 *    are truncated to integers in some non-specified way.
 */
lua_Unsigned
sk_lua_checkunsigned(
    lua_State          *L,
    int                 narg);

/**
 *    Struct used to hold information about a lua userdata "object"
 */
typedef struct sk_lua_object_st {
    const char     *name;
    const char     *ident;       /* type identifier */
    lua_CFunction   constructor; /* primary constructor */
    const luaL_Reg *metatable;
    const luaL_Reg *methods;
    const luaL_Reg *static_methods;
} sk_lua_object_t;

/**
 *    Sentinel for arrays of sk_lua_object_t
 */
#define SK_LUA_OBJECT_SENTINEL {NULL, NULL, NULL, NULL, NULL, NULL}

/**
 *    Modifies the lua table at the given index adding descriptions of
 *    lua userdata "objects".  objs should be an array of
 *    sk_lua_object_t items terminated with SK_LUA_OBJECT_SENTINEL.
 *
 *    The resulting table has an entry for each object, indexed by
 *    object name.  The value of each entry is a table with the
 *    following entries:
 *
 *    + *constructor*    The primary function used to construct the object
 *    + *metatable*      The metatable for this object
 *    + *methods*        A table of method-names to functions
 *    + *static_methods* A table of static method-names to functions
 */
void
sk_lua_add_to_object_table(
    lua_State              *L,
    int                     index,
    const sk_lua_object_t  *objs);


/**
 *    Return an integer reference to a new table that can be used to
 *    store pointers for eventual garbage collection.
 */
int
sk_lua_create_gc_table(
    lua_State          *L);

/**
 *    Signature of a generic free function
 */
typedef void (*sk_lua_free_fn_t)(void *);

/**
 *    Store a pointer and its free function into the gc_table tref.
 *    If free_fn is NULL, this will remove a pointer from the table.
 */
void
sk_lua_gc_protect_pointer(
    lua_State          *L,
    int                 tref,
    void               *ptr,
    sk_lua_free_fn_t    free_fn);

#define sk_lua_gc_unprotect_pointer(L, tref, ptr) \
    sk_lua_gc_protect_pointer((L), (tref), (ptr), NULL)

/**
 *    Release the gc_table so that all of its stored pointers can be
 *    garbage collected.  If this is not called, lua_close() will
 *    still free the table and its pointers.
 */
#define sk_lua_free_gc_table(L, tref) \
    luaL_unref((L), LUA_REGISTRYINDEX, (tref))

/**
 *    Push an object on the stack which contains a pointer along with
 *    its free function.  The pointer will be freed when the object is
 *    garbage collected.
 */
void
sk_lua_push_protected_pointer(
    lua_State          *L,
    void               *ptr,
    sk_lua_free_fn_t    free_fn);

/**
 *   Un-protect the pointer previously protected by
 *   sk_lua_push_protected_pointer() at location 'index' on the stack.
 */
#define sk_lua_unprotect_pointer(L, index)      \
    do {                                        \
        int _i = lua_absindex(L, index);        \
        lua_pushnil(L);                         \
        lua_setmetatable(L, _i);                \
    } while (0)


/**
 *    Allocate an object the size of 'obj_type', push a userdata
 *    object on the stack, and return the memory cast to pointer of
 *    type 'obj_type'.
 */
#define sk_lua_newuserdata(L, obj_type)                         \
    (( obj_type *)lua_newuserdata(L, sizeof( obj_type )))


/**
 *    Push a weak table onto the stack.  The type should be "k" for
 *    weak keys, "v" for weak values, or "kv" for weak keys and
 *    values.
 */
void
sk_lua_create_weaktable(
    lua_State          *L,
    const char         *type);

/**
 *    Check whether the table at index 't' on the Lua stack contains
 *    any keys other than those specified in 'table_keys'.  Return the
 *    number of keys in the table that are not in 'table_keys'.
 *
 *    The caller may specify the number of elements of 'table_keys' to
 *    examine in the 'num_table_keys' parameter.  If 'num_table_keys'
 *    is less than 0, the function assumes all entries in 'table_keys'
 *    until a NULL value is reached.
 *
 *    If 'unknown_key_callback' is provided, it will be called for
 *    each key in the table that is not in 'table_keys'.  The
 *    'cb_data' parameter is user callback data that is passed
 *    unchanged to the 'unknown_key_callback' function.
 *
 *    This function attempts to use lua_tostring() on all the keys in
 *    the table.  When lua_tostring() fails, the
 *    'unknown_key_callback' is invoked with a 'key' of NULL.  When
 *    the stringified key is not present in 'table_keys' (using
 *    strcmp() as the test), the 'unknown_key_callback' is called with
 *    the stringified key.
 */
size_t
sk_lua_check_table_unknown_keys(
    lua_State          *L,
    const int           t,
    ssize_t             num_table_keys,
    const char         *table_keys[],
    void              (*unknown_key_callback)(const char *key, void *cb_data),
    void               *cb_data);


/**
 *    Replace the Lua table at the top of the stack with a
 *    version of the table that cannot be modified because the
 *    __newindex metamethod is not present.
 *
 *    Recursively visits all subtables in the table.
 */
int
sk_lua_make_table_read_only(
    lua_State          *L);


/**
 *    Load a compiled lua blob, then call it with the top 'nargs'
 *    args, which will then return 'nresults' values.  (nresults may
 *    be LUA_MULTRET.)  This will also load the silkutils module and
 *    install it in the global table (if not already there).
 */
void
sk_lua_load_lua_blob(
    lua_State          *L,
    const uint8_t      *blob,
    size_t              blob_size,
    const char         *blob_name,
    int                 nargs,
    int                 nresults);

/**
 *    Return a string representing the type of object at index 'arg'
 *    on the Lua stack.  Similar to luaL_typename() but knows about
 *    silk object types.
 */
const char *
sk_lua_typename(
    lua_State          *L,
    int                 arg);

/**
 *    A wrapper over luaL_argerror() that accepts a printf-style
 *    formatting string and a list of arguments.
 */
int
sk_lua_argerror(
    lua_State          *L,
    int                 arg,
    const char         *format,
    ...)
    /* SK_CHECK_PRINTF(3, 4) */;


/******************/
/*** sklua-silk ***/
/******************/

/**
 *    Check whether the function argument at position 'check_arg' on
 *    the Lua stack is an sktime_t userdata and return the argument
 *    cast to that type.  Raise an error if not.
 */
#define sk_lua_checkdatetime(check_L, check_arg)                        \
    SKLUA_CHECK_TYPE(check_L, check_arg, SK_LUA_DATETIME, sktime_t *)

/**
 *    Check whether the function argument at position 'check_arg' on
 *    the Lua stack is an skipaddr_t userdata and return the argument
 *    cast to that type.  Raise an error if not.
 */
#define sk_lua_checkipaddr(check_L, check_arg)                          \
    SKLUA_CHECK_TYPE(check_L, check_arg, SK_LUA_IPADDR, skipaddr_t *)

/**
 *    Check whether the function argument at position 'check_arg' on
 *    the Lua stack is an rwRec userdata and return the argument cast
 *    to that type.  Raise an error if not.
 */
#define sk_lua_checkrwrec(check_L, check_arg)                           \
    SKLUA_CHECK_TYPE(check_L, check_arg, SK_LUA_RWREC, rwRec *)

/**
 *    Check whether the function argument at position 'check_arg' on
 *    the Lua stack is an sk_sidecar_t userdata and return the
 *    argument cast to that type.  Raise an error if not.
 */
#define sk_lua_checksidecar(check_L, check_arg)                 \
    SKLUA_CHECK_TYPE(check_L, check_arg, SK_LUA_SIDECAR, sk_sidecar_t **)

/**
 *    If the item on the stack at 'to_idx' is an sktime_t userdata,
 *    return the value cast to that type.  Otherwise return NULL.
 */
#define sk_lua_todatetime(to_L, to_idx)                                 \
    SKLUA_TEST_TYPE(to_L, to_idx, SK_LUA_DATETIME, sktime_t *)

/**
 *    If the item on the stack at 'to_idx' is an skipaddr_t userdata,
 *    return the value cast to that type.  Otherwise return NULL.
 */
#define sk_lua_toipaddr(to_L, to_idx)                                   \
    SKLUA_TEST_TYPE(to_L, to_idx, SK_LUA_IPADDR, skipaddr_t *)

/**
 *    If the item on the stack at 'to_idx' is an rwRec userdata,
 *    return the value cast to that type.  Otherwise return NULL.
 */
#define sk_lua_torwrec(to_L, to_arg)                            \
    SKLUA_TEST_TYPE(to_L, to_arg, SK_LUA_RWREC, rwRec *)

/**
 *    If the item on the stack at 'to_idx' is an sk_sidecar_t
 *    userdata, return the value cast to that type.  Otherwise return
 *    NULL.
 */
#define sk_lua_tosidecar(to_L, to_idx)                                  \
    SKLUA_TEST_TYPE(to_L, to_idx, SK_LUA_SIDECAR, sk_sidecar_t **)

/**
 *    If the item on the stack at 'to_idx' is an skipset_t userdata,
 *    return the value cast to that type.  Otherwise return NULL.
 */
#define sk_lua_toipset(to_L, to_idx)                                   \
    SKLUA_TEST_TYPE(to_L, to_idx, SK_LUA_IPSET, sk_lua_ipset_t *)

/**
 *    If the item on the stack at 'to_idx' is an skIPWildcard_t
 *    userdata, return the value cast to that type.  Otherwise return
 *    NULL.
 */
#define sk_lua_toipwildcard(to_L, to_idx)                                 \
    SKLUA_TEST_TYPE(to_L, to_idx, SK_LUA_IPWILDCARD, skIPWildcard_t *)

/**
 *    Push a ipaddr onto the Lua stack and return a pointer to it.
 */
skipaddr_t *
sk_lua_push_ipaddr(
    lua_State          *L);

/**
 *    Push an sktime_t onto the Lua stack as a datetime and return a
 *    pointer to it.
 */
sktime_t *
sk_lua_push_datetime(
    lua_State          *L);

/**
 *    Push an ipaddr onto the Lua stack and set it to the IPv6 address
 *    at ptr.
 */
void
sk_lua_push_ipv6_from_byte_ptr(
    lua_State          *L,
    const uint8_t      *ptr);

/**
 *    Push a read-only ipset.  This ipset will _not_ be destroyed on
 *    garbage collection.
 */
void
sk_lua_push_readonly_ipset(
    lua_State          *L,
    skipset_t          *ipset);

/**
 *    Push an rwRec onto the Lua stack.  If the 'rwrec' parameter is
 *    NULL, initialize the rwRec as an empty record.  When 'rwrec' is
 *    not NULL, copy the data from 'rwrec' to the new record.  Return
 *    a pointer to the new record.
 */
rwRec *
sk_lua_push_rwrec(
    lua_State          *L,
    const rwRec        *rwrec);


/*** IP set cache  ***/

typedef sk_rbtree_t sk_ipset_cache_t;

/**
 *    Create an IP set file cache
 */
sk_ipset_cache_t *
sk_ipset_cache_create(
    void);

/**
 *    Destroy an IP set file cache
 */
#define sk_ipset_cache_destroy(cache) sk_rbtree_destroy(cache)

/**
 *    Load an IP set, returning the cached copy if available.
 */
int
sk_ipset_cache_get_ipset(
    sk_ipset_cache_t   *cache,
    skipset_t         **ipset,
    const char         *path);


/********************/
/*** sklua-schema ***/
/********************/

/**
 *    Check whether the function argument 'arg' is a fixrec userdata
 *    and return the argument cast to a sk_fixrec_t **.  Raise an
 *    error if it is not.
 */
#define sk_lua_checkfixrec(check_L, check_arg)                          \
    SKLUA_CHECK_TYPE(check_L, check_arg, SK_LUA_FIXREC, sk_fixrec_t **)

/**
 *    Check whether the function argument 'arg' is a fixrec userdata
 *    and return the argument cast to a sk_fixrec_t **.  Return NULL
 *    if it is not.
 */
#define sk_lua_tofixrec(to_L, to_arg)                                   \
    SKLUA_TEST_TYPE(to_L, to_arg, SK_LUA_FIXREC, sk_fixrec_t **)

/**
 *    Do raw setup for sklua-schema functions.
 *    (luaopen_schema() calls this.)
 */
void
sk_lua_schema_init(
    lua_State          *L);

/**
 *    Push an sk_fixrec_t onto the stack.  This sk_fixrec_t will _not_
 *    be destroyed upon garbage collection.
 */
void
sk_lua_push_fixrec(
    lua_State          *L,
    sk_fixrec_t        *rec);

/**
 *    Register schema plug-in fields that were defined from within an
 *    application's Lua start-up file.
 *
 *    Any application wanting to support plug-in fields defined in Lua
 *    must call this function with its Lua state after loading the Lua
 *    files.
 */
int
sk_lua_plugin_register_fields(
    lua_State          *L);


/*****************************************************/
/*** Functions which are of the type lua_CFunction ***/
/*****************************************************/

/**
 *    Create an IPv4 address from the string, number, or ip address
 *    argument. (sklua-silk)
 */
int
sk_lua_ipaddr_create_v4(
    lua_State          *L);

/**
 *    Create a schema object.  Takes a variable number of IE
 *    identifier arguments.  See silk.schema()
 *    documentation. (sklua-schema)
 */
int
sk_lua_schema_create(
    lua_State          *L);

/**
 *    Create a fixrec object.  Takes a single fixrec arugment.
 *    (sklua-schema)
 */
int
sk_lua_fixrec_create(
    lua_State          *L);


/**
 *    Get the schema object from a fixrec. (sklua-schema)
 */
int
sk_lua_fixrec_get_schema(
    lua_State          *L);

/**
 *    Create, open, and return a stream object for writing.  Requires
 *    two arguments: a string that is the file name and a string that
 *    denotets the type of file to create, either "ipfix" or
 *    "silk". (sklua-schema)
 */
int
sk_lua_stream_open_writer(
    lua_State          *L);

/**
 *    Write a fixrec or an rwRec to a stream.  Arguments are the
 *    stream and the record.  For a fixrec, an optional schema object
 *    is allowed. (sklua-schema)
 */
int
sk_lua_stream_write(
    lua_State          *L);

/**
 *    Create and return an stream object open for reading.  Take a
 *    single string file name argument and an optional type string
 *    ("ipfix", "silk"). (sklua-schema)
 */
int
sk_lua_stream_open_reader(
    lua_State          *L);

/**
 *    Read and return a fixrec from the given stream object.  A
 *    second optional fixrec argument can be supplied as a fixrec to
 *    fill and return. (sklua-schema)
 */
int
sk_lua_stream_read(
    lua_State          *L);

/**
 *    A replacement for baselib loadfile() which uses an skstream_t as
 *    its base.  (sklua-utils)
 */
int
sk_lua_skstream_loadfile(
    lua_State          *L);

/**
 *    Create a sidecar userdata.
 *
 *    If 'sidecar' is NULL, a new empty sidecar object is created and
 *    wrapped as a Lua userdata.
 *
 *    Create a uservalue table for the userdata, and add to that table
 *    another table that contains a description of the sidecar
 *    elements.
 *
 *    If 'make_frozen' is set, the sidecar userdata is marked as
 *    frozen, and the table of sidecar elements is made read-only.
 */
void
sk_lua_push_sidecar(
    lua_State          *L,
    sk_sidecar_t       *sidecar,
    int                 make_frozen);

/*
 *    Expects a sidecar userdata on the stack.  Pops the sidecar and
 *    pushes a boolean value, true if frozen, false if not.
 */
int
sk_lua_sidecar_is_frozen(
    lua_State          *L);


/*** Lua State creation and destruction ***/

/**
 *    Create a new Lua state, load the standard Lua libraries into the
 *    state, load the SiLK modules into that state, and return the
 *    state.  Exit the program if a Lua state cannot be created.
 */
lua_State *
sk_lua_newstate(
    void);

/**
 *    Destroy (close) the Lua state at 'L' or (unlike lua_close()) do
 *    nothing if 'L' is NULL.
 */
void
sk_lua_closestate(
    lua_State          *L);


/*** Module creation ***/

/**
 *    Load the silk and schema modules into a global "silk"
 *    table.
 */
void
sk_lua_install_silk_modules(
    lua_State          *L);

/**
 *    If the first argument is a table, this will augment it with the
 *    exports of the silk module.  Otherwise it will return a new silk
 *    module.  (sklua-silk)
 */
int
luaopen_silk(
    lua_State          *L);

/**
 *    If the first argument is a table, this will augment it with the
 *    exports of the silk site module.  Otherwise it will return a new
 *    silk module.  (sklua-site)
 */
int
luaopen_silk_site(
    lua_State          *L);

/**
 *    If the first argument is a table, this will augment it with the
 *    exports of the schema module.  Otherwise it will return a
 *    new schema module.  (sklua-schema)
 */
int
luaopen_schema(
    lua_State          *L);

/**
 *    If the first argument is a table, this will augment it with the
 *    exports of the silkutils module.  Otherwise it will return a new
 *    silkutils module.  (sklua-silkutils)
 */
int
luaopen_silkutils(
    lua_State          *L);


#ifdef __cplusplus
}
#endif
#endif /* _SKLUA_H */

/*
** Local Variables:
** mode:c
** indent-tabs-mode:nil
** c-basic-offset:4
** End:
*/
