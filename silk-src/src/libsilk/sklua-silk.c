/*
** Copyright (C) 2014-2017 by Carnegie Mellon University.
**
** @OPENSOURCE_LICENSE_START@
** See license information in ../../LICENSE.txt
** @OPENSOURCE_LICENSE_END@
*/

/*
**  SilK binding for lua
**
*/


#include <silk/silk.h>

RCSIDENT("$SiLK: sklua-silk.c efd886457770 2017-06-21 18:43:23Z mthomas $");

#include <silk/rwrec.h>
#include <silk/skcountry.h>
#include <silk/skipset.h>
#include <silk/sklua.h>
#include <silk/skprefixmap.h>
#include <silk/skstream.h>
#include <silk/sksite.h>
#include <silk/utils.h>

/* LOCAL DEFINES AND TYPEDEFS */

/* Lua-based init code index */
#define SK_LUA_INIT   "sk_lua_silk_init"

/* Lua ipset container */
typedef struct sk_lua_ipset_st {
    skipset_t *ipset;
    unsigned   readonly : 1;
} sk_lua_ipset_t;

/* IPset cache file item */
typedef struct ipset_file_item_st {
    skipset_t          *ipset;
    dev_t               dev;
    ino_t               ino;
} ipset_file_item_t;

/* char_buf_t holds a character array and its length.  The char_buf_t
 * can be used by sk_lua_push_protected_pointer() while allowing safe
 * reallocation of the character array. */
struct char_buf_st {
    char               *buf;
    size_t              len;
};
typedef struct char_buf_st char_buf_t;


#define SK_LUA_SIDECAR_ELEM     "silk.sidecar_elem"



/**
 *    Check whether the function argument 'arg' is an sk_bitmap_t
 *    userdata and return the argument cast to that type.
 */
#define sk_lua_checkbitmap(check_L, check_arg)                          \
    SKLUA_CHECK_TYPE(check_L, check_arg, SK_LUA_BITMAP, sk_bitmap_t **)

/**
 *    Check whether the function argument 'arg' is an skipset_t
 *    userdata and return the argument cast to that type.
 */
#define sk_lua_checkipset(check_L, check_arg)                           \
    SKLUA_CHECK_TYPE(check_L, check_arg, SK_LUA_IPSET, sk_lua_ipset_t *)

/**
 *    Check whether the function argument 'arg' is an skIPWildcard_t
 *    userdata and return the argument cast to that type.
 */
#define sk_lua_checkipwildcard(check_L, check_arg)                      \
    SKLUA_CHECK_TYPE(check_L, check_arg, SK_LUA_IPWILDCARD, skIPWildcard_t *)

/**
 *    Check whether the function argument 'arg' is an sk_lua_pamp_t
 *    userdata and return the argument cast to that type.
 */
#define sk_lua_checkpmap(check_L, check_arg)                            \
    SKLUA_CHECK_TYPE(check_L, check_arg, SK_LUA_PMAP, sk_lua_pmap_t *)

/**
 *    Check whether the function argument 'arg' is an sk_lua_sc_elem_t
 *    userdata and return the argument cast to that type.
 */
#define sk_lua_checksidecarelem(check_L, check_arg)                     \
    SKLUA_CHECK_TYPE(check_L, check_arg, SK_LUA_SIDECAR_ELEM, sk_lua_sc_elem_t*)

/**
 *    Check whether the function argument 'arg' is an sk_lua_sc_elem_t
 *    userdata and return the argument cast to that type.
 */
#define sk_lua_tosidecarelem(to_L, to_arg)                              \
    SKLUA_TEST_TYPE(to_L, to_arg, SK_LUA_SIDECAR_ELEM, sk_lua_sc_elem_t*)


/* LOCAL VARIABLE DEFINITIONS */

static const uint8_t sk_lua_init_blob[] = {
#include "lua/silk.i"
};

static char error_buffer[2 * PATH_MAX];


/* FUNCTION DEFINITIONS */

#ifdef TEST_PRINTF_FORMATS
#define error_printf  printf
#else  /* TEST_PRINTF_FORMATS */

static int
error_printf(
    const char         *fmt,
    ...)
{
    int rv;

    va_list args;
    va_start(args, fmt);

SK_DIAGNOSTIC_FORMAT_NONLITERAL_PUSH

    rv = vsnprintf(error_buffer, sizeof(error_buffer), fmt, args);

SK_DIAGNOSTIC_FORMAT_NONLITERAL_POP

    error_buffer[sizeof(error_buffer) - 1] = '\0';
    va_end(args);

    return rv;
}
#endif  /* TEST_PRINTF_FORMATS */


/*
 *    Free a char_buf_t.
 */
static void
char_buf_free(
    char_buf_t         *cbuf)
{
    if (cbuf) {
        free(cbuf->buf);
        free(cbuf);
    }
}



/*
 *  ********************************************************************
 *  IP ADDRESS
 *  ********************************************************************
 */

skipaddr_t *
sk_lua_push_ipaddr(
    lua_State          *L)
{
    skipaddr_t *addr = sk_lua_newuserdata(L, skipaddr_t);
    luaL_setmetatable(L, SK_LUA_IPADDR);

    return addr;
}

static int
sk_lua_ipaddr_eq(
    lua_State          *L)
{
    const skipaddr_t *a = sk_lua_checkipaddr(L, 1);
    const skipaddr_t *b = sk_lua_checkipaddr(L, 2);

    lua_pushboolean(L, skipaddrCompare(a, b) == 0);
    return 1;
}

static int
sk_lua_ipaddr_lt(
    lua_State          *L)
{
    const skipaddr_t *a = sk_lua_checkipaddr(L, 1);
    const skipaddr_t *b = sk_lua_checkipaddr(L, 2);

    lua_pushboolean(L, skipaddrCompare(a, b) < 0);
    return 1;
}

static int
sk_lua_ipaddr_gt(
    lua_State          *L)
{
    const skipaddr_t *a = sk_lua_checkipaddr(L, 1);
    const skipaddr_t *b = sk_lua_checkipaddr(L, 2);

    lua_pushboolean(L, skipaddrCompare(a, b) > 0);
    return 1;
}

/*
 * =pod
 *
 * =item silk.B<ipaddr_to_string(>I<ipaddr>[, I<form>])B<)>
 *
 * Return a human-readable representation of I<ipaddr>.  If the string
 * I<form> is supplied, it specifies how the IP address is
 * converted to a string.  I<form> defaults to C<canonical>.  I<form>
 * should be one of:
 *
 * =over 4
 *
 * =item canonical
 *
 * Return the address in canonical form: dotted quad for IPv4
 * (C<127.0.0.1>) and hexadectet for IPv6 (C<2001:db8::1>).  Note that
 * IPv6 addresses in ::ffff:0:0/96 and some IPv6 addresses in ::/96
 * are returned as a mixture of IPv6 and IPv4.
 *
 * =item zero-padded
 *
 * Return the address in canonical form, but fully expand every octet
 * or hexadectet with the maximum number of zeros.  The addresses
 * C<127.0.0.1> and C<2001:db8::1> are returned as C<127.000.000.001>
 * and C<2001:0db8:0000:0000:0000:0000:0000:0001>, respectively.
 *
 * =item decimal
 *
 * Return a string showing the address as an integer in decimal
 * format.  The addresses C<127.0.0.1> and C<2001:db8::1> are returned
 * as C<2130706433> and C<42540766411282592856903984951653826561>,
 * respectively.
 *
 * =item hexadecimal
 *
 * Return a string showing the address as an integer in hexadecimal
 * format.  The addresses C<127.0.0.1> and C<2001:db8::1> are returned
 * as C<7f000001> and C<20010db8000000000000000000000001>,
 * respectively.
 *
 * =item force-ipv6
 *
 * Return the addresses in the canonical form for IPv6 without using
 * any IPv4 notation.  Any IPv4 address is mapped into the
 * ::ffff:0:0/96 netblock.  The addresses C<127.0.0.1> and
 * C<2001:db8::1> are returned as C<::ffff:7f00:1> and C<2001:db8::1>,
 * respectively.
 *
 * =back
 *
 * =cut
 */
static int
sk_lua_ipaddr_to_string(
    lua_State          *L)
{
    static const char *print_options[] = {
        "canonical", "zero-padded", "decimal", "hexadecimal", "force-ipv6",
        NULL
    };
    char buf[SK_NUM2DOT_STRLEN];
    const skipaddr_t *addr;
    uint32_t flag;

    addr = sk_lua_checkipaddr(L, 1);
    flag = luaL_checkoption(L, 2, "canonical", print_options);
    lua_pushstring(L, skipaddrString(buf, addr, flag));

    return 1;
}

/*
 * =pod
 *
 * =item silk.B<ipaddr_to_bytes(>I<ipaddr>B<)>
 *
 * Return a string which represents the raw bytes of the I<ipaddr> in
 * network byte order.  The resulting string is of length 4 for
 * IPv4 addresses, and of length 16 for IPv6 addresses.  The resulting
 * string my contain embedded zeroes.
 *
 * =cut
 */
static int
sk_lua_ipaddr_to_bytes(
    lua_State          *L)
{
    const skipaddr_t *addr = sk_lua_checkipaddr(L, 1);
    if (skipaddrIsV6(addr)) {
        char buf[16];
        skipaddrGetV6(addr, buf);
        lua_pushlstring(L, buf, 16);
    } else {
        uint32_t val = htonl(skipaddrGetV4(addr));
        lua_pushlstring(L, (const char *)&val, 4);
    }

    return 1;
}

/*
 * =pod
 *
 * =item silk.B<ipaddr_is_ipv6(>I<ipaddr>B<)>
 *
 * Return B<true> if I<ipaddr> is an IPv6 address, B<false> if it is
 * an IPv4 address.
 *
 * =cut
 */
static int
sk_lua_ipaddr_is_ipv6(
    lua_State          *L)
{
    const skipaddr_t *addr = sk_lua_checkipaddr(L, 1);
    lua_pushboolean(L, skipaddrIsV6(addr));

    return 1;
}

/*
 * =pod
 *
 * =item silk.B<ipaddr_to_ipv6(>I<ipaddr>B<)>
 *
 * If I<ipaddr> is an IPv6 address, return I<ipaddr>.  Otherwise,
 * return a new ipaddr mapping I<ipaddr> into the ::ffff:0:0/96
 * prefix.
 *
 * =cut
 */
static int
sk_lua_ipaddr_to_ipv6(
    lua_State          *L)
{
    const skipaddr_t *addr = sk_lua_checkipaddr(L, 1);

    if (!skipaddrIsV6(addr)) {
        skipaddr_t *v6addr = sk_lua_push_ipaddr(L);
        skipaddrV4toV6(addr, v6addr);
    }

    return 1;
}

/*
 * =pod
 *
 * =item silk.B<ipaddr_to_ipv4(>I<ipaddr>B<)>
 *
 * If I<ipaddr> is an IPv4 address, return I<ipaddr>.  If I<ipaddr> is
 * in the ::ffff:0:0/96 prefix, return a new ipaddr containing the
 * IPv4 address.  Otherwise, return B<nil>.
 *
 * =cut
 */
static int
sk_lua_ipaddr_to_ipv4(
    lua_State          *L)
{
    const skipaddr_t *addr = sk_lua_checkipaddr(L, 1);

    if (skipaddrIsV6(addr)) {
        skipaddr_t *v4addr = sk_lua_push_ipaddr(L);
        if (skipaddrV6toV4(addr, v4addr)) {
            lua_pushnil(L);
        }
    }

    return 1;
}

/*
 * =pod
 *
 * =item silk.B<ipaddr_octets(>I<ipaddr>B<)>
 *
 * Return a sequence of the octets of I<ipaddr>.  This is a sequence
 * of four integers for an IPv4 address and a sequence of sixteen
 * integers for an IPv6 address.  In both cases,
 * B<ipaddr_octets(I<ipaddr>)[1]> is the most significant byte of the
 * address.
 *
 * =cut
 */
static int
sk_lua_ipaddr_octets(
    lua_State          *L)
{
    int i;
    const skipaddr_t *addr = sk_lua_checkipaddr(L, 1);

    if (skipaddrIsV6(addr)) {
        uint8_t v6[16];
        skipaddrGetV6(addr, v6);
        lua_createtable(L, 16, 0);
        for (i = 0; i < 16; ++i) {
            lua_pushinteger(L, v6[i]);
            lua_rawseti(L, -2, i + 1);
        }
    } else {
        uint32_t v4 = skipaddrGetV4(addr);
        lua_createtable(L, 4, 0);
        for (i = 3; i >= 0; --i) {
            lua_pushinteger(L, v4 & 0xff);
            lua_rawseti(L, -2, i + 1);
            v4 >>= 8;
        }
    }

    return 1;
}

/*
 * =pod
 *
 * =item silk.B<ipaddr_to_int(>I<ipaddr>B<)>
 *
 * Return the integer representation of I<ipaddr>.  For an IPv4
 * address, this is a 32-bit integer.  For an IPv6 address, this is a
 * floating-point approximation of the actual 128-bit number.
 *
 * =cut
 */
static int
sk_lua_ipaddr_to_int(
    lua_State          *L)
{
    const skipaddr_t *addr;

    addr = sk_lua_checkipaddr(L, 1);
    if (!skipaddrIsV6(addr)) {
        lua_Unsigned n;

        n = skipaddrGetV4(addr);
        lua_pushinteger(L, n);
    } else {
        uint8_t v6[16];
        lua_Number n = 0;
        int i;

        skipaddrGetAsV6(addr, v6);
        for (i = 0; i < 16; ++i) {
            n = n * 256 + v6[i];
        }
        lua_pushnumber(L, n);
    }

    return 1;
}

/*
 * =pod
 *
 * =item silk.B<ipaddr_mask(>I<ipaddr>, I<mask>B<)>
 *
 * Return a copy of I<ipaddr> masked by the ipaddr I<mask>.
 *
 * When both addresses are either IPv4 or IPv6, applying the mask is
 * straightforward.
 *
 * If I<ipaddr> is IPv6 but I<mask> is IPv4, I<mask> is converted to
 * IPv6 and then the mask is applied.  This may result in an odd
 * result.
 *
 * If I<ipaddr> is IPv4 and I<mask> is IPv6, I<ipaddr> remains an
 * IPv4 address if masking I<mask> with C<::ffff:0000:0000> results in
 * C<::ffff:0000:0000>, (namely, if bytes 10 and 11 of I<mask> are
 * 0xFFFF).  Otherwise, I<ipaddr> is converted to an IPv6 address and
 * the mask is performed in IPv6 space, which may result in an odd
 * result.
 *
 * =cut
 */
static int
sk_lua_ipaddr_mask(
    lua_State          *L)
{
    const skipaddr_t *addr;
    const skipaddr_t *mask;
    skipaddr_t *masked;

    addr = sk_lua_checkipaddr(L, 1);
    mask = sk_lua_checkipaddr(L, 2);
    masked = sk_lua_push_ipaddr(L);

    skipaddrCopy(masked, addr);
    skipaddrMask(masked, mask);

    return 1;
}


/*
 * =pod
 *
 * =item silk.B<ipaddr_mask_prefix(>I<ipaddr>, I<prefix>B<)>
 *
 * Return a copy of I<ipaddr> masked by the high I<prefix> bits.  All
 * bits below the I<prefix>th bit are set to zero.  The maximum
 * value for I<prefix> is 32 for an IPv4 address, and 128 for an
 * IPv6 address.
 *
 * =cut
 */
static int
sk_lua_ipaddr_mask_prefix(
    lua_State          *L)
{
    unsigned int max;
    const skipaddr_t *addr;
    lua_Integer n;
    skipaddr_t *masked;

    addr = sk_lua_checkipaddr(L, 1);
    if (skipaddrIsV6(addr)) {
        max = 128;
    } else {
        max = 32;
    }
    n = luaL_checkinteger(L, 2);
    if (n < 0 || n > max) {
        return sk_lua_argerror(L, 2, "value between 0 and %d expected, got %I",
                               max, n);
    }
    masked = sk_lua_push_ipaddr(L);
    skipaddrCopy(masked, addr);
    skipaddrApplyCIDR(masked, (unsigned int)n);

    return 1;
}

/*
 * =pod
 *
 * =item silk.B<ipaddr_country_code(>I<ipaddr>B<)>
 *
 * Return the two character country code associated with I<ipaddr>.
 * If no country code is associated with I<ipaddr>, return B<nil>.
 * The country code association is initialized by the
 * L<silk.B<init_country_codes()>|/"silk.B<init_country_codes(>[I<filename>]B<)>">
 * function.  Calling this function before calling
 * B<init_country_codes()> causes B<init_country_codes()> to be
 * invoked with no argument.
 *
 * =cut
 */
static int
sk_lua_ipaddr_country_code(
    lua_State          *L)
{
    char name[3];
    sk_countrycode_t code;
    const skipaddr_t *addr;
    int rv;

    addr = sk_lua_checkipaddr(L, 1);
    rv = skCountrySetup(NULL, error_printf);
    if (rv != 0) {
        return luaL_error(L, "%s", error_buffer);
    }

    code = skCountryLookupCode(addr);
    if (code == SK_COUNTRYCODE_INVALID) {
        lua_pushnil(L);
    } else {
        lua_pushstring(L, skCountryCodeToName(code, name, sizeof(name)));
    }

    return 1;
}


/*
 * =pod
 *
 * =item silk.B<ipaddr(>I<address>B<)>
 *
 * Return an ipaddr based on I<address>.  I<address> must be a string
 * representation of either an IPv4 or IPv6 address, or an ipaddr
 * object.
 *
 * =cut
 */
static int
sk_lua_ipaddr(
    lua_State          *L)
{
    const char *straddr;
    skipaddr_t *addr;
    int rv;

    luaL_checkany(L, 1);
    switch (lua_type(L, 1)) {
      case LUA_TSTRING:
        straddr = lua_tostring(L, 1);
        addr = sk_lua_push_ipaddr(L);
        rv = skStringParseIP(addr, straddr);
        if (rv) {
            return sk_lua_argerror(L, 1, "invalid IP address '%s': %s",
                                   straddr, skStringParseStrerror(rv));
        }
        return 1;
      case LUA_TUSERDATA:
        if (sk_lua_toipaddr(L, 1)) {
            lua_settop(L, 1);
            return 1;
        }
        break;
      default:
        break;
    }

    return sk_lua_argerror(L, 1, "string or ipaddr expected");
}

/*
 * =pod
 *
 * =item silk.B<ipv4addr(>I<addr>B<)>
 *
 * Create an IPv4 ipaddr from an IP address string, integer, or
 * ipaddr.  Raises an error if the given argument cannot be converted
 * to an IPv4 address.
 *
 * =cut
 */
int
sk_lua_ipaddr_create_v4(
    lua_State          *L)
{
    skipaddr_t *addr;
    uint32_t u32;
    const lua_Number max = UINT32_MAX;
    lua_Number n;

    luaL_checkany(L, 1);
    if (lua_type(L, 1) == LUA_TNUMBER) {
        n = lua_tonumber(L, 1);
        if (n > max || n < 0) {
            return sk_lua_argerror(L, 1,
                                   "value between 0 and %f expected, got %f",
                                   max, n);
        }
        u32 = (uint32_t)n;
        addr = sk_lua_push_ipaddr(L);
        skipaddrSetV4(addr, &u32);

        return 1;
    }
    lua_pushcfunction(L, sk_lua_ipaddr_to_ipv4);
    lua_pushcfunction(L, sk_lua_ipaddr);
    lua_pushvalue(L, 1);
    lua_call(L, 1, 1);
    lua_call(L, 1, 1);
    luaL_argcheck(L, lua_isuserdata(L, -1), 1, "Cannot be converted to IPv4");

    return 1;
}

/*
 * =pod
 *
 * =item silk.B<ipv6addr(>I<addr>B<)>
 *
 * Create an IPv6 ipaddr from an IP address string or ipaddr.
 *
 * =cut
 */
static int
sk_lua_ipaddr_create_v6(
    lua_State          *L)
{
    luaL_checkany(L, 1);
    lua_pushcfunction(L, sk_lua_ipaddr_to_ipv6);
    lua_pushcfunction(L, sk_lua_ipaddr);
    lua_pushvalue(L, 1);
    lua_call(L, 1, 1);
    lua_call(L, 1, 1);

    return 1;
}

/*
 * =pod
 *
 * =item silk.B<ipaddr_ipv4_from_bytes(>I<bytes>B<)>
 *
 * Create an IPv4 ipaddr from a string of four bytes.
 *
 * =cut
 */
static int
sk_lua_ipaddr_ipv4_from_bytes(
    lua_State          *L)
{
    size_t len;
    uint32_t val;
    skipaddr_t *addr;
    const char *str;

    str = sk_lua_checklstring(L, 1, &len);
    if (len != 4) {
        return sk_lua_argerror(L, 1, "expected input to be 4 bytes long");
    }
    val = ntohl(*(uint32_t *)str);
    addr = sk_lua_push_ipaddr(L);
    skipaddrSetV4(addr, &val);

    return 1;
}

/*
 * =pod
 *
 * =item silk.B<ipaddr_ipv6_from_bytes(>I<bytes>B<)>
 *
 * Create an IPv6 ipaddr from a string of 16 bytes.
 *
 * =cut
 */
static int
sk_lua_ipaddr_ipv6_from_bytes(
    lua_State          *L)
{
    size_t len;
    skipaddr_t *addr;
    const char *str;

    str = sk_lua_checklstring(L, 1, &len);
    if (len != 16) {
        return sk_lua_argerror(L, 1, "expected input to be 16 bytes long");
    }
    addr = sk_lua_push_ipaddr(L);
    skipaddrSetV6(addr, str);

    return 1;
}

void
sk_lua_push_ipv6_from_byte_ptr(
    lua_State          *L,
    const uint8_t      *ptr)
{
    skipaddr_t *addr = sk_lua_push_ipaddr(L);
    skipaddrSetV6(addr, ptr);
}

/*
 * =pod
 *
 * =item silk.B<ipaddr_from_bytes(>I<bytes>B<)>
 *
 * Create an ipaddr from a string of bytes.  For an IPv4 address,
 * I<bytes> must be a 4-byte string in network byte order.  For an
 * IPv6 address, I<bytes> must be a 16-byte string in network byte
 * order.  Note: This function converts from a byte representation of
 * an IP address to an ipaddr object, not from a string
 * representation.
 *
 * =cut
 */
static int
sk_lua_ipaddr_from_bytes(
    lua_State          *L)
{
    size_t len;

    sk_lua_checklstring(L, 1, &len);
    switch (len) {
      case 4:
        lua_pushcfunction(L, sk_lua_ipaddr_ipv4_from_bytes);
        break;
      case 16:
        lua_pushcfunction(L, sk_lua_ipaddr_ipv6_from_bytes);
        break;
      default:
        return sk_lua_argerror(
            L, 1, "expected input to be either 4 or 16 bytes long");
    }
    lua_pushvalue(L, 1);
    lua_call(L, 1, 1);

    return 1;
}

static const luaL_Reg sk_lua_ipaddr_metatable[] = {
    {"__eq",        sk_lua_ipaddr_eq},
    {"__lt",        sk_lua_ipaddr_lt},
    {"__gt",        sk_lua_ipaddr_gt},
    {"__tostring",  sk_lua_ipaddr_to_string},
    {NULL, NULL}
};

static const luaL_Reg sk_lua_ipaddr_methods[] = {
    {"to_string",    sk_lua_ipaddr_to_string},
    {"to_bytes",     sk_lua_ipaddr_to_bytes},
    {"is_ipv6",      sk_lua_ipaddr_is_ipv6},
    {"to_ipv6",      sk_lua_ipaddr_to_ipv6},
    {"to_ipv4",      sk_lua_ipaddr_to_ipv4},
    {"to_int",       sk_lua_ipaddr_to_int},
    {"octets",       sk_lua_ipaddr_octets},
    {"mask",         sk_lua_ipaddr_mask},
    {"mask_prefix",  sk_lua_ipaddr_mask_prefix},
    {"country_code", sk_lua_ipaddr_country_code},
    {NULL, NULL}
};

static const luaL_Reg sk_lua_ipaddr_static_methods[] = {
    {"create_v4", sk_lua_ipaddr_create_v4},
    {"create_v6", sk_lua_ipaddr_create_v6},
    {"from_bytes", sk_lua_ipaddr_from_bytes},
    {"ipv6_from_bytes", sk_lua_ipaddr_ipv6_from_bytes},
    {"ipv4_from_bytes", sk_lua_ipaddr_ipv4_from_bytes},
    {NULL, NULL}
};




/*
 *  ********************************************************************
 *  IP WILDCARD
 *  ********************************************************************
 */


/*
 * =pod
 *
 * =item silk.B<ipwildcard_to_string(>I<ipwildcard>B<)>
 *
 * Return the string that was used to construct I<ipwildcard>.
 *
 * =cut
 */
static int
sk_lua_ipwildcard_tostring(
    lua_State          *L)
{
    sk_lua_checkipwildcard(L, 1);
    lua_getuservalue(L, 1);
    lua_getfield(L, -1, "string");

    return 1;
}

/*
 * =pod
 *
 * =item silk.B<ipwildcard_is_ipv6(>I<ipwildcard>B<)>
 *
 * Return B<true> if I<ipwildcard> represents a block of IPv6
 * addresses, B<false> if it represents IPv4 addresses.
 *
 * =cut
 */
static int
sk_lua_ipwildcard_is_ipv6(
    lua_State          *L)
{
    const skIPWildcard_t *wildcard = sk_lua_checkipwildcard(L, 1);
    lua_pushboolean(L, skIPWildcardIsV6(wildcard));

    return 1;
}

/*
 * =pod
 *
 * =item silk.B<ipwildcard_contains(>I<ipwildcard>, I<element>B<)>
 *
 * Return B<true> if I<ipwildcard> contains the IP address in
 * I<element>.  Return B<false> otherwise.  I<element> can be an
 * L<ipaddr|/IP Address> or an ipaddr string.
 *
 * =item I<ipwildcard>B<[> I<element> B<]>
 *
 * An alias for
 * L<silk.B<ipwildcard_contains()>|/"silk.B<ipwildcard_contains(>I<ipwildcard>, I<element>B<)>">.
 *
 * =cut
 */
static int
sk_lua_ipwildcard_index(
    lua_State          *L)
{
    const skIPWildcard_t *wildcard;
    const skipaddr_t *addr;

    wildcard = sk_lua_checkipwildcard(L, 1);
    if (lua_type(L, 2) == LUA_TSTRING) {
        lua_pushcfunction(L, sk_lua_ipaddr);
        lua_pushvalue(L, 2);
        lua_call(L, 1, 1);
        addr = (const skipaddr_t *)lua_touserdata(L, -1);
    } else {
        addr = sk_lua_checkipaddr(L, 2);
    }
    lua_pushboolean(L, skIPWildcardCheckIp(wildcard, addr));

    return 1;
}

static int
sk_lua_ipwildcard_iter_func(
    lua_State          *L)
{
    skIPWildcardIterator_t *state;
    skipaddr_t *addr;

    state = (skIPWildcardIterator_t *)lua_touserdata(L, 1);
    if (!state) {
        return sk_lua_argerror(L, 1, "wildcard iterator expected, got %s",
                               sk_lua_typename(L, 1));
    }
    addr = sk_lua_push_ipaddr(L);
    if (skIPWildcardIteratorNext(state, addr) != SK_ITERATOR_OK) {
        return 0;
    }

    return 1;
}

static int
sk_lua_ipwildcard_iter_cidr_func(
    lua_State          *L)
{
    uint32_t prefix;
    skIPWildcardIterator_t *state;
    skipaddr_t *addr;

    state = (skIPWildcardIterator_t *)lua_touserdata(L, 1);
    if (!state) {
        return sk_lua_argerror(L, 1, "wildcard iterator expected, got %s",
                               sk_lua_typename(L, 1));
    }
    addr = sk_lua_push_ipaddr(L);
    if (skIPWildcardIteratorNextCidr(state, addr, &prefix) != SK_ITERATOR_OK)
    {
        return 0;
    }
    lua_pushinteger(L, prefix);

    return 2;
}

/*
 * =pod
 *
 * =item silk.B<ipwildcard_iter(>I<ipwildcard>[, I<ipv6_policy>]B<)>
 *
 * Return an iterator over the IP addresses in I<ipwildcard>.  May be
 * used as
 * B<for I<addr> in silk.ipwildcard_iter(I<ipwildcard>) do...end>.
 *
 * By default or when the second argument, I<ipv6_policy>, is C<mix>,
 * the iterator returns IPv6 addresses for an IPv6 wildcard and IPv4
 * addresses for an IPv4 wildcard.
 *
 * If I<ipv6_policy> is C<force> the addresses returned are always
 * IPv6 addresses.  For an IPv4 wildcard, the addresses are mapped
 * into the ::ffff:0:0/96 netblock.
 *
 * If I<ipv6_policy> is C<asv4>, the addresses returned are always
 * IPv4 and, for an IPv6 wildcard, only the IPs in the ::ffff:0:0/96
 * netblock are visited.
 *
 * If I<ipv6_policy> is C<ignore>, the iterator returns no IPs when
 * B<ipwildcard_is_ipv6()> returns true.
 *
 * If I<ipv6_policy> is C<only>, the iterator returns no IPs when
 * I<ipwildcard_is_ipv6()> returns false.
 *
 * =cut
 */
static int
sk_lua_ipwildcard_iter(
    lua_State          *L)
{
    const skIPWildcard_t *wildcard;
    skIPWildcardIterator_t *state;
    sk_ipv6policy_t policy;
    int bound;

    policy = SK_IPV6POLICY_MIX;

    wildcard = sk_lua_checkipwildcard(L, 1);
    if (lua_gettop(L) > 1) {
        const char *str;

        str = luaL_checkstring(L, 2);
        if (skIPv6PolicyParse(&policy, str, NULL)) {
            return sk_lua_argerror(L, 2, "invalid ipv6 policy '%s'", str);
        }
    }
    lua_pushcfunction(L, sk_lua_ipwildcard_iter_func);
    state = sk_lua_newuserdata(L, skIPWildcardIterator_t);

    bound = 0;
    switch (policy) {
      case SK_IPV6POLICY_MIX:
        skIPWildcardIteratorBind(state, wildcard);
        bound = 1;
        break;
      case SK_IPV6POLICY_IGNORE:
        if (skIPWildcardIsV6(wildcard)) {
            break;
        }
        /* FALLTHROUGH */
      case SK_IPV6POLICY_ASV4:
        skIPWildcardIteratorBindV4(state, wildcard);
        bound = 1;
        break;
      case SK_IPV6POLICY_ONLY:
        if (!skIPWildcardIsV6(wildcard)) {
            break;
        }
        /* FALLTHROUGH */
      case SK_IPV6POLICY_FORCE:
        skIPWildcardIteratorBindV6(state, wildcard);
        bound = 1;
        break;
    }

    if (!bound) {
        skipaddr_t ipaddr;
        uint32_t prefix;

        /* move iterator to the final IP */
        skIPWildcardIteratorBind(state, wildcard);
        while (skIPWildcardIteratorNextCidr(state, &ipaddr, &prefix)
               == SK_ITERATOR_OK)
            ;                   /* empty */
    }

    /* add wildcard object dependency */
    lua_createtable(L, 1, 0);
    lua_pushvalue(L, 1);
    lua_rawseti(L, -2, 1);
    lua_setuservalue(L, -2);

    return 2;
}

/*
 * =pod
 *
 * =item silk.B<ipwildcard_cidr_iter(>I<ipwildcard>[, I<ipv6_policy>]B<)>
 *
 * Similar to silk.B<ipwildcard_iter()>, but return an iterator over
 * the CIDR blocks in I<ipwildcard>.  Each iteration returns two
 * values, the first of which is the first L<IP address|/IP Address>
 * in the block, the second of which is the prefix length of the
 * block.  May be used as
 * B<for I<addr>, I<prefix> in silk.ipwildcard_cidr_iter(I<ipwildcard>) do...end>.
 *
 * =cut
 */
static int
sk_lua_ipwildcard_cidr_iter(
    lua_State          *L)
{
    int n;
    int i;

    n = lua_gettop(L);
    lua_pushcfunction(L, sk_lua_ipwildcard_iter);
    for (i = 1; i <= n; ++i) {
        lua_pushvalue(L, i);
    }
    lua_call(L, n, 2);
    lua_pushcfunction(L, sk_lua_ipwildcard_iter_cidr_func);
    lua_pushvalue(L, -2);

    return 2;
}

/*
 * =pod
 *
 * =item silk.B<ipwildcard(>I<wildcard>B<)>
 *
 * Return a new ipwildcard.  The string I<wildcard> can be an IP
 * address, an IP with a CIDR notation, an integer, an integer with a
 * CIDR designation, or an entry in SiLK wildcard notation.  In SiLK
 * wildcard notation, a wildcard is represented as an IP address in
 * canonical form with each octet (IPv4) or hexadectet (IPv6)
 * represented by one of following: a value, a range of values, a
 * comma separated list of values and ranges, or the character 'x'
 * used to represent the entire octet or hexadectet.  The I<wildcard>
 * element can also be an ipwildcard, in which case a duplicate
 * reference is returned.
 *
 * =cut
 */
static int
sk_lua_ipwildcard(
    lua_State          *L)
{
    skIPWildcard_t *wildcard;
    const char *strwild;
    int rv;

    lua_settop(L, 1);
    wildcard = sk_lua_newuserdata(L, skIPWildcard_t);
    luaL_setmetatable(L, SK_LUA_IPWILDCARD);
    switch (lua_type(L, 1)) {
      case LUA_TSTRING:
        strwild = lua_tostring(L, 1);
        rv = skStringParseIPWildcard(wildcard, strwild);
        if (rv) {
            return sk_lua_argerror(L, 1, "invalid IP wildcard '%s': %s",
                                   strwild, skStringParseStrerror(rv));
        }
        lua_createtable(L, 0, 1);
        lua_pushvalue(L, 1);
        lua_setfield(L, -2, "string");
        lua_setuservalue(L, -2);
        return 1;
      case LUA_TUSERDATA:
        if (sk_lua_toipwildcard(L, 1)) {
            lua_settop(L, 1);
            return 1;
        }
        break;
      default:
        break;
    }

    return sk_lua_argerror(L, 1, "string or ipwildcard expected, got %s",
                           sk_lua_typename(L, 1));
}

static const luaL_Reg sk_lua_ipwildcard_metatable[] = {
    {"__tostring",  sk_lua_ipwildcard_tostring},
    {"__index",     sk_lua_ipwildcard_index},
    {NULL, NULL}
};

static const luaL_Reg sk_lua_ipwildcard_methods[] = {
    {"to_string",   sk_lua_ipwildcard_tostring},
    {"is_ipv6",     sk_lua_ipwildcard_is_ipv6},
    {"contains",    sk_lua_ipwildcard_index},
    {"iter",        sk_lua_ipwildcard_iter},
    {"cidr_iter",   sk_lua_ipwildcard_cidr_iter},
    {NULL, NULL}
};



/*
 *  ********************************************************************
 *  IP SET
 *  ********************************************************************
 */

/*
 * This function is automatically generated from within Lua, so
 * document it here.
 *
 * =pod
 *
 * =item silk.B<ipset_to_string(>I<ipset>B<)>
 *
 * Return a unique string designating I<ipset>.
 *
 * =cut
 */


static int
sk_lua_ipset_gc(
    lua_State          *L)
{
    sk_lua_ipset_t *ipset = (sk_lua_ipset_t *)lua_touserdata(L, 1);
    if (!ipset->readonly) {
        skIPSetDestroy(&ipset->ipset);
    }

    return 0;
}

/*
 * =pod
 *
 * =item B<#>I<ipset>
 *
 * An alias for
 * L<silk.B<ipset_cardinality()>|/"silk.B<ipset_cardinality(>I<ipset>B<)>">.
 *
 * =item silk.B<ipset_cardinality(>I<ipset>B<)>
 *
 * Return the cardinality of I<ipset>.
 *
 * =cut
 */
static int
sk_lua_ipset_len(
    lua_State          *L)
{
    sk_lua_ipset_t *ipset;
    lua_Number count;

    ipset = sk_lua_checkipset(L, 1);
    skIPSetClean(ipset->ipset);
    skIPSetCountIPs(ipset->ipset, &count);
    lua_pushnumber(L, count);

    return 1;
}

static skipset_t *
sk_lua_ipset_push(
    lua_State          *L,
    int                 ipv6)
{
    sk_lua_ipset_t *ipset;
    int rv;

    ipset = sk_lua_newuserdata(L, sk_lua_ipset_t);
    ipset->readonly = 0;
    rv = skIPSetCreate(&ipset->ipset, ipv6);
    if (rv != SKIPSET_OK) {
        luaL_error(L, "%s", skIPSetStrerror(rv));
        return NULL;
    }
    luaL_setmetatable(L, SK_LUA_IPSET);

    return ipset->ipset;
}

void
sk_lua_push_readonly_ipset(
    lua_State          *L,
    skipset_t          *ipset)
{
    sk_lua_ipset_t *lua_ipset;
    lua_ipset = sk_lua_newuserdata(L, sk_lua_ipset_t);
    lua_ipset->readonly = 1;
    lua_ipset->ipset = ipset;
    luaL_setmetatable(L, SK_LUA_IPSET);
}

/*
 * =pod
 *
 * =item I<ipset>B<[> I<element> B<]>
 *
 * Return B<true> if I<ipset> contains the any of the IP addresses in
 * I<element>.  Return B<false> otherwise.  I<element> may be an
 * L<ipaddr|/IP Address>, an ipset, an L<ipwildcard|/IP Wildcard>, or
 * an ipwildcard string.
 *
 * =cut
 */
static int
sk_lua_ipset_index(
    lua_State          *L)
{
    const sk_lua_ipset_t *ipset_x;
    const skipaddr_t *addr;
    const skIPWildcard_t *wild;
    const sk_lua_ipset_t *ipset_y;
    int found;

    ipset_x = (const sk_lua_ipset_t *)lua_touserdata(L, 1);
    lua_settop(L, 2);
    if (lua_type(L, 2) == LUA_TSTRING) {
        lua_pushcfunction(L, sk_lua_ipwildcard);
        lua_pushvalue(L, 2);
        lua_call(L, 1, 1);
    }
    if ((addr = sk_lua_toipaddr(L, -1))) {
        found = skIPSetCheckAddress(ipset_x->ipset, addr);
    } else if ((wild = sk_lua_toipwildcard(L, -1))) {
        found = skIPSetCheckIPWildcard(ipset_x->ipset, wild);
    } else if ((ipset_y = sk_lua_toipset(L, -1))) {
        found = skIPSetCheckIPSet(ipset_x->ipset, ipset_y->ipset);
    } else {
        return sk_lua_argerror(L, 2,
                               "ipaddr, ipwildcard, or ipset expected, got %s",
                               sk_lua_typename(L, 2));
    }

    lua_pushboolean(L, found);

    return 1;
}

static void
sk_lua_ipset_make_writable(
    lua_State          *L,
    int                 idx)
{
    int rv;
    sk_lua_ipset_t *ipset = (sk_lua_ipset_t *)lua_touserdata(L, idx);

    if (ipset->readonly) {
        skipset_t *newset;

        rv = skIPSetCreate(&newset, skIPSetContainsV6(ipset->ipset));
        if (rv) {
            goto ERR;
        }
        rv = skIPSetUnion(newset, ipset->ipset);
        if (rv) {
            skIPSetDestroy(&newset);
            goto ERR;
        }
        ipset->ipset = newset;
        ipset->readonly = 0;
    }

    return;
  ERR:
    luaL_error(L, "%s", skIPSetStrerror(rv));
}

/*
 * =pod
 *
 * =item I<ipset>B<[> I<element> B<] => I<bool>
 *
 * Add or remove the IP addresses referenced by I<element> to or from
 * I<ipset>.  If I<bool> is B<true>, the addresses are added; if
 * B<false>, the addresses are removed. I<element> may be an
 * L<ipaddr|/IP Address>, an ipset, an L<ipwildcard|/IP Wildcard>, or
 * an ipwildcard string.
 *
 * =cut
 */
static int
sk_lua_ipset_newindex(
    lua_State          *L)
{
    sk_lua_ipset_t *ipset_x;
    int add;
    const skipaddr_t *addr;
    const skIPWildcard_t *wild;
    sk_lua_ipset_t *ipset_y;
    int rv;

    /* Get the set */
    ipset_x = sk_lua_checkipset(L, 1);

    /* Verify a third argument  */
    luaL_checkany(L, 3);


    /* Clean the set */
    sk_lua_ipset_make_writable(L, 1);
    skIPSetClean(ipset_x->ipset);

    /* Parse the 3rd argument as boolean */
    add = lua_toboolean(L, 3);

    /* Convert 2nd string argument to ipwildcard */
    if (lua_type(L, 2) == LUA_TSTRING) {
        lua_pushcfunction(L, sk_lua_ipwildcard);
        lua_pushvalue(L, 2);
        lua_call(L, 1, 1);
    } else {
        lua_pushvalue(L, 2);
    }

    if ((addr = sk_lua_toipaddr(L, -1))) {
        /* If second arg is ipaddr... */
        if (add) {
            rv = skIPSetInsertAddress(ipset_x->ipset, addr, 0);
        } else {
            rv = skIPSetRemoveAddress(ipset_x->ipset, addr, 0);
        }
    } else if ((wild = sk_lua_toipwildcard(L, -1))) {
        /* If second arg is ipwildcard */
        if (add) {
            rv = skIPSetInsertIPWildcard(ipset_x->ipset, wild);
        } else {
            rv = skIPSetRemoveIPWildcard(ipset_x->ipset, wild);
        }
    } else if ((ipset_y = sk_lua_toipset(L, -1))) {
        /* If second arg is ipset */
        if (!ipset_y->readonly) {
            skIPSetClean(ipset_y->ipset);
        }
        if (add) {
            rv = skIPSetUnion(ipset_x->ipset, ipset_y->ipset);
        } else {
            rv = skIPSetSubtract(ipset_x->ipset, ipset_y->ipset);
        }
    } else {
        return sk_lua_argerror(
            L, 2, "ipaddr, ipwildcard, or ipset expected, got %s",
            sk_lua_typename(L, 2));
    }
    if (rv) {
        return luaL_error(L, "%s", skIPSetStrerror(rv));
    }

    lua_settop(L, 1);

    return 1;
}

/*
 * =pod
 *
 * =item silk.B<ipset_add_range(>I<ipset>, I<start>, I<end>B<)>
 *
 * Add all IP addresses from I<start> to I<end>, inclusive, to
 * I<ipset>.  I<start> and I<end> must be either
 * L<ipaddr objects|/IP Address> or IP address strings.  An error is
 * raised if I<end> is less than I<start>.
 *
 * =cut
 */
static int
sk_lua_ipset_add_range(
    lua_State          *L)
{
    sk_lua_ipset_t *ipset;
    const skipaddr_t *addra;
    const skipaddr_t *addrb;
    int rv;

    ipset = sk_lua_checkipset(L, 1);
    if (lua_type(L, 2) == LUA_TSTRING) {
        /* If second argument is a string, convert to ipaddr */
        lua_pushcfunction(L, sk_lua_ipaddr);
        lua_pushvalue(L, 2);
        lua_call(L, 1, 1);
        addra = (const skipaddr_t *)lua_touserdata(L, -1);
    } else {
        addra = sk_lua_checkipaddr(L, 2);
    }
    if (lua_type(L, 3) == LUA_TSTRING) {
        /* If third argument is a string, convert to ipaddr */
        lua_pushcfunction(L, sk_lua_ipaddr);
        lua_pushvalue(L, 3);
        lua_call(L, 1, 1);
        addrb = (const skipaddr_t *)lua_touserdata(L, -1);
    } else {
        addrb = sk_lua_checkipaddr(L, 3);
    }

    sk_lua_ipset_make_writable(L, 1);
    rv = skIPSetInsertRange(ipset->ipset, addra, addrb);
    if (rv) {
        return luaL_error(L, "%s", skIPSetStrerror(rv));
    }

    lua_settop(L, 1);

    return 1;
}

/*
 * =pod
 *
 * =item silk.B<ipset_clear(>I<ipset>B<)>
 *
 * Remove all IP addresses from I<ipset> and return I<ipset>.
 *
 * =cut
 */
static int
sk_lua_ipset_clear(
    lua_State          *L)
{
    sk_lua_ipset_t *ipset = sk_lua_checkipset(L, 1);
    sk_lua_ipset_make_writable(L, 1);
    skIPSetRemoveAll(ipset->ipset);

    return 1;
}

/*
 * =pod
 *
 * =item silk.B<ipset_copy(>I<ipset>B<)>
 *
 * Return a new IPset with a copy of I<ipset>.
 *
 * =cut
 */
static int
sk_lua_ipset_copy(
    lua_State          *L)
{
    sk_lua_ipset_t *ipset_x;
    skipset_t *ipset;
    int rv;

    ipset_x = sk_lua_checkipset(L, 1);
    if (ipset_x->readonly) {
        sk_lua_push_readonly_ipset(L, ipset_x->ipset);
        return 1;
    }

    ipset = sk_lua_ipset_push(L, skIPSetContainsV6(ipset_x->ipset));

    rv = skIPSetUnion(ipset, ipset_x->ipset);
    if (rv) {
        return luaL_error(L, "%s", skIPSetStrerror(rv));
    }

    return 1;
}

/*
 *  =pod
 *
 *  =item silk.B<ipset_intersection_update(>I<ipset>, I<other>[, ...]B<)>
 *
 *  Remove from I<ipset> any IP addresses that do not appear in
 *  I<other>s.  I<other>s may be ipsets, L<ipwildcards|/IP Wildcard>,
 *  ipwildcard strings, or arrays of L<ipaddrs|/IP Address> and
 *  ipwildcard strings.  Return I<ipset>.
 *
 *  =cut
 */
static int
sk_lua_ipset_intersection_update(
    lua_State          *L)
{
    int rv;
    sk_lua_ipset_t *ipset_x = sk_lua_checkipset(L, 1);
    sk_lua_ipset_t *ipset_y = sk_lua_checkipset(L, 2);

    sk_lua_ipset_make_writable(L, 1);
    skIPSetClean(ipset_x->ipset);
    if (!ipset_y->readonly) {
        skIPSetClean(ipset_y->ipset);
    }
    rv = skIPSetIntersect(ipset_x->ipset, ipset_y->ipset);
    if (rv) {
        return luaL_error(L, "%s", skIPSetStrerror(rv));
    }
    lua_settop(L, 1);

    return 1;
}

/*
 * =pod
 *
 * =item silk.B<ipset_convert_v4(>I<ipset>B<)>
 *
 * Convert I<ipset> to an IPv4 IPset.  Return I<ipset>.  If I<ipset>
 * contains IPv6 addresses outside of the ::ffff:0:0/96 prefix, raise
 * an error and leave I<ipset> unchanged.
 *
 * =cut
 */
static int
sk_lua_ipset_convert_v4(
    lua_State          *L)
{
    int rv;
    sk_lua_ipset_t *ipset;

    ipset = sk_lua_checkipset(L, 1);
    sk_lua_ipset_make_writable(L, 1);
    rv = skIPSetConvert(ipset->ipset, 4);
    if (rv == 0) {
        return 1;
    }
    if (rv == SKIPSET_ERR_IPV6) {
        return luaL_error(L, ("ipset cannot be converted to v4,"
                              " as it contains v6 addresses"));
    }

    return luaL_error(L, "%s", skIPSetStrerror(rv));
}

/*
 * =pod
 *
 * =item silk.B<ipset_convert_v6(>I<ipset>B<)>
 *
 * Convert I<ipset> to an IPv6 IPset.  Return I<ipset>.
 *
 * =cut
 */
static int
sk_lua_ipset_convert_v6(
    lua_State          *L)
{
    int rv;
    sk_lua_ipset_t *ipset;

    ipset = sk_lua_checkipset(L, 1);
    sk_lua_ipset_make_writable(L, 1);
    rv = skIPSetConvert(ipset->ipset, 6);
    if (rv != 0) {
        return luaL_error(L, "%s", skIPSetStrerror(rv));
    }

    return 1;
}

/*
 * =pod
 *
 * =item silk.B<ipset_is_ipv6(>I<ipset>B<)>
 *
 * Return B<true> if I<ipset> is a set of IPv6 addresses, and B<false>
 * if it a set of IPv4 addresses.  For the purposes of this method,
 * IPv4-in-IPv6 addresses (that is, addresses in the ::ffff:0:0/96
 * prefix) are considered IPv6 addresses.
 *
 * =cut
 */
static int
sk_lua_ipset_is_ipv6(
    lua_State          *L)
{
    sk_lua_ipset_t *ipset = sk_lua_checkipset(L, 1);
    lua_pushboolean(L, skIPSetIsV6(ipset->ipset));

    return 1;
}

/*
 * =pod
 *
 * =item silk.B<ipset_save(>I<ipset>, I<filename>B<)>
 *
 * Save the contents of I<ipset> in the file I<filename>.
 *
 * =cut
 */
static int
sk_lua_ipset_save(
    lua_State          *L)
{
    int rv;
    sk_lua_ipset_t *ipset;
    const char *filename;

    ipset = sk_lua_checkipset(L, 1);
    filename = sk_lua_checkstring(L, 2);
    if (!ipset->readonly) {
        skIPSetClean(ipset->ipset);
    }
    rv = skIPSetSave(ipset->ipset, filename);
    if (rv != 0) {
        return luaL_error(L, "%s", skIPSetStrerror(rv));
    }

    return 0;
}

static int
sk_lua_ipset_iter_func(
    lua_State          *L)
{
    skipset_iterator_t *iter;
    int cidr;
    skipaddr_t *addr;
    uint32_t prefix;

    iter = (skipset_iterator_t *)lua_touserdata(L, lua_upvalueindex(1));
    cidr = lua_toboolean(L, lua_upvalueindex(2));
    addr = sk_lua_push_ipaddr(L);
    if (skIPSetIteratorNext(iter, addr, &prefix)
        == SK_ITERATOR_NO_MORE_ENTRIES)
    {
        return 0;
    }
    if (cidr) {
        lua_pushinteger(L, prefix);
        return 2;
    }

    return 1;
}

static int
sk_lua_ipset_iter_helper(
    lua_State          *L,
    int                 cidr)
{
    int rv;
    sk_lua_ipset_t *ipset;
    sk_ipv6policy_t policy;
    skipset_iterator_t *iter;

    policy = SK_IPV6POLICY_MIX;

    ipset = sk_lua_checkipset(L, 1);
    if (lua_gettop(L) > 1) {
        const char *str;

        str = luaL_checkstring(L, 2);
        if (skIPv6PolicyParse(&policy, str, NULL)) {
            return sk_lua_argerror(L, 2, "invalid ipv6 policy '%s'", str);
        }
    }
    iter = sk_lua_newuserdata(L, skipset_iterator_t);

    if (!ipset->readonly) {
        skIPSetClean(ipset->ipset);
    }
    rv = skIPSetIteratorBind(iter, ipset->ipset, cidr, policy);
    if (rv != 0) {
        return luaL_error(L, "%s", skIPSetStrerror(rv));
    }
    lua_pushboolean(L, cidr);
    /* Include the ipset in the closure to make sure it isn't garbage
     * collected during iteration */
    lua_pushvalue(L, 1);
    lua_pushcclosure(L, sk_lua_ipset_iter_func, 3);

    return 1;
}

/*
 * =pod
 *
 * =item silk.B<ipset_iter(>I<ipset>[, I<ipv6_policy>]B<)>
 *
 * Return an iterator over the L<IP addresses|/IP Address> in
 * I<ipset>.  May be used as
 * B<for I<addr> in silk.ipset_iter(I<ipset>) do...end>.
 *
 * I<ipv6_policy> is a string that determines how IP addresses in the
 * set are returned.  The default is C<mix>, and valid values are:
 *
 * =over 4
 *
 * =item ignore
 *
 * Return only IPv4 addresses, ignoring any IPv6 addresses.
 *
 * =item asv4
 *
 * Return any IPv6 addresses in the ::ffff:0:0/96 prefix as IPv4
 * addresses and ignore all other IPv6 addresses.
 *
 * =item mix
 *
 * Return all IP addresses normally.
 *
 * =item force
 *
 * Return all addresses as IPv6 addresses, mapping the IPv4 addresses
 * into the ::ffff:0:0/96 prefix.
 *
 * =item only
 *
 * Return only IPv6 addresses, ignoring any IPv4 addresses.
 *
 * =back
 *
 * =cut
 */
static int
sk_lua_ipset_iter(
    lua_State          *L)
{
    return sk_lua_ipset_iter_helper(L, 0);
}

/*
 * =pod
 *
 * =item silk.B<ipset_cidr_iter(>I<ipset>[, I<ipv6_policy>]B<)>
 *
 * Return an iterator over the CIDR blocks in I<ipset>.  Each iteration
 * returns two values, the first of which is the first
 * L<IP address|/IP Address> in the block, the second of which is the
 * prefix length of the block.  May be used as
 * B<for I<addr>, I<prefix> in silk.ipset_cidr_iter(I<ipset>) do...end>.
 *
 * The I<ipv6_policy> parameter is handled as in silk.B<ipset_iter()>.
 *
 * =cut
 */
static int
sk_lua_ipset_cidr_iter(
    lua_State          *L)
{
    return sk_lua_ipset_iter_helper(L, 1);
}

/*
 * =pod
 *
 * =item silk.B<ipset_create_v4(>B<)>
 *
 * Return a new IPset optimized to contain IPv4 addresses.
 *
 * =cut
 */
static int
sk_lua_ipset_create_v4(
    lua_State          *L)
{
    sk_lua_ipset_push(L, 0);

    return 1;
}

/*
 * =pod
 *
 * =item silk.B<ipset_create_v6(>B<)>
 *
 * Return a new IPset that can contain IPv4 or IPv6 addresses.
 *
 * =cut
 */
static int
sk_lua_ipset_create_v6(
    lua_State          *L)
{
    sk_lua_ipset_push(L, 1);

    return 1;
}

/*
 * =pod
 *
 * =item silk.B<ipset_load(>I<filename>B<)>
 *
 * Load and return an IPset from the file I<filename>.
 *
 * =cut
 */
static int
sk_lua_ipset_load(
    lua_State          *L)
{
    int rv;
    char        errbuf[2 * PATH_MAX];
    skstream_t *stream = NULL;
    const char *fname;
    sk_lua_ipset_t *ipset;
    const char *err;

    fname = sk_lua_checkstring(L, 1);
    ipset = sk_lua_newuserdata(L, sk_lua_ipset_t);
    ipset->readonly = 1;

    if ((rv = skStreamCreate(&stream, SK_IO_READ, SK_CONTENT_SILK))
        || (rv = skStreamBind(stream, fname))
        || (rv = skStreamOpen(stream)))
    {
        skStreamLastErrMessage(stream, rv, errbuf, sizeof(errbuf));
        skStreamDestroy(&stream);

        return luaL_error(L, "Unable to read IPSet '%s': %s", fname, errbuf);
    }
    rv = skIPSetRead(&ipset->ipset, stream);
    if (rv) {
        if (SKIPSET_ERR_FILEIO == rv) {
            skStreamLastErrMessage(stream, skStreamGetLastReturnValue(stream),
                                   errbuf, sizeof(errbuf));
            err = errbuf;
        } else {
            err = skIPSetStrerror(rv);
        }
        skStreamDestroy(&stream);

        return luaL_error(L, "Unable to read IPSet '%s': %s", fname, err);
    }
    ipset->readonly = 0;
    skStreamDestroy(&stream);
    luaL_setmetatable(L, SK_LUA_IPSET);

    return 1;
}

static const luaL_Reg sk_lua_ipset_metatable[] = {
    {"__gc",  sk_lua_ipset_gc},
    {"__len", sk_lua_ipset_len},
    {"__index", sk_lua_ipset_index},
    {"__newindex", sk_lua_ipset_newindex},
    {NULL, NULL}
};

static const luaL_Reg sk_lua_ipset_methods[] = {
    {"cardinality", sk_lua_ipset_len},
    {"add_range", sk_lua_ipset_add_range},
    {"clear", sk_lua_ipset_clear},
    {"copy", sk_lua_ipset_copy},
    {"intersection_update", sk_lua_ipset_intersection_update},
    {"convert_v4", sk_lua_ipset_convert_v4},
    {"convert_v6", sk_lua_ipset_convert_v6},
    {"is_ipv6", sk_lua_ipset_is_ipv6},
    {"save", sk_lua_ipset_save},
    {"iter", sk_lua_ipset_iter},
    {"cidr_iter", sk_lua_ipset_cidr_iter},
    {NULL, NULL}
};

static const luaL_Reg sk_lua_ipset_static_methods[] = {
    {"create_v4", sk_lua_ipset_create_v4},
    {"create_v6", sk_lua_ipset_create_v6},
    {"load", sk_lua_ipset_load},
    {NULL, NULL}
};

/*
 *  ********************************************************************
 *  IP SET CACHE
 *  ********************************************************************
 */

static int
ipset_file_item_compare(
    const void         *va,
    const void         *vb,
    const void  UNUSED(*ctx))
{
    const ipset_file_item_t *a = (const ipset_file_item_t *)va;
    const ipset_file_item_t *b = (const ipset_file_item_t *)vb;
    return ((a->dev < b->dev) ? -1
            : ((a->dev > b->dev) ? 1
               : ((a->ino < b->ino) ? -1
                  : (a->ino > b->ino))));
}

static void
ipset_file_item_destroy(
    void               *vd)
{
    ipset_file_item_t *d = (ipset_file_item_t *)vd;
    skIPSetDestroy(&d->ipset);
    free(d);
}


sk_rbtree_t *
sk_ipset_cache_create(
    void)
{
    sk_rbtree_t *tree;
    sk_rbtree_create(&tree, ipset_file_item_compare, ipset_file_item_destroy,
                     NULL);
    return tree;
}

int
sk_ipset_cache_get_ipset(
    sk_rbtree_t        *cache,
    skipset_t         **ipset,
    const char         *path)
{
    static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
    struct stat st;
    ipset_file_item_t *item;
    ipset_file_item_t target;
    skstream_t *stream = NULL;
    int rv;

    rv = stat(path, &st);
    if (rv != 0) {
        return SKIPSET_ERR_OPEN;
    }
    target.dev = st.st_dev;
    target.ino = st.st_ino;
    pthread_mutex_lock(&mutex);
    item = (ipset_file_item_t *)sk_rbtree_find(cache, (void *)&target);
    if (item != NULL) {
        *ipset = item->ipset;
        goto END;
    }
    if ((rv = skStreamCreate(&stream, SK_IO_READ, SK_CONTENT_SILK))
        || (rv = skStreamBind(stream, path))
        || (rv = skStreamOpen(stream)))
    {
        skStreamPrintLastErr(stream, rv, &skAppPrintErr);
        rv = SKIPSET_ERR_OPEN;
        goto END;
    }

    rv = skIPSetRead(&target.ipset, stream);
    if (rv != 0) {
        if (SKIPSET_ERR_FILEIO == rv) {
            skStreamPrintLastErr(stream, skStreamGetLastReturnValue(stream),
                                 &skAppPrintErr);
        }
        goto END;
    }
    item = sk_alloc(ipset_file_item_t);
    *item = target;
    if ((rv = sk_rbtree_insert(cache, (void *)item, NULL))) {
        assert(rv == SK_RBTREE_ERR_ALLOC);
        skAppPrintOutOfMemory(NULL);
        ipset_file_item_destroy((void *)item);
        pthread_mutex_unlock(&mutex);
        exit(EXIT_FAILURE);
    }
    *ipset = target.ipset;
    rv = 0;

  END:
    skStreamDestroy(&stream);
    pthread_mutex_unlock(&mutex);
    return rv;
}


/*
 *  ********************************************************************
 *  PREFIX MAP
 *  ********************************************************************
 */

/*
 * This function is automatically generated from within Lua, so
 * document it here.
 *
 * =pod
 *
 * =item silk.B<pmap_to_string(>I<pmap>B<)>
 *
 * Return a unique string designating I<pmap>.
 *
 * =cut
 */

/*
 *    sk_lua_pmap_t is the type that wraps the SiLK Prefix Map type,
 *    skPrefixMap_t.
 *
 *    The wrapper includes a character buffer that is used by
 *    skPrefixMapDictionaryGetEntry() to hold the returned name.  That
 *    buffer is allocated after the map is loaded and the length of
 *    the longest name is known.
 */
typedef struct sk_lua_pmap_st {
    skPrefixMap_t *map;
    uint32_t       size;
    char           namebuf[1];
} sk_lua_pmap_t;

/*
 * =pod
 *
 * =item silk.B<pmap_get(>I<pmap>, I<element>B<)>
 *
 * Return the label the prefix map associates with I<element>.
 * I<element> may be an L<ipaddr|/IP Address> or a 2-element sequence
 * where the first element is a protocol number and the second element
 * is a port number.
 *
 * =item silk.B<pmap_get(>I<pmap>, I<protocol>, I<port>B<)>
 *
 * Return the label the prefix map associates with proto-port pair
 * I<protocol>, I<port>.
 *
 * =item I<pmap>B<[> I<element> B<]>
 *
 * An alias for the two-argument form of
 * L<silk.B<pmap_get()>|/"silk.B<pmap_get(>I<pmap>, I<element>B<)>">.
 *
 * =cut
 */
static int
sk_lua_pmap_index(
    lua_State          *L)
{
    void *key = NULL;
    skPrefixMapProtoPort_t protoport;
    lua_Integer num;
    uint32_t value;
    int rv;
    sk_lua_pmap_t *x_map;

    x_map = (sk_lua_pmap_t *)lua_touserdata(L, 1);

    switch (skPrefixMapGetContentType(x_map->map)) {
      case SKPREFIXMAP_CONT_ADDR_V4:
      case SKPREFIXMAP_CONT_ADDR_V6:
        key = luaL_checkudata(L, 2, SK_LUA_IPADDR);
        break;
      case SKPREFIXMAP_CONT_PROTO_PORT:
        if (lua_isnone(L, 3)) {
            lua_geti(L, 2, 1);
            lua_geti(L, 2, 2);
        } else {
            luaL_checkany(L, 2);
        }
        num = luaL_checkinteger(L, -2);
        if (num < 0 || num > UINT8_MAX) {
            return sk_lua_argerror(L, 2, "Protocol is out of bounds");
        }
        protoport.proto = num;
        num = luaL_checkinteger(L, -1);
        if (num < 0 || num > UINT16_MAX) {
            return sk_lua_argerror(L, 2, "Port is out of bounds");
        }
        protoport.port = num;
        key = &protoport;
        break;
    }
    value = skPrefixMapFindValue(x_map->map, key);
    rv = skPrefixMapDictionaryGetEntry(
        x_map->map, value, x_map->namebuf, x_map->size);
    lua_pushlstring(L, x_map->namebuf, rv);

    return 1;
}

static int
sk_lua_pmap_gc(
    lua_State          *L)
{
    sk_lua_pmap_t *x_map = (sk_lua_pmap_t *)lua_touserdata(L, 1);
    skPrefixMapDelete(x_map->map);

    return 0;
}

/*
 * =pod
 *
 * =item silk.B<pmap_get_values(>I<pmap>B<)>
 *
 * Return a sequence of the labels defined by I<pmap>.
 *
 * =cut
 */
static int
sk_lua_pmap_get_values(
    lua_State          *L)
{
    uint32_t i;
    int rv;
    sk_lua_pmap_t *x_map;
    uint32_t count;

    x_map = sk_lua_checkpmap(L, 1);
    count = skPrefixMapDictionaryGetWordCount(x_map->map);

    if (count > INT_MAX) {
        lua_newtable(L);
    } else {
        lua_createtable(L, count, 0);
    }
    for (i = 0; i < count; ++i) {
        rv = skPrefixMapDictionaryGetEntry(
            x_map->map, i, x_map->namebuf, x_map->size);
        lua_pushlstring(L, x_map->namebuf, rv);
        lua_rawseti(L, -2, i + 1);
    }

    return 1;
}

/*
 * =pod
 *
 * =item silk.B<pmap_get_content_type(>I<pmap>B<)>
 *
 * Return the content type of I<pmap> as a string.  The result is one
 * of C<IPv4-address>, C<IPv6-address>, or C<proto-port>.
 *
 * =cut
 */
static int
sk_lua_pmap_get_content_type(
    lua_State          *L)
{
    const sk_lua_pmap_t *x_map = sk_lua_checkpmap(L, 1);
    lua_pushstring(
        L, skPrefixMapGetContentName(skPrefixMapGetContentType(x_map->map)));

    return 1;
}

static int
sk_lua_pmap_iterranges_func(
    lua_State          *L)
{
    skPrefixMapIterator_t *iter;
    sk_lua_pmap_t *x_map;
    skipaddr_t *addr;
    union pmap_val_un {
        skipaddr_t             addr;
        skPrefixMapProtoPort_t pp;
    } start, end;
    uint32_t value;
    int numvalues = 0;
    int rv;

    iter = (skPrefixMapIterator_t *)lua_touserdata(L, lua_upvalueindex(1));
    x_map = (sk_lua_pmap_t *)lua_touserdata(L, lua_upvalueindex(2));

    if (skPrefixMapIteratorNext(iter, (void*)&start, (void*)&end, &value)
        == SK_ITERATOR_NO_MORE_ENTRIES)
    {
        return 0;
    }
    switch (skPrefixMapGetContentType(x_map->map)) {
      case SKPREFIXMAP_CONT_ADDR_V4:
      case SKPREFIXMAP_CONT_ADDR_V6:
        addr = sk_lua_push_ipaddr(L);
        skipaddrCopy(addr, &start.addr);
        addr = sk_lua_push_ipaddr(L);
        skipaddrCopy(addr, &end.addr);
        numvalues = 3;
        break;
      case SKPREFIXMAP_CONT_PROTO_PORT:
        lua_pushinteger(L, start.pp.proto);
        lua_pushinteger(L, start.pp.port);
        lua_pushinteger(L, end.pp.proto);
        lua_pushinteger(L, end.pp.port);
        numvalues = 5;
        break;
    }
    rv = skPrefixMapDictionaryGetEntry(
        x_map->map, value, x_map->namebuf, x_map->size);
    lua_pushlstring(L, x_map->namebuf, rv);

    return numvalues;
}


/*
 * =pod
 *
 * =item silk.B<pmap_iterranges(>I<pmap>B<)>
 *
 * Return an iterator that iterates over ranges of contiguous
 * values with the same label.
 *
 * If I<pmap> is an IP address map, each iteration returns three
 * values, (I<start>, I<end>, I<label>), where I<start> is the first
 * L<ipaddr|/IP Address> of the range, I<end> is the last ipaddr of
 * the range, and I<label> is the label for that range.
 *
 * If I<pmap> is a proto-port map, each iteration returns five values,
 * (I<start_proto>, I<start_port>, I<end_proto>, I<end_port>,
 * I<label>), where I<start_proto> and I<start_port> represent the
 * first element of the range, I<end_proto> and I<end_port> represent
 * the last element of the range, and I<label> is the label for that
 * range.
 *
 * =cut
 */
static int
sk_lua_pmap_iterranges(
    lua_State          *L)
{
    sk_lua_pmap_t *x_map;
    skPrefixMapIterator_t *iter;
    int rv;

    x_map = sk_lua_checkpmap(L, 1);
    iter = sk_lua_newuserdata(L, skPrefixMapIterator_t);
    if ((rv = skPrefixMapIteratorBind(iter, x_map->map))) {
        return luaL_error(L, "%s", skPrefixMapStrerror(rv));
    }
    lua_pushvalue(L, 1);
    lua_pushcclosure(L, sk_lua_pmap_iterranges_func, 2);

    return 1;
}

/*
 * =pod
 *
 * =item silk.B<pmap(>I<filename>B<)>
 *
 * Load a prefix map from I<filename>.
 *
 * =item silk.B<pmap_load(>I<filename>B<)>
 *
 * Load a prefix map from I<filename>.
 *
 * =cut
 */
static int
sk_lua_pmap_load(
    lua_State          *L)
{
    skstream_t *stream = NULL;
    skPrefixMap_t *map;
    sk_lua_pmap_t *x_map;
    char errbuf[2 * PATH_MAX];
    const char *fname;
    uint32_t size;
    const char *err;
    int rv;

    fname = sk_lua_checkstring(L, 1);
    if ((rv = skStreamCreate(&stream, SK_IO_READ, SK_CONTENT_SILK))
        || (rv = skStreamBind(stream, fname))
        || (rv = skStreamOpen(stream)))
    {
        skStreamLastErrMessage(stream, rv, errbuf, sizeof(errbuf));
        skStreamDestroy(&stream);
        return luaL_error(L, "Unable to read prefix map from '%s': %s",
                          fname, errbuf);
    }
    rv = (int)skPrefixMapRead(&map, stream);
    if (rv) {
        if (SKPREFIXMAP_ERR_IO == rv) {
            skStreamLastErrMessage(
                stream, skStreamGetLastReturnValue(stream),
                errbuf, sizeof(errbuf));
            err = errbuf;
        } else {
            err = skPrefixMapStrerror(rv);
        }
        skStreamDestroy(&stream);
        return luaL_error(L, "Unable to read prefix map from '%s': %s",
                          fname, err);
    }
    /* fprintf(stderr, ("%s:%d: pushing protected pointer %p" */
    /*                  " and free_fn %p\n"), */
    /*         __FILE__, __LINE__, (void *)map, (void *)&skPrefixMapDelete); */
    sk_lua_push_protected_pointer(
        L, map, (sk_lua_free_fn_t)skPrefixMapDelete);
    skStreamDestroy(&stream);
    size = skPrefixMapDictionaryGetMaxWordSize(map) + 1;
    x_map = (sk_lua_pmap_t *)lua_newuserdata(
        L, offsetof(sk_lua_pmap_t, namebuf) + size);
    sk_lua_unprotect_pointer(L, -2);
    x_map->map = map;
    x_map->size = size;
    luaL_setmetatable(L, SK_LUA_PMAP);
    return 1;
}

static const luaL_Reg sk_lua_pmap_metatable[] = {
    {"__index", sk_lua_pmap_index},
    {"__gc", sk_lua_pmap_gc},
    {NULL, NULL}
};

static const luaL_Reg sk_lua_pmap_methods[] = {
    {"get", sk_lua_pmap_index},
    {"get_values", sk_lua_pmap_get_values},
    {"get_content_type", sk_lua_pmap_get_content_type},
    {"iterranges", sk_lua_pmap_iterranges},
    {NULL, NULL}
};

static const luaL_Reg sk_lua_pmap_static_methods[] = {
    {"load", sk_lua_pmap_load},
    {NULL, NULL}
};



/*
 *  ********************************************************************
 *  BITMAP
 *  ********************************************************************
 */

/*
 * This function is automatically generated from within Lua, so
 * document it here.
 *
 * =pod
 *
 * =item silk.B<bitmap_to_string(>I<bitmap>B<)>
 *
 * Return a unique string designating I<bitmap>.
 *
 * =cut
 */

/*
 * =pod
 *
 * =item silk.B<bitmap_clear_all(>I<bitmap>B<)>
 *
 * Set to B<false> (set to 0; turn off) all bits in I<bitmap>.
 *
 * =cut
 */
static int
sk_lua_bitmap_clear_all(
    lua_State          *L)
{
    sk_bitmap_t **bmap;

    bmap = sk_lua_checkbitmap(L, 1);
    skBitmapClearAllBits(*bmap);
    return 0;
}

/*
 * =pod
 *
 * =item silk.B<bitmap_set_all(>I<bitmap>B<)>
 *
 * Set to B<true> (set to 1; turn on) all bits in I<bitmap>.
 *
 * =cut
 */
static int
sk_lua_bitmap_set_all(
    lua_State          *L)
{
    sk_bitmap_t **bmap;

    bmap = sk_lua_checkbitmap(L, 1);
    skBitmapSetAllBits(*bmap);
    return 0;
}

/*
 * =pod
 *
 * =item B<#>I<bitmap>
 *
 * An alias for
 * L<silk.B<bitmap_get_size()>|/"silk.B<bitmap_get_size(>I<bitmap>B<)>">.
 *
 * =item silk.B<bitmap_get_size(>I<bitmap>B<)>
 *
 * Return the size that was specified when I<bitmap> was created.
 *
 * =cut
 */
static int
sk_lua_bitmap_get_size(
    lua_State          *L)
{
    const sk_bitmap_t **bmap;
    lua_Unsigned count;

    bmap = (const sk_bitmap_t**)sk_lua_checkbitmap(L, 1);
    count = (lua_Unsigned)skBitmapGetSize(*bmap);
    lua_pushinteger(L, count);
    return 1;
}

/*
 * =pod
 *
 * =item silk.B<bitmap_get_count(>I<bitmap>B<)>
 *
 * Return the number of bits that are B<true> (set to 1; turned on) in
 * I<bitmap>.
 *
 * =cut
 */
static int
sk_lua_bitmap_get_count(
    lua_State          *L)
{
    const sk_bitmap_t **bmap;
    lua_Unsigned count;

    bmap = (const sk_bitmap_t**)sk_lua_checkbitmap(L, 1);
    count = (lua_Unsigned)skBitmapGetHighCount(*bmap);
    lua_pushinteger(L, count);
    return 1;
}

/*
 * =pod
 *
 * =item I<bitmap>B<[> I<n> B<]>
 *
 * An alias for
 * L<silk.B<bitmap_get_bit()>|/"silk.B<bitmap_get_bit(>I<bitmap>, I<n>B<)>">.
 *
 * =item silk.B<bitmap_get_bit(>I<bitmap>, I<n>B<)>
 *
 * Return the state of bit I<n> in I<bitmap>; that is, return B<true>
 * if bit I<n> in I<bitmap> is 1 or return B<false> if it is 0.
 *
 * Raise an error if I<n> is equal to or greater than the size of
 * I<bitmap>.
 *
 * =cut
 */
static int
sk_lua_bitmap_get_bit(
    lua_State          *L)
{
    const sk_bitmap_t **bmap;
    lua_Unsigned pos;

    bmap = (const sk_bitmap_t**)sk_lua_checkbitmap(L, 1);
    pos = sk_lua_checkunsigned(L, 2);
    if (pos >= skBitmapGetSize(*bmap)) {
        return luaL_error(L, "position is larger than bitmap size %d",
                          skBitmapGetSize(*bmap));
    }
    lua_pushboolean(L, skBitmapGetBit(*bmap, (uint32_t)pos));
    return 1;
}

/*
 * =pod
 *
 * =item silk.B<bitmap_set_bit(>I<bitmap>B<)>
 *
 * Set to B<true> (set to 1; turn on) bit I<n> in I<bitmap>.
 *
 * Raise an error if I<n> is equal to or greater than the size of
 * I<bitmap>.
 *
 * =cut
 */
static int
sk_lua_bitmap_set_bit(
    lua_State          *L)
{
    sk_bitmap_t **bmap;
    lua_Unsigned pos;

    bmap = sk_lua_checkbitmap(L, 1);
    pos = sk_lua_checkunsigned(L, 2);
    if (pos >= skBitmapGetSize(*bmap)) {
        return luaL_error(L, "position is larger than bitmap size %d",
                          skBitmapGetSize(*bmap));
    }
    skBitmapSetBit(*bmap, (uint32_t)pos);
    return 0;
}

/*
 * =pod
 *
 * =item silk.B<bitmap_clear_bit(>I<bitmap>B<)>
 *
 * Set to B<false> (set to 0; turn off) bit I<n> in I<bitmap>.
 *
 * Raise an error if I<n> is equal to or greater than the size of
 * I<bitmap>.
 *
 * =cut
 */
static int
sk_lua_bitmap_clear_bit(
    lua_State          *L)
{
    sk_bitmap_t **bmap;
    lua_Unsigned pos;

    bmap = sk_lua_checkbitmap(L, 1);
    pos = sk_lua_checkunsigned(L, 2);
    if (pos >= skBitmapGetSize(*bmap)) {
        return luaL_error(L, "Position is larger than bitmap size %d",
                          skBitmapGetSize(*bmap));
    }
    skBitmapClearBit(*bmap, (uint32_t)pos);
    return 0;
}

/*
 * =pod
 *
 * =item silk.B<bitmap_compliment_update(>I<bitmap>B<)>
 *
 * Flip all bits in I<bitmap> so that bits that were B<false> are now
 * B<true> and bits that were B<true> are now B<false>.
 *
 * =cut
 */
static int
sk_lua_bitmap_compliment_update(
    lua_State          *L)
{
    sk_bitmap_t **bmap;

    bmap = sk_lua_checkbitmap(L, 1);
    skBitmapComplement(*bmap);
    lua_settop(L, 1);
    return 1;
}

/*
 * =pod
 *
 * =item silk.B<bitmap_intersect_update(>I<bitmap>, I<bitmap2>B<)>
 *
 * Compute the intersection of I<bitmap> and I<bitmap2>, putting the
 * result in B<bitmap>.  In effect, this sets to B<false> all bits in
 * I<bitmap> that are B<false> in I<bitmap2>.  (Bits in I<bitmap> that
 * were previously B<false> are unchanged.)
 *
 * Raise an error if I<bitmap> and I<bitmap2> are of different sizes.
 *
 * =cut
 */
static int
sk_lua_bitmap_intersect_update(
    lua_State          *L)
{
    sk_bitmap_t **bmap1;
    const sk_bitmap_t **bmap2;
    int rv;

    bmap1 = sk_lua_checkbitmap(L, 1);
    bmap2 = (const sk_bitmap_t**)sk_lua_checkbitmap(L, 2);
    rv = skBitmapIntersection(*bmap1, *bmap2);
    if (rv) {
        return luaL_error(L, ("May not intersect bitmaps of different sizes"
                              "(%d and %d)"),
                          skBitmapGetSize(*bmap1), skBitmapGetSize(*bmap2));
    }
    lua_settop(L, 1);
    return 1;
}

/*
 * =pod
 *
 * =item silk.B<bitmap_union_update(>I<bitmap>B<)>
 *
 * Compute the union of I<bitmap> and I<bitmap2>, putting the result
 * in B<bitmap>.  In effect, this sets to B<true> all bits in I<bitmap>
 * that are B<true> in I<bitmap2>.  (Bits in I<bitmap> that were
 * previously B<true> are unchanged.)
 *
 * Raise an error if I<bitmap> and I<bitmap2> are of different sizes.
 *
 * =cut
 */
static int
sk_lua_bitmap_union_update(
    lua_State          *L)
{
    sk_bitmap_t **bmap1;
    const sk_bitmap_t **bmap2;
    int rv;

    bmap1 = sk_lua_checkbitmap(L, 1);
    bmap2 = (const sk_bitmap_t**)sk_lua_checkbitmap(L, 2);
    rv = skBitmapUnion(*bmap1, *bmap2);
    if (rv) {
        return luaL_error(L, ("May not combine bitmaps of different sizes"
                              "(%d and %d)"),
                          skBitmapGetSize(*bmap1), skBitmapGetSize(*bmap2));
    }
    lua_settop(L, 1);
    return 1;
}

/*
 * =pod
 *
 * =item silk.B<bitmap_count_consecutive(>I<bitmap>, I<start_pos>, I<state>B<)>
 *
 * Starting from I<start_pos> and increasing the index, count and
 * return the number of bits in I<bitmap> whose state is I<state>.
 * When I<state> is a number, 0 is false and any other value is true;
 * otherwise, I<state> is interpreted as a Lua boolean value.
 *
 * Raise an error if I<start_pos> is equal to or greater than the size of
 * I<bitmap>.
 *
 * =cut
 */
static int
sk_lua_bitmap_count_consecutive(
    lua_State          *L)
{
    const sk_bitmap_t **bmap;
    lua_Unsigned start_pos;
    int state;

    bmap = (const sk_bitmap_t**)sk_lua_checkbitmap(L, 1);
    start_pos = sk_lua_checkunsigned(L, 2);
    luaL_checkany(L, 3);
    /* treaat state as a Lua boolean but allow number "0" to be false
     * as well */
    if (lua_type(L, 3) == LUA_TNUMBER) {
        state = (0 != lua_tointeger(L, 3));
    } else {
        state = lua_toboolean(L, 3);
    }
    if (start_pos >= skBitmapGetSize(*bmap)) {
        return luaL_error(L, "Position is larger than bitmap size %d",
                          skBitmapGetSize(*bmap));
    }
    lua_pushinteger(L, skBitmapCountConsecutive(*bmap, (uint32_t)start_pos,
                                                state));
    return 1;
}

/*
 * =pod
 *
 * =item silk.B<bitmap_set_range(>I<bitmap>, I<start_pos>, I<end_pos>B<)>
 *
 * Set to B<true> (set to 1; turn on) all bits in I<bitmap> from
 * I<start_pos> to I<end_pos> inclusive.
 *
 * Raise an error if I<end_pos> is equal to or greater than the size
 * of I<bitmap> or if I<start_pos> is greater than I<end_pos>.
 *
 * =cut
 */
static int
sk_lua_bitmap_set_range(
    lua_State          *L)
{
    sk_bitmap_t **bmap;
    lua_Unsigned start_pos;
    lua_Unsigned end_pos;

    bmap = sk_lua_checkbitmap(L, 1);
    start_pos = sk_lua_checkunsigned(L, 2);
    end_pos = sk_lua_checkunsigned(L, 3);
    if (start_pos > end_pos) {
        return luaL_error(L, "Invalid range %d -- %d",
                          (uint32_t)start_pos, (uint32_t)end_pos);
    }
    if (end_pos >= skBitmapGetSize(*bmap)) {
        return luaL_error(L, "Position is larger than bitmap size %d",
                          skBitmapGetSize(*bmap));
    }
    if (skBitmapRangeSet(*bmap, (uint32_t)start_pos, (uint32_t)end_pos)) {
        skAbort();
    }
    return 0;
}

/*
 * =pod
 *
 * =item silk.B<bitmap_clear_range(>I<bitmap>, I<start_pos>, I<end_pos>B<)>
 *
 * Set to B<false> (set to 0; turn off) all bits in I<bitmap> from
 * I<start_pos> to I<end_pos> inclusive.
 *
 * Raise an error if I<end_pos> is equal to or greater than the size
 * of I<bitmap> or if I<start_pos> is greater than I<end_pos>.
 *
 * =cut
 */
static int
sk_lua_bitmap_clear_range(
    lua_State          *L)
{
    sk_bitmap_t **bmap;
    lua_Unsigned start_pos;
    lua_Unsigned end_pos;

    bmap = sk_lua_checkbitmap(L, 1);
    start_pos = sk_lua_checkunsigned(L, 2);
    end_pos = sk_lua_checkunsigned(L, 3);
    if (start_pos > end_pos) {
        return luaL_error(L, "Invalid range %d -- %d",
                          (uint32_t)start_pos, (uint32_t)end_pos);
    }
    if (end_pos >= skBitmapGetSize(*bmap)) {
        return luaL_error(L, "Position is larger than bitmap size %d",
                          skBitmapGetSize(*bmap));
    }
    if (skBitmapRangeClear(*bmap, (uint32_t)start_pos, (uint32_t)end_pos)) {
        skAbort();
    }
    return 0;
}

/*
 * =pod
 *
 * =item silk.B<bitmap_count_range(>I<bitmap>, I<start_pos>, I<end_pos>B<)>
 *
 * Return the number of bits that are B<true> (set to 1; turned on) in
 * I<bitmap> between I<start_pos> to I<end_pos> inclusive.
 *
 * Raise an error if I<end_pos> is equal to or greater than the size
 * of I<bitmap> or if I<start_pos> is greater than I<end_pos>.
 *
 * =cut
 */
static int
sk_lua_bitmap_count_range(
    lua_State          *L)
{
    sk_bitmap_t **bmap;
    lua_Unsigned start_pos;
    lua_Unsigned end_pos;

    bmap = sk_lua_checkbitmap(L, 1);
    start_pos = sk_lua_checkunsigned(L, 2);
    end_pos = sk_lua_checkunsigned(L, 3);
    if (start_pos > end_pos) {
        return luaL_error(L, "Invalid range %d -- %d",
                          (uint32_t)start_pos, (uint32_t)end_pos);
    }
    if (end_pos >= skBitmapGetSize(*bmap)) {
        return luaL_error(L, "Position is larger than bitmap size %d",
                          skBitmapGetSize(*bmap));
    }
    lua_pushinteger(L, skBitmapRangeCountHigh(*bmap, (uint32_t)start_pos,
                                              (uint32_t)end_pos));
    return 1;
}


/*
 * =pod
 *
 * =item silk.B<bitmap_copy(>I<bitmap>B<)>
 *
 * Create and return a copy of I<bitmap>.
 *
 * =cut
 */
static int
sk_lua_bitmap_copy(
    lua_State          *L)
{
    sk_bitmap_t **bmap;
    sk_bitmap_t **bmap2;

    bmap = sk_lua_checkbitmap(L, 1);

    bmap2 = sk_lua_newuserdata(L, sk_bitmap_t*);
    luaL_setmetatable(L, SK_LUA_BITMAP);

    if (0 != skBitmapCreate(bmap2, skBitmapGetSize(*bmap))) {
        return luaL_error(L, "Out of memory");
    }
    skBitmapUnion(*bmap2, *bmap);
    return 1;
}


/*
 *    Bitmap iterator callback function.
 */
static int
sk_lua_bitmap_iter_func(
    lua_State          *L)
{
    sk_bitmap_iter_t *iter;
    uint32_t pos;

    iter = (sk_bitmap_iter_t*)lua_touserdata(L, lua_upvalueindex(1));
    if (skBitmapIteratorNext(iter, &pos) != SK_ITERATOR_OK) {
        return 0;
    }
    lua_pushinteger(L, (lua_Unsigned)pos);
    return 1;
}


/*
 * =pod
 *
 * =item silk.B<bitmap_iter(>I<bitmap>B<)>
 *
 * Return an iterator that returns the positions of all the set bits
 * in I<bitmap>.
 *
 * =cut
 */
static int
sk_lua_bitmap_iter(
    lua_State          *L)
{
    sk_bitmap_t **bmap;
    sk_bitmap_iter_t *iter;

    bmap = sk_lua_checkbitmap(L, 1);

    iter = sk_lua_newuserdata(L, sk_bitmap_iter_t);
    skBitmapIteratorBind(*bmap, iter);

    /* Include the bitmap in the closure to make sure it is not
     * garbage collected during iteration */
    lua_pushvalue(L, 1);
    lua_pushcclosure(L, sk_lua_bitmap_iter_func, 2);
    return 1;
}

/*
 *    Garbage collection callback for Bitmaps.
 */
static int
sk_lua_bitmap_gc(
    lua_State          *L)
{
    sk_bitmap_t **bmap;

    bmap = (sk_bitmap_t**)lua_touserdata(L, 1);
    skBitmapDestroy(bmap);
    return 0;
}

/*
 * =pod
 *
 * =item silk.B<bitmap(>I<size>B<)>
 *
 * Create and return a new bitmap capable of holding B<true>/B<false>
 * values where the allowable bitmap indexes range from 0 to 1 less
 * than I<size>.  All bits in the new bitmap are set to B<false>.
 *
 * =cut
 */
static int
sk_lua_bitmap(
    lua_State          *L)
{
    sk_bitmap_t **bmap;
    lua_Integer num_bits;
    int is_num;

    lua_settop(L, 1);
    switch (lua_type(L, 1)) {
      case LUA_TSTRING:
      case LUA_TNUMBER:
        is_num = 0;
        num_bits = lua_tointegerx(L, 1, &is_num);
        if (1 == is_num) {
            if (UINT32_MAX < num_bits || 0 >= num_bits) {
                return luaL_error(L, "Bitmap size is out of bounds");
            }
            bmap = sk_lua_newuserdata(L, sk_bitmap_t*);
            luaL_setmetatable(L, SK_LUA_BITMAP);
            *bmap = NULL;
            if (0 != skBitmapCreate(bmap, (uint32_t)num_bits)) {
                return luaL_error(L, "Out of memory");
            }
            return 1;
        }
        break;
    }
    return sk_lua_argerror(L, 1, "number expected, got %s",
                           sk_lua_typename(L, 1));
}

static const luaL_Reg sk_lua_bitmap_metatable[] = {
    {"__gc",        sk_lua_bitmap_gc},
    {"__index",     sk_lua_bitmap_get_bit},
    {"__len",       sk_lua_bitmap_get_size},
    {NULL, NULL}
};

static const luaL_Reg sk_lua_bitmap_methods[] = {
    {"set_range",           sk_lua_bitmap_set_range},
    {"clear_range",         sk_lua_bitmap_clear_range},
    {"count_range",         sk_lua_bitmap_count_range},
    {"intersect_update",    sk_lua_bitmap_intersect_update},
    {"union_update",        sk_lua_bitmap_union_update},
    {"get_size",            sk_lua_bitmap_get_size},
    {"compliment_update",   sk_lua_bitmap_compliment_update},
    {"clear_all",           sk_lua_bitmap_clear_all},
    {"set_all",             sk_lua_bitmap_set_all},
    {"clear_bit",           sk_lua_bitmap_clear_bit},
    {"set_bit",             sk_lua_bitmap_set_bit},
    {"get_bit",             sk_lua_bitmap_get_bit},
    {"get_count",           sk_lua_bitmap_get_count},
    {"copy",                sk_lua_bitmap_copy},
    {"count_consecutive",   sk_lua_bitmap_count_consecutive},
    {"iter",                sk_lua_bitmap_iter},
    {NULL, NULL}
};



/*
 *  ********************************************************************
 *  TCP Flags
 *  ********************************************************************
 */

/*
 * =pod
 *
 * =item silk.B<tcpflags_parse(>I<string>B<)>
 *
 * Parse I<string> as a set of TCP flags and return an unsigned
 * integer representing those flags.  Any whitespace in I<string> is
 * ignored.  I<string> may contain the following characters, which map
 * to the specified value.
 *
 * =over 4
 *
 * =item F
 *
 * =item f
 *
 * TCP_FIN
 *
 * =item S
 *
 * =item s
 *
 * TCP_SYN
 *
 * =item R
 *
 * =item r
 *
 * TCP_RST
 *
 * =item P
 *
 * =item p
 *
 * TCP_PSH
 *
 * =item A
 *
 * =item a
 *
 * TCP_ACK
 *
 * =item U
 *
 * =item u
 *
 * TCP_URG
 *
 * =item E
 *
 * =item e
 *
 * TCP_ECE
 *
 * =item C
 *
 * =item c
 *
 * TCP_CWR
 *
 * =back
 *
 * =cut
 */
static int
sk_lua_tcpflags_parse(
    lua_State          *L)
{
    const char *str;
    lua_Unsigned n;
    uint8_t flags;
    int rv;

    str = luaL_checkstring(L, 1);
    rv = skStringParseTCPFlags(&flags, str);
    if (rv) {
        return sk_lua_argerror(L, 1, "invalid tcpflags '%s': %s",
                               str, skStringParseStrerror(rv));
    }
    n = (lua_Unsigned)flags;
    lua_pushinteger(L, n);
    return 1;
}

/*
 * =pod
 *
 * =item silk.B<tcpflags_to_string(>I<integer>[, I<format>]B<)>
 *
 * Assume I<integer> represents a TCP flags value and return a
 * human-readable version of those flags.
 *
 * Possible values for I<format> are:
 *
 * =over 4
 *
 * =item padded
 *
 * Format the TCP flags with a space representing any unset flag.
 *
 * =item compact
 *
 * Format the TCP flags in as few characters as possible.  When
 * I<integer> is 0, the empty string is returned.
 *
 * =back
 *
 * When no I<format> value is specified, the default is I<compact>.
 *
 * =cut
 */
static int
sk_lua_tcpflags_to_string(
    lua_State          *L)
{
    static const char *options[] = {
        "compact", "padded", NULL
    };
    char buf[SK_TCPFLAGS_STRLEN];
    lua_Integer n;
    uint8_t tcp_flags;
    int print_flags;

    n = luaL_checkinteger(L, 1);
    if (n < 0 || n > UINT8_MAX) {
        return sk_lua_argerror(L, 1,
                               "integer beteen 0 and %d expected, got %I",
                               UINT8_MAX, n);
    }
    print_flags = luaL_checkoption(L, 2, options[0], options);
    if (print_flags) {
        print_flags = SK_PADDED_FLAGS;
    }
    tcp_flags = (uint8_t)n;
    lua_pushfstring(L, "%s", skTCPFlagsString(tcp_flags, buf, print_flags));
    return 1;
}

/*
 * =pod
 *
 * =item silk.B<tcpflags_matches(>I<integer>, I<string>B<)>
 *
 * Treat I<integer> as a TCP flags value, such as that produced by
 * B<tcpflags_parse()>.
 *
 * Parse I<string> as a pair of TCP flags values separated by a slash
 * (C</>), where the values before and after the slash are parsed
 * using B<tcpflags_parse()>.
 *
 * Treat the TCP flags value that appear after the slash as a mask,
 * and use it to mask the TCP flags in I<integer>.
 *
 * Treat the TCP flags value that appear before the slash as a check,
 * and verify that the masked TCP flag value matches it.  If the
 * values match, return B<true>; otherwise, return B<false>.
 *
 * When no slash is present in I<string>, parse it using
 * B<tcpflags_parse()> and treat it as both the mask and the bits to
 * check.
 *
 * =cut
 */
static int
sk_lua_tcpflags_matches(
    lua_State          *L)
{
    const char *str;
    lua_Integer n;
    uint8_t flags, check, mask;
    int rv;

    n = luaL_checkinteger(L, 1);
    if (n < 0 || n > UINT8_MAX) {
        return sk_lua_argerror(L, 1,"integer beteen 0 and %d expected, got %I",
                               UINT8_MAX, n);
    }
    flags = (uint8_t)n;

    str = luaL_checkstring(L, 2);
    rv = skStringParseTCPFlagsHighMask(&check, &mask, str);
    if (rv) {
        if (SKUTILS_ERR_SHORT == rv && NULL == strchr(str, '/')) {
            mask = check;
        } else {
            return sk_lua_argerror(L, 2, "invalid check/mask pair '%s': %s",
                                   str, skStringParseStrerror(rv));
        }
    }

    lua_pushboolean(L, ((flags & mask) == check));
    return 1;
}



/*
 *  ********************************************************************
 *  TIMES
 *  ********************************************************************
 */

sktime_t *
sk_lua_push_datetime(
    lua_State          *L)
{
    sktime_t *dt = sk_lua_newuserdata(L, sktime_t);
    luaL_setmetatable(L, SK_LUA_DATETIME);
    return dt;
}

/*
 * =pod
 *
 * =item silk.B<datetime(>I<string>B<)>
 *
 * Parse I<string> as a date-time and return an datetime object
 * representing the number of milliseconds since the UNIX epoch.
 * I<string> must represent a time to at least day precision.
 *
 * The format for I<string> is that accepted by B<rwfilter(1)>.
 *
 * =item silk.B<datetime(>I<number>B<)>
 *
 * Create a datetime object from a number.  The number represents the
 * number of milliseconds since the UNIX epoch time.
 *
 * =cut
 */
static int
sk_lua_datetime(
    lua_State          *L)
{
    const char *str;
    sktime_t *t;
    int rv;

    luaL_checkany(L, 1);
    t = sk_lua_push_datetime(L);
    switch (lua_type(L, 1)) {
      case LUA_TSTRING:
        str = lua_tostring(L, 1);
        rv = skStringParseDatetime(t, str, NULL);
        if (rv) {
            return sk_lua_argerror(L, 1, "invalid datetime '%s': %s",
                                   str, skStringParseStrerror(rv));
        }
        break;
      case LUA_TNUMBER:
        *t = (sktime_t)lua_tointeger(L, 1);
        break;
      default:
        return sk_lua_argerror(L, 1, "string or number expected, got %s",
                               sk_lua_typename(L, 1));
    }
    return 1;
}


/*
 * =pod
 *
 * =item silk.B<datetime_parse_range(>I<string>B<)>
 *
 * Parse I<string> as a pair of date-time strings separated by a
 * single hyphen C<-> and return a pair of datetimes representing the
 * start of the range and the end of the range.  Both dates in
 * I<string> must represent times to at least day precision.
 *
 * When the end time is not specified to millisecond precision, any
 * unspecified values are set to the maximum possible value.
 *
 * The format for I<string> is that accepted by B<rwfilter(1)>.
 *
 * =cut
 */
static int
sk_lua_datetime_parse_range(
    lua_State          *L)
{
    const char *str;
    sktime_t *t1, *t2;
    unsigned int precision;
    int rv;

    str = sk_lua_checkstring(L, 1);
    t1 = sk_lua_push_datetime(L);
    t2 = sk_lua_push_datetime(L);
    rv = skStringParseDatetimeRange(t1, t2, str, NULL, &precision);
    if (rv || *t2 == INT64_MAX) {
        const char *errmsg;
        if (rv) {
            errmsg = skStringParseStrerror(rv);
        } else {
            errmsg = "Not a range";
        }
        return sk_lua_argerror(L, 1, "invalid datetime range '%s': %s", str,
                               errmsg);
    }
    rv = skDatetimeCeiling(t2, t2, precision);
    if (rv) {
        return luaL_error(L, "ceiling return value of 0 expected, got %d", rv);
    }

    return 2;
}


/*
 * =pod
 *
 * =item silk.B<datetime_to_string(>I<datetime>[, I<format>[, I<format>...]]B<)>
 *
 * Return a human-readable version of the given datetime.
 *
 * Possible values for I<format> are zero or more of the following:
 *
 * =over 4
 *
 * =item y/m/d
 *
 * Format the timestamp as I<YYYY>/I<MM>/I<DD>TI<hh>:I<mm>:I<ss>.I<sss>.
 *
 * =item iso
 *
 * Format the timestamp as I<YYYY>-I<MM>-I<DD> I<hh>:I<mm>:I<ss>.I<sss>.
 *
 * =item m/d/y
 *
 * Format the timestamp as I<MM>/I<DD>/I<YYYY> I<hh>:I<mm>:I<ss>.I<sss>.
 *
 * =item epoch
 *
 * Format the timestamp as the number of seconds since 00:00:00 UTC on
 * 1970-01-01.
 *
 * =back
 *
 * When a timezone is specified, it is used regardless of the default
 * timezone support compiled into SiLK.  The timezone is one of:
 *
 * =over 4
 *
 * =item utc
 *
 * Use Coordinated Universal Time to print timestamps.
 *
 * =item local
 *
 * Use the TZ environment variable or the local timezone.
 *
 * =back
 *
 * One modifier is available:
 *
 * =over 4
 *
 * =item no-msec
 *
 * Truncate the milliseconds value on the timestamp.
 *
 * =back
 *
 * When no I<format> values are specified, the default is to print
 * milliseconds, use the the C<y/m/d> format, and use the default
 * timezone that was specified when SiLK was compiled.
 *
 * =cut
 */
static int
sk_lua_datetime_to_string(
    lua_State          *L)
{
    static const char *options[] = {
        "y/m/d", "no-msec", "m/d/y", "epoch", "iso", "utc", "local", NULL
    };
    char buf[SKTIMESTAMP_STRLEN];
    sktime_t *t;
    int rv;
    int argc;
    int i;
    unsigned int flags = 0;

    t = sk_lua_checkdatetime(L, 1);

    argc = lua_gettop(L);
    for (i = 2; i <= argc; ++i) {
        rv = luaL_checkoption(L, i, NULL, options);
        if (rv > 0) {
            flags |= (1 << (rv - 1));
        }
    }
    lua_pushfstring(L, "%s", sktimestamp_r(buf, *t, flags));
    return 1;
}

/*
 * =pod
 *
 * =item silk.B<datetime_difference(>I<end>, I<start>B<)>
 *
 * Return the difference of the datetime I<end> minus the datetime
 * I<start> as a number of milliseconds.
 *
 * =cut
 */
static int
sk_lua_datetime_difference(
    lua_State          *L)
{
    sktime_t *a, *b;
    a = sk_lua_checkdatetime(L, 1);
    b = sk_lua_checkdatetime(L, 2);
    lua_pushinteger(L, *a - *b);
    return 1;
}

/*
 * =pod
 *
 * =item silk.B<datetime_to_number(>I<datetime>B<)>
 *
 * Return I<datetime> as a number of milliseconds since the UNIX epoch
 * datetime.
 *
 * =cut
 */
static int
sk_lua_datetime_to_number(
    lua_State          *L)
{
    sktime_t *t = sk_lua_checkdatetime(L, 1);
    lua_pushinteger(L, *t);
    return 1;
}

/*
 * =pod
 *
 * =item silk.B<datetime_add_duration(>I<datetime>, I<number>B<)>
 *
 * Return a new datetime that is the sum of I<datetime> and I<number>
 * milliseconds.  I<number> may be negative.  An error is thrown
 * if the operation would cause an overflow or an underflow.
 *
 * =cut
 */
static int
sk_lua_datetime_add_duration(
    lua_State          *L)
{
    sktime_t *t;
    lua_Integer d;
    int sign;
    sktime_t *res;

    t = sk_lua_checkdatetime(L, 1);
    d = luaL_checkinteger(L, 2);
    res = sk_lua_push_datetime(L);
    *res = *t + d;
    sign = d < 0;
    if (((*t < 0) == sign) && ((*res < 0) != sign)) {
        return luaL_error(L, "Datetime over-or-underflow");
    }
    return 1;
}

static const luaL_Reg sk_lua_datetime_metatable[] = {
    {"__tostring", sk_lua_datetime_to_string},
    {NULL, NULL}
};

static const luaL_Reg sk_lua_datetime_methods[] = {
    {"to_string", sk_lua_datetime_to_string},
    {"to_number", sk_lua_datetime_to_number},
    {"difference", sk_lua_datetime_difference},
    {"add_duration", sk_lua_datetime_add_duration},
    {NULL, NULL}
};

static const luaL_Reg sk_lua_datetime_static_methods[] = {
    {"parse_range", sk_lua_datetime_parse_range},
    {NULL, NULL}
};


/*
 *  ********************************************************************
 *  Flow Attributes
 *  ********************************************************************
 */

/*
 * =pod
 *
 * =item silk.B<attributes_parse(>I<string>B<)>
 *
 * Parse I<string> as a set of flow attributes and return an unsigned
 * integer representing those attributes.  Any whitespace in I<string>
 * is ignored.  I<string> may contain the following characters, which
 * map to the specified value.
 *
 * =over 4
 *
 * =item C, c
 *
 * 0x40.  Flow is a continuation of a previous flow that was killed
 * prematurely due to a timeout by the collector.
 *
 * =item T, t
 *
 * 0x20.  Flow ends prematurely due to a timeout by the collector.
 *
 * =item S, s
 *
 * 0x10.  Flow has packets all of the same size.
 *
 * =item F, f
 *
 * 0x08.  Flow received packets following the FIN packet that were not
 * ACK or RST packets.
 *
 * =back
 *
 * =cut
 */
static int
sk_lua_attributes_parse(
    lua_State          *L)
{
    const char *str;
    lua_Unsigned n;
    uint8_t attributes;
    int rv;

    str = luaL_checkstring(L, 1);
    rv = skStringParseTCPState(&attributes, str);
    if (rv) {
        return sk_lua_argerror(L, 1, "invalid attributes '%s': %s",
                               str, skStringParseStrerror(rv));
    }
    n = (lua_Unsigned)attributes;
    lua_pushinteger(L, n);
    return 1;
}

/*
 * =pod
 *
 * =item silk.B<attributes_to_string(>I<integer>[, I<format>]B<)>
 *
 * Assume I<integer> represents a flow attributes value and return a
 * human-readable version of those attributes.
 *
 * Possible values for I<format> are:
 *
 * =over 4
 *
 * =item padded
 *
 * Format the flow attributes with a space representing any unset flag.
 *
 * =item compact
 *
 * Format the flow attributes in as few characters as possible.  When
 * I<integer> is 0, the empty string is returned.
 *
 * =back
 *
 * When no I<format> value is specified, the default is I<compact>.
 *
 * =cut
 */
static int
sk_lua_attributes_to_string(
    lua_State          *L)
{
    static const char *options[] = {
        "compact", "padded", NULL
    };
    char buf[SK_TCP_STATE_STRLEN];
    lua_Integer n;
    uint8_t attributes;
    int print_flags;

    n = luaL_checkinteger(L, 1);
    if (n < 0 || n > UINT8_MAX) {
        return sk_lua_argerror(L, 1, "integer beteen 0 and %d expected, got %I",
                               UINT8_MAX, n);
    }
    print_flags = luaL_checkoption(L, 2, options[0], options);
    if (print_flags) {
        print_flags = SK_PADDED_FLAGS;
    }
    attributes = (uint8_t)n;
    lua_pushfstring(L, "%s", skTCPStateString(attributes, buf, print_flags));
    return 1;
}

/*
 * =pod
 *
 * =item silk.B<attributes_matches(>I<integer>, I<string>B<)>
 *
 * Treat I<integer> as a flow attributes value, such as that produced
 * by B<attributes_parse()>.
 *
 * Parse I<string> as a pair of flow attributes values separated by a
 * slash (C</>), where the values before and after the slash are
 * parsed using B<attributes_parse()>.
 *
 * Treat the flow attributes value that appear after the slash as a
 * mask, and use it to mask the flow attributes in I<integer>.
 *
 * Treat the flow attributes value that appear before the slash as a
 * check, and verify that the masked attributes value matches it.  If
 * the values match, return B<true>; otherwise, return B<false>.
 *
 * When no slash is present in I<string>, parse it using
 * B<attributes_parse()> and treat it as both the mask and the bits to
 * check.
 *
 * =cut
 */
static int
sk_lua_attributes_matches(
    lua_State          *L)
{
    const char *str;
    lua_Integer n;
    uint8_t attributes, check, mask;
    int rv;

    n = luaL_checkinteger(L, 1);
    if (n < 0 || n > UINT8_MAX) {
        return sk_lua_argerror(L, 1, "integer beteen 0 and %d expected, got %I",
                               UINT8_MAX, n);
    }
    attributes = (uint8_t)n;

    str = luaL_checkstring(L, 2);
    rv = skStringParseTCPStateHighMask(&check, &mask, str);
    if (rv) {
        if (SKUTILS_ERR_SHORT == rv && NULL == strchr(str, '/')) {
            mask = check;
        } else {
            return sk_lua_argerror(L, 2, "invalid check/mask pair '%s': %s",
                                   str, skStringParseStrerror(rv));
        }
    }

    lua_pushboolean(L, ((attributes & mask) == check));
    return 1;
}



/*
 *  ********************************************************************
 *  SIDECAR
 *  ********************************************************************
 */

/*
 *    A sidecar userdata is simply a pointer to an sk_sidecar_t.
 *
 *    The sidecar userdata has a uservalue that is a table which
 *    contains two values as documented next.
 *
 *    At index SKLUA_SIDECAR_IDX_TABLE is a table whose structure
 *    matches the potential structure of a sidecar field on an rwRec.
 *    This table is frozen (read-only) when reading a sidecar from a
 *    file.  The structure is writable when creating a sidecar from
 *    within Lua, but it must be frozen before being addd to a record.
 *    The keys of this table are strings.  For a frozen table, the
 *    values are sk_lua_sc_elem_t userdata, or a table the represents
 *    a structured data.  Before being frozen, a the values may also
 *    be a string that is the type of field (types given by the
 *    sk_lua_sc_elem_type_name[] array) or a table that that contains
 *    keys from the sk_lua_sc_elem_key_name[] and represents a
 *    sk_lua_sc_elem_t.
 *
 *    At index SKLUA_SIDECAR_IDX_ISFROZEN is a boolean to say whether
 *    the sidecar is frozen.
 */

/* indexes into the sidecar uservalue */
#define SKLUA_SIDECAR_IDX_TABLE     1
#define SKLUA_SIDECAR_IDX_ISFROZEN  2
#define SKLUA_SIDECAR_IDX___MAX__   2

/*
 *    The data type names for each element of an sk_lua_sc_elem_t.
 *    Keep this array in sync with sk_lua_sc_elem_type_id[] below.
 */
static const char *sk_lua_sc_elem_type_name[] = {
    "uint8",
    "uint16",
    "uint32",
    "uint64",
    "double",
    "string",
    "binary",
    "ip4",
    "ip6",
    "datetime",
    "boolean",
    "empty",
    "list",
    "table",
    NULL
};

/*
 *    The numeric data types for each element of an sk_lua_sc_elem_t.
 *    Keep this array in sync with sk_lua_sc_elem_type_name[] above.
 */
static const sk_sidecar_type_t sk_lua_sc_elem_type_id[] = {
    SK_SIDECAR_UINT8,
    SK_SIDECAR_UINT16,
    SK_SIDECAR_UINT32,
    SK_SIDECAR_UINT64,
    SK_SIDECAR_DOUBLE,
    SK_SIDECAR_STRING,
    SK_SIDECAR_BINARY,
    SK_SIDECAR_ADDR_IP4,
    SK_SIDECAR_ADDR_IP6,
    SK_SIDECAR_DATETIME,
    SK_SIDECAR_BOOLEAN,
    SK_SIDECAR_EMPTY,
    SK_SIDECAR_LIST,
    SK_SIDECAR_TABLE,
    SK_SIDECAR_UNKNOWN
};

/*
 *    Name of fields on an sk_lua_sc_elem_t.  Keep this in sync with
 *    values of sk_lua_sc_elem_key_id_t defined below.
 */
static const char *sk_lua_sc_elem_key_name[] = {
    "type", "list_elem_type", "enterprise_id", "element_id", NULL
};

/*
 *    The type of things one may ask of an sk_lua_sc_elem_t.  Keep
 *    this in sync with sk_lua_sc_elem_key_name[] defined above.
 */
enum sk_lua_sc_elem_key_id_en {
    SKLUA_SC_ELEM_KEY_TYPE,
    SKLUA_SC_ELEM_KEY_LIST_ELEM_TYPE,
    SKLUA_SC_ELEM_KEY_ENTERPRISE_ID,
    SKLUA_SC_ELEM_KEY_ELEMENT_ID
};
typedef enum sk_lua_sc_elem_key_id_en sk_lua_sc_elem_key_id_t;

/*
 *    Lua representation of an sk_sidecar_elem_t.  Name is not stored
 *    here; instead these are stored in a table, and the name comes
 *    from the location in the table.
 */
struct sk_lua_sc_elem_st {
    /*  The name of the type or the name of the list_elem_type when is
     *  the 'e_is_list' member is true */
    const char         *e_type_name;
    /*  The IPFIX ident ((enterpriseId << 32) | elementId) */
    sk_field_ident_t    e_ipfix_ident;
    /*  The type of element or the type of the list element when the
     *  'e_is_list' member is true */
    sk_sidecar_type_t   e_type_id;
    /*  Whether this item is a list (1 == yes, 0 == no) */
    uint8_t             e_is_list;
};
typedef struct sk_lua_sc_elem_st sk_lua_sc_elem_t;


#define sk_lua_sc_elem_push_type(m_L, m_sc_elem)        \
    lua_pushstring((m_L), (((m_sc_elem)->e_is_list)     \
                           ? "list"                     \
                           : (m_sc_elem)->e_type_name))

#define sk_lua_sc_elem_push_list_type(m_L, m_sc_elem)   \
    lua_pushstring((m_L), (((m_sc_elem)->e_is_list)     \
                           ? (m_sc_elem)->e_type_name   \
                           : NULL))

#define sk_lua_sc_elem_push_element_id(m_L, m_sc_elem)                  \
    lua_pushinteger((m_L), SK_FIELD_IDENT_GET_ID((m_sc_elem)->e_ipfix_ident))

#define sk_lua_sc_elem_push_enterprise_id(m_L, m_sc_elem)               \
    lua_pushinteger((m_L), SK_FIELD_IDENT_GET_PEN((m_sc_elem)->e_ipfix_ident))


/*
 *    Return the name that corresponds to the sidecar element data
 *    type in 't'.
 */
static const char *
sk_lua_sc_elem_type_id_to_name(
    sk_sidecar_type_t   t)
{
    size_t i;

    for (i = 0; sk_lua_sc_elem_type_name[i] != NULL; ++i) {
        if (sk_lua_sc_elem_type_id[i] == t) {
            return sk_lua_sc_elem_type_name[i];
        }
    }
    return NULL;
}

/*
 *    Return a table that contains the sidecar element types indexed
 *    in two ways: Each type name is used as a key with its integer
 *    type as a value, and each integer type is used as a key with its
 *    name as the value.
 *
 *    Currently this is an internal function that is called by
 *    silk.lua when freezing a sidecar.
 */
static int
sk_lua_sc_elem_make_type_table(
    lua_State          *L)
{
    const size_t count = (sizeof(sk_lua_sc_elem_type_name)
                          / sizeof(sk_lua_sc_elem_type_name[0]) - 1);
    size_t i;
    size_t maxid;

    /* since number keys are sparse, does this make sense? */
    maxid = 0;
    for (i = 0; i < count; ++i) {
        if (sk_lua_sc_elem_type_id[i] > maxid) {
            maxid = sk_lua_sc_elem_type_id[i];
        }
    }

    lua_createtable(L, count, maxid);
    for (i = 0; sk_lua_sc_elem_type_name[i] != NULL; ++i) {
        /* map from name to id */
        lua_pushinteger(L, sk_lua_sc_elem_type_id[i]);
        lua_setfield(L, -2, sk_lua_sc_elem_type_name[i]);
        /* map from id to name */
        lua_pushstring(L, sk_lua_sc_elem_type_name[i]);
        lua_seti(L, -2, sk_lua_sc_elem_type_id[i]);
    }

    return 1;
}


/*
 *    If the type of 'elem' is SK_SIDECAR_TABLE, push an empty table
 *    onto the Lua stack.
 *
 *    Otherwise, create an sk_lua_sc_elem_t userdata, push it onto the
 *    stack, and use it to store the information stored in the
 *    sk_sidecar_elem_t 'elem'.
 */
static void
sk_lua_push_sidecar_elem(
    lua_State                  *L,
    const sk_sidecar_elem_t    *elem)
{
    sk_lua_sc_elem_t *e;
    sk_sidecar_type_t t;

    t = sk_sidecar_elem_get_data_type(elem);
    if (SK_SIDECAR_TABLE == t) {
        lua_createtable(L, 0, 0);
        return;
    }

    e = sk_lua_newuserdata(L, sk_lua_sc_elem_t);
    luaL_setmetatable(L, SK_LUA_SIDECAR_ELEM);

    e->e_ipfix_ident = sk_sidecar_elem_get_ipfix_ident(elem);
    if (SK_SIDECAR_LIST == t) {
        e->e_is_list = 1;
        e->e_type_id = sk_sidecar_elem_get_list_elem_type(elem);
        e->e_type_name = sk_lua_sc_elem_type_id_to_name(e->e_type_id);
    } else {
        e->e_is_list = 0;
        e->e_type_id = t;
        e->e_type_name = sk_lua_sc_elem_type_id_to_name(t);
    }
}


/*
 *
 *    sc_elem_create(type [, element_id [, enterprise_id]])
 *
 *    sc_elem_create("list", list_type [, element_id [, enterprise_id]])
 *
 *    The type is required.  This may be followed by the IPFIX
 *    element_id and an optional IPFIX enterprise_id (PEN).
 *
 *    If type is "list", then the second argument must be the type of
 *    list, and this may be followed by the element and enterprise
 *    IDs.
 *
 *    Currently this is an internal function that is called by
 *    silk.lua when freezing a sidecar.
 *
 *
 */
/*
 */
static int
sk_lua_sc_elem_create(
    lua_State          *L)
{
    sk_lua_sc_elem_t *e;
    sk_sidecar_type_t type1;
    sk_sidecar_type_t type2;
    lua_Integer elem_id;
    lua_Integer ent_id;
    int pos1;
    int pos2;
    int arg;

    pos1 = luaL_checkoption(L, 1, NULL, sk_lua_sc_elem_type_name);
    type1 = sk_lua_sc_elem_type_id[pos1];

    arg = 2;
    if (SK_SIDECAR_LIST == type1) {
        pos2 = luaL_checkoption(L, arg, NULL, sk_lua_sc_elem_type_name);
        type2 = sk_lua_sc_elem_type_id[pos2];
        ++arg;
    }
    if (lua_isnoneornil(L, arg)) {
        elem_id = ent_id = 0;
    } else {
        elem_id = sk_lua_checkunsigned(L, arg);
        ++arg;
        if (lua_isnoneornil(L, arg)) {
            ent_id = 0;
        } else {
            ent_id = sk_lua_checkunsigned(L, arg);
            ++arg;
            if (!lua_isnoneornil(L, arg)) {
                luaL_error(L, "too many arguments");
            }
        }
    }

    e = sk_lua_newuserdata(L, sk_lua_sc_elem_t);
    luaL_setmetatable(L, SK_LUA_SIDECAR_ELEM);

    e->e_ipfix_ident = SK_FIELD_IDENT_CREATE(ent_id, elem_id);
    if (SK_SIDECAR_LIST == type1) {
        e->e_is_list = 1;
        e->e_type_id = type2;
        e->e_type_name = sk_lua_sc_elem_type_name[pos2];
    } else {
        e->e_is_list = 0;
        e->e_type_id = type1;
        e->e_type_name = sk_lua_sc_elem_type_name[pos1];
    }

    return 1;
}


/*
 * =pod
 *
 * =item I<sidecar_elem>B<[> I<name> B<]>
 *
 * Return the value of the I<name> attribute of the sidecar element
 * I<sidecar_elem>.  I<name> is one of
 *
 * =over 4
 *
 * =item type
 *
 * A string giving the type of data in this element.
 *
 * =item list_elem_type
 *
 * When C<type> is C<list>, a string giving the type of elements in
 * the list, or B<nil> when the element is not a list.
 *
 * =item enterprise_id
 *
 * A number representing the IPFIX enterprise id (a.k.a. Private
 * Enterprise Number (PEN)) that was specified when the sidecar
 * element was created.
 *
 * =item element_id
 *
 * A number representing the IPFIX element id that was specified when
 * the sidecar element was created.
 *
 * =back
 *
 * =cut
 */
static int
sk_lua_sc_elem_index(
    lua_State          *L)
{
    const sk_lua_sc_elem_t *elem;

    elem = (sk_lua_sc_elem_t *)lua_touserdata(L, 1);
    switch (luaL_checkoption(L, 2, NULL, sk_lua_sc_elem_key_name)) {
      case SKLUA_SC_ELEM_KEY_ELEMENT_ID:
        lua_pushinteger(L, SK_FIELD_IDENT_GET_ID(elem->e_ipfix_ident));
        break;
      case SKLUA_SC_ELEM_KEY_ENTERPRISE_ID:
        lua_pushinteger(L, SK_FIELD_IDENT_GET_PEN(elem->e_ipfix_ident));
        break;
      case SKLUA_SC_ELEM_KEY_LIST_ELEM_TYPE:
        sk_lua_sc_elem_push_list_type(L, elem);
        break;
      case SKLUA_SC_ELEM_KEY_TYPE:
        sk_lua_sc_elem_push_type(L, elem);
        break;
      default:
        skAbort();
    }
    return 1;
}


/*
 *    Function that is the __pairs iterator.  Use the integer upvalue
 *    to determine which key,value pair to return.  Skip over keys
 *    when the value is NULL or 0.
 */
static int
sk_lua_sc_elem_pairs_iter(
    lua_State          *L)
{
    const sk_lua_sc_elem_t *elem;
    lua_Integer i;
    int isnum;
    int retval = 2;

    i = lua_tointegerx(L, lua_upvalueindex(1), &isnum);
    if (!isnum || i < 0) {
        lua_pushnil(L);
        return 1;
    }
    elem = (sk_lua_sc_elem_t *)lua_touserdata(L, 1);
    for (;;) {
        switch (i) {
          case SKLUA_SC_ELEM_KEY_TYPE:
            lua_pushstring(L, sk_lua_sc_elem_key_name[i]);
            sk_lua_sc_elem_push_type(L, elem);
            ++i;
            goto END;
          case SKLUA_SC_ELEM_KEY_LIST_ELEM_TYPE:
            if (elem->e_is_list) {
                lua_pushstring(L, sk_lua_sc_elem_key_name[i]);
                sk_lua_sc_elem_push_list_type(L, elem);
                ++i;
                goto END;
            }
            break;
          case SKLUA_SC_ELEM_KEY_ENTERPRISE_ID:
            if (SK_FIELD_IDENT_GET_PEN(elem->e_ipfix_ident)) {
                lua_pushstring(L, sk_lua_sc_elem_key_name[i]);
                sk_lua_sc_elem_push_enterprise_id(L, elem);
                ++i;
                goto END;
            }
            break;
          case SKLUA_SC_ELEM_KEY_ELEMENT_ID:
            if (SK_FIELD_IDENT_GET_ID(elem->e_ipfix_ident)) {
                lua_pushstring(L, sk_lua_sc_elem_key_name[i]);
                sk_lua_sc_elem_push_element_id(L, elem);
                ++i;
                goto END;
            }
            break;
          default:
            retval = 1;
            lua_pushnil(L);
            goto END;
        }
        ++i;
    }

  END:
    /* update the upvalue */
    lua_pushinteger(L, i);
    lua_replace(L, lua_upvalueindex(1));

    return retval;
}


/*
 * =pod
 *
 * =item B<pairs(>I<sidecar_elem>B<)>
 *
 * Return an iterator designed for the Lua B<for> statement that
 * iterates over (name, value) pairs of I<sidecar_elem>.  May be used
 * as
 * B<for I<name>, I<value> in pairs(I<sidecar_elem>) do...end>
 *
 * =cut
 *
 *    To implement the iterator, push an integer to use as the index,
 *    push a closure that uses the integer as the upvalue, swap the
 *    closure with the sidecar_elem.
 */
static int
sk_lua_sc_elem_pairs(
    lua_State          *L)
{
    /* when called, the sidecar_elem is on the stack */
    lua_pushinteger(L, 0);
    lua_pushcclosure(L, sk_lua_sc_elem_pairs_iter, 1);
    /* swap closure with sidecar_elem */
    lua_insert(L, -2);
    return 2;
}


/*
 *
 * =item silk.B<sidecar_elem_to_string(>I<sidecar_elem>B<)>
 *
 * Return a string representation of the sidecar element
 * I<sidecar_elem>.
 *
 * =cut
 */
static int
sk_lua_sc_elem_tostring(
    lua_State          *L)
{
    const sk_lua_sc_elem_t *elem;
    luaL_Buffer b;
    char *c;
    size_t sz;

    elem = (sk_lua_sc_elem_t *)lua_touserdata(L, 1);
    luaL_buffinit(L, &b);

    luaL_addchar(&b, '{');
    luaL_addstring(&b, sk_lua_sc_elem_key_name[SKLUA_SC_ELEM_KEY_TYPE]);
    if (elem->e_is_list) {
        luaL_addstring(&b, "=\"list\", ");
        luaL_addstring(
            &b, sk_lua_sc_elem_key_name[SKLUA_SC_ELEM_KEY_LIST_ELEM_TYPE]);
    }
    luaL_addstring(&b, "=\"");
    luaL_addstring(&b, elem->e_type_name);
    luaL_addchar(&b, '"');

    if (elem->e_ipfix_ident) {
        sz = 512;
        c = luaL_prepbuffsize(&b, sz);
        if (SK_FIELD_IDENT_GET_PEN(elem->e_ipfix_ident)) {
            sz = snprintf(c, sz, ", %s=%u, %s=%u",
                          sk_lua_sc_elem_key_name[
                              SKLUA_SC_ELEM_KEY_ENTERPRISE_ID],
                          SK_FIELD_IDENT_GET_PEN(elem->e_ipfix_ident),
                          sk_lua_sc_elem_key_name[SKLUA_SC_ELEM_KEY_ELEMENT_ID],
                          SK_FIELD_IDENT_GET_ID(elem->e_ipfix_ident));
        } else {
            sz = snprintf(c, sz, ", %s=%u",
                          sk_lua_sc_elem_key_name[SKLUA_SC_ELEM_KEY_ELEMENT_ID],
                          SK_FIELD_IDENT_GET_ID(elem->e_ipfix_ident));
        }
        luaL_addsize(&b, sz);
    }
    luaL_addchar(&b, '}');
    luaL_pushresult(&b);
    return 1;
}


/*
 * =pod
 *
 * =item silk.B<sidecar_elem_get_type(>I<sidecar_elem>B<)>
 *
 * Return a string that represents the type of data in the sidecar
 * element I<sidecar_elem>.
 *
 * =cut
 */
static int
sk_lua_sc_elem_get_type(
    lua_State          *L)
{
    sk_lua_sc_elem_t *elem;

    elem = sk_lua_checksidecarelem(L, 1);
    sk_lua_sc_elem_push_type(L, elem);
    return 1;
}

/*
 * =pod
 *
 * =item silk.B<sidecar_elem_get_list_elem_type(>I<sidecar_elem>B<)>
 *
 * Return a string that represents the type of data in the sidecar
 * list element I<sidecar_elem>.  Return B<nil> when I<sidecar_elem>
 * is not a list.
 *
 * =cut
 */
static int
sk_lua_sc_elem_get_list_elem_type(
    lua_State          *L)
{
    sk_lua_sc_elem_t *elem;

    elem = sk_lua_checksidecarelem(L, 1);
    sk_lua_sc_elem_push_list_type(L, elem);
    return 1;
}

/*
 * =pod
 *
 * =item silk.B<sidecar_elem_get_enterprise_id(>I<sidecar_elem>B<)>
 *
 * Return the IPFIX enterprise id (a.k.a. Private Enterprise Number
 * (PEN)) that was specified when the sidecar element I<sidecar_elem>
 * was created.
 *
 * =cut
 */
static int
sk_lua_sc_elem_get_enterprise_id(
    lua_State          *L)
{
    sk_lua_sc_elem_t *elem;

    elem = sk_lua_checksidecarelem(L, 1);
    sk_lua_sc_elem_push_enterprise_id(L, elem);
    return 1;
}

/*
 * =pod
 *
 * =item silk.B<sidecar_elem_get_element_id(>I<sidecar_elem>B<)>
 *
 * Return the IPFIX element id that was specified when the sidecar
 * element I<sidecar_elem> was created.
 *
 * =cut
 */
static int
sk_lua_sc_elem_get_element_id(
    lua_State          *L)
{
    sk_lua_sc_elem_t *elem;

    elem = sk_lua_checksidecarelem(L, 1);
    sk_lua_sc_elem_push_element_id(L, elem);
    return 1;
}


/*
 * These functions are automatically generated from within Lua, so
 * document them here.
 *
 * =pod
 *
 * =item silk.B<sidecar_to_string(>I<sidecar>B<)>
 *
 * Return a unique string designating I<sidecar>.
 *
 * =cut
 */

/*
 *    Garbage collect a sidecar.
 */
static int
sk_lua_sidecar_gc(
    lua_State          *L)
{
    sk_sidecar_t **sc_userdata;

    sc_userdata = (sk_sidecar_t **)lua_touserdata(L, 1);
    sk_sidecar_free(*sc_userdata);
    return 0;
}


/*
 *    Wrap the sidecar object 'sidecar' in a userdata.
 *
 *    If 'sidecar' is NULL, create a new sidecar object.
 *
 *    If 'sidecar' has elements, fill the sidecar uservalue's table
 *    with the structure that the sk_sidecar_t represents.
 *
 *    If 'make_frozen' is true, freeze the sidecar userdata.
 */
void
sk_lua_push_sidecar(
    lua_State          *L,
    sk_sidecar_t       *sidecar,
    int                 make_frozen)
{
    sk_sidecar_t **sc_userdata;
    char_buf_t *cbuf = NULL;
    sk_sidecar_iter_t iter;
    const sk_sidecar_elem_t *elem;
    const char *b;
    int tt, t;
    size_t len, sz, sl;

    /* ensure make_frozen is 0 or 1 */
    make_frozen = !!make_frozen;

    if (NULL == sidecar) {
        sk_sidecar_create(&sidecar);
    } else if (sk_sidecar_count_elements(sidecar)) {
        /* create a character array to use while getting the names of
         * the sidecar fields.  We cannot protect the character array
         * directly since it may get reallocated, so protect a
         * structure that contains it. */
        cbuf = sk_alloc(char_buf_t);
        cbuf->len = 2048;
        cbuf->buf = sk_alloc_array(char, cbuf->len);
        /* fprintf(stderr, ("%s:%d: pushing protected pointer %p" */
        /*                  " and free_fn %p\n"), */
        /*         __FILE__, __LINE__, (void *)cbuf, (void *)&char_buf_free); */
        sk_lua_push_protected_pointer(
            L, cbuf, (sk_lua_free_fn_t)char_buf_free);
    }

    sc_userdata = sk_lua_newuserdata(L, sk_sidecar_t *);
    *sc_userdata = sidecar;
    luaL_setmetatable(L, SK_LUA_SIDECAR);

    /* create the table that will become the uservalue */
    lua_createtable(L, SKLUA_SIDECAR_IDX___MAX__, 0);

    if (make_frozen) {
        /* Once the table holding the sidecar elements (which is
         * created below) is complete, we want to make it read-only */
        lua_pushcfunction(L, sk_lua_make_table_read_only);
    }

    /* create a table to hold the sidecar elements.  This table will
     * be stored on the uservalue table. */
    lua_newtable(L);
    tt = lua_gettop(L);

    /* visit the elements in the sidecar and add to the table */
    sk_sidecar_iter_bind(sidecar, &iter);
    while (sk_sidecar_iter_next(&iter, &elem) == SK_ITERATOR_OK) {
        /* get the name */
        len = cbuf->len;
        while (sk_sidecar_elem_get_name(elem, cbuf->buf, &len) == NULL) {
            cbuf->len *= 2;
            cbuf->buf = sk_alloc_realloc_noclear(cbuf->buf, char, cbuf->len);
        }

        t = tt;
        b = cbuf->buf;
        sz = len;
        while ((sl = (1 + strlen(b))) < sz) {
            /* this is a nested element */
            if (lua_getfield(L, t, b) != LUA_TTABLE) {
                luaL_error(L, "subtable key found before subtable name");
            }
            t = lua_gettop(L);
            b += sl;
            sz -= sl;
        }
        /* this is the leaf element */

        /* create an object to hold the type and IPFIX id and add
         * it to the table */
        sk_lua_push_sidecar_elem(L, elem);
        lua_setfield(L, t, b);

        /* make the top level sidecar table the top of the stack */
        if (t != tt) {
            lua_settop(L, tt);
        }
    }

    if (make_frozen) {
        /* run silkutils.make_table_read_only(t) */
        lua_call(L, 1, 1);
    }

    /* add sidecar elements table to the uservalue table */
    lua_seti(L, -2, SKLUA_SIDECAR_IDX_TABLE);

    /* set the is_frozen flag */
    lua_pushboolean(L, make_frozen);
    lua_seti(L, -2, SKLUA_SIDECAR_IDX_ISFROZEN);

    /* add uservalue table to the sidecar userdata */
    lua_setuservalue(L, -2);

    assert(sk_lua_checksidecar(L, lua_gettop(L)));

    if (cbuf) {
        sk_lua_unprotect_pointer(L, -2);
    }
    assert(sk_lua_checksidecar(L, lua_gettop(L)));
}


/*
 *    Expect two arguments: a sidecar userdata and an array that
 *    contains pairs where the first item is a name (with embedded
 *    NULLs representing depth) and the second item is an
 *    sk_lua_sc_elem_t userdata.
 *
 *    Append to the sk_sidecar_t C object the elements that are
 *    represented by the array.
 *
 *    Currently this is an internal function that is called by
 *    silk.lua when freezing a sidecar.
 */
static int
sk_lua_sidecar_freeze_helper(
    lua_State          *L)
{
    sk_sidecar_t *sidecar;
    sk_sidecar_elem_t *sc_elem;
    sk_lua_sc_elem_t *e;
    lua_Integer count;
    lua_Integer i;
    const char *name;
    size_t len;

    sidecar = *sk_lua_checksidecar(L, 1);
    lua_getuservalue(L, 1);
    if (lua_geti(L, -1, SKLUA_SIDECAR_IDX_ISFROZEN) != LUA_TBOOLEAN
        || lua_toboolean(L, -1) != 1)
    {
        luaL_error(L, "sidecar is not frozen");
    }
    lua_pop(L, 1);

    lua_len(L, 2);
    count = lua_tointeger(L, -1);
    lua_pop(L, 1);

    for (i = 1; i <= count; ++i) {
        lua_geti(L, 2, i);
        assert(lua_type(L, -1) == LUA_TTABLE);
        lua_geti(L, -1, 1);
        assert(lua_type(L, -1) == LUA_TSTRING);
        name = lua_tolstring(L, -1, &len);
        lua_geti(L, -2, 2);
        assert(lua_type(L, -1) == LUA_TUSERDATA);
        e = sk_lua_tosidecarelem(L, -1);
        if (e->e_is_list) {
            sc_elem = sk_sidecar_append_list(sidecar, name, len,
                                             e->e_type_id, e->e_ipfix_ident);
        } else {
            sc_elem = sk_sidecar_append(sidecar, name, len,
                                        e->e_type_id, e->e_ipfix_ident);
        }
        if (NULL == sc_elem) {
            luaL_error(L, "error creating sidecar element");
        }
        lua_pop(L, 3);
    }

    return 0;
}


/*
 * =pod
 *
 * =item silk.B<sidecar_is_frozen(>I<sidecar>B<)>
 *
 * Return B<true> if the sidecar description I<sidecar> is frozen;
 * B<false> otherwise.
 *
 * =cut
 */
int
sk_lua_sidecar_is_frozen(
    lua_State          *L)
{
    sk_lua_checksidecar(L, 1);
    lua_getuservalue(L, 1);
    lua_geti(L, -1, SKLUA_SIDECAR_IDX_ISFROZEN);
    return 1;
}


/*
 * =pod
 *
 * =item I<sidecar>B<[> I<name> B<]>
 *
 * Get the value associated with I<name> in the sidecar description
 * object I<sidecar>.
 *
 * When I<sidecar> is frozen, each value is either a
 * L<sidecar_elem|/Sidecar Element> or a table of whose keys are
 * strings and whose values are either L<sidecar_elem|/Sidecar
 * Element> or another table, et cetera.
 *
 * When I<sidecar> is editable (not frozen), each value is either a
 * string specifying the type or a table whose contents may describe a
 * single element or may represent a more complicated structured
 * data. (The contents of the table are not checked until the sidecar
 * description is frozen.
 *
 * =cut
 *
 *    Defer to the table that contains the descriptions
 */
static int
sk_lua_sidecar_index(
    lua_State          *L)
{
    lua_getuservalue(L, 1);
    lua_geti(L, -1, SKLUA_SIDECAR_IDX_TABLE);
    lua_pushvalue(L, 2);
    lua_gettable(L, -2);
    return 1;
}


/*
 * =pod
 *
 * =item I<sidecar>B<[> I<name> B<]> = I<obj>
 *
 * Set the value of the I<name> element of the sidecar description
 * I<sidecar> to I<obj>.
 *
 * Raise an error if I<sidecar> is frozen.  Raise an error unless
 * I<obj> is one of B<nil>, a string, a table describing a single
 * element, a table representing a complex element, or a
 * L<sidecar_elem|/Sidecar Element> taken from another sidecar
 * description.
 *
 * =over 4
 *
 * =item *
 *
 * If I<obj> is B<nil>, remove I<name> from I<sidecar>, or do nothing
 * if I<name> is not present on I<sidecar>.
 *
 * =item *
 *
 * If I<obj> is a string, it designates a type that is used to encode
 * the value of the C<sidecar> member of an L<rwrec|/RWRec> object
 * whose key is I<name>.  The valid values for these type strings and
 * how the corresponding value on an rwrec is encoded are shown next.
 * If I<name> is not one of these strings, raise an error.
 *
 * =item uint8
 *
 * an unsigned integer less than 256 (2^8)
 *
 * =item uint16
 *
 * an unsigned integer less than 65,536 (2^16)
 *
 * =item uint32
 *
 * an unsigned integer less than 4,294,967,296 (2^32)
 *
 * =item uint64
 *
 * an unsigned integer less than 18,446,744,073,709,551,614 (2^64)
 * B<NOTE>: Values equal to or greater than 2^63 are probably broken
 * right now.
 *
 * =item double
 *
 * a floating point number that fits into a 64-bit IEEE floating point
 * value
 *
 * =item string
 *
 * a variable length string that is not expected to contain binary
 * values
 *
 * =item binary
 *
 * a variable length string that may contain binary values
 *
 * =item ip4
 *
 * a L<ipaddr|/IP Address> that holds an IPv4 address
 *
 * =item ip6
 *
 * a L<ipaddr|/IP Address> that holds an IPv6 address
 *
 * =item datetime
 *
 * a L<datetime|/Datetime> object that represents a date and time
 *
 * =item boolean
 *
 * a boolean value
 *
 * =item empty
 *
 * no value
 *
 * =back
 *
 * =item *
 *
 * For I<obj> to be a table describing a single element, it must have
 * a key named C<type> whose value is either one of the strings
 * immediately above that describe the type of data or the string
 * C<list> in which case the key C<list_elem_type> should be present
 * and its value must be one of the strings immediately above.
 *
 * Two additional keys may be present.  The key C<element_id> should
 * have an integer value that indicates the IPFIX element id to use
 * when converting the SiLK Flow record to an IPFIX record (something
 * that is not currently supported).  The key C<enterprise_id> should
 * have an integer value giving the IPFIX enterprise id
 * (a.k.a. Private Enterprise Number (PEN)) for the element id.
 *
 * B<Note:> The contents of this table are only checked when the
 * sidecar is frozen.
 *
 * =item *
 *
 * For I<obj> to be a table describing a complex (or structured) piece
 * of data, the table repeats the structure just described: the keys
 * of the table may be any string (that does not contain binary data)
 * and value is either a single string, a table describing an element,
 * or another table, et cetera.
 *
 * B<Note:> The contents of this table are only checked when the
 * sidecar is frozen.
 *
 * =item *
 *
 * I<obj> may be a L<sidecar_elem|/Sidecar Element> taken from a
 * different frozen sidecar description object.
 *
 * =back
 *
 * =cut
 *
 *    Defer to the table that contains the descriptions unless the
 *    sidecar is frozen.
 *
 *    Note: When adding a table to a sidecar, the table is not
 *    inspected for correctness.  That only occurs when the sidecar is
 *    frozen.
 */
static int
sk_lua_sidecar_newindex(
    lua_State          *L)
{
    const char *key;
    size_t keylen;
    int opt;

    lua_getuservalue(L, 1);

    /* check whether the sidecar if frozen */
    lua_geti(L, -1, SKLUA_SIDECAR_IDX_ISFROZEN);
    if (lua_toboolean(L, -1)) {
        return luaL_error(L, "sidecar is frozen");
    }
    lua_pop(L, 1);

    /* Get the description table, move it on the stack in place of the
     * sidecar, then remove the uservalue table */
    lua_geti(L, -1, SKLUA_SIDECAR_IDX_TABLE);
    lua_replace(L, 1);
    lua_pop(L, 1);

    /* check the key and value */
    key = luaL_checklstring(L, 2, &keylen);
    if (strlen(key) != keylen) {
        sk_lua_argerror(L, 1, "sidecar key may not contain embedded NULLs");
    }

    switch (lua_type(L, 3)) {
      case LUA_TNONE:
        /* we expected a third argument; checkany() should error */
        luaL_checkany(L, 3);
        skAbort();
      case LUA_TNIL:
        /* remove from sidecar */
        break;
      case LUA_TUSERDATA:
        /* must be a sidecar_elem */
        if (NULL == sk_lua_tosidecarelem(L, 3)) {
            sk_lua_argerror(L, 3, "table, %s, or nil expected, got %s",
                            SK_LUA_SIDECAR_ELEM, sk_lua_typename(L, 3));
        }
        break;
      case LUA_TSTRING:
        /* must be a type */
        opt = luaL_checkoption(L, 3, NULL, sk_lua_sc_elem_type_name);
        if (SK_SIDECAR_LIST == opt || SK_SIDECAR_TABLE == opt) {
            sk_lua_argerror(L, 3, "bad argument (invalid option '%s')",
                            lua_tostring(L, 3));
        }
      case LUA_TTABLE:
        /* may either be a table describing a sidecar element or a
         * subtable to become a SK_SIDECAR_TABLE element. */
        /* FIXME: check the contents now or later? */
        break;
      default:
        sk_lua_argerror(L, 3, "table, string, %s, or nil expected, got %s",
                        SK_LUA_SIDECAR_ELEM, sk_lua_typename(L, 3));
    }

    /* call the normal settable() function. */
    lua_settable(L, 1);

    return 0;
}


/*
 * =pod
 *
 * =item B<#>I<sidecar>
 *
 * Return the number of top-level elements in the sidecar description.
 *
 * =cut
 */
static int
sk_lua_sidecar_len(
    lua_State          *L)
{
    unsigned count = 0;

    lua_getuservalue(L, 1);
    lua_geti(L, -1, SKLUA_SIDECAR_IDX_TABLE);

    /* if the sidecar is frozen, then the sidecar elements table is
     * empty and the real table of keys is on the __index field of the
     * sidecar elements table's metatable */
    lua_geti(L, -2, SKLUA_SIDECAR_IDX_ISFROZEN);
    if (0 == lua_toboolean(L, -1)) {
        lua_pop(L, 1);
    } else {
        lua_pop(L, 1);
        lua_getmetatable(L, -1);
        lua_getfield(L, -1, "__index");
    }

    for (lua_pushnil(L); lua_next(L, -2) != 0; lua_pop(L, 1)) {
        ++count;
    }
    lua_pushinteger(L, count);
    return 1;
}


/*
 * =pod
 *
 * =item B<pairs(>I<sidecar>B<)>
 *
 * Return an iterator designed for the Lua B<for> statement that
 * iterates over (name, value) pairs of I<sidecar>.  See the
 * description of
 * L<I<sidecar>B<[]>|/"I<sidecar>B<[> I<name> B<]>">
 * for the type of the value.  May be used as
 * B<for I<name>, I<value> in pairs(I<sidecar>) do...end>
 *
 * =cut
 */
static int
sk_lua_sidecar_pairs(
    lua_State          *L)
{
    lua_getuservalue(L, 1);

    /* Call "pairs" on the description table. */
    lua_getglobal(L, "pairs");
    lua_geti(L, -2, SKLUA_SIDECAR_IDX_TABLE);
    lua_call(L, 1, 3);
    return 3;
}



/*
 * =pod
 *
 * =item silk.B<sidecar(>[I<table>]B<)>
 *
 * Create a sidecar description object.
 *
 * With no argument or a B<nil> as an argument, create an empty
 * sidecar description.  If an argument is provided that is not B<nil>
 * and not a table, raise an error and do not create the sidecar.
 *
 * When I<table> is provided, new elements are added to the new
 * sidecar by calling
 * L<I<sidecar>B<[> I<name> B<]> = I<obj>|/"I<sidecar>B<[> I<name> B<]> = I<obj>">
 * for each key-object pair in I<table>, and the function returns
 * either one or two values.  If no errors are detected while adding
 * the key-object pairs, the new sidecar object is the only return
 * value.  If an error is detected, two values are returned: the first
 * is the new, partially constructed sidecar object and the second is
 * a table containing the keys from I<table> that could not be added.
 * The value for each key is the error message generated.
 *
 * =item silk.B<sidecar_create(>[I<table>]B<)>
 *
 * Create a sidecar description object.  An alias for
 * L<silk.B<sidecar()>|/"silk.B<sidecar(>[I<table>]B<)>">
 *
 * =cut
 *
 * =cut
 *
 *    Create a sidecar.  If an argument is provided, invoke the
 *    __newindex function to create elements in the sidecar.
 *
 *    If no aurgument is provided, return the sidecar.
 *
 *    If an argument is provided and it is not table (and not nil),
 *    raise an error and do not create the sidecar.  Otherwise, return
 *    one or two values.  If no errors were encountered adding the
 *    elements to the sidecar, return a single value which is the
 *    sidecar.  Otherwise return the sidecar and a table, where the
 *    table contains one or more keys from the argument to this
 *    function and the value is the error that was raised by the
 *    __newindex method.
 *
 *
 */
static int
sk_lua_sidecar_create(
    lua_State          *L)
{
    int have_arg;
    int have_errors;

    switch (lua_type(L, 1)) {
      case LUA_TNONE:
      case LUA_TNIL:
        have_arg = 0;
        break;
      case LUA_TTABLE:
        have_arg = 1;
        break;
      default:
        return sk_lua_argerror(L, 1, "table or no argument expected, got %s",
                               sk_lua_typename(L, 1));
    }

    /* create a new sidecar userdata object */
    sk_lua_push_sidecar(L, NULL, 0);

    if (!have_arg) {
        return 1;
    }

    /* create a table to hold any errors we generate while adding
     * elements to the sidecar */
    lua_newtable(L);
    have_errors = 0;

    /* loop over the table argument to this function */
    lua_pushnil(L);
    while (lua_next(L, 1) != 0) {
        /* after the following call, sidecar at -5, error table at -4,
         * key at -3, value at -2, function at -1 */
        lua_pushcfunction(L, sk_lua_sidecar_newindex);
        lua_pushvalue(L, -5);
        lua_pushvalue(L, -4);
        lua_pushvalue(L, -4);
        if (lua_pcall(L, 3, 0, 0) == LUA_OK) {
            /* pop the value */
            lua_pop(L, 1);
        } else {
            /* push a copy of the key, move the copied key in place of
             * the value, add to error table; the current stack
             * positions are the same as those above */
            lua_pushvalue(L, -3);
            lua_replace(L, -3);
            lua_settable(L, -4);
            have_errors = 1;
        }
    }

    if (have_errors) {
        return 2;
    }
    lua_pop(L, 1);
    return 1;
}


static const luaL_Reg sk_lua_sidecar_metatable[] = {
    {"__gc",            sk_lua_sidecar_gc},
    {"__index",         sk_lua_sidecar_index},
    {"__len",           sk_lua_sidecar_len},
    {"__newindex",      sk_lua_sidecar_newindex},
    {"__pairs",         sk_lua_sidecar_pairs},
    {NULL, NULL}
};

static const luaL_Reg sk_lua_sidecar_methods[] = {
    {"is_frozen",       sk_lua_sidecar_is_frozen},
    {NULL, NULL}
};

static const luaL_Reg sk_lua_sidecar_static_methods[] = {
    {"create",          sk_lua_sidecar_create},
    {NULL, NULL}
};


static const luaL_Reg sk_lua_sidecar_elem_metatable[] = {
    {"__index",         sk_lua_sc_elem_index},
    {"__pairs",         sk_lua_sc_elem_pairs},
    {"__tostring",      sk_lua_sc_elem_tostring},
    {NULL, NULL}
};

static const luaL_Reg sk_lua_sidecar_elem_methods[] = {
    {"get_element_id",      sk_lua_sc_elem_get_element_id},
    {"get_enterprise_id",   sk_lua_sc_elem_get_enterprise_id},
    {"get_list_elem_type",  sk_lua_sc_elem_get_list_elem_type},
    {"get_type",            sk_lua_sc_elem_get_type},
    {NULL, NULL}
};

/* static const luaL_Reg sk_lua_sidecar_elem_static_methods[] = { */
/*     {NULL, NULL} */
/* }; */

#if 0
static const luaL_Reg sk_lua_sidecar_table_metatable[] = {
    /* metatable methods are set in Lua code */
    {NULL, NULL}
};
#define SK_LUA_SIDECAR_TABLE    "silk.sidecar_table"
    {"sidecar_table", SK_LUA_SIDECAR_TABLE, NULL,
     sk_lua_sidecar_table_metatable, NULL, NULL},
#endif  /* 0 */



/*
 *  ********************************************************************
 *  RWREC
 *  ********************************************************************
 */

#define RWREC_FIELD_SENSOR_ID       23
#define RWREC_FIELD_FLOWTYPE_ID     24
#define RWREC_FIELD_FLOWTYPE        25
#define RWREC_FIELD_TIMEOUT_KILLED  26
#define RWREC_FIELD_TIMEOUT_STARTED 27
#define RWREC_FIELD_UNIFORM_PACKETS 28
#define RWREC_FIELD_SIDECAR         29

static const char *
rwrec_field_list[] = {
    "sip",                      /* RWREC_FIELD_SIP */
    "dip",                      /* RWREC_FIELD_DIP */
    "sport",                    /* RWREC_FIELD_SPORT */
    "dport",                    /* RWREC_FIELD_DPORT */

    "protocol",                 /* RWREC_FIELD_PROTO */
    "packets",                  /* RWREC_FIELD_PKTS */
    "bytes",                    /* RWREC_FIELD_BYTES */
    "tcpflags",                 /* RWREC_FIELD_FLAGS */

    "stime",                    /* RWREC_FIELD_STIME */
    "duration",                 /* RWREC_FIELD_ELAPSED */
    "etime",                    /* RWREC_FIELD_ETIME */
    "sensor",                   /* RWREC_FIELD_SID */

    "input",                    /* RWREC_FIELD_INPUT */
    "output",                   /* RWREC_FIELD_OUTPUT */
    "nhip",                     /* RWREC_FIELD_NHIP */
    "initial_tcpflags",         /* RWREC_FIELD_INIT_FLAGS */

    "session_tcpflags",         /* RWREC_FIELD_REST_FLAGS */
    "attributes",               /* RWREC_FIELD_TCP_STATE */
    "application",              /* RWREC_FIELD_APPLICATION */
    "classname",                /* RWREC_FIELD_FTYPE_CLASS */

    "typename",                 /* RWREC_FIELD_FTYPE_TYPE */
    "icmptype",                 /* RWREC_FIELD_ICMP_TYPE */
    "icmpcode",                 /* RWREC_FIELD_ICMP_CODE */
    "sensor_id",                /* RWREC_FIELD_SENSOR_ID */

    "classtype_id",             /* RWREC_FIELD_FLOWTYPE_ID */
    "classtype",                /* RWREC_FIELD_FLOWTYPE */
    "timeout_killed",           /* RWREC_FIELD_TIMEOUT_KILLED */
    "timeout_started",          /* RWREC_FIELD_TIMEOUT_STARTED */

    "uniform_packets",          /* RWREC_FIELD_UNIFORM_PACKETS */
    "sidecar",                  /* RWREC_FIELD_SIDECAR */
    NULL
};



static int
sk_lua_rwrec_gc(
    lua_State          *L)
{
    rwRec *rwrec;

    rwrec = (rwRec*)lua_touserdata(L, 1);
    if (rwrec->lua_state) {
        luaL_unref(
            rwrec->lua_state, LUA_REGISTRYINDEX, rwRecGetSidecar(rwrec));
    }
    return 0;
}


/*
 * rwrec_get_value() and rwrec __index metamethod (rwrec_index)
 *
 * =pod
 *
 * =item I<rwrec>B<[> I<name> B<]>
 *
 * Get the value of the I<name> field on the RWRec I<rwrec>. An alias
 * for
 * L<silk.B<rwrec_get_value()>|/"silk.B<rwrec_get_value(>I<name>B<)>">
 *
 *
 * =item silk.B<rwrec_get_value(>I<name>B<)>
 *
 * Get the value of the I<name> field on the RWRec I<rwrec>.  I<name>
 * must be one of the following string values:
 *
 * =over 4
 *
 * =item sip
 *
 * source IP address, an L<ipaddr|/IP Address> object
 *
 * =item dip
 *
 * destination IP address, an L<ipaddr|/IP Address> object
 *
 * =item sport
 *
 * source port for TCP and UDP, or equivalent, a number
 *
 * =item dport
 *
 * destination port for TCP and UDP, or equivalent, a number
 *
 * =item protocol
 *
 * IP protocol, a number
 *
 * =item packets
 *
 * packet count, a number
 *
 * =item bytes
 *
 * byte count, a number
 *
 * =item flags
 *
 * bit-wise OR of the TCP flags seen on all packets in the flow, a
 * number. Use the functions in L</TCP Flags> to convert the number to
 * a string.
 *
 * =item initial_tcpflags
 *
 * TCP flags seen on the first packet in the flow, a number
 *
 * =item session_tcpflags
 *
 * bit-wise OR of the TCP flags seen on the second through final
 * packets in the flow, a number
 *
 * =item stime
 *
 * starting time of flow, a L<datetime|/Datetime> object
 *
 * =item duration
 *
 * duration of flow, a floating point number representing seconds
 *
 * =item etime
 *
 * end time of flow, a L<datetime|/Datetime> object
 *
 * =item sensor
 *
 * name of the sensor at the collection point, a string
 *
 * =item sensor_id
 *
 * numeric ID of the sensor at the collection point, a number
 *
 * =item classname
 *
 * name of the class of sensor at the collection point, a string
 *
 * =item typename
 *
 * name of the type of sensor at the collection point, a string
 *
 * =item classtype
 *
 * a tuble that represents the unique class/type pair, a list (table)
 * of two elements
 *
 * =item classtype_id
 *
 * number representing the unique class/type pair, a number
 *
 * =item icmptype
 *
 * the ICMP type value for ICMP or ICMPv6 flows and 0 for non-ICMP
 * flows, a number
 *
 * =item icmpcode
 *
 * the ICMP code value for ICMP or ICMPv6 flows and 0 for non-ICMP
 * flows, a number
 *
 * =item input
 *
 * router SNMP input interface, a number
 *
 * =item output
 *
 * router SNMP output interface, a number
 *
 * =item nhip
 *
 * router next hop IP, an L<ipaddr|/IP Address> object
 *
 * =item application
 *
 * guess as to the content of the flow, a number
 *
 * =item attributes
 *
 * flow attributes set by the flow generator, a number. Use the
 * functions in L</Attributes> to convert the number to a string.
 *
 * =item timeout_killed
 *
 * whether the flow generator prematurely created this flow for a
 * long-running connection due to a timeout, a boolean.  This value is
 * a member of the attributes field.
 *
 * =item timeout_started
 *
 * whether the flow generator created this flow as a continuation of
 * long-running connection, where the previous flow for this
 * connection met a timeout, a boolean.  This value is a member of the
 * attributes field.
 *
 * =item uniform_packets
 *
 * whether all the packets that this flow record represents were
 * exactly the same size, a boolean.  This value is a member of the
 * attributes field.
 *
 * =item sidecar
 *
 * additional values that augment this rwrec, a table or nil when not
 * present
 *
 * =back
 *
 * =cut
 */
static int
sk_lua_rwrec_get_value(
    lua_State          *L)
{
    char buf[SK_MAX_STRLEN_FLOWTYPE + SK_MAX_STRLEN_SENSOR];
    const rwRec *rwrec;
    int field;
    skipaddr_t *ip;
    sktime_t *t;
    sk_flowtype_id_t flowtype_id;

    /* get the rwRec */
    rwrec = sk_lua_checkrwrec(L, 1);
    field = luaL_checkoption(L, 2, NULL, rwrec_field_list);
    switch (field) {
      case RWREC_FIELD_SIP:
        ip = sk_lua_push_ipaddr(L);
        rwRecMemGetSIP(rwrec, ip);
        break;
      case RWREC_FIELD_DIP:
        ip = sk_lua_push_ipaddr(L);
        rwRecMemGetDIP(rwrec, ip);
        break;
      case RWREC_FIELD_SPORT:
        lua_pushinteger(L, rwRecGetSPort(rwrec));
        break;
      case RWREC_FIELD_DPORT:
        lua_pushinteger(L, rwRecGetDPort(rwrec));
        break;
      case RWREC_FIELD_PROTO:
        lua_pushinteger(L, rwRecGetProto(rwrec));
        break;
      case RWREC_FIELD_PKTS:
        lua_pushinteger(L, rwRecGetPkts(rwrec));
        break;
      case RWREC_FIELD_BYTES:
        lua_pushinteger(L, rwRecGetBytes(rwrec));
        break;
      case RWREC_FIELD_FLAGS:
        lua_pushinteger(L, rwRecGetFlags(rwrec));
        break;
      case RWREC_FIELD_STIME:
        t = sk_lua_push_datetime(L);
        rwRecMemGetStartTime(rwrec, t);
        break;
      case RWREC_FIELD_ELAPSED:
        /* FIXME -- how to handle elapsed time? */
        lua_pushnumber(L, (double)rwRecGetElapsed(rwrec) / 1000.0);
        break;
      case RWREC_FIELD_ETIME:
        t = sk_lua_push_datetime(L);
        rwRecMemGetEndTime(rwrec, t);
        break;
      case RWREC_FIELD_SID:
        /* FIXME -- how to handle unknown sensor id? */
        sksiteSensorGetName(buf, sizeof(buf), rwRecGetSensor(rwrec));
        lua_pushstring(L, buf);
        break;
      case RWREC_FIELD_INPUT:
        lua_pushinteger(L, rwRecGetInput(rwrec));
        break;
      case RWREC_FIELD_OUTPUT:
        lua_pushinteger(L, rwRecGetOutput(rwrec));
        break;
      case RWREC_FIELD_NHIP:
        ip = sk_lua_push_ipaddr(L);
        rwRecMemGetNhIP(rwrec, ip);
        break;
      case RWREC_FIELD_INIT_FLAGS:
        lua_pushinteger(L, rwRecGetInitFlags(rwrec));
        break;
      case RWREC_FIELD_REST_FLAGS:
        lua_pushinteger(L, rwRecGetRestFlags(rwrec));
        break;
      case RWREC_FIELD_TCP_STATE:
        lua_pushinteger(L, rwRecGetTcpState(rwrec));
        break;
      case RWREC_FIELD_APPLICATION:
        lua_pushinteger(L, rwRecGetApplication(rwrec));
        break;
      case RWREC_FIELD_FTYPE_CLASS:
        sksiteFlowtypeGetClass(buf, sizeof(buf), rwRecGetFlowType(rwrec));
        lua_pushstring(L, buf);
        break;
      case RWREC_FIELD_FTYPE_TYPE:
        sksiteFlowtypeGetType(buf, sizeof(buf), rwRecGetFlowType(rwrec));
        lua_pushstring(L, buf);
        break;
      case RWREC_FIELD_ICMP_TYPE:
        lua_pushinteger(L, rwRecGetIcmpType(rwrec));
        break;
      case RWREC_FIELD_ICMP_CODE:
        lua_pushinteger(L, rwRecGetIcmpCode(rwrec));
        break;
      case RWREC_FIELD_SENSOR_ID:
        lua_pushinteger(L, rwRecGetSensor(rwrec));
        break;
      case RWREC_FIELD_FLOWTYPE_ID:
        lua_pushinteger(L, rwRecGetFlowType(rwrec));
        break;
      case RWREC_FIELD_FLOWTYPE:
        lua_createtable(L, 0, 2);
        flowtype_id = rwRecGetFlowType(rwrec);
        sksiteFlowtypeGetClass(buf, sizeof(buf), flowtype_id);
        lua_pushstring(L, buf);
        lua_setfield(L, -2, rwrec_field_list[RWREC_FIELD_FTYPE_CLASS]);
        sksiteFlowtypeGetType(buf, sizeof(buf), flowtype_id);
        lua_pushstring(L, buf);
        lua_setfield(L, -2, rwrec_field_list[RWREC_FIELD_FTYPE_TYPE]);
        break;
      case RWREC_FIELD_TIMEOUT_KILLED:
        /* FIXME: Push nil if field not available? */
        lua_pushboolean(
            L, rwRecGetTcpState(rwrec) & SK_TCPSTATE_TIMEOUT_KILLED);
        break;
      case RWREC_FIELD_TIMEOUT_STARTED:
        lua_pushboolean(
            L, rwRecGetTcpState(rwrec) & SK_TCPSTATE_TIMEOUT_STARTED);
        break;
      case RWREC_FIELD_UNIFORM_PACKETS:
        lua_pushboolean(
            L, rwRecGetTcpState(rwrec) & SK_TCPSTATE_UNIFORM_PACKET_SIZE);
        break;
      case RWREC_FIELD_SIDECAR:
        if (LUA_NOREF == rwRecGetSidecar(rwrec)) {
            lua_pushnil(L);
        } else {
            switch (lua_rawgeti(L, LUA_REGISTRYINDEX, rwRecGetSidecar(rwrec))){
              case LUA_TNIL:
                /* there is no sidecar and nil is on the stack */
                break;
              case LUA_TTABLE:
                /* probably need to do something to register this
                 * table with the record so changes to table are
                 * reflected on the record */
                break;
              default:
                /* pop whatever is here and push nil */
                lua_pop(L, 1);
                lua_pushnil(L);
                break;
            }
        }
        break;
      default:
        skAbortBadCase(field);
    }

    return 1;
}


/*
 * rwrec_set_value() and rwrec __newindex metamethod (rwrec_newindex)
 *
 * =pod
 *
 * =item I<rwrec>B<[> I<name> B<] => I<value>
 *
 * Set the value for the field I<name> in I<rwrec> to I<value>.  An
 * alias for
 * L<silk.B<rwrec_set_value()>|/"silk.B<rwrec_set_value(>I<rwrec>, I<name>, I<value>B<)>">.
 *
 * =item silk.B<rwrec_set_value(>I<rwrec>, I<name>, I<value>B<)>
 *
 * Set the value for the field I<name> in I<rwrec> to I<value>.
 * I<name> is one of the strings specified in the documentation for
 * L<silk.B<rwrec_get_value()>|/"silk.B<rwrec_get_value(>I<name>B<)>">
 * except C<classname> and C<typename>.
 *
 * =cut
 */
static int
sk_lua_rwrec_set_value(
    lua_State          *L)
{
    rwRec *rwrec;
    int field;
    skipaddr_t *ip;
    sktime_t *t;
    sk_sensor_id_t sensor_id;
    sk_flowtype_id_t flowtype_id;
    const char *arg;
    const char *arg2;
    lua_Number n;
    lua_Unsigned i;
    int ref;

#define CHECK_INTEGER_ARG(m_maxval)                                     \
    i = sk_lua_checkunsigned(L, 3);                                     \
    if (i > m_maxval) {                                                 \
        n = (lua_Number)m_maxval;                                       \
        return sk_lua_argerror(L, 3, "%s between 0 and %f expected, got %I", \
                               rwrec_field_list[field], n, i);          \
    }

    /* get the rwRec */
    rwrec = sk_lua_checkrwrec(L, 1);
    field = luaL_checkoption(L, 2, NULL, rwrec_field_list);
    switch (field) {
      case RWREC_FIELD_SIP:
        ip = sk_lua_checkipaddr(L, 3);
        rwRecMemSetSIP(rwrec, ip);
        break;
      case RWREC_FIELD_DIP:
        ip = sk_lua_checkipaddr(L, 3);
        rwRecMemSetDIP(rwrec, ip);
        break;
      case RWREC_FIELD_SPORT:
        CHECK_INTEGER_ARG(UINT16_MAX);
        rwRecSetSPort(rwrec, i);
        break;
      case RWREC_FIELD_DPORT:
        CHECK_INTEGER_ARG(UINT16_MAX);
        rwRecSetDPort(rwrec, i);
        break;
      case RWREC_FIELD_PROTO:
        CHECK_INTEGER_ARG(UINT8_MAX);
        rwRecSetProto(rwrec, i);
        break;
      case RWREC_FIELD_PKTS:
        CHECK_INTEGER_ARG(UINT64_MAX);
        rwRecSetPkts(rwrec, i);
        break;
      case RWREC_FIELD_BYTES:
        CHECK_INTEGER_ARG(UINT64_MAX);
        rwRecSetBytes(rwrec, i);
        break;
      case RWREC_FIELD_FLAGS:
        CHECK_INTEGER_ARG(UINT8_MAX);
        rwRecSetFlags(rwrec, i);
        break;
      case RWREC_FIELD_STIME:
        t = sk_lua_checkdatetime(L, 3);
        rwRecMemSetStartTime(rwrec, t);
        break;
      case RWREC_FIELD_ELAPSED:
        /* FIXME -- how to handle elapsed time? */
        n = luaL_checknumber(L, 3);
        if (n < 0) {
            return sk_lua_argerror(L, 3, "expected %s greater than 0, got %f",
                                   rwrec_field_list[field], n);
        }
        rwRecSetElapsed(rwrec, n * 1000);
        break;
      case RWREC_FIELD_ETIME:
        t = sk_lua_checkdatetime(L, 3);
        /* FIXME PLEASE!!!! */
        rwRecSetStartTime(rwrec, *t);
        /* rwRecSetEndTime(rwrec, *t); */
        break;
      case RWREC_FIELD_SID:
        arg = sk_lua_checkstring(L, 3);
        sensor_id = sksiteSensorLookup(arg);
        if (SK_INVALID_SENSOR == sensor_id) {
            return sk_lua_argerror(L, 3, "unknown %s '%s'",
                                   rwrec_field_list[field], arg);
        }
        rwRecSetSensor(rwrec, sensor_id);
        break;
      case RWREC_FIELD_INPUT:
        CHECK_INTEGER_ARG(UINT32_MAX);
        rwRecSetInput(rwrec, i);
        break;
      case RWREC_FIELD_OUTPUT:
        CHECK_INTEGER_ARG(UINT32_MAX);
        rwRecSetOutput(rwrec, i);
        break;
      case RWREC_FIELD_NHIP:
        ip = sk_lua_checkipaddr(L, 3);
        rwRecMemSetNhIP(rwrec, ip);
        break;
      case RWREC_FIELD_INIT_FLAGS:
        CHECK_INTEGER_ARG(UINT8_MAX);
        rwRecSetInitFlags(rwrec, i);
        break;
      case RWREC_FIELD_REST_FLAGS:
        CHECK_INTEGER_ARG(UINT8_MAX);
        rwRecSetRestFlags(rwrec, i);
        break;
      case RWREC_FIELD_TCP_STATE:
        CHECK_INTEGER_ARG(UINT8_MAX);
        rwRecSetTcpState(rwrec, i);
        break;
      case RWREC_FIELD_APPLICATION:
        CHECK_INTEGER_ARG(UINT16_MAX);
        rwRecSetApplication(rwrec, i);
        break;
      case RWREC_FIELD_FTYPE_CLASS:
      case RWREC_FIELD_FTYPE_TYPE:
        luaL_error(L, "field %s is read only", rwrec_field_list[field]);
        break;
      case RWREC_FIELD_ICMP_TYPE:
        CHECK_INTEGER_ARG(UINT8_MAX);
        rwRecSetIcmpType(rwrec, i);
        break;
      case RWREC_FIELD_ICMP_CODE:
        CHECK_INTEGER_ARG(UINT8_MAX);
        rwRecSetIcmpCode(rwrec, i);
        break;
      case RWREC_FIELD_SENSOR_ID:
        CHECK_INTEGER_ARG(UINT16_MAX);
        rwRecSetSensor(rwrec, i);
        break;
      case RWREC_FIELD_FLOWTYPE_ID:
        CHECK_INTEGER_ARG(UINT8_MAX);
        rwRecSetFlowType(rwrec, i);
        break;
      case RWREC_FIELD_FLOWTYPE:
        luaL_checktype(L, 3, LUA_TTABLE);
        switch (lua_getfield(L, 3, rwrec_field_list[RWREC_FIELD_FTYPE_CLASS])){
          case LUA_TSTRING:
            arg = lua_tostring(L, -1);
            break;
          case LUA_TNIL:
            return sk_lua_argerror(L, 3, "expected key %s not found in table",
                                   rwrec_field_list[RWREC_FIELD_FTYPE_CLASS]);
          default:
            return sk_lua_argerror(L, 3, "expected string for key %s, got %s",
                                   rwrec_field_list[RWREC_FIELD_FTYPE_CLASS],
                                   sk_lua_typename(L, -1));
        }
        switch (lua_getfield(L, 3, rwrec_field_list[RWREC_FIELD_FTYPE_TYPE])){
          case LUA_TSTRING:
            arg2 = lua_tostring(L, -1);
            break;
          case LUA_TNIL:
            return sk_lua_argerror(L, 3, "expected key %s not found in table",
                                   rwrec_field_list[RWREC_FIELD_FTYPE_TYPE]);
          default:
            return sk_lua_argerror(L, 3, "expected string for key %s, got %s",
                                   rwrec_field_list[RWREC_FIELD_FTYPE_TYPE],
                                   sk_lua_typename(L, -1));
        }
        flowtype_id = sksiteFlowtypeLookupByClassType(arg, arg2);
        if (SK_INVALID_FLOWTYPE == flowtype_id) {
            return sk_lua_argerror(L, 3, "unknown %s,%s pair '%s','%s'",
                                   rwrec_field_list[RWREC_FIELD_FTYPE_CLASS],
                                   rwrec_field_list[RWREC_FIELD_FTYPE_TYPE],
                                   arg, arg2);
        }
        rwRecSetFlowType(rwrec, flowtype_id);
        break;
      case RWREC_FIELD_TIMEOUT_KILLED:
        i = lua_toboolean(L, 3) ? SK_TCPSTATE_TIMEOUT_KILLED : 0;
        rwRecSetTcpState(rwrec, (i | (rwRecGetTcpState(rwrec)
                                      & ~SK_TCPSTATE_TIMEOUT_KILLED)));
        break;
      case RWREC_FIELD_TIMEOUT_STARTED:
        i = lua_toboolean(L, 3) ? SK_TCPSTATE_TIMEOUT_STARTED : 0;
        rwRecSetTcpState(rwrec, (i | (rwRecGetTcpState(rwrec)
                                      & ~SK_TCPSTATE_TIMEOUT_STARTED)));
        break;
      case RWREC_FIELD_UNIFORM_PACKETS:
        i = lua_toboolean(L, 3) ? SK_TCPSTATE_UNIFORM_PACKET_SIZE : 0;
        rwRecSetTcpState(rwrec, (i | (rwRecGetTcpState(rwrec)
                                      & ~SK_TCPSTATE_UNIFORM_PACKET_SIZE)));
        break;
      case RWREC_FIELD_SIDECAR:
        switch (lua_type(L, 3)) {
          case LUA_TNIL:
            /* remove any existing sidecar */
            ref = rwRecGetSidecar(rwrec);
            if (LUA_NOREF != ref) {
                rwRecSetSidecar(rwrec, LUA_NOREF);
                luaL_unref(L, LUA_REGISTRYINDEX, ref);
            }
            break;
          case LUA_TTABLE:
            /* FIXME: handle case of same table */
            ref = rwRecGetSidecar(rwrec);
            if (LUA_NOREF != ref) {
                rwRecSetSidecar(rwrec, LUA_NOREF);
                luaL_unref(L, LUA_REGISTRYINDEX, ref);
            }
            rwRecSetSidecar(rwrec, luaL_ref(L, LUA_REGISTRYINDEX));
            break;
          default:
            return sk_lua_argerror(L, 3, "table or nil expected, got %s",
                                   sk_lua_typename(L, 3));
        }
        break;
      default:
        skAbortBadCase(field);
    }

    return 0;
}


rwRec *
sk_lua_push_rwrec(
    lua_State          *L,
    const rwRec        *rwrec)
{
    rwRec *rec;

    rec = sk_lua_newuserdata(L, rwRec);
    luaL_setmetatable(L, SK_LUA_RWREC);
    if (rwrec) {
        rwRecCopy(rec, rwrec, SK_RWREC_COPY_UNINIT);
        rec->lua_state = L;
    } else {
        rwRecInitialize(rec, L);
    }

    return rec;
}


/*
 * =pod
 *
 * =item silk.B<rwrec(>[I<table>]B<)>
 *
 * Create an rwrec object.  With no argument or a B<nil> as an
 * argument, create an empty rwrec.  When I<table> is provided, the
 * new record is initialized by calling
 * L<silk.B<rwrec_set_value()>|/"silk.B<rwrec_set_value(>I<rwrec>, I<name>, I<value>B<)>">
 * for each key-value pair in I<table>.  Raise an error if the
 * argument is not a table or when a key is not a valid rwrec field
 * name.
 *
 * =item silk.B<rwrec_create(>[I<table>]B<)>
 *
 * Create an rwrec object.  An alias for
 * L<silk.B<rwrec()>|/"silk.B<rwrec(>[I<table>]B<)>">
 *
 * =cut
 */
static int
sk_lua_rwrec_create(
    lua_State          *L)
{
    if (lua_isnoneornil(L, 1)) {
        sk_lua_push_rwrec(L, NULL);
        return 1;
    }
    luaL_checktype(L, 1, LUA_TTABLE);

    sk_lua_push_rwrec(L, NULL);

    /* ensure we set the start-time first */
    lua_pushcfunction(L, sk_lua_rwrec_set_value);
    lua_pushvalue(L, -2);
    lua_pushstring(L, rwrec_field_list[RWREC_FIELD_STIME]);
    if (lua_getfield(L, 1, rwrec_field_list[RWREC_FIELD_STIME])
        == LUA_TUSERDATA)
    {
        lua_call(L, 3, 0);
    } else {
        lua_pop(L, 3);
    }

    for (lua_pushnil(L); lua_next(L, 1) != 0; lua_pop(L, 1)) {
        /* key and value are on the stack.  push rwrec_set_value(),
         * the rwRec, the key, and the value */
        lua_pushcfunction(L, sk_lua_rwrec_set_value);
        lua_pushvalue(L, -4);
        lua_pushvalue(L, -4);
        lua_pushvalue(L, -4);
        lua_call(L, 3, 0);
    }

    return 1;
}


/*
 * =pod
 *
 * =item silk.B<rwrec_clear(>I<rwrec>B<)>
 *
 * Clear all values in the RWRec object I<rwrec>.
 *
 * =cut
 */
static int
sk_lua_rwrec_clear(
    lua_State          *L)
{
    rwRec *rec;

    rec = sk_lua_checkrwrec(L, 1);
    rwRecReset(rec);
    return 0;
}


/*
 * =pod
 *
 * =item silk.B<rwrec_copy(>I<rwrec>B<)>
 *
 * Create a new RWRec that is a copy of the RWRec object I<rwrec>.
 *
 * =cut
 */
static int
sk_lua_rwrec_copy(
    lua_State          *L)
{
    const rwRec *src;

    src = sk_lua_checkrwrec(L, 1);
    sk_lua_push_rwrec(L, src);
    return 1;
}


/*
 * =pod
 *
 * =item silk.B<rwrec_is_icmp(>I<rwrec>B<)>
 *
 * Return B<true> if the protocol of I<rwrec> is 1 (ICMP) or if the
 * protocol of I<rwrec> is 58 (ICMPv6) and
 * L<silk.B<rwrec_is_ipv6()>|/"silk.B<rwrec_is_ipv6(>I<rwrec>B<)>">
 * is B<true>. Return B<false> otherwise.
 *
 * =cut
 */
static int
sk_lua_rwrec_is_icmp(
    lua_State          *L)
{
    const rwRec *rec;

    rec = sk_lua_checkrwrec(L, 1);
    lua_pushboolean(L, rwRecIsICMP(rec));
    return 1;
}


/*
 * =pod
 *
 * =item silk.B<rwrec_is_ipv6(>I<rwrec>B<)>
 *
 * Return B<true> if I<rwrec> contains IPv6 addresses. Return B<false>
 * otherwise.
 *
 * =cut
 */
/*
 */
static int
sk_lua_rwrec_is_ipv6(
    lua_State          *L)
{
    const rwRec *rec;

    rec = sk_lua_checkrwrec(L, 1);
    lua_pushboolean(L, rwRecIsIPv6(rec));
    return 1;
}

/*
 * =pod
 *
 * =item silk.B<rwrec_is_web(>I<rwrec>B<)>
 *
 * Return B<true> if I<rwrec> can be represented as a web record, or
 * B<false> otherwise.  A record can be represented as a web record if
 * the protocol is TCP (6) and either the source or destination port
 * is one of 80, 443, or 8080.
 *
 * =cut
 */
static int
sk_lua_rwrec_is_web(
    lua_State          *L)
{
    const rwRec *rec;

    rec = sk_lua_checkrwrec(L, 1);
    lua_pushboolean(L, rwRecIsWeb(rec));
    return 1;
}

/*
 * =pod
 *
 * =item silk.B<rwrec_as_table(>I<rwrec>B<)>
 *
 * Return a table representing the contents of I<rwrec>.  The keys of
 * the table are the strings listed in the documentation of
 * L<silk.B<rwrec_get_value()>|/"silk.B<rwrec_get_value(>I<name>B<)>">
 *
 * =cut
 */
static int
sk_lua_rwrec_as_table(
    lua_State          *L)
{
    unsigned int i;

    sk_lua_checkrwrec(L, 1);
    lua_createtable(L, 0,sizeof(rwrec_field_list)/sizeof(rwrec_field_list[0]));

    for (i = 0; rwrec_field_list[i] != NULL; ++i) {
        /* call get_value(rec, key) */
        lua_pushcfunction(L, sk_lua_rwrec_get_value);
        lua_pushvalue(L, 1);
        lua_pushstring(L, rwrec_field_list[i]);
        lua_call(L, 2, 1);
        lua_setfield(L, -2, rwrec_field_list[i]);
    }
    return 1;
}


/*
 *    Function that is the __pairs iterator.  Use the integer upvalue
 *    to determine which key,value pair to return.
 */
static int
sk_lua_rwrec_pairs_iter(
    lua_State          *L)
{
    lua_Integer i;
    int isnum;

    i = lua_tointegerx(L, lua_upvalueindex(1), &isnum);
    if (!isnum
        || i >= (int)(sizeof(rwrec_field_list)/sizeof(rwrec_field_list[0]) - 1)
        || i < 0)
    {
        lua_pushnil(L);
        return 1;
    }
    /* increment i and store */
    lua_pushinteger(L, i + 1);
    lua_replace(L, lua_upvalueindex(1));
    /* push the key that this function will return */
    lua_pushstring(L, rwrec_field_list[i]);
    /* call get_value(rec, key) to get the value that is returned */
    lua_pushcfunction(L, sk_lua_rwrec_get_value);
    lua_pushvalue(L, 1);
    lua_pushstring(L, rwrec_field_list[i]);
    lua_call(L, 2, 1);

    return 2;
}


/*
 * =pod
 *
 * =item B<pairs(>I<rwrec>B<)>
 *
 * Return an iterator designed for the Lua B<for> statement that
 * iterates over (name, value) pairs of I<rwrec>, where name is the
 * string name of the field and value is that field's value in
 * I<rwrec>.  May be used as
 * B<for I<name>, I<value> in pairs(I<rwrec>) do...end>
 *
 * =cut
 *
 *    To implement the iterator, push an integer to use as the index,
 *    push a closure that uses the integer as the upvalue, push the
 *    record.
 */
static int
sk_lua_rwrec_pairs(
    lua_State          *L)
{
    sk_lua_checkrwrec(L, 1);
    lua_pushinteger(L, 0);
    lua_pushcclosure(L, sk_lua_rwrec_pairs_iter, 1);
    lua_pushvalue(L, 1);
    return 2;
}


/*
 * =pod
 *
 * =item silk.B<rwrec_to_ipv4(>I<rwrec>B<)>
 *
 * Return a new copy of I<rwrec> with the IP addresses (sip, dip, and
 * nhip) converted to IPv4. If any of these addresses cannot be
 * converted to IPv4, (that is, if any address is not in the
 * ::ffff:0:0/96 netblock) return B<nil>.
 *
 * =cut
 */
static int
sk_lua_rwrec_to_ipv4(
    lua_State          *L)
{
    const rwRec *src;
    rwRec *dest;

    src = sk_lua_checkrwrec(L, 1);
    dest = sk_lua_push_rwrec(L, src);
    if (rwRecConvertToIPv4(dest)) {
        lua_pushnil(L);
    }
    return 1;
}


/*
 * =pod
 *
 * =item silk.B<rwrec_to_ipv6(>I<rwrec>B<)>
 *
 * Return a new copy of I<rwrec> with the IP addresses (sip, dip, and
 * nhip) converted to IPv6. Specifically, the function maps the IPv4
 * addresses into the ::ffff:0:0/96 netblock.
 *
 * =cut
 */
static int
sk_lua_rwrec_to_ipv6(
    lua_State          *L)
{
    const rwRec *src;
    rwRec *dest;

    src = sk_lua_checkrwrec(L, 1);
    dest = sk_lua_push_rwrec(L, src);
    rwRecConvertToIPv6(dest);
    return 1;
}


/*
 *    Return True if rec1 is structurally equivalent to rec2. Return
 *    False otherwise.
 */
static int
sk_lua_rwrec_equal(
    lua_State          *L)
{
    const rwRec *r1;
    const rwRec *r2;

    r1 = sk_lua_checkrwrec(L, 1);
    r2 = sk_lua_checkrwrec(L, 2);

    lua_pushboolean(L, 0 == memcmp(r1, r2, sizeof(*r1)));
    return 1;
}


static const luaL_Reg sk_lua_rwrec_metatable[] = {
    {"__gc",            sk_lua_rwrec_gc},
    {"__eq",            sk_lua_rwrec_equal},
    {"__index",         sk_lua_rwrec_get_value},
    {"__newindex",      sk_lua_rwrec_set_value},
    {"__pairs",         sk_lua_rwrec_pairs},
    {NULL, NULL}
};

static const luaL_Reg sk_lua_rwrec_methods[] = {
    {"as_table",        sk_lua_rwrec_as_table},
    {"clear",           sk_lua_rwrec_clear},
    {"copy",            sk_lua_rwrec_copy},
    {"get_value",       sk_lua_rwrec_get_value},
    {"set_value",       sk_lua_rwrec_set_value},
    {"is_icmp",         sk_lua_rwrec_is_icmp},
    {"is_ipv6",         sk_lua_rwrec_is_ipv6},
    {"is_web",          sk_lua_rwrec_is_web},
    {"to_ipv4",         sk_lua_rwrec_to_ipv4},
    {"to_ipv6",         sk_lua_rwrec_to_ipv6},
    {NULL, NULL}
};

static const luaL_Reg sk_lua_rwrec_static_methods[] = {
    {"create",          sk_lua_rwrec_create},
    {NULL, NULL}
};


/*
 *  ********************************************************************
 *  FILE FORMATS
 *  ********************************************************************
 */

/*
 * =pod
 *
 * =item silk.B<file_format_id(>I<name>B<)>
 *
 * Return the numeric ID associated with the file format I<name>.
 * Raise an error if I<name> is not a valid file format.
 *
 * =cut
 */
static int
sk_lua_file_format_id(
    lua_State          *L)
{
    const char *name;
    sk_file_format_t f;

    name = luaL_checkstring(L, 1);
    f = skFileFormatFromName(name);
    if (skFileFormatIsValid(f)) {
        lua_pushinteger(L, f);
        return 1;
    }

    return luaL_error(L, lua_pushfstring(L, "Invalid file format: %s", name));
}

/*
 * =pod
 *
 * =item silk.B<file_format_from_id(>I<id>B<)>
 *
 * Return the file format associated with the numeric ID I<id>.  Raise
 * an error if I<id> is not a valid file format ID.
 *
 * =cut
 */
static int
sk_lua_file_format_from_id(
    lua_State          *L)
{
    sk_file_format_t f;
    lua_Unsigned i;

    f = ~0;

    i = sk_lua_checkunsigned(L, 1);
    if ((uintmax_t)i > (uintmax_t)f) {
        /* Value is out of range */
        goto ERR;
    }
    f = (sk_file_format_t)i;
    if (skFileFormatIsValid(f)) {
        char buf[256];
        skFileFormatGetName(buf, sizeof(buf), f);
        lua_pushstring(L, buf);
        return 1;
    }

  ERR:
    return luaL_error(L, "Not a valid file format: %I", i);
}


/*
 *  ********************************************************************
 *  INITIALIZATION
 *  ********************************************************************
 */

/*
 * =pod
 *
 * =item silk.B<init_country_codes(>[I<filename>]B<)>
 *
 * Initialize the country code database.  I<filename> should be the
 * path to a country code prefix map, as created by
 * B<rwgeoip2ccmap(1)>.  If I<filename> is not supplied or B<nil>,
 * SiLK looks first for the file specified by F<$SILK_COUNTRY_CODES>,
 * and then for a file named F<country_codes.pmap> in
 * F<$SILK_PATH/share/silk>, F<$SILK_PATH/share>,
 * F<@prefix@/share/silk>, and F<@prefix@/share>.  (The latter two
 * assume that SiLK was installed in F<@prefix@>.)  The function
 * raises an error if loading the country code prefix map fails.
 *
 * =cut
 */
static int
sk_lua_init_country_codes(
    lua_State          *L)
{
    int   rv;
    const char *filename = luaL_optstring(L, 1, NULL);
    skCountryTeardown();
    rv = skCountrySetup(filename, error_printf);
    if (rv != 0) {
        return luaL_error(L, "%s", error_buffer);
    }
    return 0;
}


static const sk_lua_object_t objects[] = {
    {"ipaddr", SK_LUA_IPADDR, sk_lua_ipaddr,
     sk_lua_ipaddr_metatable, sk_lua_ipaddr_methods,
     sk_lua_ipaddr_static_methods},
    {"ipwildcard", SK_LUA_IPWILDCARD, sk_lua_ipwildcard,
     sk_lua_ipwildcard_metatable, sk_lua_ipwildcard_methods, NULL},
    {"ipset", SK_LUA_IPSET, NULL,
     sk_lua_ipset_metatable, sk_lua_ipset_methods,
     sk_lua_ipset_static_methods},
    {"pmap", SK_LUA_PMAP, sk_lua_pmap_load,
     sk_lua_pmap_metatable, sk_lua_pmap_methods, sk_lua_pmap_static_methods},
    {"bitmap", SK_LUA_BITMAP, sk_lua_bitmap,
     sk_lua_bitmap_metatable, sk_lua_bitmap_methods, NULL},
    {"datetime", SK_LUA_DATETIME, sk_lua_datetime,
     sk_lua_datetime_metatable, sk_lua_datetime_methods,
     sk_lua_datetime_static_methods},
    {"rwrec", SK_LUA_RWREC, sk_lua_rwrec_create,
     sk_lua_rwrec_metatable, sk_lua_rwrec_methods,
     sk_lua_rwrec_static_methods},
    {"sidecar", SK_LUA_SIDECAR, sk_lua_sidecar_create,
     sk_lua_sidecar_metatable, sk_lua_sidecar_methods,
     sk_lua_sidecar_static_methods},
    {"sidecar_elem", SK_LUA_SIDECAR_ELEM, NULL,
     sk_lua_sidecar_elem_metatable, sk_lua_sidecar_elem_methods, NULL},
    SK_LUA_OBJECT_SENTINEL
};

static const luaL_Reg sk_lua_silk_module_functions[] = {
    {"init_country_codes",      sk_lua_init_country_codes},
    {"tcpflags_parse",          sk_lua_tcpflags_parse},
    {"tcpflags_matches",        sk_lua_tcpflags_matches},
    {"tcpflags_to_string",      sk_lua_tcpflags_to_string},
    {"attributes_parse",        sk_lua_attributes_parse},
    {"attributes_matches",      sk_lua_attributes_matches},
    {"attributes_to_string",    sk_lua_attributes_to_string},
    {"file_format_id",          sk_lua_file_format_id},
    {"file_format_from_id",     sk_lua_file_format_from_id},
    {NULL, NULL}
};

static const luaL_Reg sk_lua_silk_internal_functions[] = {
    {"sc_elem_make_type_table", sk_lua_sc_elem_make_type_table},
    {"sc_elem_create",          sk_lua_sc_elem_create},
    {"sidecar_freeze_helper",   sk_lua_sidecar_freeze_helper},
    {NULL, NULL}
};


lua_State *
sk_lua_newstate(
    void)
{
    lua_State *L;

    L = luaL_newstate();
    if (NULL == L) {
        skAppPrintOutOfMemory("new Lua state");
        exit(EXIT_FAILURE);
    }

    luaL_openlibs(L);
    sk_lua_install_silk_modules(L);

    return L;
}

void
sk_lua_closestate(
    lua_State          *L)
{
    if (L) {
        lua_close(L);
    }
}


void
sk_lua_install_silk_modules(
    lua_State          *L)
{
    lua_pushcfunction(L, luaopen_schema);
    lua_pushcfunction(L, luaopen_silk);
    lua_call(L, 0, 1);
    lua_call(L, 1, 1);
    lua_setglobal(L, "silk");

    /* Replace loadfile with skstream loadfile */
    lua_pushcfunction(L, sk_lua_skstream_loadfile);
    lua_setglobal(L, "loadfile");
}

static int
luaclose_silk(
    lua_State          *L)
{
    if (!lua_isnil(L, lua_upvalueindex(1))) {
        lua_pushvalue(L, lua_upvalueindex(1));
        lua_pushvalue(L, 1);
        lua_call(L, 1, 0);
    }
    skAppUnregister();
    return 0;
}

int
luaopen_silk(
    lua_State          *L)
{
    const char *name;
    int inittable;

    inittable = lua_istable(L, 1);

    /* Check lua versions */
    luaL_checkversion(L);

    /* Load the lua portion */
    sk_lua_load_lua_blob(L, sk_lua_init_blob, sizeof(sk_lua_init_blob),
                         "silk.lua", 0, 1);
    lua_setfield(L, LUA_REGISTRYINDEX, SK_LUA_INIT);

    /* Register app name if necessary */
    lua_getglobal(L, "arg");
    if (!lua_isnil(L, -1)) {
        lua_rawgeti(L, -1, 0);
    }
    name = lua_tostring(L, -1);
    skAppRegister(name ? name : "LUA_program");

    /* Run the make_silk_module function to make the silk module */
    lua_getfield(L, LUA_REGISTRYINDEX, SK_LUA_INIT);
    lua_getfield(L, -1, "make_silk_module");
    lua_newtable(L);
    sk_lua_add_to_object_table(L, -1, objects);
    luaL_newlib(L, sk_lua_silk_module_functions);
    luaL_newlib(L, sk_lua_silk_internal_functions);
    if (inittable) {
        lua_pushvalue(L, 1);
        lua_call(L, 4, 1);
    } else {
        lua_call(L, 3, 1);
    }

    /* Set up a teardown function for the module */
    if (lua_getmetatable(L, -1)) {
        lua_getfield(L, -1, "__gc");
    } else {
        lua_createtable(L, 0, 1);
        lua_pushnil(L);
    }
    lua_pushcclosure(L, luaclose_silk, 1);
    lua_setfield(L, -2, "__gc");
    lua_setmetatable(L, -2);

    /* Add the site module */
    lua_pushcfunction(L, luaopen_silk_site);
    lua_call(L, 0, 1);
    lua_setfield(L, -2, "site");

    /* Return the silk module */
    return 1;
}

/*
** Local Variables:
** mode:c
** indent-tabs-mode:nil
** c-basic-offset:4
** End:
*/
