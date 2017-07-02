/*
** Copyright (C) 2015-2017 by Carnegie Mellon University.
**
** @OPENSOURCE_LICENSE_START@
** See license information in ../../LICENSE.txt
** @OPENSOURCE_LICENSE_END@
*/
#ifndef _SKSIDECAR_H
#define _SKSIDECAR_H
#ifdef __cplusplus
extern "C" {
#endif

#include <silk/silk.h>

RCSIDENTVAR(rcsID_SKSIDECAR_H, "$SiLK: sksidecar.h 373f990778e9 2017-06-22 21:57:36Z mthomas $");

#include <silk/silk_types.h>
#include <silk/skheader.h>
#include <silk/sklua.h>


/*
 *  sksidecar.h
 *
 *    The sidecar data structure describes the add-on (sidecar) fields
 *    that suppliment the standard fields in a SiLK rwRec.
 *
 *    This files also has functions to serialize and deserialize the
 *    description of the sidecar data and the data itself.
 *
 *  Mark Thomas
 *  October 2015
 */


/**
 *    sk_sidecar_t describes all the possible elements that may be
 *    used by the sidecar data elements of a SiLK flow record, rwRec.
 *
 *    The sk_sidecar_t holds a list of elements, each of which is
 *    represented by the sk_sidecar_elem_t object.
 */
/* typedef struct sk_sidecar_st sk_sidecar_t; // silk_types.h */


/**
 *    sk_sidecar_elem_t represents one element in an sk_sidecar_t.
 *
 *    A sk_sidecar_elem_t has a name that is unique across all
 *    elements in a sidecar.  It has an ID that is used when
 *    serializing data represented by this element.  It has a data
 *    type (e.g., IP, number, string) that is represented by an
 *    sk_sidecar_type_t.  It may have an optional reference to an
 *    IPFIX information element ID.
 */
typedef struct sk_sidecar_elem_st sk_sidecar_elem_t;

#ifndef SK_FIELD_IDENT_CREATE
/**
 *    An information element identifier, comprising an enterpriseId
 *    (a.k.a, a Private Enterprise Number or PEN) and an elementId.
 *    An enterpriseId of 0 indicates a standard information element.
 *
 *    Currently implemented as a uint64_t, with the enterpriseId in
 *    the high 32 bits, and the elementId in the low 16 bits.
 */
typedef uint64_t sk_field_ident_t;
#endif  /* SK_FIELD_IDENT_CREATE */


/**
 *    sk_sidecar_iter_t allows one to iterate over the elements of a
 *    sidecar object.  It is used by sk_sidecar_iter_bind() and
 *    sk_sidecar_iter_next().
 */
struct sk_sidecar_iter_st {
    const sk_sidecar_t *sc;
    size_t              pos;
};
typedef struct sk_sidecar_iter_st sk_sidecar_iter_t;


/**
 *    sk_sidecar_type_t represents the types of data that the sidecar
 *    structure supports
 */
enum sk_sidecar_type_en {
    SK_SIDECAR_UNKNOWN = 0,
    SK_SIDECAR_UINT8 = 1,
    SK_SIDECAR_UINT16 = 2,
    SK_SIDECAR_UINT32 = 4,
    SK_SIDECAR_UINT64 = 8,
    SK_SIDECAR_DOUBLE = 32,
    SK_SIDECAR_STRING = 33,
    SK_SIDECAR_BINARY = 34,
    SK_SIDECAR_ADDR_IP4 = 35,
    SK_SIDECAR_ADDR_IP6 = 36,
    SK_SIDECAR_DATETIME = 37,
    SK_SIDECAR_BOOLEAN = 38,
    SK_SIDECAR_EMPTY = 63,
    SK_SIDECAR_LIST = 64,
    SK_SIDECAR_TABLE = 65
};
typedef enum sk_sidecar_type_en sk_sidecar_type_t;


/**
 *    sk_sidecar_status_en holds the return codes that the functions
 *    in this file may return.
 */
enum sk_sidecar_status_en {
    SK_SIDECAR_OK = 0,
    SK_SIDECAR_E_NULL_PARAM,
    SK_SIDECAR_E_BAD_PARAM,
    SK_SIDECAR_E_DUPLICATE,
    SK_SIDECAR_E_NO_SPACE,
    SK_SIDECAR_E_SHORT_DATA,
    SK_SIDECAR_E_DECODE_ERROR
};


/*
 *  ****************************************************************
 *  Creation and Destruction
 *  ****************************************************************
 */

/**
 *    Create a new sidecar object and store it at the location
 *    referenced by 'sc'.  It is an error if 'sc' is NULL.
 */
int
sk_sidecar_create(
    sk_sidecar_t      **sc);

/**
 *    Free all resources associated with the sidecar object referenced
 *    by the memory at 'sc'.  Does nothing if 'sc' is NULL or the
 *    memory it references is NULL.  See also sk_sidecar_free().
 */
int
sk_sidecar_destroy(
    sk_sidecar_t      **sc);

/**
 *    Free all resources associated with the sidecar object 'sc'.
 *    Does nothing if 'sc' is NULL.  See also sk_sidecar_destroy().
 */
void
sk_sidecar_free(
    sk_sidecar_t       *sc);

/**
 *    Create a new sidecar object and store it at the location
 *    referenced by 'sec_dest'.  Add to the new sidecar object all the
 *    elements that exist on the sidecar 'sc_src'.  It is an error if
 *    either argument is NULL.
 *
 *    Exit on memory allocation error.
 */
int
sk_sidecar_copy(
    sk_sidecar_t      **sc_dest,
    const sk_sidecar_t *sc_src);

/**
 *    Add a new element to the sidecar object 'sc'.
 *
 *    A pointer to the new element is stored at the location
 *    referenced by 'new_elem'.  If 'new_elem' is NULL, the element is
 *    added but no handle is returned.
 *
 *    The element is given the name 'name', which must be unique for
 *    all elements known to this sidecar.  If the element refers to
 *    the member of a structured data, the name must use embedded
 *    '\0's to denote each level.
 *
 *    The 'namelen' parameter specifies the length of 'name' including
 *    the terminating '\0'.  If 'namelen' is 0, 'name' is assumed to
 *    end at the first '\0' and the length is computed by this
 *    function.
 *
 *    The type of the element is specified by 'data_type'.  An IPFIX
 *    information element ID to associate with this element may be
 *    specified by setting 'ident' to a non-zero value.
 *
 *    When 'data_type' is SK_SIDECAR_LIST, the 'list_elem_type'
 *    parameter must indicate the type of elements in the list.  The
 *    type cannot be SK_SIDECAR_LIST, SK_SIDECAR_TABLE, or
 *    SK_SIDECAR_UNKNOWN.
 *
 *    The element is inserted before the element specified by
 *    'before_elem'.  The element is appended if 'before_elem' is
 *    NULL.  FIXME: We may decide not to allow 'before_elem' and
 *    instead we always append.  Order is used as the key when
 *    serializing data, and inserting a field after some data has been
 *    serialized breaks things.
 *
 *    Return SK_SIDECAR_OK on success.
 *
 *    Return SK_SIDECAR_E_DUPLICATE if an element with 'name' already
 *    exists on 'sc'.  Return SK_SIDECAR_E_NULL_PARAM if 'sc' or
 *    'name' is NULL.  Return SK_SIDECAR_E_BAD_PARAM when 'data_type'
 *    is SK_SIDECAR_UNKNOWN, when 'before_elem' is not on 'sc', or
 *    when 'namelen' is not-zero and the 'namelen'th character of
 *    'name' is not '\0'.
 *
 */
int
sk_sidecar_add(
    sk_sidecar_t               *sc,
    const char                 *name,
    size_t                      namelen,
    sk_sidecar_type_t           data_type,
    sk_sidecar_type_t           list_elem_type,
    sk_field_ident_t            ident,
    const sk_sidecar_elem_t    *before_elem,
    sk_sidecar_elem_t         **new_elem);


/**
 *    Append a new element to the sidecar object 'sc'.
 *
 *    Call sk_sidecar_add() using the name, namelen, data_type, and
 *    list_elem_type on the existing sidecar element 'src_elem' (which
 *    must belong to a different sidecar object).  The new element is
 *    placed at the end of element list.
 *
 *    A pointer to the new element is stored at the location
 *    referenced by 'new_elem'.  If 'new_elem' is NULL, the element is
 *    added but no handle is returned.
 *
 *    The return status is the same as sk_sidecar_add().
 */
int
sk_sidecar_add_elem(
    sk_sidecar_t               *sc,
    const sk_sidecar_elem_t    *src_elem,
    sk_sidecar_elem_t         **new_elem);


/**
 *    Append a new elem to the sidecar object 'sc'.
 *
 *    The 'data_type' may not be SK_SIDECAR_LIST; use
 *    sk_sidecar_append_list() instead.
 *
 *    The function is roughly equivalent to
 *
 *    sk_sidecar_elem_t *elem = NULL;
 *    sk_sidecar_add(&elem, sc, name, namelen, data_type,
 *                   SK_SIDECAR_UNKNOWN, ident, NULL);
 *    return elem;
 */
sk_sidecar_elem_t *
sk_sidecar_append(
    sk_sidecar_t       *sc,
    const char         *name,
    size_t              namelen,
    sk_sidecar_type_t   data_type,
    sk_field_ident_t    ident);

/**
 *    Append a new list elem to the sidecar object 'sc'.
 *
 *    The 'list_elem_type' may not be SK_SIDECAR_LIST,
 *    SK_SIDECAR_TABLE, or SK_SIDECAR_UNKNOWN.
 *
 *    The function is roughly equivalent to
 *
 *    sk_sidecar_elem_t *elem = NULL;
 *    sk_sidecar_add(&elem, sc, name, namelen, SK_SIDECAR_LIST,
 *                   list_elem_type, ident, NULL);
 *    return elem;
 */
sk_sidecar_elem_t *
sk_sidecar_append_list(
    sk_sidecar_t       *sc,
    const char         *name,
    size_t              namelen,
    sk_sidecar_type_t   list_elem_type,
    sk_field_ident_t    ident);


/*
 *  ****************************************************************
 *  Querying, Iterating, and Searching
 *  ****************************************************************
 */

/**
 *    Return the number of elements present in the sidecar object
 *    'sc'.  Return SIZE_MAX when 'sc' is NULL.
 */
size_t
sk_sidecar_count_elements(
    const sk_sidecar_t *sc);

/**
 *    Bind the iterator object 'iter' to iterate over the elements of
 *    the sidecar object 'sc'.
 *
 *    To visit the elements, call sk_sidecar_iter_next().
 *
 *    Return SK_SIDECAR_OK on sucess.
 *
 *    Return SK_SIDECAR_E_NULL_PARAM when 'sc' or 'iter' is NULL.
 */
int
sk_sidecar_iter_bind(
    const sk_sidecar_t *sc,
    sk_sidecar_iter_t  *iter);

/**
 *    Fill the memory referenced by 'elem' with the next element of
 *    the sidecar object to which the iterator 'iter' was bound by a
 *    call to sk_sidecar_iter_bind().
 *
 *    Return SK_ITERATOR_OK if an element exists.
 *
 *    Leave the value referenced by 'elem' unchanged and return
 *    SK_ITERATOR_NO_MORE_ENTRIES when all elements have been visited
 *    or when 'iter' or 'elem' is NULL.
 */
int
sk_sidecar_iter_next(
    sk_sidecar_iter_t          *iter,
    const sk_sidecar_elem_t   **elem);

/**
 *    Return an element on the sidecar object 'sc' whose type is
 *    'data_type'.  Return NULL if no such element exists.  If 'after'
 *    is specified, begin the search with the element following
 *    'after'.
 *
 *    Return NULL when 'sc' is NULL or when 'after' is not on 'sc'.
 */
const sk_sidecar_elem_t *
sk_sidecar_find_by_data_type(
    const sk_sidecar_t         *sc,
    sk_sidecar_type_t           data_type,
    const sk_sidecar_elem_t    *after);

/**
 *    Return the element on the sidecar object 'sc' whose integer ID
 *    is 'id'.  Return NULL if no such element exists.
 *
 *    Return NULL when 'sc' is NULL.
 */
const sk_sidecar_elem_t *
sk_sidecar_find_by_id(
    const sk_sidecar_t *sc,
    size_t              id);

/**
 *    Return an element on the sidecar object 'sc' whose IPFIX
 *    information element ID is 'ipfix_ident'.  Return NULL if no such
 *    element exists.  If 'after' is specified, begin the search with
 *    the element following 'after'.
 *
 *    Return NULL when 'sc' is NULL or when 'after' is not on 'sc'.
 */
const sk_sidecar_elem_t *
sk_sidecar_find_by_ipfix_ident(
    const sk_sidecar_t         *sc,
    sk_field_ident_t            ipfix_ident,
    const sk_sidecar_elem_t    *after);

/**
 *    Return the element on the sidecar object 'sc' whose name is
 *    'name'.  The 'namelen' parameter specifies the length of 'name'
 *    including the terminating '\0'.  If 'namelen' is 0, 'name' is
 *    assumed to end at the first '\0' and the length is computed by
 *    this function.
 *
 *    Return NULL when 'sc' is NULL or when 'name is NULL.
 */
const sk_sidecar_elem_t *
sk_sidecar_find_by_name(
    const sk_sidecar_t *sc,
    const char         *name,
    size_t              namelen);


/**
 *    Return a string describing the element type.
 */
const char *
sk_sidecar_type_get_name(
    sk_sidecar_type_t   data_type);


/*
 *  ****************************************************************
 *  Individual Element Operations
 *  ****************************************************************
 */

/**
 *    Return the data type associated with the sidecar element 'elem'.
 *
 *    Return SK_SIDECAR_UNKNOWN when 'elem' is NULL.
 */
sk_sidecar_type_t
sk_sidecar_elem_get_data_type(
    const sk_sidecar_elem_t   *elem);

/**
 *    Return the integer ID used when serializing data whose name is
 *    that used by the sidecar element 'elem'.
 *
 *    Return SIZE_MAX when 'elem' is NULL.
 */
size_t
sk_sidecar_elem_get_id(
    const sk_sidecar_elem_t   *elem);

/**
 *    Return the ID of the IPFIX information element associated with
 *    the sidecar element 'elem', or return 0 if there is no
 *    associated IPFIX IE.
 *
 *    Return 0 when 'elem' is NULL.
 */
sk_field_ident_t
sk_sidecar_elem_get_ipfix_ident(
    const sk_sidecar_elem_t   *elem);

/**
 *    Return the data type associated with the elements of the list
 *    represented by the sidecar element 'elem'.
 *
 *    Return SK_SIDECAR_UNKNOWN when 'elem' is NULL or when the data
 *    type of 'elem' is not SK_SIDECAR_LIST.
 */
sk_sidecar_type_t
sk_sidecar_elem_get_list_elem_type(
    const sk_sidecar_elem_t   *elem);

/**
 *    Fill 'buf' with the name associated with the sidecar element
 *    'elem'.  The length of 'buf' must be specified in the location
 *    referenced by 'buflen', and 'buflen' is modified to be the
 *    length of the name including the terminating '\0'.  Return the
 *    parameter 'buf'.
 *
 *    When 'buflen' is smaller than the length of the name, set
 *    'buflen' to the length of the name and return NULL.
 *
 *    Return NULL when 'elem' is NULL, when 'buf' is NULL, or when
 *    'buflen' is NULL.
 */
const char *
sk_sidecar_elem_get_name(
    const sk_sidecar_elem_t   *elem,
    char                      *buf,
    size_t                    *buflen);

/**
 *    Return the length of the name associated with the sidecar
 *    element 'elem'.  This length includes the terminating '\0' and
 *    any embedded '\0's used to denote levels of structured data.
 *
 *    Return SIZE_MAX when 'elem' is NULL.
 */
size_t
sk_sidecar_elem_get_name_length(
    const sk_sidecar_elem_t   *elem);


/*
 *  ****************************************************************
 *  Serialization
 *  ****************************************************************
 */

/**
 *    Given a Lua table in the Lua registry at index 'lua_ref' in the
 *    Lua state object 'L', serialize it into 'buffer' using the
 *    elements specified in the sidecar object 'sc'.
 *
 *    If 'tbl_ref' is LUA_NOREF or LUA_REFNIL or the object at
 *    'lua_ref' is not a table, a sidecar with zero elements is
 *    serialized into 'buffer'.
 *
 *    The 'buflen' parameter must be set to available space in
 *    'buffer'.  This function modifies that value to the number of
 *    bytes added to 'buffer'.
 *
 *    To deserialize the data, call sk_sidecar_deserialize_data().
 */
int
sk_sidecar_serialize_data(
    const sk_sidecar_t *sc,
    lua_State          *L,
    int                 lua_ref,
    uint8_t            *buffer,
    size_t             *buflen);

/**
 *    Given the octet array 'buffer' that was created by a call to
 *    sk_sidecar_serialize_data() using the sidecar object 'sc',
 *    reconstitute the object represented by that buffer, store that
 *    object in the Lua registry, and set the referent of 'lua_ref' to
 *    its location in the registry.
 *
 *    The 'buflen' parameter must be set to available bytes in
 *    'buffer'.  This function modifies that value to the number of
 *    bytes of 'buffer' that this function processed.
 */
int
sk_sidecar_deserialize_data(
    const sk_sidecar_t *sc,
    lua_State          *L,
    const uint8_t      *buffer,
    size_t             *buflen,
    int                *lua_ref);

/**
 *    Given the octet array 'buffer' that was created by a call to
 *    sk_sidecar_serialize_data() using the sidecar object 'sc', set
 *    the location referenced by 'buflen' to the length of the data.
 *
 *    The 'buflen' parameter must be set to available bytes in
 *    'buffer'.  This function modifies that value to the number of
 *    bytes of 'buffer' that the sk_sidecar_deserialize_data()
 *    function would have processed.
 */
int
sk_sidecar_skip_data(
    const sk_sidecar_t *sc,
    const uint8_t      *buffer,
    size_t             *buflen);

/**
 *    Given a sidecar object 'sc', serialize it into 'buffer'.
 *
 *    This function serializes the description of the elements
 *    represented by 'sc'.  To serialize an object that uses this
 *    description, use sk_sidecar_serialize_data().
 *
 *    The 'buflen' parameter must be set to available space in
 *    'buffer'.  This function modifies that value to the number of
 *    bytes added to 'buffer'.
 *
 *    To deserialize the sidecar object, call
 *    sk_sidecar_deserialize_self().
 */
int
sk_sidecar_serialize_self(
    const sk_sidecar_t *sc,
    uint8_t            *buffer,
    size_t             *buflen);

/**
 *    Given the octet array 'buffer' that was created by a call to
 *    sk_sidecar_serialize_self(), reconstitute the sidecar object
 *    represented by that buffer and store it in the location
 *    referenced by 'sc'.
 *
 *    The 'buflen' parameter must be set to available bytes in
 *    'buffer'.  This function modifies that value to the number of
 *    bytes of 'buffer' that this function processed.
 */
int
sk_sidecar_deserialize_self(
    sk_sidecar_t       *sc,
    const uint8_t      *buffer,
    size_t             *buflen);


/*
 *  ****************************************************************
 *  Getting From & Adding To A File Header
 *  ****************************************************************
 */

/**
 *    Serialize the sidecat object 'sc' and add it to the file header
 *    'hdr'.
 *
 *    Return SK_SIDECAR_E_BAD_PARAM if 'hdr' there is an error adding
 *    the header entry to the header (probably due to the header being
 *    locked).
 */
int
sk_sidecar_add_to_header(
    const sk_sidecar_t *sc,
    sk_file_header_t   *hdr);

/**
 *    Create a new sidecar object by deserializing the sidecar entry
 *    in the file header 'hdr' and return the new sidecar.
 *
 *    When 'status_parm' is not NULL, set its referent to the status
 *    code of deserializing the header.
 *
 *    If 'hdr' does not contain a sidecar header entry, set the
 *    referent of 'status_parm' to SK_SIDECAR_OK and return NULL.
 */
sk_sidecar_t *
sk_sidecar_create_from_header(
    const sk_file_header_t *hdr,
    int                    *status_parm);



#ifndef SK_FIELD_IDENT_CREATE
/*
 *  ****************************************************************
 *  Field Ident Operations
 *  ****************************************************************
 */

/**
 *    Create an sk_field_ident_t from a PEN/ID pair.
 */
#define SK_FIELD_IDENT_CREATE(_pen_, _id_) \
    ((((uint64_t)(_pen_)) << 32) | ((_id_) & 0x7fff))

/**
 *    Return the PEN from an sk_field_ident_t, as a uint32_t.
 */
#define SK_FIELD_IDENT_GET_PEN(_ident_)                 \
    ((uint32_t)(((sk_field_ident_t)(_ident_)) >> 32))

/**
 *    Return the ID from an sk_field_ident_t, as a uint16_t.
 */
#define SK_FIELD_IDENT_GET_ID(_ident_)  ((uint16_t)((_ident_) & 0x7fff))

#endif  /* #ifndef SK_FIELD_IDENT_CREATE */

#ifdef __cplusplus
}
#endif
#endif /* _SKSIDECAR_H */

/*
** Local Variables:
** mode:c
** indent-tabs-mode:nil
** c-basic-offset:4
** End:
*/
