/*
** Copyright (C) 2004-2020 by Carnegie Mellon University.
**
** @OPENSOURCE_LICENSE_START@
** See license information in ../../LICENSE.txt
** @OPENSOURCE_LICENSE_END@
*/

/*
**  bagtree.h
**
**    This file and the functions it declares are deprecated as of
**    SiLK 3.0.  Use skbag.h instead.
**
**    The new functions that replace the functions declared here are
**    declared in skbag.h.  In addition, skbag.h declares functions
**    that were maintained between SiLK-2.x and SiLK-3.x.  Finally,
**    this file references types and macros that are defined in
**    skbag.h.
**
*/
#ifndef _BAGTREE_H
#define _BAGTREE_H
#ifdef __cplusplus
extern "C" {
#endif

#include <silk/silk.h>

RCSIDENTVAR(rcsID_BAGTREE_H, "$SiLK: bagtree.h ef14e54179be 2020-04-14 21:57:45Z mthomas $");

#include <silk/skbag.h>


/* DEFINES AND TYPEDEFS */

/* the number of a level */
typedef uint8_t skBagLevel_t;

/* the number of bits encoded on a level */
typedef uint8_t skBagLevelsize_t;


/*
 *  ok = skBagStreamFunc_t(key, counter, cb_data);
 *
 *    Deprecated as of SiLK 3.0.0.
 *
 *    The function signature of the callback invoked by the deprecated
 *    skBagProcessStream() function when reading a bag from a stream.
 *    This callback uses an skBagKey_t {uint32_t} key and an
 *    skBagCounter_t {uint64_t) counter.
 */
typedef skBagErr_t (*skBagStreamFunc_t)(
    const skBagKey_t       *key,
    const skBagCounter_t   *counter,
    void                   *cb_data);


/* FUNCTION DECLARATIONS */

/*
 *  status = skBagAddToCounter(bag, &key, &counter_add);
 *
 *    Deprecated as of SiLK 3.0.0.
 *
 *    This function is a wrapper around skBagCounterAdd().  This
 *    function expects an skBagKey_t {uint32_t} key and an
 *    skBagCounter_t {uint64_t} counter.
 *
 *    In 'bag', add to the counter associated with 'key' the value
 *    pointed to by 'counter_add'.  If 'key' is not in the Bag, insert
 *    it.
 */
skBagErr_t
skBagAddToCounter(
    skBag_t                *bag,
    const skBagKey_t       *key,
    const skBagCounter_t   *counter_add);


/*
 *  status = skBagAlloc(&bag, num_levels, level_sizes);
 *
 *    Deprecated as of SiLK 3.0.0.
 *
 *    This function is a wrapper for 'skBagCreateTyped().  The number
 *    of bits in the 'level_sizes' array are summed and divided by 8
 *    to compute the number of key_octets to specified.
 *
 *    The type of the key and counter are set to SKBAG_FIELD_CUSTOM.
 */
skBagErr_t
skBagAlloc(
    skBag_t                   **bag,
    skBagLevel_t                levels,
    const skBagLevelsize_t     *level_sizes);


/*
 *  status = skBagDecrementCounter(bag, &key);
 *
 *    Deprecated as of SiLK 3.0.0.
 *
 *    This function is a wrapper around skBagCounterSubtract().  This
 *    function expects an skBagKey_t {uint32_t} key.
 *
 *    In 'bag', subtract 1 from the counter associated with 'key', or
 *    do nothing if 'key' is does not exist in the bag.
 */
skBagErr_t
skBagDecrementCounter(
    skBag_t            *bag,
    const skBagKey_t   *key);


/*
 *  status = skBagFree(bag);
 *
 *    Free all memory associated with the bag 'bag'.  The function
 *    returns SKBAG_ERR_INPUT if the 'bag' parameter is NULL.
 *
 *    Deprecated as of SiLK 3.0.0.  Use skBagDestroy() instead.
 */
skBagErr_t
skBagFree(
    skBag_t            *bag);


/*
 *  status = skBagGetCounter(bag, &key, &counter);
 *
 *    Deprecated as of SiLK 3.0.0.
 *
 *    This function is a wrapper around skBagCounterGet().  This
 *    function expects an skBagKey_t {uint32_t} key and an
 *    skBagCounter_t {uint64_t} counter.
 *
 *    Set 'counter' to the value for the counter associated with 'key'
 *    in 'bag'.  Set 'counter' to 0 if 'key' is not in the bag.
 */
skBagErr_t
skBagGetCounter(
    skBag_t            *bag,
    const skBagKey_t   *key,
    skBagCounter_t     *counter);


/*
 *  status = skBagIncrCounter(bag, &key);
 *
 *    Deprecated as of SiLK 3.0.0.
 *
 *    This function is a wrapper around skBagCounterAdd().  This
 *    function expects an skBagKey_t {uint32_t} key.
 *
 *    In 'bag', add 1 to the counter associated with 'key', creating
 *    'key' if it does not already exist in 'bag'.
 */
skBagErr_t
skBagIncrCounter(
    skBag_t            *bag,
    const skBagKey_t   *key);


/*
 *  status = skBagIteratorNext(iter, &key, &counter);
 *
 *    Deprecated as of SiLK 3.0.0.
 *
 *    This function is a wrapper around skBagIteratorNextTyped().
 *    This function fills a pointer to an skBagKey_t {uint32_t} key
 *    and an skBagCounter_t {uint64_t} counter.
 *
 *    Get the next key/counter pair associated with the given
 *    iterator, 'iter', store them in the memory pointed at by
 *    'key' and 'counter', respectively, and return SKBAG_OK.
 */
skBagErr_t
skBagIteratorNext(
    skBagIterator_t    *iter,
    skBagKey_t         *key,
    skBagCounter_t     *counter);


/*
 *  status = skBagProcessStream(stream, cb_data, cb_func);
 *
 *    Deprecated as of SiLK 3.0.0.
 *
 *    This function is a wrapper around skBagProcessStreamTyped().
 *
 *    Read a Bag from the 'stream'.  For each key/counter pair in the
 *    Bag, the function invokes the callback function 'cb_entry_func'
 *    with an skBagKey_t {uin32_t} key, an skBagCounter_t {uint64_t}
 *    counter, and the 'cb_data'.  Processing continues until the
 *    stream is exhausted or until 'cb_func' returns a value other
 *    than 'SKBAG_OK'.
 */
skBagErr_t
skBagProcessStream(
    skstream_t         *stream_in,
    void               *cb_data,
    skBagStreamFunc_t   cb_func);


/*
 *  status = skBagRemoveKey(bag, key);
 *
 *    Deprecated as of SiLK 3.0.0.
 *
 *    This function is a wrapper around skBagCounterSet().  This
 *    function expects an skBagKey_t {uint32_t} key.
 *
 *    In 'bag', set the counter associated with 'key' to 0, or do
 *    nothing if 'key' is not in the Bag.
 */
skBagErr_t
skBagRemoveKey(
    skBag_t            *bag,
    const skBagKey_t   *key);


/*
 *  status = skBagSetCounter(bag, &key, &counter);
 *
 *    Deprecated as of SiLK 3.0.0.
 *
 *    This function is a wrapper around skBagCounterSet().  This
 *    function expects an skBagKey_t {uint32_t} key and an
 *    skBagCounter_t {uint64_t} counter.
 *
 *    In 'bag', set the counter associated with 'key' to the value
 *    pointed to by 'counter'.  If 'counter' is 0, remove 'key' from
 *    the Bag; otherwise this function creates the key if it does not
 *    exist in the bag.
 */
skBagErr_t
skBagSetCounter(
    skBag_t                *bag,
    const skBagKey_t       *key,
    const skBagCounter_t   *counter);


/*
 *  status = skBagSubtractFromCounter(bag, &key, &counter_sub);
 *
 *    Deprecated as of SiLK 3.0.0.
 *
 *    This function is a wrapper around skBagCounterSubtract().  This
 *    function expects an skBagKey_t {uint32_t} key and an
 *    skBagCounter_t {uint64_t} counter.
 *
 *    In 'bag', subtract from the counter associated with 'key' the
 *    value pointed to by 'counter_sub'.  The 'key' must exist in the
 *    bag; if it does not, SKBAG_ERR_OP_BOUNDS is returned.
 */
skBagErr_t
skBagSubtractFromCounter(
    skBag_t                *bag,
    const skBagKey_t       *key,
    const skBagCounter_t   *counter_sub);

#ifdef __cplusplus
}
#endif
#endif /* _BAGTREE_H */

/*
** Local Variables:
** mode:c
** indent-tabs-mode:nil
** c-basic-offset:4
** End:
*/
