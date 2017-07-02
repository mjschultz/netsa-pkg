/*
** Copyright (C) 2014-2017 by Carnegie Mellon University.
**
** @OPENSOURCE_LICENSE_START@
** See license information in ../../LICENSE.txt
** @OPENSOURCE_LICENSE_END@
*/

/*
**  SilK site bindings for lua
**
*/


#include <silk/silk.h>

RCSIDENT("$SiLK: sklua-site.c efd886457770 2017-06-21 18:43:23Z mthomas $");

#include <silk/sklua.h>
#include <silk/sksite.h>

/* LOCAL DEFINES AND TYPEDEFS */

/* LOCAL VARIABLE DEFINITIONS */

static const uint8_t sk_lua_init_blob[] = {
#include "lua/silk-site.i"
};


/* FUNCTION DEFINITIONS */

static int
sk_lua_site_get_class_info(
    lua_State *L)
{
    sk_class_iter_t iter;
    sk_class_id_t   class_id;
    char            name[SK_MAX_STRLEN_FLOWTYPE+1];

    /* Class table */
    lua_newtable(L);

    /* default class (if any) */
    class_id = sksiteClassGetDefault();
    if (class_id != SK_INVALID_CLASS) {
        lua_pushinteger(L, class_id);
        lua_setfield(L, -2, "default");
    }

    /* Class data table */
    lua_newtable(L);
    sksiteClassIterator(&iter);
    while (sksiteClassIteratorNext(&iter, &class_id)) {
        sk_sensor_iter_t    sensor_iter;
        sk_sensor_id_t      sensor;
        sk_flowtype_iter_t  flowtype_iter;
        sk_flowtype_id_t    flowtype;
        lua_Integer         i;

        lua_createtable(L, 0, 5);

        /* id */
        lua_pushinteger(L, class_id);
        lua_setfield(L, -2, "id");

        /* name */
        sksiteClassGetName(name, sizeof(name), class_id);
        lua_pushstring(L, name);
        lua_setfield(L, -2, "name");

        /* sensors */
        lua_newtable(L);
        sksiteClassSensorIterator(class_id, &sensor_iter);
        for (i = 1; sksiteSensorIteratorNext(&sensor_iter, &sensor); ++i) {
            lua_pushinteger(L, sensor);
            lua_rawseti(L, -2, i);
        }
        lua_setfield(L, -2, "sensors");

        /* flowtypes */
        lua_newtable(L);
        sksiteClassFlowtypeIterator(class_id, &flowtype_iter);
        for (i = 1; sksiteFlowtypeIteratorNext(&flowtype_iter, &flowtype); ++i) {
            lua_pushinteger(L, flowtype);
            lua_rawseti(L, -2, i);
        }
        lua_setfield(L, -2, "flowtypes");

        /* default_flowtypes */
        lua_newtable(L);
        sksiteClassDefaultFlowtypeIterator(class_id, &flowtype_iter);
        for(i = 1; sksiteFlowtypeIteratorNext(&flowtype_iter, &flowtype); ++i) {
            lua_pushinteger(L, flowtype);
            lua_rawseti(L, -2, i);
        }
        lua_setfield(L, -2, "default_flowtypes");

        lua_rawseti(L, -2, class_id);
    }
    lua_setfield(L, -2, "data");

    return 1;
}

static int
sk_lua_site_get_flowtype_info(
    lua_State *L)
{
    sk_flowtype_iter_t  flowtype_iter;
    sk_flowtype_id_t    flowtype;
    char                name[SK_MAX_STRLEN_SENSOR+1];

    lua_newtable(L);
    sksiteFlowtypeIterator(&flowtype_iter);
    while (sksiteFlowtypeIteratorNext(&flowtype_iter, &flowtype)) {
        sk_class_id_t class_id;

        lua_createtable(L, 0, 4);

        /* id */
        lua_pushinteger(L, flowtype);
        lua_setfield(L, -2, "id");

        /* name */
        sksiteFlowtypeGetName(name, sizeof(name), flowtype);
        lua_pushstring(L, name);
        lua_setfield(L, -2, "name");

        /* type */
        sksiteFlowtypeGetType(name, sizeof(name), flowtype);
        lua_pushstring(L, name);
        lua_setfield(L, -2, "type");

        /* class */
        class_id = sksiteFlowtypeGetClassID(flowtype);
        lua_pushinteger(L, class_id);
        lua_setfield(L, -2, "class");

        lua_rawseti(L, -2, flowtype);
    }
    return 1;
}

static int
sk_lua_site_get_sensor_info(
    lua_State *L)
{
    sk_sensor_iter_t    sensor_iter;
    sk_sensor_id_t      sensor;
    char                name[SK_MAX_STRLEN_SENSOR+1];

    lua_newtable(L);
    sksiteSensorIterator(&sensor_iter);
    while (sksiteSensorIteratorNext(&sensor_iter, &sensor)) {
        sk_class_iter_t class_iter;
        sk_class_id_t   class_id;
        const char     *desc;
        lua_Integer     i;

        lua_createtable(L, 0, 4);

        /* id */
        lua_pushinteger(L, sensor);
        lua_setfield(L, -2, "id");

        /* name */
        sksiteSensorGetName(name, sizeof(name), sensor);
        lua_pushstring(L, name);
        lua_setfield(L, -2, "name");

        /* description (this works even if desc is nil) */
        desc = sksiteSensorGetDescription(sensor);
        lua_pushstring(L, desc);
        lua_setfield(L, -2, "description");

        /* classes */
        lua_newtable(L);
        sksiteSensorClassIterator(sensor, &class_iter);
        for (i = 1; sksiteClassIteratorNext(&class_iter, &class_id); ++i) {
            lua_pushinteger(L, class_id);
            lua_rawseti(L, -2, i);
        }
        lua_setfield(L, -2, "classes");

        lua_rawseti(L, -2, sensor);
    }
    return 1;
}

static int
sk_lua_site_configured(
    lua_State *L)
{
    lua_pushboolean(L, sksiteIsConfigured());
    return 1;
}

static int
sk_lua_init_site(
    lua_State *L)
{
    static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
    const char *site_path;
    const char *rootdir_path;
    int rv;
    int verbose;

    site_path = luaL_optstring(L, 1, NULL);
    rootdir_path = luaL_optstring(L, 2, NULL);
    verbose = lua_toboolean(L, 3);

    pthread_mutex_lock(&mutex);
    if (site_path && sksiteSetConfigPath(site_path)) {
        pthread_mutex_unlock(&mutex);
        return luaL_error(L, "Site already configured or path too long");
    }
    if (rootdir_path && sksiteSetRootDir(rootdir_path)) {
        pthread_mutex_unlock(&mutex);
        return luaL_error(L, "Rootdir is empty or too long");
    }
    rv = sksiteConfigure(verbose);
    pthread_mutex_unlock(&mutex);
    switch (rv) {
      case 0:
        lua_pushboolean(L, 1);
        break;
      case -1:
        return luaL_error(L, "Error loading site configuration");
      case -2:
        lua_pushboolean(L, 0);
        break;
      default:
        skAbortBadCase(rv);
    }

    return 1;
}

static const luaL_Reg sk_lua_site_module_internal_functions[] = {
    {"site_configured",     sk_lua_site_configured},
    {"init_site",           sk_lua_init_site},
    {"get_sensor_info",     sk_lua_site_get_sensor_info},
    {"get_class_info",      sk_lua_site_get_class_info},
    {"get_flowtype_info",   sk_lua_site_get_flowtype_info},
    {NULL, NULL}
};

int
luaopen_silk_site(
    lua_State          *L)
{
    int inittable;

    inittable = lua_istable(L, 1);

    /* Check lua versions */
    luaL_checkversion(L);

    /* Load the lua portion */
    luaL_newlib(L, sk_lua_site_module_internal_functions);
    if (inittable) {
        lua_pushvalue(L, 1);
    } else {
        lua_newtable(L);
    }
    sk_lua_load_lua_blob(L, sk_lua_init_blob, sizeof(sk_lua_init_blob),
                         "silk-site.lua", 2, 1);
    return 1;
}

/*
** Local Variables:
** mode:c
** indent-tabs-mode:nil
** c-basic-offset:4
** End:
*/
