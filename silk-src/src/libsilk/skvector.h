/*
** Copyright (C) 2005-2017 by Carnegie Mellon University.
**
** @OPENSOURCE_LICENSE_START@
** See license information in ../../LICENSE.txt
** @OPENSOURCE_LICENSE_END@
*/

/*
 *  skvector.h
 *
 *    Implementation of a resizeable array.
 *
 */
#ifndef _SKVECTOR_H
#define _SKVECTOR_H
#ifdef __cplusplus
extern "C" {
#endif

#include <silk/silk.h>

RCSIDENTVAR(rcsID_SKVECTOR_H, "$SiLK: skvector.h e2e369df9c53 2017-06-23 14:59:01Z mthomas $");

#include <silk/silk_types.h>

/**
 *  @file
 *
 *    Implementation of sk_vector_t, a simple growable array.
 *
 *    Elements in a vector are accessed by a numeric index.  The
 *    minimum index is 0.
 *
 *    The size of an individual element in the vector is specified
 *    when the vector is created.  This is the element_size.
 *
 *    Operations that add and get elements to and from the vector copy
 *    data in multiples of the element_size.
 *
 *    A vector has a maximum number of items it can hold without
 *    needing to reallocate its iternal memory.  This is the capacity.
 *    Appending an item to the vector automatically grows the capacity
 *    as needed, but other functions that insert into the vector do
 *    not modify the capacity.
 *
 *    A vector also knows the numeric index of the last element in its
 *    internal memory.  One more than this value is the count of
 *    elements in the vector.
 *
 *    The functions in this file exit when memory cannot be allocated.
 *
 *    This file is part of libsilk.
 */

/* typedef struct sk_vector_st sk_vector_t; // silk_types.h */


/**
 *    Creates a new vector where the size of each element is
 *    'element_size' bytes.
 *
 *    Does not allocate space for the elements; that is, the initial
 *    capacity of the vector is 0.
 *
 *    Returns the new vector.  Returns NULL if 'element_size' is 0.
 *    Exits on allocation error.
 *
 *    The caller should use sk_vector_destroy() to free the vector
 *    once it is no longer needed.
 *
 *    Other functions that create a new vector are
 *    sk_vector_create_from_array() and sk_vector_clone().
 */
sk_vector_t *
sk_vector_create(
    size_t              element_size);

#define skVectorNew(element_size)               \
    sk_vector_create(element_size)


/**
 *    Creates a new vector where the size of each element is
 *    'element_size' bytes, allocates enough space for 'count'
 *    elements, and copies ('count' * 'element_size') bytes from
 *    'array' into the vector.  This is equivalent to calling
 *    sk_vector_create() and sk_vector_append_from_array().
 *
 *    Returns NULL when 'element_size' is 0.  When 'array' is NULL or
 *    'count' is 0, equivalent to sk_vector_create(element_size).
 *
 *    The caller should use sk_vector_destroy() to free the vector
 *    once it is no longer needed.
 *
 *    Returns the new vector.  Exits on allocation error.
 *
 *    Other functions that create a new vector are sk_vector_create()
 *    and sk_vector_clone().
 */
sk_vector_t *
sk_vector_create_from_array(
    size_t              element_size,
    const void         *array,
    size_t              count);

#define skVectorNewFromArray(element_size, array, count)        \
    sk_vector_create_from_array(element_size, array, count)


/**
 *    Creates a new vector having the same element size as vector 'v',
 *    copies the contents of 'v' into it, and returns the new vector.
 *    The capacity of the new vector is set to the count of the number
 *    of elements in 'v'.
 *
 *    Returns the new vector.  Exits on allocation error.
 *
 *    The caller should use sk_vector_destroy() to free the vector
 *    once it is no longer needed.
 *
 *    Other functions that create a new vector are sk_vector_create()
 *    and sk_vector_create_from_array().
 */
sk_vector_t *
sk_vector_clone(
    const sk_vector_t  *v);

#define skVectorClone(v)                        \
    sk_vector_clone(v)


/**
 *    Destroys the vector, v, freeing all memory that the vector
 *    manages.  Does nothing if 'v' is NULL.
 *
 *    If the vector contains pointers, it is the caller's
 *    responsibility to free the elements of the vector before
 *    destroying it.
 */
void
sk_vector_destroy(
    sk_vector_t        *v);

#define skVectorDestroy(v)                      \
    sk_vector_destroy(v)


/**
 *    Sets the capacity of the vector 'v' to 'capacity', growing or
 *    shrinking the spaced allocated for the elements as required.
 *
 *    When shrinking a vector that contains pointers, it is the
 *    caller's responsibility to free the items before changing its
 *    capacity.
 *
 *    Returns 0 on success.  Exits on allocation error.
 *
 *    See also sk_vector_get_capacity().
 */
int
sk_vector_set_capacity(
    sk_vector_t        *v,
    size_t              capacity);

#define skVectorSetCapacity(v, capacity)        \
    sk_vector_set_capacity(v, capacity)


/**
 *    Zeroes all memory for the elements of the vector 'v' and sets
 *    the count of elements in the vector to zero.  Does not change
 *    the capacity of the vector.
 *
 *    If the vector contains pointers, it is the caller's
 *    responsibility to free the elements of the vector before
 *    clearing it.
 */
void
sk_vector_clear(
    sk_vector_t        *v);

#define skVectorClear(v)                        \
    sk_vector_clear(v)


/**
 *    Returns the element size that was specified when the vector 'v'
 *    was created via sk_vector_create() or
 *    sk_vector_create_from_array().
 */
size_t
sk_vector_get_element_size(
    const sk_vector_t  *v);

#define skVectorGetElementSize(v)               \
    sk_vector_get_element_size(v)


/**
 *    Returns the capacity of the vector 'v', i.e., the number of
 *    elements the vector can hold without requiring a re-allocation.
 *
 *    The functions sk_vector_insert_value() and sk_vector_set_value()
 *    return -1 when their 'position' argument is not less than the
 *    value returned by this function.
 *
 *    See also sk_vector_set_capacity().
 */
size_t
sk_vector_get_capacity(
    const sk_vector_t  *v);

#define skVectorGetCapacity(v)                  \
    sk_vector_get_capacity(v)


/**
 *    Returns the number elements that have been added to the vector
 *    'v'.  (Technically, returns one more than the highest position
 *    currently in use in 'v'.)
 *
 *    The functions sk_vector_get_value() and
 *    sk_vector_get_value_pointer() return -1 when their 'position'
 *    argument is not less than the value returned by this function.
 */
size_t
sk_vector_get_count(
    const sk_vector_t  *v);

#define skVectorGetCount(v)                     \
    sk_vector_get_count(v)


/**
 *    Copies the data at 'value' into the vector 'v' at position
 *    sk_vector_get_count(v).
 *
 *    Equivalent to the following, except this function increases the
 *    capacity of the vector as needed:
 *
 *    sk_vector_set_value(v, sk_vector_get_count(v), value);
 *
 *    Returns 0.  Exits on allocation error.
 */
int
sk_vector_append_value(
    sk_vector_t        *v,
    const void         *value);

#define skVectorAppendValue(v, value)           \
    sk_vector_append_value(v, value)


/**
 *    Copies the data from 'src' into the vector 'dst' at position
 *    sk_vector_get_count(v).
 *
 *    Returns 0 on success.  Returns -1 if 'src' and 'dst' do not have
 *    the same element size.  Exits on allocation error.
 */
int
sk_vector_append_vector(
    sk_vector_t        *dst,
    const sk_vector_t  *src);

#define skVectorAppendVector(dst, src)          \
    sk_vector_append_vector(dst, src)


/**
 *    Copies 'count' elements from 'array' into the vector 'v' at
 *    position sk_vector_get_count(v).  Assumes the size of the
 *    elements in 'array' are sk_vector_get_element_size(v).
 *
 *    Returns 0 on success.  Exits on allocation error.
 *
 *    See also sk_vector_append_value() and
 *    sk_vector_create_from_array().
 */
int
sk_vector_append_from_array(
    sk_vector_t        *v,
    const void         *array,
    size_t              count);

#define skVectorAppendFromArray(v, array, count)        \
    sk_vector_append_from_array(v, array, count)


/**
 *    Copies the data at 'value' into the vector 'v' at 'position',
 *    where 0 denotes the first position in the vector.
 *
 *    If 'position' is less than the value sk_vector_get_count(), the
 *    previous element at that position is overwritten.  If the vector
 *    contains pointers, it is the caller's responsibility to free the
 *    element at that position prior to overwriting it.
 *
 *    Use sk_vector_insert_value() to insert a value at a position
 *    without overwriting the existing data.
 *
 *    The value 'position' must be within the current capacity of the
 *    vector (that is, less than the value sk_vector_get_capacity())
 *    since the vector will not grow to support data at 'position'.
 *    If 'position' is too large, -1 is returned.
 *
 *    When 'position' is greater than or equal to
 *    sk_vector_get_count() and less than sk_vector_get_capacity(),
 *    the count of elements in 'v' is set to 1+'position' and the
 *    bytes for the elements from sk_vector_get_count() to 'position'
 *    are set to '\0'.
 *
 *    Return 0 on success or -1 if 'position' is too large.
 */
int
sk_vector_set_value(
    sk_vector_t        *v,
    size_t              position,
    const void         *value);

#define skVectorSetValue(v, position, value)    \
    sk_vector_set_value(v, position, value)


/**
 *    Copies the data at 'value' into the vector 'v' at position
 *    'position', where 0 is the first position in the vector.
 *
 *    If 'position' is less than sk_vector_get_count(v), elements from
 *    'position' to sk_vector_get_count(v) are moved one location
 *    higher, increasing the capacity of the vector if necessary.
 *
 *    When 'position' is not less than sk_vector_get_count(v), this
 *    function is equivalent to sk_vector_set_value(v, position,
 *    value), which requires 'position' to be within the current
 *    capacity of the vector.  See sk_vector_set_value() for details.
 *
 *    Returns 0 on success..  Returns -1 if 'position' is not less
 *    than sk_vector_get_capacity().  Exits on allocation error.
 */
int
sk_vector_insert_value(
    sk_vector_t        *v,
    size_t              position,
    const void         *value);

#define skVectorInsertValue(v, position, value) \
    sk_vector_insert_value(v, position, value)


/**
 *    Copies the data in vector 'v' at 'position' to the location
 *    pointed at by 'out_element' and then removes that element from
 *    the vector.  The first position in the vector is position 0.
 *    The 'out_element' parameter may be NULL.
 *
 *    All elements in 'v' from 'position' to sk_vector_get_count(v)
 *    are moved one location lower.  If the vector contains pointers,
 *    it is the caller's responsibility to free the removed item.
 *    Does not change the capacity of the vector.
 *
 *    Returns 0 on success.  Returns -1 if 'position' is not less than
 *    sk_vector_get_count(v).
 *
 *    Use sk_vector_get_value() to get a value without removing it
 *    from the vector.
 */
int
sk_vector_remove_value(
    sk_vector_t        *v,
    size_t              position,
    void               *out_element);

#define skVectorRemoveValue(v, position, out_element)   \
    sk_vector_remove_value(v, position, out_element)


/**
 *    Copies the data in vector 'v' at 'position' to the location
 *    pointed at by 'out_element'.  The first position in the vector
 *    is position 0.  Returns 0 on success, or -1 if 'position' is
 *    not less than sk_vector_get_count(v).
 *
 *    It is the caller's responsibility to ensure that the location
 *    referenced by 'out_element' can hold a value of size
 *    sk_vector_get_element_size(v).
 *
 *    See also sk_vector_get_value_pointer(), which returns a pointer
 *    to the element without needing to copy the contents of the
 *    element.
 *
 *    To get multiple values from a vector, consider using
 *    sk_vector_get_multiple_values(), sk_vector_to_array(), or
 *    sk_vector_to_array_alloc().  This function is equivalent to
 *
 *    -1 + sk_vector_get_multiple_values(v, position, out_element, 1)
 *
 *    To get a value and also remove it from the vector, use
 *    sk_vector_remove_value().
 */
int
sk_vector_get_value(
    const sk_vector_t  *v,
    size_t              position,
    void               *out_element);

/* note argument order change */
#define skVectorGetValue(out_element, v, position)      \
    sk_vector_get_value(v, position, out_element)


/**
 *    Returns a pointer to the data item in vector 'v' at 'position'.
 *    The first position in the vector is position 0.  Returns NULL if
 *    'position' is not less than sk_vector_get_count(v).
 *
 *    The caller should not cache this value, since any addition to
 *    the vector may result in a re-allocation that could make the
 *    pointer invalid.
 *
 *    See also sk_vector_get_value().
 */
void *
sk_vector_get_value_pointer(
    const sk_vector_t  *v,
    size_t              position);

#define skVectorGetValuePointer(v, position)    \
    sk_vector_get_value_pointer(v, position)


/**
 *    Copies up to 'num_elements' data elements starting at
 *    'start_position' from vector 'v' to the location pointed at by
 *    'out_array'.  The first position in the vector is position 0.
 *
 *    It is the caller's responsibility to ensure that 'out_array' can
 *    hold 'num_elements' elements of size
 *    sk_vector_get_element_size(v).
 *
 *    Returns the number of elements copied into the array.  Returns
 *    fewer than 'num_elements' if the end of the vector is reached.
 *    Returns 0 if 'start_position' is not less than
 *    sk_vector_get_count(v).
 *
 *    See also sk_vector_to_array() and sk_vector_to_array_alloc().
 */
size_t
sk_vector_get_multiple_values(
    const sk_vector_t  *v,
    size_t              start_position,
    void               *out_array,
    size_t              num_elements);

/* note argument order change */
#define skVectorGetMultipleValues(out_array, v, start_position, num_elements) \
    sk_vector_get_multiple_values(v, start_position, out_array, num_elements)



/**
 *    Copies the data in the vector 'v' to the C-array 'out_array'.
 *    This is equivalent to:
 *
 *    sk_vector_get_multiple_values(v, 0, out_array, sk_vector_get_count(v));
 *
 *    It is the caller's responsibility to ensure that 'out_array' is
 *    large enough to hold sk_vector_get_count(v) elements of size
 *    sk_vector_get_element_size(v).
 *
 *    See also sk_vector_to_array_alloc().
 */
void
sk_vector_to_array(
    const sk_vector_t  *v,
    void               *out_array);

/* note argument order change */
#define skVectorToArray(out_array,  v)          \
    sk_vector_to_array(v, out_array)



/**
 *    Allocates an array large enough to hold all the elements of the
 *    vector 'v', copies the elements from 'v' into the array, and
 *    returns the array.
 *
 *    The caller must free() the array when it is no longer required.
 *
 *    Returns NULL if the vector is empty.  Exits if memory cannot be
 *    allocated.
 *
 *    This function is equivalent to:
 *
 *    a = malloc(sk_vector_get_element_size(v) * sk_vector_get_count(v));
 *    sk_vector_get_multiple_values(v, 0, a, sk_vector_get_count(v));
 */
void *
sk_vector_to_array_alloc(
    const sk_vector_t  *v);

#define skVectorToArrayAlloc(v)                 \
    sk_vector_to_array_alloc(v)


#ifdef __cplusplus
}
#endif
#endif /* _SKVECTOR_H */

/*
** Local Variables:
** mode:c
** indent-tabs-mode:nil
** c-basic-offset:4
** End:
*/
