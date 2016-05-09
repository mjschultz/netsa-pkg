/*
** Copyright (C) 2001-2016 by Carnegie Mellon University.
**
** @OPENSOURCE_LICENSE_START@
** See license information in ../../LICENSE.txt
** @OPENSOURCE_LICENSE_END@
*/
#ifndef _SILK_FILES_H
#define _SILK_FILES_H
#ifdef __cplusplus
extern "C" {
#endif

#include <silk/silk.h>

RCSIDENTVAR(rcsID_SILK_FILES_H, "$SiLK: silk_files.h 85572f89ddf9 2016-05-05 20:07:39Z mthomas $");


/* Special compression method values */
#define SK_COMPMETHOD_DEFAULT 255
#define SK_COMPMETHOD_BEST    254

/* The available compression methods */
#define SK_COMPMETHOD_NONE      0
#define SK_COMPMETHOD_ZLIB      1
#define SK_COMPMETHOD_LZO1X     2
/* when you add a compression method, add it to the following array */

#ifdef SKSITE_SOURCE
static const char *skCompressionMethods[] = {
    "none",
    "zlib",
    "lzo1x",
    ""
};
#endif /* SKSITE_SOURCE */


/* define various output file formats here that we write to disk */

#define FT_TCPDUMP          0x00
#define FT_GRAPH            0x01
#define FT_ADDRESSES        0x02        /* old address array used by addrtype */
#define FT_PORTMAP          0x03
#define FT_SERVICEMAP       0x04
#define FT_NIDSMAP          0x05
#define FT_EXPERIMENT1      0x06        /* free for all ID */
#define FT_EXPERIMENT2      0x07        /* free for all ID */
#define FT_TEMPFILE         0x08
#define FT_RESERVED_09      0x09
#define FT_IPFIX            0x0A
#define FT_RWIPV6           0x0B
#define FT_RWIPV6ROUTING    0x0C
#define FT_RWAUGSNMPOUT     0x0D
#define FT_RWAUGROUTING     0x0E
#define FT_RESERVED_0F      0x0F
#define FT_RWROUTED         0X10    /* raw routed */
#define FT_RWNOTROUTED      0X11    /* raw not routed */
#define FT_RWSPLIT          0X12    /* raw split after r/nr */
#define FT_RWFILTER         0X13    /* raw split after r/nr */
#define FT_RWAUGMENTED      0X14
#define FT_RWAUGWEB         0X15
#define FT_RWGENERIC        0x16
#define FT_RESERVED_17      0x17
#define FT_RWDAILY          0x18
#define FT_RWSCAN           0x19
#define FT_RWACL            0x1A
#define FT_RWCOUNT          0x1B
#define FT_FLOWCAP          0x1C
#define FT_IPSET            0x1D
#define FT_TAGTREE          0x1E
#define FT_RWWWW            0x1F
#define FT_SHUFFLE          0x20
#define FT_RWBAG            0x21
#define FT_BLOOM            0x22
#define FT_RWPRINTSTATS     0x23
#define FT_PDUFLOWCAP       0x24
#define FT_PREFIXMAP        0x25
/* When you add new types here; add the name to the array below. */

/* old identifier names */
#define FT_IPTREE           FT_IPSET
#define FT_MACROBAGTREE     FT_RWBAG


/*
 *   This header is included by sksite.c after declaring
 *   SKSITE_SOURCE.  Users should use the function
 *   calls from sksite.h to access these strings values.
 */

#ifdef SKSITE_SOURCE
static const char *fileOutputFormats[] = {
    /* 0x00 */  "FT_TCPDUMP",
    /* 0x01 */  "FT_GRAPH",
    /* 0x02 */  "FT_ADDRESSES",
    /* 0x03 */  "FT_PORTMAP",
    /* 0x04 */  "FT_SERVICEMAP",
    /* 0x05 */  "FT_NIDSMAP",
    /* 0x06 */  "FT_EXPERIMENT1",
    /* 0x07 */  "FT_EXPERIMENT2",
    /* 0x08 */  "FT_TEMPFILE",
    /* 0x09 */  "FT_RESERVED_09",
    /* 0x0A */  "FT_IPFIX",
    /* 0x0B */  "FT_RWIPV6",
    /* 0x0C */  "FT_RWIPV6ROUTING",
    /* 0x0D */  "FT_RWAUGSNMPOUT",
    /* 0x0E */  "FT_RWAUGROUTING",
    /* 0x0F */  "FT_RESERVED_0F",
    /* 0X10 */  "FT_RWROUTED",
    /* 0X11 */  "FT_RWNOTROUTED",
    /* 0X12 */  "FT_RWSPLIT",
    /* 0X13 */  "FT_RWFILTER",
    /* 0X14 */  "FT_RWAUGMENTED",
    /* 0X15 */  "FT_RWAUGWEB",
    /* 0x16 */  "FT_RWGENERIC",
    /* 0x17 */  "FT_RESERVED_17",
    /* 0x18 */  "FT_RWDAILY",
    /* 0x19 */  "FT_RWSCAN",
    /* 0x1A */  "FT_RWACL",
    /* 0x1B */  "FT_RWCOUNT",
    /* 0x1C */  "FT_FLOWCAP",
    /* 0x1D */  "FT_IPSET",
    /* 0x1E */  "FT_TAGTREE",
    /* 0x1F */  "FT_RWWWW",
    /* 0x20 */  "FT_SHUFFLE",
    /* 0x21 */  "FT_RWBAG",
    /* 0x22 */  "FT_BLOOM",
    /* 0x23 */  "FT_RWPRINTSTATS",
    /* 0x24 */  "FT_PDUFLOWCAP",
    /* 0x25 */  "FT_PREFIXMAP",
    ""
};
#endif /* SKSITE_SOURCE */

#ifdef __cplusplus
}
#endif
#endif  /* _SILK_FILES_H */

/*
** Local Variables:
** mode:c
** indent-tabs-mode:nil
** c-basic-offset:4
** End:
*/
