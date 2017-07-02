/*
** Copyright (C) 2014-2017 by Carnegie Mellon University.
**
** @OPENSOURCE_LICENSE_START@
** See license information in ../../LICENSE.txt
** @OPENSOURCE_LICENSE_END@
*/

/*
**  Lua utility functions
**
*/


#include <silk/silk.h>

RCSIDENT("$SiLK: sklua-utils.c efd886457770 2017-06-21 18:43:23Z mthomas $");

#include <silk/sklua.h>
#include <silk/skstream.h>

#define LUA_SKSTREAM_CHUNK_SIZE 8096

#define SK_LUA_SILKUTILS "silkutils"

#define SK_LUA_SILKUTILS "silkutils"

/* LOCAL VARIABLE DEFINITIONS */

static const uint8_t sk_lua_utils_init_blob[] = {
#include "lua/silkutils.i"
};

/* FUNCTION DEFINITIONS */

const char *
sk_lua_checklstring(
    lua_State          *L,
    int                 narg,
    size_t             *len)
{
    if (lua_type(L, narg) != LUA_TSTRING) {
        sk_lua_argerror(L, narg, "string expected, got %s",
                        sk_lua_typename(L, narg));
        return NULL;
    }
    return lua_tolstring(L, narg, len);
}

lua_Unsigned
sk_lua_checkunsigned(
    lua_State          *L,
    int                 narg)
{
    int isnum;
    lua_Integer i;

    i = lua_tointegerx(L, narg, &isnum);
    if (!isnum) {
        sk_lua_argerror(L, narg, "%s expected, got %s",
                        lua_typename(L, LUA_TNUMBER),
                        sk_lua_typename(L, narg));
    } else if (i < 0) {
        sk_lua_argerror(L, narg,"unsigned number expected, got signed number");
    }
    return (lua_Unsigned)i;
}

void
sk_lua_add_to_object_table(
    lua_State              *L,
    int                     index,
    const sk_lua_object_t  *objs)
{
    int idx = lua_absindex(L, index);

    while (objs->name) {
        lua_createtable(L, 0, 4);
        if (objs->constructor) {
            lua_pushcfunction(L, objs->constructor);
            lua_setfield(L, -2, "constructor");
        }
        luaL_newmetatable(L, objs->ident);
        if (objs->metatable) {
            luaL_setfuncs(L, objs->metatable, 0);
        }
        lua_setfield(L, -2, "metatable");
        lua_newtable(L);
        if (objs->methods) {
            luaL_setfuncs(L, objs->methods, 0);
        }
        lua_setfield(L, -2, "methods");
        lua_newtable(L);
        if (objs->static_methods) {
            luaL_setfuncs(L, objs->static_methods, 0);
        }
        lua_setfield(L, -2, "static_methods");
        lua_setfield(L, idx, objs->name);
        ++objs;
    }
}

/* Garbage collection function for tables created using
 * sk_lua_create_gc_table */
static int
sk_lua_gc_table_gc(
    lua_State          *L)
{
    /* fprintf(stderr, ("in sk_lua_gc_table_gc(%p)," */
    /*                  " object at top of stack is %s\n"), */
    /*         (void *)L, luaL_typename(L, 1)); */
    lua_pushnil(L);
    while (lua_next(L, 1) != 0) {
        sk_lua_free_fn_t free_fn;
        void *obj;

        /* fprintf(stderr, "lua_next(%p) ok key: %s\n", */
        /*         (void *)L, luaL_typename(L, -2)); */
        obj = lua_touserdata(L, -2);
        *((void **)&free_fn) = lua_touserdata(L, -1);
        /* fprintf(stderr, "invoking free_fn %p on object %p\n", */
        /*         (void *)free_fn, (void *)obj); */
        free_fn(obj);
        lua_pop(L, 1);
        /* fprintf(stderr, "exiting sk_lua_gc_table_gc(%p)\n", (void *)L); */
    }
    return 0;
}

static void
sk_lua_push_gc_table(
    lua_State          *L)
{
    /* Create a table and set up a metatable with
     * sk_lua_gc_table_gc as its garbage collection function */
    lua_newtable(L);
    /* fprintf(stderr, ("in sk_lua_push_gc_table(%p)" */
    /*                  " newtable is at index %d\n"), */
    /*         (void *)L, lua_absindex(L, -1)); */
    lua_createtable(L, 0, 1);
    lua_pushcfunction(L, sk_lua_gc_table_gc);
    lua_setfield(L, -2, "__gc");
    lua_setmetatable(L, -2);
}

void
sk_lua_push_protected_pointer(
    lua_State          *L,
    void               *ptr,
    sk_lua_free_fn_t    free_fn)
{
    /* fprintf(stderr, "in sk_lua_push_protected_pointer(%p, %p, %p)\n", */
    /*         (void *)L, (void *)ptr, (void *)free_fn); */
    sk_lua_push_gc_table(L);
    lua_pushlightuserdata(L, ptr);
    lua_pushlightuserdata(L, *(void **)&free_fn);
    lua_rawset(L, -3);
}

int
sk_lua_create_gc_table(
    lua_State          *L)
{
    /* fprintf(stderr, "in sk_lua_create_gc_table(%p)\n", (void *)L); */
    sk_lua_push_gc_table(L);
    return luaL_ref(L, LUA_REGISTRYINDEX);
}

void
sk_lua_gc_protect_pointer(
    lua_State          *L,
    int                 tref,
    void               *ptr,
    sk_lua_free_fn_t    free_fn)
{
    /* fprintf(stderr, "in sk_lua_gc_protect_pointer(%p, %d [%d], %p, %p)\n",*/
    /*         (void *)L, tref, lua_absindex(L, tref), */
    /*         (void *)ptr, (void *)free_fn); */
    lua_rawgeti(L, LUA_REGISTRYINDEX, tref);
    lua_pushlightuserdata(L, ptr);
    if (free_fn) {
        lua_pushlightuserdata(L, *(void**)&free_fn);
    } else {
        lua_pushnil(L);
    }
    lua_rawset(L, -3);
    lua_pop(L, 1);
}

static void
sk_lua_skstream_pusherror(
    lua_State          *L,
    skstream_t         *stream)
{
    char err[1024];
    int rv;
    rv = skStreamLastErrMessage(stream,
                                skStreamGetLastReturnValue(stream),
                                err, sizeof(err));
    if (rv >= (int)sizeof(err)) {
        err[sizeof(err) - 1] = '\0';
    }
    lua_pushlstring(L, err, rv);
}

typedef struct sk_lua_streambuf_st {
    skstream_t *stream;
    size_t size;
    char *buf;
} sk_lua_streambuf_t;


static const char *
sk_lua_skstream_read(
    lua_State          *L,
    void               *data,
    size_t             *size)
{
    sk_lua_streambuf_t *sbuf = (sk_lua_streambuf_t *)data;
    ssize_t count;

    count = skStreamRead(sbuf->stream, sbuf->buf, sbuf->size);
    if (count < 0) {
        sk_lua_skstream_pusherror(L, sbuf->stream);
        lua_error(L);
        return NULL;
    }
    *size = (size_t)(count);
    return sbuf->buf;
}

static int
sk_lua_skstream_load_helper(
    lua_State          *L)
{
    void *data;
    const char *source;
    const char *mode;
    int rv;

    data = lua_touserdata(L, 1);
    source = lua_tostring(L, 2);
    mode = lua_tostring(L, 3);

    rv = lua_load(L, sk_lua_skstream_read, data, source, mode);
    if (rv != LUA_OK) {
        return lua_error(L);
    }
    return 1;
}

int
sk_lua_skstream_loadfile(
    lua_State          *L)
{
    int err = 0;
    sk_lua_streambuf_t sbuf;
    const char *filename;
    int top;

    top = lua_gettop(L);
    filename = sk_lua_checkstring(L, 1);

    /* Create buffer in which to read data chunks */
    sbuf.buf = (char *)malloc(LUA_SKSTREAM_CHUNK_SIZE);
    if (sbuf.buf == NULL) {
        return luaL_error(
            L, "Memory allocation failure in sk_lua_skstream_load");
    }
    sbuf.size = LUA_SKSTREAM_CHUNK_SIZE;

    /* Open the stream */
    if (skStreamCreate(&sbuf.stream, SK_IO_READ, SK_CONTENT_OTHERBINARY)
        || skStreamBind(sbuf.stream, filename)
        || skStreamOpen(sbuf.stream))
    {
        sk_lua_skstream_pusherror(L, sbuf.stream);
        goto ERR;
    }

    lua_pushcfunction(L, sk_lua_skstream_load_helper);
    lua_pushlightuserdata(L, (void *)&sbuf); /* Push buffer & stream */
    lua_pushliteral(L, "@");
    lua_pushstring(L, skStreamGetPathname(sbuf.stream));
    lua_concat(L, 2);                        /* Push name */
    if (top >= 2) {                          /* Push mode */
        lua_pushvalue(L, 2);
    } else {
        lua_pushnil(L);
    }
    if (lua_pcall(L, 3, 1, 0) != LUA_OK) {   /* Call load */
        goto ERR;
    }
    if (top >= 3) {                          /* Set env upvalue */
        lua_pushvalue(L, 3);
        if (!lua_setupvalue(L, -2, 1)) {
            lua_pop(L, 1);
        }
    }

  CLEANUP:
    free(sbuf.buf);
    skStreamDestroy(&sbuf.stream);
    if (err) {
        return lua_error(L);
    }
    return 1;

  ERR:
    err = 1;
    goto CLEANUP;
}

/* Push a weak table onto the stack.  The type should be "k" for weak
 * keys, "v" for weak values, or "kv" for weak keys and values.  */
void
sk_lua_create_weaktable(
    lua_State          *L,
    const char         *type)
{
    /* the table to return */
    lua_createtable(L, 0, 0);
    /* the meta table of the returned table */
    lua_createtable(L, 0, 1);
    lua_pushstring(L, type);
    lua_setfield(L, -2, "__mode");
    lua_setmetatable(L, -2);
}

/* look for keys in table at index 't' not named in 'table_keys' */
size_t
sk_lua_check_table_unknown_keys(
    lua_State          *L,
    const int           t,
    ssize_t             num_table_keys,
    const char         *table_keys[],
    void              (*unknown_key_callback)(const char *key, void *cb_data),
    void               *cb_data)
{
#ifndef NDEBUG
    int gettop = lua_gettop(L);
#endif
    const char *key;
    size_t count;
    size_t i;
    int cmp;

    count = 0;

    /* check for unexpected keys in the table; push a dummy first key
     * that lua_next() can pop */
    lua_pushnil(L);
    while (lua_next(L, t) != 0) {
        /* 'key' is at index -2 and 'value' is at index -1 */
        /* push a copy of 'key' onto the stack so that we can
         * manipulate it to a string */
        lua_pushvalue(L, -2);
        key = lua_tostring(L, -1);
        if (NULL == key) {
            ++count;
            if (unknown_key_callback) {
                unknown_key_callback(NULL, cb_data);
            }
        } else {
            cmp = 1;
            if (num_table_keys < 0) {
                for (i = 0; cmp != 0 && NULL != table_keys[i]; ++i) {
                    cmp = strcmp(key, table_keys[i]);
                }
            } else {
                for (i = 0; cmp != 0 && i < (size_t)num_table_keys; ++i) {
                    if (table_keys[i]) {
                        cmp = strcmp(key, table_keys[i]);
                    }
                }
            }
            if (cmp) {
                ++count;
                if (unknown_key_callback) {
                    unknown_key_callback(key, cb_data);
                }
            }
        }
        /* remove copy of 'key' and 'value' from stack; keep original
         * 'key' for next iteration */
        lua_pop(L, 2);
    }

    assert(lua_gettop(L) == gettop);
    return count;
}

/* C-wrapper over the make_table_read_only() function implemented in
 * silkutils.lua */
int
sk_lua_make_table_read_only(
    lua_State          *L)
{
    luaL_checktype(L, 1, LUA_TTABLE);
    /* run silkutils.make_table_read_only(t).  lua_rotate() moves the
     * table argument to the top of the stack */
    lua_getfield(L, LUA_REGISTRYINDEX, SK_LUA_SILKUTILS);
    lua_getfield(L, -1, "make_table_read_only");
    lua_rotate(L, 1, -1);
    lua_call(L, 1, 1);
    return 1;
}


/* Loads a lua binary blob.  Ensures that the silkutils module has
 * been loaded */
void
sk_lua_load_lua_blob(
    lua_State          *L,
    const uint8_t      *blob,
    size_t              blob_size,
    const char         *blob_name,
    int                 nargs,
    int                 nresults)
{
    int rv;

    /* Get silkutils module */
    if (lua_getfield(L, LUA_REGISTRYINDEX, SK_LUA_SILKUTILS) == LUA_TNIL) {
        lua_pop(L, 1);
        lua_pushcfunction(L, luaopen_silkutils);
        lua_call(L, 0, 1);
        lua_pushvalue(L, -1);
        lua_setfield(L, LUA_REGISTRYINDEX, SK_LUA_SILKUTILS);
        lua_setglobal(L, SK_LUA_SILKUTILS);
    } else {
        lua_pop(L, 1);
    }

    /* Load the blob */
    rv = luaL_loadbufferx(L, (const char *)blob, blob_size, blob_name, "b");
    if (rv != LUA_OK) {
        lua_error(L);
    }

    /* Call the blob */
    lua_insert(L, -nargs - 1);
    lua_call(L, nargs, nresults);
}

const char *
sk_lua_typename(
    lua_State          *L,
    int                 arg)
{
    const char *tname;
    if (luaL_getmetafield(L, arg, "__name") == LUA_TSTRING) {
        /* since value is a string from the object's metatable, assume
         * we can safely return it even though we pop it from the
         * stack */
        tname = lua_tostring(L, -1);
        lua_pop(L, 1);
        return tname;
    }
    if (lua_type(L, arg) == LUA_TLIGHTUSERDATA) {
        return "light userdata";
    }
    return luaL_typename(L, arg);
}

int
sk_lua_argerror(
    lua_State          *L,
    int                 arg,
    const char         *format,
    ...)
{
    va_list vargs;
    const char *msg;

    va_start(vargs, format);
    msg = lua_pushvfstring(L, format, vargs);
    va_end(vargs);

    return luaL_argerror(L, arg, msg);
}


/* Output a pointer string for a userdata object */
static int
sk_lua_get_pointer_string(
    lua_State          *L)
{
    void *addr = lua_touserdata(L, 1);
    if (addr == NULL) {
        return luaL_argerror(L, 1, "Not a userdata value");
    }
    lua_pushfstring(L, "%p", addr);
    return 1;
}

/* Functions that are exported to silkutils.lua, but are not meant
 * to be exported to "users" */
static const luaL_Reg sk_lua_utils_internal_fns[] = {
    {"get_pointer_string", sk_lua_get_pointer_string},
    {NULL, NULL}
};

int
luaopen_silkutils(
    lua_State          *L)
{
    int rv;
    int noarg;

    /* Check lua versions */
    luaL_checkversion(L);

    if (!(noarg = lua_isnoneornil(L, 1))) {
        luaL_checktype(L, 1, LUA_TTABLE);
    }

    luaL_openlibs(L);

    /* Load the lua portion */
    rv = luaL_loadbufferx(L, (const char *)sk_lua_utils_init_blob,
                          sizeof(sk_lua_utils_init_blob),
                          "silkutils.lua", "b");
    if (rv != LUA_OK) {
        return lua_error(L);
    }

    /* Pass the internal functions to the silkutils.lua file */
    luaL_newlib(L, sk_lua_utils_internal_fns);
    lua_call(L, 1, 1);

    if (noarg) {
        lua_pushnil(L);
    } else {
        lua_pushvalue(L, 1);
    }

    /* This calls the install_silkutils function returned by
     * silkutils.lua.  It takes a nil or module argument, and returns
     * a silkutils module. */
    lua_call(L, 1, 1);

    return 1;
}


/*
** Local Variables:
** mode:c
** indent-tabs-mode:nil
** c-basic-offset:4
** End:
*/
