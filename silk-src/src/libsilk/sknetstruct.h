/*
** Copyright (C) 2005-2020 by Carnegie Mellon University.
**
** @OPENSOURCE_LICENSE_START@
** See license information in ../../LICENSE.txt
** @OPENSOURCE_LICENSE_END@
*/

/*
**  sknetstruct.h
**
**    Utilities used by IPsets and Bags to group IPs into arbitrarily
**    sized netblocks for printing.  Each netblock keeps a count of
**    the number of smaller netblocks seen.  In the case of Bags, each
**    netblock sums the counters for the entries in that netblock.
**
*/
#ifndef _SKNETSTRUCT_H
#define _SKNETSTRUCT_H
#ifdef __cplusplus
extern "C" {
#endif

#include <silk/silk.h>

RCSIDENTVAR(rcsID_SKNETSTRUCT_H, "$SiLK: sknetstruct.h ef14e54179be 2020-04-14 21:57:45Z mthomas $");

#include <silk/silk_types.h>

/**
 *    The context object for processing IP addresses.
 */
typedef struct sk_netstruct_st sk_netstruct_t;
typedef struct sk_netstruct_st skNetStruct_t    SK_GCC_DEPRECATED;

/**
 *    Add the CIDR block 'ipaddr'/'prefix' to the network structure
 *    context object 'ns'.  It is an error to call this function on a
 *    network structure object configured to process counters.
 */
void
skNetStructureAddCIDR(
    sk_netstruct_t     *ns,
    const skipaddr_t   *ipaddr,
    uint32_t            prefix);

/**
 *    Add the ('ipaddr', 'counter') pair to the network structure
 *    context object 'ns'.  It is an error to call this function on a
 *    network structure object that is not configured to process
 *    counters.
 */
void
skNetStructureAddKeyCounter(
    sk_netstruct_t     *ns,
    const skipaddr_t   *ipaddr,
    const uint64_t     *counter);

/**
 *    Creates a new context object for processing IP addresses and
 *    stores that object in the location specified by 'ns'.
 *
 *    When 'has_count' is non-zero, the context object is configured
 *    to work with Bag files, and the caller must use
 *    skNetStructureAddKeyCounter() to add new (IP,counter) pairs to
 *    the context object for printing.
 *
 *    When 'has_count' is zero, the context object is configured to
 *    work with IPset files and the caller must use
 *    skNetStructureAddCIDR() to add a new CIDR block to the context
 *    object for printing.
 *
 *    Once all IPs have been processed, the caller must invoke
 *    skNetStructurePrintFinalize() to close any netblock that is
 *    still open and to print the total.
 *
 *    Text is printed in pipe-delimited columns by default.
 *
 *    By default, the context object prints to standard output.
 *
 *    Whether the network structure groups the IPs into IPv4 or IPv6
 *    netblocks is determined by the input passed to
 *    skNetStructureParse().  The default is to use the IPv4
 *    netblocks.
 *
 *    When configured to process IPv4 addresses, hosts are grouped by
 *    the /8, /16, /24, and /27 netblocks by default.  This may be
 *    changed by calling skNetStructureParse().
 *
 *    When configured to process IPv6 addresses, hosts are grouped by
 *    the /48 and /64 netblocks.  This may be changed by calling
 *    skNetStructureParse().
 *
 *    The default output prints the number of unique hosts seen and
 *    the number of each of the above netblocks that were seen.
 *
 *
 */
int
skNetStructureCreate(
    sk_netstruct_t    **ns,
    int                 has_count);

/**
 *    Destroy the network structure context object pointed at by 'ns'
 *    and set 'ns' to NULL.  Does nothing if 'ns' or *ns is NULL.
 */
void
skNetStructureDestroy(
    sk_netstruct_t    **ns);

/**
 *    Have the network structure context object 'ns' parse the user's
 *    configuration setting in input.  The input configures whether
 *    the network structure context object groups into IPv4 or IPv6
 *    netblocks and whether the be counted and/or printed.
 */
int
skNetStructureParse(
    sk_netstruct_t     *ns,
    const char         *input);

/**
 *    Tell the network structure context object 'ns' that all IPs have
 *    been added and that it should finalize its output by closing any
 *    open netblocks and printing the results.
 */
void
skNetStructurePrintFinalize(
    sk_netstruct_t     *ns);

/**
 *    Configure the network structure context object 'ns' to use
 *    'width' as the width of the column that contains the counter
 *    sum.  The value is only used when processing Bag files.
 */
void
skNetStructureSetCountWidth(
    sk_netstruct_t     *ns,
    int                 width);

/**
 *    Configure the network structure context object 'ns' to print
 *    'delimiter' between columns and at the end of each row.
 */
void
skNetStructureSetDelimiter(
    sk_netstruct_t     *ns,
    char                delimiter);

/**
 *    Configure the network structure context object 'ns' so it uses
 *    'format' when printing IP addresses, where 'format' will be
 *    passed to the skipaddrString() function.
 */
void
skNetStructureSetIpFormat(
    sk_netstruct_t     *ns,
    uint32_t            format);

/**
 *    Configure the network structure context object 'ns' so it does
 *    not print the data in columns.
 */
void
skNetStructureSetNoColumns(
    sk_netstruct_t     *ns);

/**
 *    Configure the network structure context object 'ns' so it does
 *    not print the final delimiter on each row.
 */
void
skNetStructureSetNoFinalDelimiter(
    sk_netstruct_t     *ns);

/**
 *    Configure the network structure context object 'ns' to send its
 *    output to 'stream'.
 */
void
skNetStructureSetOutputStream(
    sk_netstruct_t     *ns,
    skstream_t         *stream);

#ifdef __cplusplus
}
#endif
#endif /* _SKNETSTRUCT_H */

/*
** Local Variables:
** mode:c
** indent-tabs-mode:nil
** c-basic-offset:4
** End:
*/
