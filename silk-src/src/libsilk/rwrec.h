/*
** Copyright (C) 2001-2017 by Carnegie Mellon University.
**
** @OPENSOURCE_LICENSE_START@
** See license information in ../../LICENSE.txt
** @OPENSOURCE_LICENSE_END@
*/

/*
 *  rwrec.h
 *
 *    The SiLK Flow record (rwRec) definition and functions/macros for
 *    manipulating it.
 *
 */
#ifndef _RWREC_H
#define _RWREC_H
#ifdef __cplusplus
extern "C" {
#endif

#include <silk/silk.h>

RCSIDENTVAR(rcsID_RWREC_H, "$SiLK: rwrec.h efd886457770 2017-06-21 18:43:23Z mthomas $");

#include <silk/silk_types.h>
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

#if SK_HAVE_INLINE && defined(SOURCE_FILE_RWREC_C)
#undef  SK_EXTERN
#define SK_EXTERN extern
#elif !defined(SK_EXTERN)
#define SK_EXTERN
#endif


#ifndef RWREC_OPAQUE
#  define RWREC_OPAQUE 0
/*#  include <silk/skipaddr.h> */
#endif


/**
 *    An identifier for each field
 */
enum rwrec_field_id_en {
    RWREC_FIELD_SIP = 0,
    RWREC_FIELD_DIP = 1,
    RWREC_FIELD_SPORT = 2,
    RWREC_FIELD_DPORT = 3,
    RWREC_FIELD_PROTO = 4,
    RWREC_FIELD_PKTS = 5,
    RWREC_FIELD_BYTES = 6,
    RWREC_FIELD_FLAGS = 7,
    RWREC_FIELD_STIME = 8,
    RWREC_FIELD_ELAPSED = 9,
    RWREC_FIELD_ETIME = 10,
    RWREC_FIELD_SID = 11,
    RWREC_FIELD_INPUT = 12,
    RWREC_FIELD_OUTPUT = 13,
    RWREC_FIELD_NHIP = 14,
    RWREC_FIELD_INIT_FLAGS = 15,
    RWREC_FIELD_REST_FLAGS = 16,
    RWREC_FIELD_TCP_STATE = 17,
    RWREC_FIELD_APPLICATION = 18,
    RWREC_FIELD_FTYPE_CLASS = 19,
    RWREC_FIELD_FTYPE_TYPE = 20,
    RWREC_FIELD_ICMP_TYPE = 21,
    RWREC_FIELD_ICMP_CODE = 22
};
typedef enum rwrec_field_id_en rwrec_field_id_t;

/**
 *    Number of field rwrec_field_id_t entries.
 *
 *    Since rwrec_field_id_t starts at 0, this is one more than the
 *    last ID in that enum.
 *
 *    (This needs a new name to distinguish it from the names defined
 *    in the rwrec_field_id_t enum.)
 */
#define RWREC_FIELD_ID_COUNT  23


/*
 *    Return a true value if port 'p' is a "web" port; false otherwise
 */
#define SK_WEBPORT_CHECK(p) ((p) == 80 || (p) == 443 || (p) == 8080)


/*
 *  This is the generic SiLK Flow record returned from ANY file format
 *  containing packed SiLK Flow records.
 */
struct rwGenericRec_V6 {
#if RWREC_OPAQUE && !defined(RWREC_DEFINE_BODY)
    uint8_t         ar[120];
#else
    int64_t         sTime;       /*   0-  7  Flow start time in milliseconds
                                  *          since UNIX epoch */

    int64_t         eTime;       /*   8- 16  Flow end time in milliseconds */

    uint16_t        sPort;       /*  16- 17  Source port */
    uint16_t        dPort;       /*  18- 19  Destination port */

    uint8_t         proto;       /*  20      IP protocol */
    sk_flowtype_id_t flow_type;  /*  21      Class & Type info */
    sk_sensor_id_t  sID;         /*  22- 23  Sensor ID */

    uint8_t         flags;       /*  24      OR of all flags (Netflow flags) */
    uint8_t         init_flags;  /*  25      TCP flags in first packet
                                  *          or blank for "legacy" data */
    uint8_t         rest_flags;  /*  26      TCP flags on non-initial packet
                                  *          or blank for "legacy" data */
    uint8_t         tcp_state;   /*  27      TCP state machine info (below) */

    uint16_t        application; /*  28- 29  "Service" port set by collector */
    uint16_t        memo;        /*  30- 31  Application specific field */

    uint64_t        pkts;        /*  32- 39  Count of packets */

    uint64_t        bytes;       /*  40- 47  Count of bytes */

    uint32_t        input;       /*  48- 51  Router incoming SNMP interface */
    uint32_t        output;      /*  52- 55  Router outgoing SNMP interface */

    skIPUnion_t     sIP;         /*  56- 71  (IPv4 in 56-59) Source IP */
    skIPUnion_t     dIP;         /*  72- 87  (IPv4 in 72-75) Destination IP */
    skIPUnion_t     nhIP;        /*  88-103  (IPv4 in 88-91) Next Hop IP */

    int64_t         sidecar;     /* 104-111  Lua reference */
    lua_State      *lua_state;   /* 112-119  Lua state */
#endif  /* RWREC_OPAQUE && !defined(RWREC_DEFINE_BODY) */
};
typedef struct rwGenericRec_V6 rwGenericRec_V6;
/* typedef struct rwGenericRec_V6 rwRec;       // silk_types.h */


/*
 *  Values for tcp_state value in rwGeneric and packed formats
 */

/* No additional TCP-state machine information is available */
#define SK_TCPSTATE_NO_INFO               0x00

/* Expanded TCP-flags: This bit must be set if and only if the flow is
 * TCP and the init_flags and rest_flags fields are valid.  */
#define SK_TCPSTATE_EXPANDED              0x01

/* Flow received packets following the FIN packet that were not ACK or
 * RST packets. */
#define SK_TCPSTATE_FIN_FOLLOWED_NOT_ACK  0x08

/* Flow has packets all of the same size */
#define SK_TCPSTATE_UNIFORM_PACKET_SIZE   0x10

/* Flow ends prematurely due to a timeout by the collector. */
#define SK_TCPSTATE_TIMEOUT_KILLED        0x20

/* Flow is a continuation of a previous flow that was killed
 * prematurely due to a timeout by the collector. */
#define SK_TCPSTATE_TIMEOUT_STARTED       0x40

/* Define a mask that returns the defined bits in tcpstate.  This is
 * used internally by rwRecGetTcpState(), rwRecSetTcpState() */
#define SK_TCPSTATE_MASK                  0x79

/* Define a mask that returns the attribute bits in tcpstate. */
#define SK_TCPSTATE_ATTRIBUTE_MASK        0x78

/* Note: the most significant bit of tcp_state (0x80) is used as a
 * flag to mark a record as having IPv6 addresses. */


/*
 *    Flags to use for rwRecCopy()
 */

/**
 *    When this value is included in the 'flags' paraemter of
 *    rwRecCopy(), the sidecar data on the source and destination
 *    records is completely ignored.  That is, the copy behaves as it
 *    did in SiLK 3.x.
 */
#define SK_RWREC_COPY_FIXED     (1 << 0)

/**
 *    When this value is included in the 'flags' paraemter of
 *    rwRecCopy(), the function treats the destination record as
 *    uninitialized memory and does not check to see if it contains
 *    sidecar data.
 */
#define SK_RWREC_COPY_UNINIT    (1 << 1)

/**
 *    When this value is included in the 'flags' paraemter of
 *    rwRecCopy(), the sidecar data "moves" from the source record to
 *    the destination record.
 */
#define SK_RWREC_COPY_MOVE       (1 << 2)


/*
 *    ********************************************************************
 *    Record initialization and copying
 */

SK_EXTERN SK_INLINE
/**
 *    Use this function on uninitialized memory to zero all fields in
 *    the record 'r', set the Lua state to 'lua_state', and set the
 *    Sensor ID, Flowtype, and Sidecar pointer to invalid values.
 *
 *    See also rwRecInitializeArray() and rwRecReset().
 *
 *    For an sk_fixrec_t, use sk_fixrec_init() instead of this macro.
 */
void
rwRecInitialize(
    rwRec              *r,
    lua_State          *lua_state);


/**
 *    Similar to rwRecInitialize(), but works on an array containing a
 *    count of 'array_rec_count' rwRec records.
 */
SK_EXTERN SK_INLINE
void
rwRecInitializeArray(
    rwRec              *rec_array,
    lua_State          *lua_state,
    size_t              array_rec_count);


/**
 *    Copy the rwRec at 'src_rwrec' to the rwRec at 'dst_rwrec'.
 *
 *    The function assumes rwRecInitialize() has been called on both
 *    'src_rwrec' and 'dst_rwrec'.  The default behavior of this
 *    function is to remove any sidecar data from 'dst_rwrec', copy
 *    the fixed-rwRec portion from 'src_rwrec' to 'dst_rwrec', and
 *    then make a complete copy of any sidecar data on 'src_rwrec' to
 *    'dst_rwrec'.
 *
 *    To completely ignore the sidecar data on the source and
 *    destination records, include SK_RWREC_COPY_FIXED in 'flags'.
 *
 *    Existing sidecar data is removed from the destination record
 *    prior to the copy.  To prevent this check, add
 *    SK_RWREC_COPY_UNINIT to 'flags', which treats 'dst_rwrec' as
 *    uninitialized memory.
 *
 *    Adding SK_RWREC_COPY_MOVE to 'flags' causes the sidecar data to
 *    "move" from 'src_rwrec' to 'dst_rwrec'.
 *
 */
SK_EXTERN SK_INLINE
void
rwRecCopy(
    void               *dst_rwrec,
    const void         *src_rwrec,
    unsigned int        flags);


/**
 *    Zero all fields in the record 'r' that was previously
 *    initialized with rwRecInitialize() or rwRecInitializeArray().
 *    Set the Sensor ID and Flowtype to invalid values.  If sidecar
 *    data exists on the record, remove it.
 *
 *    For an sk_fixrec_t, use sk_fixrec_clear() instead of this macro.
 */
SK_EXTERN SK_INLINE
void
rwRecReset(
    rwRec              *r);

/**
 *    Similar to rwRecReset(), but works on an array containing a
 *    count of 'array_rec_count' rwRec records.
 */
SK_EXTERN SK_INLINE
void
rwRecResetArray(
    rwRec              *r,
    size_t              array_rec_count);


/**
 *    Zero out the record including all memory-bits, and set Sensor ID
 *    and Flowtype to invalid values.
 *
 *    For an sk_fixrec_t, use sk_fixrec_clear().
 */
SK_EXTERN SK_INLINE
void
RWREC_CLEAR(
    rwRec              *r);

/**
 *    Copy the rwRec from 'src_rwrec' to 'dst_rwrec'.  Sidecar data is
 *    ignored.
 */
#define RWREC_COPY(dst_rwrec, src_rwrec)                        \
    rwRecCopy((dst_rwrec), (src_rwrec), SK_RWREC_COPY_FIXED)


/*
 *    ********************************************************************
 *    Record state
 */

/**
 *    Return true if record 'r' holds IPv6 addresses, false otherwise.
 */
SK_EXTERN SK_INLINE
int
rwRecIsIPv6(
    const rwRec        *r);

/**
 *    Return true if the record is an ICMP record protocol 1 or
 *    protocol 58 and marked as IPv6.  Return false otherwise.
 */
SK_EXTERN SK_INLINE
int
rwRecIsICMP(
    const rwRec        *r);

/**
 *    Return true if the record can be represented using the SiLK
 *    web-specific file formats, zero otherwise.
 */
SK_EXTERN SK_INLINE
int
rwRecIsWeb(
    const rwRec        *r);

/**
 *    Set record 'r' to be IPv6.
 *
 *    This function does not convert the contained IP addresses.  For
 *    that, use rwRecConvertToIPv6().
 */
SK_EXTERN SK_INLINE
void
rwRecSetIPv6(
    rwRec              *r);

/**
 *    Set record 'r' to be IPv4.
 *
 *    This function does not convert the contained IP addresses.  For
 *    that, use rwRecConvertToIPv4().
 */
SK_EXTERN SK_INLINE
void
rwRecSetIPv4(
    rwRec              *r);

/**
 *    Set record 'r' to be IPv6 and convert the contained IP addresses
 *    to IPv6 (map them into the ::ffff:0:0/96 netblock).
 *
 *    To set an empty record as IPv6, use rwRecSetIPv6().
 */
SK_EXTERN SK_INLINE
void
rwRecConvertToIPv6(
    rwRec              *r);

/**
 *    Set record 'r' to be IPv4 and convert the contained IP addresses
 *    to IPv4.  This function only succeeds (and returns 0) when all
 *    IP addresses are in the ::ffff:0:0/96 netblock.  Return -1 if
 *    any IP address is outside that netblock.
 *
 *    To set an empty record as IPv4, use rwRecSetIPv4().
 */
int
rwRecConvertToIPv4(
    rwRec              *r);



/*
 *    ********************************************************************
 *    Source IP Address (sIP)
 *
 */

/***  Source IP Address (sIP) as skipaddr_t  ***/

/**
 *    Fill referent of 'out_addr' with the source IP address of 'r'.
 */
SK_EXTERN SK_INLINE
void
rwRecMemGetSIP(
    const rwRec        *r,
    skipaddr_t         *out_addr);

/**
 *    Set the source IP address of 'r' to the referent of 'in_addr'.
 */
SK_EXTERN SK_INLINE
void
rwRecMemSetSIP(
    rwRec              *r,
    const skipaddr_t   *in_addr);

/**
 *    Mask the source IP address of 'r' with 'mask_addr'.
 *
 *    If 'r' is IPv6 and 'mask_addr' is IPv4, 'mask_addr' is mapped to
 *    the ::ffff:0:0/96 netblock prior to masking.
 *
 *    If 'r' is IPv4 and 'mask_addr' is IPv6, check whether
 *    'mask_addr' is in the ::ffff:0:0/96 netblock.  If it is, convert
 *    'mask_addr' to IPv4 prior to masking.  Otherwise, convert 'r' to
 *    IPv6 prior to masking.
 */
void
rwRecApplyMaskSIP(
    rwRec              *r,
    const skipaddr_t   *mask_addr);


/***  Source IPv4 Address (sIP) as uint32_t  ***/

/**
 *    Assume record 'r' holds IPv4 addresses and return the source IP
 *    address.
 */
SK_EXTERN SK_INLINE
uint32_t
rwRecGetSIPv4(
    const rwRec        *r);

/**
 *    Assume record 'r' holds IPv4 addresses and set the source IP of
 *    'r' to 'in_v'.
 */
SK_EXTERN SK_INLINE
void
rwRecSetSIPv4(
    rwRec              *r,
    uint32_t            in_v);

/**
 *    Assume record 'r' holds IPv4 addresses and fill the referent of
 *    'out_vp' with the SIPv4 of 'r'.
 */
SK_EXTERN SK_INLINE
void
rwRecMemGetSIPv4(
    const rwRec        *r,
    void               *out_vp);

/**
 *    Assume record 'r' holds IPv4 addresses and set the source IP of
 *    'r' to the referent of 'in_vp'.
 */
SK_EXTERN SK_INLINE
void
rwRecMemSetSIPv4(
    rwRec              *r,
    const void         *in_vp);

/**
 *    Assume record 'r' holds IPv4 addresses and mask the source IP
 *    address of 'r' with 'mask'.
 */
SK_EXTERN SK_INLINE
void
rwRecApplyMaskSIPv4(
    rwRec              *r,
    uint32_t            mask);

/***  Source IPv6 Address (sIP) as uint8_t[16]  ***/

/**
 *    Fill 'out_vp' (whose type is uint8_t[16]) with the source IP
 *    address of 'r'.  If 'r' holds IPv4 addresses, return that
 *    address mapped to the ::ffff:0:0/96 netblock.
 */
SK_EXTERN SK_INLINE
void
rwRecMemGetSIPv6(
    const rwRec        *r,
    void               *out_vp);

/**
 *    Assume record 'r' holds IPv6 addresses and set the source IP of
 *    'r' to 'in_vp' whose type is uint8_t[16].
 */
SK_EXTERN SK_INLINE
void
rwRecMemSetSIPv6(
    rwRec              *r,
    const void         *in_vp);

/**
 *    Assume record 'r' holds IPv6 addresses and mask the source IP
 *    address of 'r' with 'mask_vp' whose type is uint8_t[16].
 */
SK_EXTERN SK_INLINE
void
rwRecApplyMaskSIPv6(
    rwRec              *r,
    const void         *mask_vp);



/*
 *    ********************************************************************
 *    Destination IP Address (dIP)
 *
 */

/***  Destination IP Address (dIP) as skipaddr_t  ***/

/**
 *    Fill referent of 'out_addr' with the destination IP address of
 *    'r'.
 */
SK_EXTERN SK_INLINE
void
rwRecMemGetDIP(
    const rwRec        *r,
    skipaddr_t         *out_addr);

/**
 *    Set the destination IP address of 'r' to the referent of
 *    'in_addr'.
 */
SK_EXTERN SK_INLINE
void
rwRecMemSetDIP(
    rwRec              *r,
    const skipaddr_t   *in_addr);

/**
 *    Mask the destination IP address of 'r' with 'mask_addr'.
 *
 *    If 'r' is IPv6 and 'mask_addr' is IPv4, 'mask_addr' is mapped to
 *    the ::ffff:0:0/96 netblock prior to masking.
 *
 *    If 'r' is IPv4 and 'mask_addr' is IPv6, check whether
 *    'mask_addr' is in the ::ffff:0:0/96 netblock.  If it is, convert
 *    'mask_addr' to IPv4 prior to masking.  Otherwise, convert 'r' to
 *    IPv6 prior to masking.
 */
void
rwRecApplyMaskDIP(
    rwRec              *r,
    const skipaddr_t   *mask_addr);


/***  Destination IPv4 Address (dIP) as uint32_t  ***/

/**
 *    Assume record 'r' holds IPv4 addresses and return the
 *    destination IP address.
 */
SK_EXTERN SK_INLINE
uint32_t
rwRecGetDIPv4(
    const rwRec        *r);

/**
 *    Assume record 'r' holds IPv4 addresses and set the destination
 *    IP of 'r' to 'in_v'.
 */
SK_EXTERN SK_INLINE
void
rwRecSetDIPv4(
    rwRec              *r,
    uint32_t            in_v);

/**
 *    Assume record 'r' holds IPv4 addresses and fill the referent of
 *    'out_vp' with the destination IP of 'r'.
 */
SK_EXTERN SK_INLINE
void
rwRecMemGetDIPv4(
    const rwRec        *r,
    void               *out_vp);

/**
 *    Assume record 'r' holds IPv4 addresses and set the destination
 *    IP of 'r' to the referent of 'in_vp'.
 */
SK_EXTERN SK_INLINE
void
rwRecMemSetDIPv4(
    rwRec              *r,
    const void         *in_vp);

/**
 *    Assume record 'r' holds IPv4 addresses and mask the destination
 *    IP address of 'r' with 'mask'.
 */
SK_EXTERN SK_INLINE
void
rwRecApplyMaskDIPv4(
    rwRec              *r,
    uint32_t            mask);

/***  Destination IPv6 Address (dIP) as uint8_t[16]  ***/

/**
 *    Fill 'out_vp' (whose type is uint8_t[16]) with the destination
 *    IP address of 'r'.  If 'r' holds IPv4 addresses, return that
 *    address mapped to the ::ffff:0:0/96 netblock.
 */
SK_EXTERN SK_INLINE
void
rwRecMemGetDIPv6(
    const rwRec        *r,
    void               *out_vp);

/**
 *    Assume record 'r' holds IPv6 addresses and set the destination
 *    IP of 'r' to 'in_vp' whose type is uint8_t[16].
 */
SK_EXTERN SK_INLINE
void
rwRecMemSetDIPv6(
    rwRec              *r,
    const void         *in_vp);

/**
 *    Assume record 'r' holds IPv6 addresses and mask the destination
 *    IP address of 'r' with 'mask_vp' whose type is uint8_t[16].
 */
SK_EXTERN SK_INLINE
void
rwRecApplyMaskDIPv6(
    rwRec              *r,
    const void         *mask_vp);



/*
 *    ********************************************************************
 *    Next Hop IP Address (nhIP)
 *
 */

/***  Next Hop IP Address (nhIP) as skipaddr_t  ***/

/**
 *    Fill referent of 'out_addr' with the next hop IP address of 'r'.
 */
SK_EXTERN SK_INLINE
void
rwRecMemGetNhIP(
    const rwRec        *r,
    skipaddr_t         *out_addr);

/**
 *    Set the next hop IP address of 'r' to the referent of 'in_addr'.
 */
SK_EXTERN SK_INLINE
void
rwRecMemSetNhIP(
    rwRec              *r,
    const skipaddr_t   *in_addr);

/**
 *    Mask the next hop IP address of 'r' with 'mask_addr'.
 *
 *    If 'r' is IPv6 and 'mask_addr' is IPv4, 'mask_addr' is mapped to
 *    the ::ffff:0:0/96 netblock prior to masking.
 *
 *    If 'r' is IPv4 and 'mask_addr' is IPv6, check whether
 *    'mask_addr' is in the ::ffff:0:0/96 netblock.  If it is, convert
 *    'mask_addr' to IPv4 prior to masking.  Otherwise, convert 'r' to
 *    IPv6 prior to masking.
 */
void
rwRecApplyMaskNhIP(
    rwRec              *r,
    const skipaddr_t   *mask_addr);

/***  Next Hop IPv4 Address (nhIP) as uint32_t  ***/

/**
 *    Assume record 'r' holds IPv4 addresses and return the next hop
 *    IP address.
 */
SK_EXTERN SK_INLINE
uint32_t
rwRecGetNhIPv4(
    const rwRec        *r);

/**
 *    Assume record 'r' holds IPv4 addresses and set the next hop IP
 *    of 'r' to 'in_v'.
 */
SK_EXTERN SK_INLINE
void
rwRecSetNhIPv4(
    rwRec              *r,
    uint32_t            in_v);

/**
 *    Assume record 'r' holds IPv4 addresses and fill the referent of
 *    'out_vp' with the next hop IP of 'r'.
 */
SK_EXTERN SK_INLINE
void
rwRecMemGetNhIPv4(
    const rwRec        *r,
    void               *out_vp);

/**
 *    Assume record 'r' holds IPv4 addresses and set the next hop IP
 *    of 'r' to the referent of 'in_vp'.
 */
SK_EXTERN SK_INLINE
void
rwRecMemSetNhIPv4(
    rwRec              *r,
    const void         *in_vp);

/**
 *    Assume record 'r' holds IPv4 addresses and mask the next hop IP
 *    address of 'r' with 'mask'.
 */
SK_EXTERN SK_INLINE
void
rwRecApplyMaskNhIPv4(
    rwRec              *r,
    uint32_t            mask);

/***  Next Hop IPv6 Address (nhIP) as uint8_t[16]  ***/

/**
 *    Fill 'out_vp' (whose type is uint8_t[16]) with the next hop IP
 *    address of 'r'.  If 'r' holds IPv4 addresses, return that
 *    address mapped to the ::ffff:0:0/96 netblock.
 */
SK_EXTERN SK_INLINE
void
rwRecMemGetNhIPv6(
    const rwRec        *r,
    void               *out_vp);

/**
 *    Assume record 'r' holds IPv6 addresses and set the next hop IP
 *    of 'r' to 'in_vp' whose type is uint8_t[16].
 */
SK_EXTERN SK_INLINE
void
rwRecMemSetNhIPv6(
    rwRec              *r,
    const void         *in_vp);

/**
 *    Assume record 'r' holds IPv6 addresses and mask the next hop IP
 *    address of 'r' with 'mask_vp' whose type is uint8_t[16].
 */
SK_EXTERN SK_INLINE
void
rwRecApplyMaskNhIPv6(
    rwRec              *r,
    const void         *mask_vp);



/*
 *    ********************************************************************
 *    Source Port (sPort)
 *
 *    Source port is a uint16_t
 */

/**
 *    Return the source port of 'r'.
 */
SK_EXTERN SK_INLINE
uint16_t
rwRecGetSPort(
    const rwRec        *r);

/**
 *    Set the source port of 'r' to 'in_v'.
 */
SK_EXTERN SK_INLINE
void
rwRecSetSPort(
    rwRec              *r,
    uint16_t            in_v);

/**
 *    Fill referent of 'out_vp' with the source port of 'r'.
 */
SK_EXTERN SK_INLINE
void
rwRecMemGetSPort(
    const rwRec        *r,
    void               *out_vp);

/**
 *    Set the source port of 'r' to the referent of 'in_vp'.
 */
SK_EXTERN SK_INLINE
void
rwRecMemSetSPort(
    rwRec              *r,
    const void         *in_vp);



/*
 *    ********************************************************************
 *    Destination Port (dPort)
 *
 *    Destination port is a uint16_t
 */

/**
 *    Return the destination port of 'r'.
 */
SK_EXTERN SK_INLINE
uint16_t
rwRecGetDPort(
    const rwRec        *r);

/**
 *    Set the destination port of 'r' to 'in_v'.
 */
SK_EXTERN SK_INLINE
void
rwRecSetDPort(
    rwRec              *r,
    uint16_t            in_v);

/**
 *    Fill referent of 'out_vp' with the destination port of 'r'.
 */
SK_EXTERN SK_INLINE
void
rwRecMemGetDPort(
    const rwRec        *r,
    void               *out_vp);

/**
 *    Set the destination port of 'r' to the referent of 'in_vp'.
 */
SK_EXTERN SK_INLINE
void
rwRecMemSetDPort(
    rwRec              *r,
    const void         *in_vp);


/*
 *    ********************************************************************
 *    Protocol (protocol)
 *
 *    Protocol is a uint8_t.
 */

/**
 *    Return the protocol of 'r'.
 */
SK_EXTERN SK_INLINE
uint8_t
rwRecGetProto(
    const rwRec        *r);

/**
 *    Set the protocol of 'r' to 'in_v'.
 */
SK_EXTERN SK_INLINE
void
rwRecSetProto(
    rwRec              *r,
    uint8_t             in_v);

/**
 *    Fill referent of 'out_vp' with the protocol of 'r'.
 */
SK_EXTERN SK_INLINE
void
rwRecMemGetProto(
    const rwRec        *r,
    void               *out_vp);

/**
 *    Set the protocol of 'r' to the referent of 'in_vp'.
 */
SK_EXTERN SK_INLINE
void
rwRecMemSetProto(
    rwRec              *r,
    const void         *in_vp);


/*
 *    ********************************************************************
 *    Packet Count (packets or pkts)
 *
 *    Packet count is a uint64_t.
 */

/**
 *    Return the packet count of 'r'.
 */
SK_EXTERN SK_INLINE
uint64_t
rwRecGetPkts(
    const rwRec        *r);

/**
 *    Set the packet count of 'r' to 'in_v'.
 */
SK_EXTERN SK_INLINE
void
rwRecSetPkts(
    rwRec              *r,
    uint64_t            in_v);

/**
 *    Fill referent of 'out_vp' with the packet count of 'r'.
 */
SK_EXTERN SK_INLINE
void
rwRecMemGetPkts(
    const rwRec        *r,
    void               *out_vp);

/**
 *    Set the packet count of 'r' to the referent of 'in_vp'.
 */
SK_EXTERN SK_INLINE
void
rwRecMemSetPkts(
    rwRec              *r,
    const void         *in_vp);


/*
 *    ********************************************************************
 *    Byte Count (bytes)
 *
 *    Byte count is a uint64_t.
 */

/**
 *    Return the byte count of 'r'.
 */
SK_EXTERN SK_INLINE
uint64_t
rwRecGetBytes(
    const rwRec        *r);

/**
 *    Set the byte count of 'r' to 'in_v'.
 */
SK_EXTERN SK_INLINE
void
rwRecSetBytes(
    rwRec              *r,
    uint64_t            in_v);

/**
 *    Fill referent of 'out_vp' with the byte count of 'r'.
 */
SK_EXTERN SK_INLINE
void
rwRecMemGetBytes(
    const rwRec        *r,
    void               *out_vp);

/**
 *    Set the byte count of 'r' to the referent of 'in_vp'.
 */
SK_EXTERN SK_INLINE
void
rwRecMemSetBytes(
    rwRec              *r,
    const void         *in_vp);



/*
 *    ********************************************************************
 *    Flags (flags)
 *
 *    Flags is a uint8_t containing the bitwise OR of the TCP Flags on
 *    ALL packets in flow.
 */

/**
 *    Return the TCP flags of 'r'.
 */
SK_EXTERN SK_INLINE
uint8_t
rwRecGetFlags(
    const rwRec        *r);

/**
 *    Set the TCP flags of 'r' to 'in_v'.
 */
SK_EXTERN SK_INLINE
void
rwRecSetFlags(
    rwRec              *r,
    uint8_t             in_v);

/**
 *    Fill referent of 'out_vp' with the TCP flags of 'r'.
 */
SK_EXTERN SK_INLINE
void
rwRecMemGetFlags(
    const rwRec        *r,
    void               *out_vp);

/**
 *    Set the TCP flags of 'r' to the referent of 'in_vp'.
 */
SK_EXTERN SK_INLINE
void
rwRecMemSetFlags(
    rwRec              *r,
    const void         *in_vp);



/*
 *    ********************************************************************
 *    Initial Flags (initialFlags)
 *
 *    Initial flags is the TCP flags seen on the first packet of flow
 *    as a uint8_t.  NOTE: Setting the initial flags does NOT modify
 *    the value in the (All) TCP Flags field.
 */

/**
 *    Return the initial flags of 'r'.
 */
SK_EXTERN SK_INLINE
uint8_t
rwRecGetInitFlags(
    const rwRec        *r);

/**
 *    Set the initial flags of 'r' to 'in_v'.
 */
SK_EXTERN SK_INLINE
void
rwRecSetInitFlags(
    rwRec              *r,
    uint8_t             in_v);

/**
 *    Fill referent of 'out_vp' with the initial flags of 'r'.
 */
SK_EXTERN SK_INLINE
void
rwRecMemGetInitFlags(
    const rwRec        *r,
    void               *out_vp);

/**
 *    Set the initial flags of 'r' to the referent of 'in_vp'.
 */
SK_EXTERN SK_INLINE
void
rwRecMemSetInitFlags(
    rwRec              *r,
    const void         *in_vp);



/*
 *    ********************************************************************
 *    Session Flags (sessionFlags)
 *
 *    Session flags is a uint8_t containing the bitwise OR of the TCP
 *    Flags on all packets in flow except the first.  NOTE: Setting
 *    the session flags does NOT modify the value in the (All) TCP
 *    Flags field.
 */

/**
 *    Return the session flags of 'r'.
 */
SK_EXTERN SK_INLINE
uint8_t
rwRecGetRestFlags(
    const rwRec        *r);

/**
 *    Set the session flags of 'r' to 'in_v'.
 */
SK_EXTERN SK_INLINE
void
rwRecSetRestFlags(
    rwRec              *r,
    uint8_t             in_v);

/**
 *    Fill referent of 'out_vp' with the session flags of 'r'.
 */
SK_EXTERN SK_INLINE
void
rwRecMemGetRestFlags(
    const rwRec        *r,
    void               *out_vp);

/**
 *    Set the session flags of 'r' to the referent of 'in_vp'.
 */
SK_EXTERN SK_INLINE
void
rwRecMemSetRestFlags(
    rwRec              *r,
    const void         *in_vp);



/*
 *    ********************************************************************
 *    Start timme (sTime)
 *
 *    Start time is an sktime_t containing the the start time of the
 *    flow as milliseconds since the UNIX epoch.
 *
 *    There are also API calls to get the start times as seconds since
 *    the UNIX epoch.
 */

/**
 *    Return the start time of 'r'.
 */
SK_EXTERN SK_INLINE
sktime_t
rwRecGetStartTime(
    const rwRec        *r);

/**
 *    Set the start time of 'r' to 'in_v'.
 */
SK_EXTERN SK_INLINE
void
rwRecSetStartTime(
    rwRec              *r,
    sktime_t            in_v);

/**
 *    Fill referent of 'out_vp' with the start time of 'r', where
 *    'out_vp' is an sktime_t.
 */
SK_EXTERN SK_INLINE
void
rwRecMemGetStartTime(
    const rwRec        *r,
    void               *out_vp);

/**
 *    Set the start time of 'r' to the referent of 'in_vp' where
 *    'in_vp' is an sktime_t.
 */
SK_EXTERN SK_INLINE
void
rwRecMemSetStartTime(
    rwRec              *r,
    const void         *in_vp);

/**
 *    Return the start time of 'r' as seconds since the UNIX epoch.
 */
SK_EXTERN SK_INLINE
uint32_t
rwRecGetStartSeconds(
    const rwRec        *r);

/**
 *    Fill referent of 'out_vp' with the start time of 'r' as seconds
 *    since the UNIX epoch, where 'out_vp' is an uint32_t..
 */
SK_EXTERN SK_INLINE
void
rwRecMemGetStartSeconds(
    const rwRec        *r,
    void               *out_vp);



/*
 *    ********************************************************************
 *    Duration (duration)
 *
 *    Source port is a uint16_t
 */

/***  Elapsed (duration) of the flow, in milliseconds  ***/

/***  Start Time as milliseconds since UNIX epoch (sTime)  ***/

/*
 *    In SiLK 3, eTime was derived from sTime and elapsed.  There are
 *    get-functions for sTime, eTime, and elapsed time, but only
 *    set-functions for sTime and elapsed.
 *
 *    In SiLK 4, elapsed is derived from sTime and eTime, but the API
 *    remains the same as for SiLK 3.  Setting elapsed sets the eTime;
 *    however setting the sTime also sets the eTime so that the
 *    elapsed time remains constant.
 *
 * There are no setter macros for end time, because end time is
 * derived from start time and duration (elapsed time).
 */

/*    See note at start time above */

/**
 *    Return the Elapsed of 'r'.
 */
SK_EXTERN SK_INLINE
uint32_t
rwRecGetElapsed(
    const rwRec        *r);

/**
 *    Set the Elapsed of 'r' to 'in_v'.
 */
SK_EXTERN SK_INLINE
void
rwRecSetElapsed(
    rwRec              *r,
    sktime_t            in_v);

/**
 *    Fill referent of 'out_vp' with the Elapsed of 'r'.
 */
SK_EXTERN SK_INLINE
void
rwRecMemGetElapsed(
    const rwRec        *r,
    void               *out_vp);

/**
 *    Set the Elapsed of 'r' to the referent of 'in_vp'.
 */
SK_EXTERN SK_INLINE
void
rwRecMemSetElapsed(
    rwRec              *r,
    const void         *in_vp);

/**
 *    Return the ElapsedSeconds of 'r'.
 */
SK_EXTERN SK_INLINE
uint32_t
rwRecGetElapsedSeconds(
    const rwRec        *r);

/**
 *    Fill referent of 'out_vp' with the ElapsedSeconds of 'r'.
 */
SK_EXTERN SK_INLINE
void
rwRecMemGetElapsedSeconds(
    const rwRec        *r,
    void               *out_vp);



/*
 *    ********************************************************************
 *    End Time (eTime)
 *
 *    End time is an sktime_t.
 */

/***  End Time (eTime)  ***/

/*    See note at start time above */

/**
 *    Return the EndTime of 'r'.
 */
SK_EXTERN SK_INLINE
sktime_t
rwRecGetEndTime(
    const rwRec        *r);

/**
 *    Fill referent of 'out_vp' with the EndTime of 'r'.
 */
SK_EXTERN SK_INLINE
void
rwRecMemGetEndTime(
    const rwRec        *r,
    void               *out_vp);

/**
 *    Return the EndSeconds of 'r'.
 */
SK_EXTERN SK_INLINE
uint32_t
rwRecGetEndSeconds(
    const rwRec        *r);

/**
 *    Fill referent of 'out_vp' with the EndSeconds of 'r'.
 */
SK_EXTERN SK_INLINE
void
rwRecMemGetEndSeconds(
    const rwRec        *r,
    void               *out_vp);



/*
 *    ********************************************************************
 *    Sensor (sensor)
 *
 *    Sensor is the location where the flow was captured.
 */

/**
 *    Return the sensor of 'r'.
 */
SK_EXTERN SK_INLINE
sk_sensor_id_t
rwRecGetSensor(
    const rwRec        *r);

/**
 *    Set the sensor of 'r' to 'in_v'.
 */
SK_EXTERN SK_INLINE
void
rwRecSetSensor(
    rwRec              *r,
    sk_sensor_id_t      in_v);

/**
 *    Fill referent of 'out_vp' with the sensor of 'r'.
 */
SK_EXTERN SK_INLINE
void
rwRecMemGetSensor(
    const rwRec        *r,
    void               *out_vp);

/**
 *    Set the sensor of 'r' to the referent of 'in_vp'.
 */
SK_EXTERN SK_INLINE
void
rwRecMemSetSensor(
    rwRec              *r,
    const void         *in_vp);



/*
 *    ********************************************************************
 *    Flowtype (class, type)
 *
 *    Flow type is an integer that represents the class and type.
 */

/**
 *    Return the flowtype of 'r'.
 */
SK_EXTERN SK_INLINE
sk_flowtype_id_t
rwRecGetFlowType(
    const rwRec        *r);

/**
 *    Set the flowtype of 'r' to 'in_v'.
 */
SK_EXTERN SK_INLINE
void
rwRecSetFlowType(
    rwRec              *r,
    sk_flowtype_id_t    in_v);

/**
 *    Fill referent of 'out_vp' with the flowtype of 'r'.
 */
SK_EXTERN SK_INLINE
void
rwRecMemGetFlowType(
    const rwRec        *r,
    void               *out_vp);

/**
 *    Set the flowtype of 'r' to the referent of 'in_vp'.
 */
SK_EXTERN SK_INLINE
void
rwRecMemSetFlowType(
    rwRec              *r,
    const void         *in_vp);



/*
 *    ********************************************************************
 *    SNMP Input (input)
 *
 *    SNMP input value is a uint32_t that holds the router
 *    incoming/ingress interface ID or the vlanId.
 */

/**
 *    Return the SNMP input of 'r'.
 */
SK_EXTERN SK_INLINE
uint32_t
rwRecGetInput(
    const rwRec        *r);

/**
 *    Set the SNMP input of 'r' to 'in_v'.
 */
SK_EXTERN SK_INLINE
void
rwRecSetInput(
    rwRec              *r,
    uint32_t            in_v);

/**
 *    Fill referent of 'out_vp' with the SNMP input of 'r'.
 */
SK_EXTERN SK_INLINE
void
rwRecMemGetInput(
    const rwRec        *r,
    void               *out_vp);

/**
 *    Set the SNMP input of 'r' to the referent of 'in_vp'.
 */
SK_EXTERN SK_INLINE
void
rwRecMemSetInput(
    rwRec              *r,
    const void         *in_vp);



/*
 *    ********************************************************************
 *    SNMP Output (output)
 *
 *    SNMP output value is a uint32_t that holds the router
 *    outgoing/egress interface ID or the portVlanId.
 */

/**
 *    Return the SNMP output of 'r'.
 */
SK_EXTERN SK_INLINE
uint32_t
rwRecGetOutput(
    const rwRec        *r);

/**
 *    Set the SNMP output of 'r' to 'in_v'.
 */
SK_EXTERN SK_INLINE
void
rwRecSetOutput(
    rwRec              *r,
    uint32_t            in_v);

/**
 *    Fill referent of 'out_vp' with the SNMP output of 'r'.
 */
SK_EXTERN SK_INLINE
void
rwRecMemGetOutput(
    const rwRec        *r,
    void               *out_vp);

/**
 *    Set the SNMP output of 'r' to the referent of 'in_vp'.
 */
SK_EXTERN SK_INLINE
void
rwRecMemSetOutput(
    rwRec              *r,
    const void         *in_vp);



/*
 *    ********************************************************************
 *    Attributes (attributes)
 *
 *    Attributes (or TCP State) is a uint8_t.
 *
 * The TCP state field is a bit field which states certain miscellaneous
 * information about the flow record.  The following constants are
 * defined which represent this information:
 *
 * #define SK_TCPSTATE_EXPANDED              0x01
 *  Expanded TCP-flags: This bit must be set if and only if the flow
 *  is TCP and the init_flags and rest_flags fields are set.
 *
 * #define SK_TCPSTATE_FIN_FOLLOWED_NOT_ACK  0x08
 *  Flow received packets following the FIN packet that were not ACK or
 *  RST packets.
 *
 * #define SK_TCPSTATE_UNIFORM_PACKET_SIZE   0x10
 *  Flow has packets all of the same size
 *
 * #define SK_TCPSTATE_TIMEOUT_KILLED        0x20
 *  Flow ends prematurely due to a timeout by the collector.
 *
 * #define SK_TCPSTATE_TIMEOUT_STARTED       0x40
 *  Flow is a continuation of a previous flow that was killed
 *  prematurely due to a timeout by the collector.
 *
 * Note: the most significant bit of tcp_state (0x80) is used as a flag
 * to mark a record as having IPv6 addresses.  The rwRecSetIPv4() and
 * rwRecSetIPv6() macros should be used to modify this bit.
 *
 * Be careful when setting the TCP state.  You usually want get the
 * current TCP state, add or remove specific bits by masking, then set it
 * with the resulting value.
 */

/**
 *    Return the attributes of 'r'.
 */
SK_EXTERN SK_INLINE
uint8_t
rwRecGetTcpState(
    const rwRec        *r);

/**
 *    Set the attributes of 'r' to 'in_v'.
 */
SK_EXTERN SK_INLINE
void
rwRecSetTcpState(
    rwRec              *r,
    uint8_t             in_v);

/**
 *    Fill referent of 'out_vp' with the attributes of 'r'.
 */
SK_EXTERN SK_INLINE
void
rwRecMemGetTcpState(
    const rwRec        *r,
    void               *out_vp);

/**
 *    Set the attributes of 'r' to the referent of 'in_vp'.
 */
SK_EXTERN SK_INLINE
void
rwRecMemSetTcpState(
    rwRec              *r,
    const void         *in_vp);



/*
 *    ********************************************************************
 *    Application (application)
 *
 *
 * The application field can be set by the yaf flow generation
 * software when it is configured with the "applabel" feature.  This
 * feature causes yaf to inspect the packets in the flow and guess as
 * to the type of application (HTTP, SMTP, SSH, etc) the packets
 * represent.  The value for the field is the standard service port
 * for that service (80, 25, 22, etc).
 */

/**
 *    Return the application of 'r'.
 */
SK_EXTERN SK_INLINE
uint16_t
rwRecGetApplication(
    const rwRec        *r);

/**
 *    Set the application of 'r' to 'in_v'.
 */
SK_EXTERN SK_INLINE
void
rwRecSetApplication(
    rwRec              *r,
    uint16_t            in_v);

/**
 *    Fill referent of 'out_vp' with the application of 'r'.
 */
SK_EXTERN SK_INLINE
void
rwRecMemGetApplication(
    const rwRec        *r,
    void               *out_vp);

/**
 *    Set the application of 'r' to the referent of 'in_vp'.
 */
SK_EXTERN SK_INLINE
void
rwRecMemSetApplication(
    rwRec              *r,
    const void         *in_vp);



/*
 *    ********************************************************************
 *    ICMP Type, ICMP Code, ICMP Type and Code
 *
 *    ICMP Type and Code is derived from the DPort.
 *
 *    In NetFlow, Cisco has traditionally encoded the ICMP type and
 *    code in the destination port field as (type << 8 | code).
 *    The following macros assume this Cisco-encoding.
 *
 *    Due to various issues, sometimes the ICMP type and code is
 *    encoded in the source port instead of the destination port.  As
 *    of SiLK-3.4.0, libsilk (skstream.c) handles these incorrect
 *    encodings when the record is read and modifies the record to use
 *    the traditional Cisco encoding.
 *
 *    The following functions/macros do not check the protocol.
 *
 * Since the ICMP Type and Code are stored in the dport field, modifying
 * these will modify the return value of rwRecGetDPort() and friends.
 */

/**
 *    Return the ICMP type of 'r'.
 */
SK_EXTERN SK_INLINE
uint8_t
rwRecGetIcmpType(
    const rwRec        *r);

/**
 *    Set the ICMP type of 'r' to 'in_v'.
 */
SK_EXTERN SK_INLINE
void
rwRecSetIcmpType(
    rwRec              *r,
    uint8_t             in_v);

/**
 *    Fill referent of 'out_vp' with the ICMP type of 'r'.
 */
SK_EXTERN SK_INLINE
void
rwRecMemGetIcmpType(
    const rwRec        *r,
    void               *out_vp);

/**
 *    Return the ICMP code of 'r'.
 */
SK_EXTERN SK_INLINE
uint8_t
rwRecGetIcmpCode(
    const rwRec        *r);

/**
 *    Set the ICMP code of 'r' to 'in_v'.
 */
SK_EXTERN SK_INLINE
void
rwRecSetIcmpCode(
    rwRec              *r,
    uint8_t             in_v);

/**
 *    Fill referent of 'out_vp' with the ICMP code of 'r'.
 */
SK_EXTERN SK_INLINE
void
rwRecMemGetIcmpCode(
    const rwRec        *r,
    void               *out_vp);

/**
 *    Return the ICMP type and code of 'r'.
 */
SK_EXTERN SK_INLINE
uint16_t
rwRecGetIcmpTypeAndCode(
    const rwRec        *r);

/**
 *    Set the ICMP type and code of 'r' to 'in_v'.
 */
SK_EXTERN SK_INLINE
void
rwRecSetIcmpTypeAndCode(
    rwRec              *r,
    uint16_t            in_v);

/**
 *    Fill referent of 'out_vp' with the ICMP type and code of 'r'.
 */
SK_EXTERN SK_INLINE
void
rwRecMemGetIcmpTypeAndCode(
    const rwRec        *r,
    void               *out_vp);

/**
 *    Set the ICMP type and code of 'r' to the referent of 'in_vp'.
 */
SK_EXTERN SK_INLINE
void
rwRecMemSetIcmpTypeAndCode(
    rwRec              *r,
    const void         *in_vp);



/*
 *    ********************************************************************
 *    Sidecar and Lua
 *
 *    Sidecar is an int64_t to a Lua reference.
 */

/**
 *    Return the Sidecar of 'r'.
 */
SK_EXTERN SK_INLINE
int64_t
rwRecGetSidecar(
    const rwRec        *r);

/**
 *    Set the Sidecar of 'r' to 'in_v'.
 */
SK_EXTERN SK_INLINE
void
rwRecSetSidecar(
    rwRec              *r,
    int64_t             in_v);


/**
 *    Return the Lua state pointer on 'r', or return NULL if 'r' has
 *    no Lua state.
 */
SK_EXTERN SK_INLINE
lua_State *
rwRecGetLuaState(
    const rwRec        *r);



/*
 *    ********************************************************************
 *    Memo
 *
 *    Memo is a uint16_t
 */

/**
 *    Return the Memo of 'r'.
 */
SK_EXTERN SK_INLINE
uint16_t
rwRecGetMemo(
    const rwRec        *r);

/**
 *    Set the Memo of 'r' to 'in_v'.
 */
SK_EXTERN SK_INLINE
void
rwRecSetMemo(
    rwRec              *r,
    uint16_t            in_v);

/**
 *    Fill referent of 'out_vp' with the Memo of 'r'.
 */
SK_EXTERN SK_INLINE
void
rwRecMemGetMemo(
    const rwRec        *r,
    void               *out_vp);

/**
 *    Set the Memo of 'r' to the referent of 'in_vp'.
 */
SK_EXTERN SK_INLINE
void
rwRecMemSetMemo(
    rwRec              *r,
    const void         *in_vp);


/**
 *    Append the standard rwrec fields and their numeric aliases to
 *    the string map pointed 'str_map'
 *
 *    The ID of the fields are the values specified in the
 *    rwrec_field_id_t type above.
 *
 *    Return 0 on success or -1 on failure.
 */
int
skRwrecAppendFieldsToStringMap(
    sk_stringmap_t     *str_map);



#if SK_HAVE_INLINE || defined(SOURCE_FILE_RWREC_C)
/*
 *  ************************************************************************
 *  ************************************************************************
 *  Implementation of inline functions
 *  ************************************************************************
 *  ************************************************************************
 */

#include <silk/skipaddr.h>

/* Helper macros */
#if 0
#  define MEMCPY8(dst, src)   { *((uint8_t*)(dst))  = *((uint8_t*)(src)); }
#  define MEMCPY16(dst, src)  { *((uint16_t*)(dst)) = *((uint16_t*)(src)); }
#  define MEMCPY32(dst, src)  { *((uint32_t*)(dst)) = *((uint32_t*)(src)); }
#  define MEMCPY32(dst, src)  { *((uint64_t*)(dst)) = *((uint64_t*)(src)); }
#else
#  define MEMCPY8(dst, src)   memcpy((dst), (src), sizeof(uint8_t))
#  define MEMCPY16(dst, src)  memcpy((dst), (src), sizeof(uint16_t))
#  define MEMCPY32(dst, src)  memcpy((dst), (src), sizeof(uint32_t))
#  define MEMCPY64(dst, src)  memcpy((dst), (src), sizeof(uint64_t))
#endif

SK_INLINE
int
rwRecIsIPv6(
    const rwRec        *r)
{
    return (r->tcp_state >> 7);
}

SK_INLINE
void
rwRecSetIPv4(
    rwRec              *r)
{
    r->tcp_state &= 0x7F;
}

SK_INLINE
void
rwRecSetIPv6(
    rwRec              *r)
{
    r->tcp_state |= 0x80;
}

/*
 *  Implemented in rwrec.c.
 *
 *    int
 *    rwRecConvertToIPv4(
 *        rwRec              *r);
 */

SK_INLINE
void
rwRecConvertToIPv6(
    rwRec              *r)
{
    skIPUnion4to6(&r->sIP, &r->sIP);
    skIPUnion4to6(&r->dIP, &r->dIP);
    skIPUnion4to6(&r->nhIP, &r->nhIP);
    r->tcp_state |= 0x80;
}



/***  Source IP Address (sIP)  ***/

SK_INLINE
void
rwRecMemGetSIP(
    const rwRec        *r,
    skipaddr_t         *out_vp)
{
    memcpy(out_vp, &r->sIP, sizeof(skIPUnion_t));
    skipaddrSetVersion(out_vp, rwRecIsIPv6(r));
}

SK_INLINE
void
rwRecMemSetSIP(
    rwRec              *r,
    const skipaddr_t   *in_vp)
{
    if (skipaddrIsV6(in_vp) == rwRecIsIPv6(r)) {
        /* both are either V4 or V6 */
        memcpy(&r->sIP, in_vp, sizeof(skIPUnion_t));
    } else if (rwRecIsIPv6(r)) {
        /* convert V4 IP to V6 */
        skIPUnion4to6(&(in_vp->ip_ip), &r->sIP);
    } else {
        /* must convert record to V6 */
        rwRecConvertToIPv6(r);
        memcpy(&r->sIP, in_vp, sizeof(skIPUnion_t));
    }
}

/*
 *  Implemented in rwrec.c.
 *
 *    void
 *    rwRecApplyMaskSIP(
 *        rwRec              *r,
 *        const skipaddr_t   *mask_addr);
 */

SK_INLINE
uint32_t
rwRecGetSIPv4(
    const rwRec        *r)
{
    return skIPUnionGetV4(&r->sIP);
}

SK_INLINE
void
rwRecSetSIPv4(
    rwRec              *r,
    uint32_t            in_v)
{
    r->sIP.ipu_ipv4 = in_v;
}

SK_INLINE
void
rwRecMemGetSIPv4(
    const rwRec        *r,
    void               *out_vp)
{
    MEMCPY32(out_vp, &(r->sIP.ipu_ipv4));
}

SK_INLINE
void
rwRecMemSetSIPv4(
    rwRec              *r,
    const void         *in_vp)
{
    skIPUnionSetV4(&r->sIP, in_vp);
}

SK_INLINE
void
rwRecApplyMaskSIPv4(
    rwRec              *r,
    uint32_t mask)
{
    skIPUnionApplyMaskV4(&r->sIP, (mask));
}

SK_INLINE
void
rwRecMemGetSIPv6(
    const rwRec        *r,
    void               *out_vp)
{
    if (rwRecIsIPv6(r)) {
        skIPUnionGetV6(&r->sIP, out_vp);
    } else {
        skIPUnionGetV4AsV6(&r->sIP, out_vp);
    }
}

SK_INLINE
void
rwRecMemSetSIPv6(
    rwRec              *r,
    const void         *in_vp)
{
    skIPUnionSetV6(&r->sIP, in_vp);
}

SK_INLINE
void
rwRecApplyMaskSIPv6(
    rwRec              *r,
    const void         *mask_vp)
{
    skIPUnionApplyMaskV6(&r->sIP, (mask_vp));
}



/***  Destination IP Address (dIP)  ***/

SK_INLINE
void
rwRecMemGetDIP(
    const rwRec        *r,
    skipaddr_t         *out_vp)
{
    memcpy(out_vp, &r->dIP, sizeof(skIPUnion_t));
    skipaddrSetVersion(out_vp, rwRecIsIPv6(r));
}

SK_INLINE
void
rwRecMemSetDIP(
    rwRec              *r,
    const skipaddr_t *in_vp)
{
    if (skipaddrIsV6(in_vp) == rwRecIsIPv6(r)) {
        /* both are either V4 or V6 */
        memcpy(&r->dIP, in_vp, sizeof(skIPUnion_t));
    } else if (rwRecIsIPv6(r)) {
        /* convert V4 IP to V6 */
        skIPUnion4to6(&(in_vp->ip_ip), &r->dIP);
    } else {
        /* must convert record to V6 */
        rwRecConvertToIPv6(r);
        memcpy(&r->dIP, in_vp, sizeof(skIPUnion_t));
    }
}

/*
 *  Implemented in rwrec.c.
 *
 *    void
 *    rwRecApplyMaskDIP(
 *        rwRec              *r,
 *        const skipaddr_t   *mask_addr);
 */

SK_INLINE
uint32_t
rwRecGetDIPv4(
    const rwRec        *r)
{
    return skIPUnionGetV4(&r->dIP);
}

SK_INLINE
void
rwRecSetDIPv4(
    rwRec              *r,
    uint32_t            in_v)
{
    r->dIP.ipu_ipv4 = in_v;
}

SK_INLINE
void
rwRecMemGetDIPv4(
    const rwRec        *r,
    void               *out_vp)
{
    MEMCPY32(out_vp, &(r->dIP.ipu_ipv4));
}

SK_INLINE
void
rwRecMemSetDIPv4(
    rwRec              *r,
    const void         *in_vp)
{
    skIPUnionSetV4(&r->dIP, in_vp);
}

SK_INLINE
void
rwRecApplyMaskDIPv4(
    rwRec              *r,
    uint32_t mask)
{
    skIPUnionApplyMaskV4(&r->dIP, (mask));
}

SK_INLINE
void
rwRecMemGetDIPv6(
    const rwRec        *r,
    void               *out_vp)
{
    if (rwRecIsIPv6(r)) {
        skIPUnionGetV6(&r->dIP, out_vp);
    } else {
        skIPUnionGetV4AsV6(&r->dIP, out_vp);
    }
}

SK_INLINE
void
rwRecMemSetDIPv6(
    rwRec              *r,
    const void         *in_vp)
{
    skIPUnionSetV6(&r->dIP, in_vp);
}

SK_INLINE
void
rwRecApplyMaskDIPv6(
    rwRec              *r,
    const void         *mask_vp)
{
    skIPUnionApplyMaskV6(&r->dIP, mask_vp);
}



/***  Next Hop IP Address (nhIP)  ***/

SK_INLINE
void
rwRecMemGetNhIP(
    const rwRec        *r,
    skipaddr_t         *out_vp)
{
    memcpy(out_vp, &r->nhIP, sizeof(skIPUnion_t));
    skipaddrSetVersion(out_vp, rwRecIsIPv6(r));
}

SK_INLINE
void
rwRecMemSetNhIP(
    rwRec              *r,
    const skipaddr_t   *in_vp)
{
    if (skipaddrIsV6(in_vp) == rwRecIsIPv6(r)) {
        /* both are either V4 or V6 */
        memcpy(&r->nhIP, in_vp, sizeof(skIPUnion_t));
    } else if (rwRecIsIPv6(r)) {
        /* convert V4 IP to V6 */
        skIPUnion4to6(&(in_vp->ip_ip), &r->nhIP);
    } else {
        /* must convert record to V6 */
        rwRecConvertToIPv6(r);
        memcpy(&r->nhIP, in_vp, sizeof(skIPUnion_t));
    }
}

/*
 *  Implemented in rwrec.c.
 *
 *    void
 *    rwRecApplyMaskNhIP(
 *        rwRec              *r,
 *        const skipaddr_t   *mask_addr);
 */

SK_INLINE
uint32_t
rwRecGetNhIPv4(
    const rwRec        *r)
{
    return r->nhIP.ipu_ipv4;
}

SK_INLINE
void
rwRecSetNhIPv4(
    rwRec              *r,
    uint32_t            in_v)
{
    r->nhIP.ipu_ipv4 = in_v;
}

SK_INLINE
void
rwRecMemGetNhIPv4(
    const rwRec        *r,
    void               *out_vp)
{
    MEMCPY32(out_vp, &r->nhIP.ipu_ipv4);
}

SK_INLINE
void
rwRecMemSetNhIPv4(
    rwRec              *r,
    const void         *in_vp)
{
    skIPUnionSetV4(&r->nhIP, in_vp);
}

SK_INLINE
void
rwRecApplyMaskNhIPv4(
    rwRec              *r,
    uint32_t mask)
{
    skIPUnionApplyMaskV4(&r->nhIP, (mask));
}

SK_INLINE
void
rwRecMemGetNhIPv6(
    const rwRec        *r,
    void               *out_vp)
{
    if (rwRecIsIPv6(r)) {
        skIPUnionGetV6(&r->nhIP, out_vp);
    } else {
        skIPUnionGetV4AsV6(&r->nhIP, out_vp);
    }
}

SK_INLINE
void
rwRecMemSetNhIPv6(
    rwRec              *r,
    const void         *in_vp)
{
    skIPUnionSetV6(&r->nhIP, in_vp);
}

SK_INLINE
void
rwRecApplyMaskNhIPv6(
    rwRec              *r,
    const void         *mask_vp)
{
    skIPUnionApplyMaskV6(&r->nhIP, mask_vp);
}



/***  Source Port (sPort) is a uint16_t  ***/

SK_INLINE
uint16_t
rwRecGetSPort(
    const rwRec        *r)
{
    return r->sPort;
}

SK_INLINE
void
rwRecSetSPort(
    rwRec              *r,
    uint16_t            in_v)
{
    r->sPort = in_v;
}

SK_INLINE
void
rwRecMemGetSPort(
    const rwRec        *r,
    void               *out_vp)
{
    MEMCPY16(out_vp, &r->sPort);
}

SK_INLINE
void
rwRecMemSetSPort(
    rwRec              *r,
    const void         *in_vp)
{
    MEMCPY16(&r->sPort, in_vp);
}



/***  Destination Port (dPort) is a uint16_t  ***/

SK_INLINE
uint16_t
rwRecGetDPort(
    const rwRec        *r)
{
    return r->dPort;
}

SK_INLINE
void
rwRecSetDPort(
    rwRec              *r,
    uint16_t            in_v)
{
    r->dPort = in_v;
}

SK_INLINE
void
rwRecMemGetDPort(
    const rwRec        *r,
    void               *out_vp)
{
    MEMCPY16(out_vp, &r->dPort);
}

SK_INLINE
void
rwRecMemSetDPort(
    rwRec              *r,
    const void         *in_vp)
{
    MEMCPY16(&r->dPort, in_vp);
}



/***  Protocol (proto) is a uint8_t  ***/

SK_INLINE
uint8_t
rwRecGetProto(
    const rwRec        *r)
{
    return r->proto;
}

SK_INLINE
void
rwRecSetProto(
    rwRec              *r,
    uint8_t             in_v)
{
    r->proto = in_v;
}

SK_INLINE
void
rwRecMemGetProto(
    const rwRec        *r,
    void               *out_vp)
{
    MEMCPY8(out_vp, &r->proto);
}

SK_INLINE
void
rwRecMemSetProto(
    rwRec              *r,
    const void         *in_vp)
{
    MEMCPY8(&r->proto, in_vp);
}



/***  Packet Count (pkts) is a uint64_t  ***/

SK_INLINE
uint64_t
rwRecGetPkts(
    const rwRec        *r)
{
    return r->pkts;
}

SK_INLINE
void
rwRecSetPkts(
    rwRec              *r,
    uint64_t            in_v)
{
    r->pkts = in_v;
}

SK_INLINE
void
rwRecMemGetPkts(
    const rwRec        *r,
    void               *out_vp)
{
    MEMCPY64(out_vp, &r->pkts);
}

SK_INLINE
void
rwRecMemSetPkts(
    rwRec              *r,
    const void         *in_vp)
{
    MEMCPY64(&r->pkts, in_vp);
}



/***  Byte Count (bytes) is a uint64_t  ***/

SK_INLINE
uint64_t
rwRecGetBytes(
    const rwRec        *r)
{
    return r->bytes;
}

SK_INLINE
void
rwRecSetBytes(
    rwRec              *r,
    uint64_t            in_v)
{
    r->bytes = in_v;
}

SK_INLINE
void
rwRecMemGetBytes(
    const rwRec        *r,
    void               *out_vp)
{
    MEMCPY64(out_vp, &r->bytes);
}

SK_INLINE
void
rwRecMemSetBytes(
    rwRec              *r,
    const void         *in_vp)
{
    MEMCPY64(&r->bytes, in_vp);
}



/***  Flags (flags) is a uint8_t containing the bitwise OR of TCP
 *    Flags on ALL packets in flow  ***/

SK_INLINE
uint8_t
rwRecGetFlags(
    const rwRec        *r)
{
    return r->flags;
}

SK_INLINE
void
rwRecSetFlags(
    rwRec              *r,
    uint8_t             in_v)
{
    r->flags = in_v;
}

SK_INLINE
void
rwRecMemGetFlags(
    const rwRec        *r,
    void               *out_vp)
{
    MEMCPY8(out_vp, &r->flags);
}

SK_INLINE
void
rwRecMemSetFlags(
    rwRec              *r,
    const void         *in_vp)
{
    MEMCPY8(&r->flags, in_vp);
}



/***  Initial Flags (initFlags) is the TCP Flags seen on the first
 *    packet of flow as a uint8_t  ***/

SK_INLINE
uint8_t
rwRecGetInitFlags(
    const rwRec        *r)
{
    return r->init_flags;
}

SK_INLINE
void
rwRecSetInitFlags(
    rwRec              *r,
    uint8_t             in_v)
{
    r->init_flags = in_v;
}

SK_INLINE
void
rwRecMemGetInitFlags(
    const rwRec        *r,
    void               *out_vp)
{
    MEMCPY8(out_vp, &r->init_flags);
}

SK_INLINE
void
rwRecMemSetInitFlags(
    rwRec              *r,
    const void         *in_vp)
{
    MEMCPY8(&r->init_flags, in_vp);
}



/***  Rest Flasg (restFlags) is a uint8_t containing the bitwise OR of
 *    the TCP Flags on all packets in session except first  ***/

SK_INLINE
uint8_t
rwRecGetRestFlags(
    const rwRec        *r)
{
    return r->rest_flags;
}

SK_INLINE
void
rwRecSetRestFlags(
    rwRec              *r,
    uint8_t             in_v)
{
    r->rest_flags = in_v;
}

SK_INLINE
void
rwRecMemGetRestFlags(
    const rwRec        *r,
    void               *out_vp)
{
    MEMCPY8(out_vp, &r->rest_flags);
}

SK_INLINE
void
rwRecMemSetRestFlags(
    rwRec              *r,
    const void         *in_vp)
{
    MEMCPY8(&r->rest_flags, in_vp);
}



/***  Start Time (sTime) is an sktime_t  ***/

SK_INLINE
sktime_t
rwRecGetStartTime(
    const rwRec        *r)
{
    return r->sTime;
}

SK_INLINE
void
rwRecSetStartTime(
    rwRec              *r,
    sktime_t            in_v)
{
    r->eTime += in_v - r->sTime;
    r->sTime = in_v;
}

SK_INLINE
void
rwRecMemGetStartTime(
    const rwRec        *r,
    void               *out_vp)
{
    MEMCPY64(out_vp, &r->sTime);
}

SK_INLINE
void
rwRecMemSetStartTime(
    rwRec              *r,
    const void         *in_vp)
{
    sktime_t t;
    MEMCPY64(&t, in_vp);
    r->eTime += t - r->sTime;
    r->sTime = t;
}

SK_INLINE
uint32_t
rwRecGetStartSeconds(
    const rwRec        *r)
{
    return ((uint32_t)(r->sTime / 1000));
}

SK_INLINE
void
rwRecMemGetStartSeconds(
    const rwRec        *r,
    void               *out_vp)
{
    uint32_t t = ((uint32_t)(r->sTime / 1000));
    MEMCPY32(out_vp, &t);
}



/***  Duration (duration) is a uint32_t  ***/

SK_INLINE
uint32_t
rwRecGetElapsed(
    const rwRec        *r)
{
    return ((uint32_t)(r->eTime - r->sTime));
}

SK_INLINE
void
rwRecSetElapsed(
    rwRec              *r,
    sktime_t            in_v)
{
    r->eTime = r->sTime + (uint32_t)(in_v);
}

SK_INLINE
void
rwRecMemGetElapsed(
    const rwRec        *r,
    void               *out_vp)
{
    uint32_t e = ((uint32_t)(r->eTime - r->sTime));
    MEMCPY32(out_vp, &e);
}

SK_INLINE
void
rwRecMemSetElapsed(
    rwRec              *r,
    const void         *in_vp)
{
    uint32_t e;
    MEMCPY32(&e, in_vp);
    r->eTime = r->sTime + e;
}

SK_INLINE
uint32_t
rwRecGetElapsedSeconds(
    const rwRec        *r)
{
    return ((uint32_t)(((uint32_t)(r->eTime - r->sTime)) / 1000));
}

SK_INLINE
void
rwRecMemGetElapsedSeconds(
    const rwRec        *r,
    void               *out_vp)
{
    uint32_t t = ((uint32_t)(((uint32_t)(r->eTime - r->sTime)) / 1000));
    MEMCPY32(out_vp, &t);
}



/***  End Time (eTime) is an sktime_t  ***/

SK_INLINE
sktime_t
rwRecGetEndTime(
    const rwRec        *r)
{
    return (sktime_t)r->eTime;
}

SK_INLINE
void
rwRecMemGetEndTime(
    const rwRec        *r,
    void               *out_vp)
{
    MEMCPY64(out_vp, &r->eTime);
}

SK_INLINE
uint32_t
rwRecGetEndSeconds(
    const rwRec        *r)
{
    return ((uint32_t)(((sktime_t)r->eTime) / 1000));
}

SK_INLINE
void
rwRecMemGetEndSeconds(
    const rwRec        *r,
    void               *out_vp)
{
    uint32_t t = rwRecGetEndSeconds(r);
    memcpy(out_vp, &t, sizeof(t));
}



/***  Sensor (sensor) is an sk_sensor_id_t  ***/

SK_INLINE
sk_sensor_id_t
rwRecGetSensor(
    const rwRec        *r)
{
    return r->sID;
}

SK_INLINE
void
rwRecSetSensor(
    rwRec              *r,
    sk_sensor_id_t      in_v)
{
    r->sID = in_v;
}

SK_INLINE
void
rwRecMemGetSensor(
    const rwRec        *r,
    void               *out_vp)
{
    MEMCPY16(out_vp, &r->sID);
}

SK_INLINE
void
rwRecMemSetSensor(
    rwRec              *r,
    const void         *in_vp)
{
    MEMCPY16(&r->sID, in_vp);
}



/***  FlowType (class, type) is an sk_flowtype_id_t  ***/

SK_INLINE
sk_flowtype_id_t
rwRecGetFlowType(
    const rwRec        *r)
{
    return r->flow_type;
}

SK_INLINE
void
rwRecSetFlowType(
    rwRec              *r,
    sk_flowtype_id_t    in_v)
{
    r->flow_type = in_v;
}

SK_INLINE
void
rwRecMemGetFlowType(
    const rwRec        *r,
    void               *out_vp)
{
    MEMCPY8(out_vp, &r->flow_type);
}

SK_INLINE
void
rwRecMemSetFlowType(
    rwRec              *r,
    const void         *in_vp)
{
    MEMCPY8(&r->flow_type, in_vp);
}



/***  SNMP Input (input) is a uint32_t  ***/

SK_INLINE
uint32_t
rwRecGetInput(
    const rwRec        *r)
{
    return r->input;
}

SK_INLINE
void
rwRecSetInput(
    rwRec              *r,
    uint32_t            in_v)
{
    r->input = in_v;
}

SK_INLINE
void
rwRecMemGetInput(
    const rwRec        *r,
    void               *out_vp)
{
    MEMCPY32(out_vp, &r->input);
}

SK_INLINE
void
rwRecMemSetInput(
    rwRec              *r,
    const void         *in_vp)
{
    MEMCPY32(&r->input, in_vp);
}



/***  SNMP Output (output) is a uint32_t  ***/

SK_INLINE
uint32_t
rwRecGetOutput(
    const rwRec        *r)
{
    return r->output;
}

SK_INLINE
void
rwRecSetOutput(
    rwRec              *r,
    uint32_t            in_v)
{
    r->output = in_v;
}

SK_INLINE
void
rwRecMemGetOutput(
    const rwRec        *r,
    void               *out_vp)
{
    MEMCPY32(out_vp, &r->output);
}

SK_INLINE
void
rwRecMemSetOutput(
    rwRec              *r,
    const void         *in_vp)
{
    MEMCPY32(&r->output, in_vp);
}



/***  Attributes (attributes) is a uint8_t  ***/

SK_INLINE
uint8_t
rwRecGetTcpState(
    const rwRec        *r)
{
    return (r->tcp_state & SK_TCPSTATE_MASK);
}

SK_INLINE
void
rwRecSetTcpState(
    rwRec              *r,
    uint8_t             in_v)
{
    r->tcp_state = (r->tcp_state & 0x80) | (SK_TCPSTATE_MASK & in_v);
}

SK_INLINE
void
rwRecMemGetTcpState(
    const rwRec        *r,
    void               *out_vp)
{
    *((uint8_t*)out_vp) = ((uint8_t)(r->tcp_state & SK_TCPSTATE_MASK));
}

SK_INLINE
void
rwRecMemSetTcpState(
    rwRec              *r,
    const void         *in_vp)
{
    r->tcp_state = ((r->tcp_state & 0x80)
                    | (SK_TCPSTATE_MASK & (*((uint8_t*)in_vp))));
}



/***  Application (application) is a uint16_t  ***/

SK_INLINE
uint16_t
rwRecGetApplication(
    const rwRec        *r)
{
    return r->application;
}

SK_INLINE
void
rwRecSetApplication(
    rwRec              *r,
    uint16_t            in_v)
{
    r->application = in_v;
}

SK_INLINE
void
rwRecMemGetApplication(
    const rwRec        *r,
    void               *out_vp)
{
    MEMCPY16(out_vp, &r->application);
}

SK_INLINE
void
rwRecMemSetApplication(
    rwRec              *r,
    const void         *in_vp)
{
    MEMCPY16(&r->application, in_vp);
}



/***  ICMP Type  ***/

SK_INLINE
uint8_t
rwRecGetIcmpType(
    const rwRec        *r)
{
    return (0xFF & (r->dPort >> 8));
}

SK_INLINE
void
rwRecSetIcmpType(
    rwRec              *r,
    uint8_t in_v)
{
    r->dPort = (((r->dPort & 0x00FF) | ((in_v & 0xFF) << 8)));
}

SK_INLINE
void
rwRecMemGetIcmpType(
    const rwRec        *r,
    void               *out_vp)
{
    *(uint8_t*)out_vp = ((uint8_t)(0xFF & (r->dPort >> 8)));
}

/***  ICMP Code  ***/

SK_INLINE
uint8_t
rwRecGetIcmpCode(
    const rwRec        *r)
{
    return (0xFF & r->dPort);
}

SK_INLINE
void
rwRecSetIcmpCode(
    rwRec              *r,
    uint8_t             in_v)
{
    r->dPort = (((r->dPort & 0xFF00) | (in_v & 0xFF)));
}

SK_INLINE
void
rwRecMemGetIcmpCode(
    const rwRec        *r,
    void               *out_vp)
{
    *(uint8_t*)out_vp = ((uint8_t)(0xFF & r->dPort));
}

/***  ICMP Type And Code  ***/

SK_INLINE
uint16_t
rwRecGetIcmpTypeAndCode(
    const rwRec        *r)
{
    return r->dPort;
}

SK_INLINE
void
rwRecSetIcmpTypeAndCode(
    rwRec              *r,
    uint16_t            in_v)
{
    r->dPort = in_v;
}

SK_INLINE
void
rwRecMemGetIcmpTypeAndCode(
    const rwRec        *r,
    void               *out_vp)
{
    MEMCPY16(out_vp, &r->dPort);
}

SK_INLINE
void
rwRecMemSetIcmpTypeAndCode(
    rwRec              *r,
    const void         *in_vp)
{
    MEMCPY16(&r->dPort, in_vp);
}



/***  Sidecar and Lua  ***/

SK_INLINE
int64_t
rwRecGetSidecar(
    const rwRec        *r)
{
    return r->sidecar;
}

SK_INLINE
void
rwRecSetSidecar(
    rwRec              *r,
    int64_t             in_v)
{
#if 0
    if (in_v != LUA_NOREF) {
        fprintf(stderr, "%s:%d: Setting sidecar to %" PRId64 "\n",
                __FILE__, __LINE__, in_v);
    }
#endif
    r->sidecar = in_v;
}


SK_INLINE
lua_State *
rwRecGetLuaState(
    const rwRec        *r)
{
    return r->lua_state;
}



/***  Memo  ***/

SK_INLINE
uint16_t
rwRecGetMemo(
    const rwRec        *r)
{
    return r->memo;
}

SK_INLINE
void
rwRecSetMemo(
    rwRec              *r,
    uint16_t            in_v)
{
    r->memo = in_v;
}

SK_INLINE
void
rwRecMemGetMemo(
    const rwRec        *r,
    void               *out_vp)
{
    MEMCPY16(out_vp, &r->memo);
}

SK_INLINE
void
rwRecMemSetMemo(
    rwRec              *r,
    const void         *in_vp)
{
    MEMCPY16(&r->memo, in_vp);
}


SK_INLINE
int
rwRecIsICMP(
    const rwRec        *r)
{
    return ((IPPROTO_ICMP == rwRecGetProto(r))
            || (rwRecIsIPv6(r) && (IPPROTO_ICMPV6 == rwRecGetProto(r))));
}

SK_INLINE
int
rwRecIsWeb(
    const rwRec        *r)
{
    return ((IPPROTO_TCP == rwRecGetProto(r))
            && (SK_WEBPORT_CHECK(rwRecGetSPort(r))
                || SK_WEBPORT_CHECK(rwRecGetDPort(r))));
}



SK_INLINE
void
rwRecInitialize(
    rwRec              *r,
    lua_State          *lua_state)
{
    memset(r, 0, offsetof(rwRec, sidecar));
    rwRecSetSensor(r, SK_INVALID_SENSOR);
    rwRecSetFlowType(r, SK_INVALID_FLOWTYPE);
    rwRecSetSidecar(r, LUA_NOREF);
    r->lua_state = lua_state;
}


SK_INLINE
void
rwRecInitializeArray(
    rwRec              *rec_array,
    lua_State          *lua_state,
    size_t              array_rec_count)
{
    rwRec *r;

    memset(rec_array, 0, array_rec_count * sizeof(rwRec));
    for (r = rec_array; array_rec_count > 0; ++r) {
        --array_rec_count;
        rwRecSetSensor(r, SK_INVALID_SENSOR);
        rwRecSetFlowType(r, SK_INVALID_FLOWTYPE);
        rwRecSetSidecar(r, LUA_NOREF);
        r->lua_state = lua_state;
    }
}

SK_INLINE
void
rwRecReset(
    rwRec              *r)
{
    int64_t sc_idx;

    memset(r, 0, offsetof(rwRec, sidecar));
    rwRecSetSensor(r, SK_INVALID_SENSOR);
    rwRecSetFlowType(r, SK_INVALID_FLOWTYPE);
    if ((NULL != r->lua_state)
        && ((sc_idx = rwRecGetSidecar(r)) != LUA_NOREF))
    {
#if 0
        fprintf(stderr, "Removing sidecar %" PRId64 " from registry\n"
                sc_idx);
#endif
        luaL_unref(r->lua_state, LUA_REGISTRYINDEX, sc_idx);
        rwRecSetSidecar(r, LUA_NOREF);
    }
}

SK_INLINE
void
rwRecResetArray(
    rwRec              *rec_array,
    size_t              array_rec_count)
{
    rwRec *r;
    int64_t sc_idx;

    for (r = rec_array; array_rec_count > 0; ++r) {
        --array_rec_count;
        memset(r, 0, offsetof(rwRec, sidecar));
        rwRecSetSensor(r, SK_INVALID_SENSOR);
        rwRecSetFlowType(r, SK_INVALID_FLOWTYPE);
        if ((NULL != r->lua_state)
            && ((sc_idx = rwRecGetSidecar(r)) != LUA_NOREF))
        {
#if 0
            fprintf(stderr, "Removing sidecar %" PRId64 " from registry\n"
                    sc_idx);
#endif
            luaL_unref(r->lua_state, LUA_REGISTRYINDEX, sc_idx);
            rwRecSetSidecar(r, LUA_NOREF);
        }
    }
}


/*
 *    The function assumes rwRecInitialize() has been called on both
 *    'src_rwrec' and 'dst_rwrec'.  The default behavior of this
 *    function is to remove any sidecar data from 'dst_rwrec', copy
 *    the fixed-rwRec portion from 'src_rwrec' to 'dst_rwrec', and
 *    then make a complete copy of any sidecar data on 'src_rwrec' to
 *    'dst_rwrec'.
 *
 *    To completely ignore the sidecar data on the source and
 *    destination records, include SK_RWREC_COPY_FIXED in 'flags'.
 *
 *    Existing sidecar data is removed from the destination record
 *    prior to the copy.  To prevent this check, add
 *    SK_RWREC_COPY_UNINIT to 'flags', which treats 'dst_rwrec' as
 *    uninitialized memory.
 *
 *    Adding SK_RWREC_COPY_MOVE to 'flags' causes the sidecar data to
 *    "move" from 'src_rwrec' to 'dst_rwrec'.
*/
SK_INLINE
void
rwRecCopy(
    void               *v_dst_rwrec,
    const void         *v_src_rwrec,
    unsigned int        flags)
{
    const rwRec *src_rec;
    rwRec *dst_rec;
    lua_State *S;
    int64_t sc_idx;

    /* copy the non-sidecar part of the records */
    memcpy(v_dst_rwrec, v_src_rwrec, offsetof(rwRec, sidecar));

    if (flags & SK_RWREC_COPY_FIXED) {
        return;
    }

    dst_rec = (rwRec *)v_dst_rwrec;

    /* remove existing sidecar data from the destination */
    if (flags & SK_RWREC_COPY_UNINIT) {
        dst_rec->lua_state = NULL;
    } else if ((NULL != dst_rec->lua_state)
               && ((sc_idx = rwRecGetSidecar(dst_rec)) != LUA_NOREF))
    {
        luaL_unref(dst_rec->lua_state, LUA_REGISTRYINDEX, sc_idx);
    }

    src_rec = (const rwRec *)v_src_rwrec;

    S = src_rec->lua_state;
    if (NULL == S) {
        /* no sidecar data on the source */
        rwRecSetSidecar(dst_rec, LUA_NOREF);
        return;
    }

    if (NULL == dst_rec->lua_state) {
        dst_rec->lua_state = src_rec->lua_state;
    }
    /* FIXME: Handle case where the lua states are different */

    sc_idx = rwRecGetSidecar(src_rec);
    if (LUA_NOREF == sc_idx) {
        rwRecSetSidecar(dst_rec, LUA_NOREF);
        return;
    }
    if (flags & SK_RWREC_COPY_MOVE) {
        rwRecSetSidecar((rwRec *)src_rec, LUA_NOREF);
        rwRecSetSidecar(dst_rec, sc_idx);
        return;
    }

    /* copy the table.  FIXME: Is there a way to get Lua to handle
     * this for us?  Reference counting? */
    if (lua_rawgeti(S, LUA_REGISTRYINDEX, sc_idx) != LUA_TTABLE) {
        /* not a table.  this is weird */
        lua_pop(S, 1);
        rwRecSetSidecar(dst_rec, LUA_NOREF);
        return;
    }

    /* create the table on the destination record */
    lua_newtable(S);

    /* loop over entries in src_rec's sidecar table and add to the
     * dst_tbl. After next line, stack is src_tbl, dst_tbl, nil */
    lua_pushnil(S);
    while (lua_next(S, -3) != 0) {
        /* 'key' is at index -2 and 'value' is at index -1 */
        /* we want the stack to contain key, key, value for the
         * settable() command.  we push a copy of the key, swap
         * the key and value, then call settable(). */
        lua_pushvalue(S, -2);
        lua_insert(S, -2);
        lua_settable(S, -4);
    }

    /* the copied table is at the top of the stack; get a reference to
     * it and remove it */
    rwRecSetSidecar(dst_rec, luaL_ref(S, LUA_REGISTRYINDEX));

    /* done with the source table */
    lua_pop(S, 1);
}


SK_INLINE
void
RWREC_CLEAR(
    rwRec              *r)
{
    memset(r, 0, sizeof(rwRec));
    rwRecSetSensor(r, SK_INVALID_SENSOR);
    rwRecSetFlowType(r, SK_INVALID_FLOWTYPE);
    rwRecSetSidecar(r, LUA_NOREF);
}

#endif  /* SK_HAVE_INLINE || defined(SOURCE_FILE_RWREC_C) */

#ifdef __cplusplus
}
#endif
#endif /* _RWREC_H */

/*
** Local Variables:
** mode:c
** indent-tabs-mode:nil
** c-basic-offset:4
** End:
*/
