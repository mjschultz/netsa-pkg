/*
** Copyright (C) 2001-2017 by Carnegie Mellon University.
**
** @OPENSOURCE_LICENSE_START@
** See license information in ../../LICENSE.txt
** @OPENSOURCE_LICENSE_END@
*/
#ifndef _SKFGLOB_H
#define _SKFGLOB_H
#ifdef __cplusplus
extern "C" {
#endif

#include <silk/silk.h>

RCSIDENTVAR(rcsID_SKFGLOB_H, "$SiLK: skfglob.h efd886457770 2017-06-21 18:43:23Z mthomas $");

#include <silk/silk_types.h>

/**
 *  @file
 *
 *    An interface for getting a list of hourly data files from the
 *    SiLK repository.  Used by rwfilter and rwfglob.
 *
 *    This file is part of libsilk.
 *
 */


/* TYPEDEFS AND DEFINES */

/**
 *    The file globbing state and iterator.
 */
typedef struct sk_fglob_st sk_fglob_t;


/* FUNCTION DECLARATIONS */

/**
 *    Create and initialize the values in the file globbing structure
 *    that 'fglob' refers to.  Register the fglob options.  Check the
 *    environment for the variable giving the location of the data
 *    repository.
 *
 *    Return 0 if OK.  Return -1 on configuration error.  Exit on
 *    memory allocation failure.
 */
int
skFGlobCreate(
    sk_fglob_t        **fglob);


/**
 *    Print, to the given file handle, the usage for the command-line
 *    switches provided by the fglob library.
 *
 *    Must call skFGlobCreate() prior to calling this function.
 */
void
skFGlobUsage(
    const sk_fglob_t   *fglob,
    FILE               *fh);


/**
 *    Destroy all file globbing state in the referent of 'fglob'.  Do
 *    nothing if fglob or its referent are NULL.
 */
void
skFGlobDestroy(
    sk_fglob_t        **fglob);


/**
 *    Fill the buffer 'buf' of size 'bufsize' with the name of next
 *    available file and return a pointer to 'buf'.  Return NULL if
 *    all files have been processed.
 *
 *    This function initializes the internal fglob iterator if its
 *    uninitialized.  If that operation fails, this function returns
 *    NULL.
 */
char *
skFGlobNext(
    sk_fglob_t         *fglob,
    char               *buf,
    size_t              bufsize);


/**
 *    Return an estimate (an upper bound) of the number of files
 *    remaining to process.  The returned value assumes that a file
 *    exists for every valid hour-flowtype-sensor tuple which is left
 *    to process.
 *
 *    This function initializes the internal fglob iterator if its
 *    uninitialized.  If that operation fails, this function returns
 *    -1.
 */
int
skFGlobFileCount(
    sk_fglob_t         *fglob);


/**
 *    Determine whether use of the file globbing routines was
 *    requested and whether valid options were given.
 *
 *    Return 1 if file globbing command line options were given and
 *    all arguments are valid.
 *
 *    Return 0 if options were not given or if the fglob parameter is
 *    NULL.
 *
 *    Return -1 if there errors occurred while parsing the with
 *    classes, types, sensors, or times.
 */
int
skFGlobValid(
    sk_fglob_t         *fglob);


/**
 *    Assume fglob file selection switches should instead be used as
 *    file partitioning ("filtering") switches and create bitmaps to
 *    hold the arguments to those switches.  Return 0 if no bitmaps
 *    are created, a positive value (as described below) if bitmaps
 *    are created, and a negative value on error.
 *
 *    When the user has explicitly selected sensors, create a bitmap
 *    capable of holding all sensor-ids, set to high the values that
 *    the fglob library would iterate over given the command line
 *    options, set the referent of 'sensor_bitmap' to the bitmap, and
 *    add one to the return value.
 *
 *    When the user has explicitly selected flowtypes, create a bitmap
 *    capable of holding all flowtype-ids, set to high the bits that
 *    correspond to the class/type pairs that the fglob library would
 *    iterate over given the command line options, set the referent of
 *    'file_info_bitmap' to the bitmap, and add two to the return
 *    value.
 *
 *    This function may be used when filtering a previous data pull.
 *    It allows the --class, --type, --flowtypes, and --sensor
 *    switches to work over this data.
 *
 *    This function should be called before calling skFGlobValid() as
 *    this function causes skFGlobValid() not to consider the --class,
 *    --type, --flowtypes, and --sensor switches when determining
 *    whether the fglob library should consider itself valid.
 *
 *    This function initializes the internal fglob iterator if its
 *    uninitialized.  If that operation fails, this function returns
 *    -1.
 */
int
skFGlobSetFilters(
    sk_fglob_t         *fglob,
    sk_bitmap_t       **sensor_bitmap,
    sk_bitmap_t       **flowtype_bitmap);


#ifdef __cplusplus
}
#endif
#endif /* _SKFGLOB_H */

/*
** Local Variables:
** mode:c
** indent-tabs-mode:nil
** c-basic-offset:4
** End:
*/
