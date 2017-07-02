/*
** Copyright (C) 2007-2017 by Carnegie Mellon University.
**
** @OPENSOURCE_LICENSE_START@
** See license information in ../../LICENSE.txt
** @OPENSOURCE_LICENSE_END@
*/

/*
**  skipfix.c
**
**    SiLK Flow Record / IPFIX translation core
**
**    Brian Trammell
**    February 2007
*/

#define LIBFLOWSOURCE_SOURCE 1
#include <silk/silk.h>

RCSIDENT("$SiLK: skipfix.c efd886457770 2017-06-21 18:43:23Z mthomas $");

#include <silk/skipfixcert.h>
#include <silk/rwrec.h>
#include <silk/skipaddr.h>
#include <silk/skipfix.h>
#include <silk/sklog.h>
#include <silk/skvector.h>
#include <silk/utils.h>

#ifdef SKIPFIX_TRACE_LEVEL
#define TRACEMSG_LEVEL 1
#endif
#define TRACEMSG(x)  TRACEMSG_TO_TRACEMSGLVL(1, x)
#include <silk/sktracemsg.h>


/* LOCAL DEFINES AND TYPEDEFS */

/* Whether to export information elements in IPFIX files we write */
#ifndef SKIPFIX_EXPORT_ELEMENTS
#define SKIPFIX_EXPORT_ELEMENTS 0
#endif

/* The IPFIX Private Enterprise Number for CERT */
#define IPFIX_CERT_PEN  6871

/* Internal Template ID for incoming IPFIX flows. */
#define SKI_FIXREC_TID        0xAFEB

/* Internal Template ID for TCP information. */
#define SKI_TCP_STML_TID        0xAFEC

/* Internal Template ID for NetFlowV9 Sampling Options Template */
#define SKI_NF9_SAMPLING_TID    0xAFED

/* Internal Template ID for Element Type Options Template */
#define SKI_ELEMENT_TYPE_TID    0xAFEE

/* Bit in Template ID that yaf sets for templates containing reverse
 * elements */
#define SKI_YAF_REVERSE_BIT     0x0010

/* Template ID used by yaf for a yaf stats option record */
#define SKI_YAF_STATS_TID       0xD000

/* Template ID used by yaf for a subTemplateMultiList containing only
 * forward TCP flags information. */
#define SKI_YAF_TCP_FLOW_TID    0xC003

/* Name of environment variable that, when set, cause SiLK to print
 * the templates that it receives to the log. */
#define SKI_ENV_PRINT_TEMPLATES  "SILK_IPFIX_PRINT_TEMPLATES"

/*
 *  val = CLAMP_VAL(val, max);
 *
 *    If 'val' is greater then 'max', return 'max'.  Otherwise,
 *    return (max & val).
 */
#define CLAMP_VAL(val, max) \
    (((val) > (max)) ? (max) : ((max) & (val)))

/* One more than UINT32_MAX */
#define ROLLOVER32 ((intmax_t)UINT32_MAX + 1)

/*
 *    For NetFlow V9, when the absolute value of the magnitude of the
 *    difference between the sysUpTime and the flowStartSysUpTime is
 *    greater than this value (in milliseconds), assume one of the
 *    values has rolled over.
 */
#define MAXIMUM_FLOW_TIME_DEVIATION  ((intmax_t)INT32_MAX)

/* These are IPFIX information elements either in the standard space
 * or specific to NetFlowV9.  However, these elements are not defined
 * in all versions of libfixbuf. */
static fbInfoElement_t ski_std_info_elements[] = {
    FB_IE_NULL
};


/* Bytes of padding to add to ski_fixrec_spec and ski_fixrec_st to
 * get a multiple of 64bits */
#define SKI_FIXREC_PADDING  1

/*
 *    Encoding that handles all expected IPFIX records.
 */
static fbInfoElementSpec_t ski_fixrec_spec[] = {
    /* Millisecond start and end (epoch) (native time) */
    { (char*)"flowStartMilliseconds",              8, 0 },
    { (char*)"flowEndMilliseconds",                8, 0 },
    /* 4-tuple */
    { (char*)"sourceIPv6Address",                 16, 0 },
    { (char*)"destinationIPv6Address",            16, 0 },
    { (char*)"sourceIPv4Address",                  4, 0 },
    { (char*)"destinationIPv4Address",             4, 0 },
    { (char*)"sourceTransportPort",                2, 0 },
    { (char*)"destinationTransportPort",           2, 0 },
    /* Router interface information */
    { (char*)"ipNextHopIPv4Address",               4, 0 },
    { (char*)"ipNextHopIPv6Address",              16, 0 },
    { (char*)"ingressInterface",                   4, 0 },
    { (char*)"egressInterface",                    4, 0 },
    /* Counters (reduced length encoding for SiLK) */
    { (char*)"packetDeltaCount",                   8, 0 },
    { (char*)"octetDeltaCount",                    8, 0 },
    /* Protocol; sensor information */
    { (char*)"protocolIdentifier",                 1, 0 },
    { (char*)"silkFlowType",                       1, 0 },
    { (char*)"silkFlowSensor",                     2, 0 },
    /* Flags */
    { (char*)"tcpControlBits",                     1, 0 },
    { (char*)"initialTCPFlags",                    1, 0 },
    { (char*)"unionTCPFlags",                      1, 0 },
    { (char*)"silkTCPState",                       1, 0 },
    { (char*)"silkAppLabel",                       2, 0 },
    /* MAC Addresses */
    { (char*)"sourceMacAddress",                   6, 0 },
    { (char*)"destinationMacAddress",              6, 0 },
    /* TOS */
    { (char*)"ipClassOfService",                   1, 0 },
    { (char*)"reverseIpClassOfService",            1, 0 },
    /* Total counter support */
    { (char*)"packetTotalCount",                   8, 0 },
    { (char*)"octetTotalCount",                    8, 0 },
    { (char*)"initiatorPackets",                   8, 0 },
    { (char*)"initiatorOctets",                    8, 0 },
    /* Reverse counter support */
    { (char*)"reversePacketDeltaCount",            8, 0 },
    { (char*)"reverseOctetDeltaCount",             8, 0 },
    { (char*)"reversePacketTotalCount",            8, 0 },
    { (char*)"reverseOctetTotalCount",             8, 0 },
    { (char*)"responderPackets",                   8, 0 },
    { (char*)"responderOctets",                    8, 0 },
    /* Microsecond start and end (RFC1305-style) (extended time) */
    { (char*)"flowStartMicroseconds",              8, 0 },
    { (char*)"flowEndMicroseconds",                8, 0 },
    /* Nanosecond start and end (RFC1305-style) */
    { (char*)"flowStartNanoseconds",               8, 0 },
    { (char*)"flowEndNanoseconds",                 8, 0 },
    /* SysUpTime, used to handle Netflow v9 SysUpTime offset times */
    { (char*)"systemInitTimeMilliseconds",         8, 0 },
    /* Second start and end (extended time) */
    { (char*)"flowStartSeconds",                   4, 0 },
    { (char*)"flowEndSeconds",                     4, 0 },
    /* Flow durations (extended time) */
    { (char*)"flowDurationMicroseconds",           4, 0 },
    { (char*)"flowDurationMilliseconds",           4, 0 },
    /* Microsecond delta start and end (extended time) */
    { (char*)"flowStartDeltaMicroseconds",         4, 0 },
    { (char*)"flowEndDeltaMicroseconds",           4, 0 },
    /* Initial packet roundtrip */
    { (char*)"reverseFlowDeltaMilliseconds",       4, 0 },
    /* SysUpTime-based fields */
    { (char*)"flowStartSysUpTime",                 4, 0 },
    { (char*)"flowEndSysUpTime",                   4, 0 },
    /* Reverse flags */
    { (char*)"reverseTcpControlBits",              1, 0 },
    { (char*)"reverseInitialTCPFlags",             1, 0 },
    { (char*)"reverseUnionTCPFlags",               1, 0 },
    /* End reason */
    { (char*)"flowEndReason",                      1, 0 },
    /* Flow attributes */
    { (char*)"flowAttributes",                     2, 0 },
    { (char*)"reverseFlowAttributes",              2, 0 },
    /* Vlan IDs */
    { (char*)"vlanId",                             2, 0 },
    { (char*)"postVlanId",                         2, 0 },
    { (char*)"reverseVlanId",                      2, 0 },
    { (char*)"reversePostVlanId",                  2, 0 },
    /* ASN */
    { (char*)"bgpSourceAsNumber",                  4, 0 },
    { (char*)"bgpDestinationAsNumber",             4, 0 },
    /* MPLS */
    { (char*)"mplsTopLabelIPv4Address",            4, 0 },
    { (char*)"mplsTopLabelStackSection",           3, 0 },
    { (char*)"mplsLabelStackSection2",             3, 0 },
    { (char*)"mplsLabelStackSection3",             3, 0 },
    { (char*)"mplsLabelStackSection4",             3, 0 },
    { (char*)"mplsLabelStackSection5",             3, 0 },
    { (char*)"mplsLabelStackSection6",             3, 0 },
    { (char*)"mplsTopLabelPrefixLength",           1, 0 },
    { (char*)"mplsTopLabelType",                   1, 0 },
    /* Firewall events */
    { (char*)"firewallEvent",                      1, 0 },
    { (char*)"NF_F_FW_EVENT",                      1, 0 },
    { (char*)"NF_F_FW_EXT_EVENT",                  2, 0 },
    /* ICMP */
    { (char*)"icmpTypeCodeIPv4",                   2, 0 },
    { (char*)"icmpTypeIPv4",                       1, 0 },
    { (char*)"icmpCodeIPv4",                       1, 0 },
    { (char*)"icmpTypeCodeIPv6",                   2, 0 },
    { (char*)"icmpTypeIPv6",                       1, 0 },
    { (char*)"icmpCodeIPv6",                       1, 0 },
    /* Collection time and Observation time */
    { (char*)"collectionTimeMilliseconds",         8, 0 },
    { (char*)"observationTimeMilliseconds",        8, 0 },
    { (char*)"observationTimeMicroseconds",        8, 0 },
    { (char*)"observationTimeNanoseconds",         8, 0 },
    { (char*)"observationTimeSeconds",             4, 0 },
    /* Flow Sampler ID (IE48, name differs between fixbuf versions) */
    { (char*)"flowSamplerID",                      2, 1 }, /* current fixbuf */
    { (char*)"samplerId",                          2, 2 }, /* future fixbuf */
    /* flow direction */
    { (char*)"flowDirection",                      1, 0 },
    /* pad record to 64-bit boundary. */
#if SKI_FIXREC_PADDING != 0
    { (char*)"paddingOctets", SKI_FIXREC_PADDING, 0 },
#endif
    { (char*)"subTemplateMultiList",               0, 0 },
    FB_IESPEC_NULL
};

/* Keep this in sync with the ski_fixrec_spec[] above */
typedef struct ski_fixrec_st {
    uint64_t        flowStartMilliseconds;          /*   0-  7 */
    uint64_t        flowEndMilliseconds;            /*   8- 15 */

    uint8_t         sourceIPv6Address[16];          /*  16- 31 */
    uint8_t         destinationIPv6Address[16];     /*  32- 47 */

    uint32_t        sourceIPv4Address;              /*  48- 51 */
    uint32_t        destinationIPv4Address;         /*  52- 55 */

    uint16_t        sourceTransportPort;            /*  56- 57 */
    uint16_t        destinationTransportPort;       /*  58- 59 */

    uint32_t        ipNextHopIPv4Address;           /*  60- 63 */
    uint8_t         ipNextHopIPv6Address[16];       /*  64- 79 */
    uint32_t        ingressInterface;               /*  80- 83 */
    uint32_t        egressInterface;                /*  84- 87 */

    uint64_t        packetDeltaCount;               /*  88- 95 */
    uint64_t        octetDeltaCount;                /*  96-103 */

    uint8_t         protocolIdentifier;             /* 104     */
    uint8_t         silkFlowType;                   /* 105     */
    uint16_t        silkFlowSensor;                 /* 106-107 */

    uint8_t         tcpControlBits;                 /* 108     */
    uint8_t         initialTCPFlags;                /* 109     */
    uint8_t         unionTCPFlags;                  /* 110     */
    uint8_t         silkTCPState;                   /* 111     */
    uint16_t        silkAppLabel;                   /* 112-113 */

    /* MAC Addresses */
    uint8_t         sourceMacAddress[6];            /* 114-119 */
    uint8_t         destinationMacAddress[6];       /* 120-125 */

    /* TOS */
    uint8_t         ipClassOfService;               /* 126     */
    uint8_t         reverseIpClassOfService;        /* 127     */

    uint64_t        packetTotalCount;               /* 128-135 */
    uint64_t        octetTotalCount;                /* 136-143 */
    uint64_t        initiatorPackets;               /* 144-151 */
    uint64_t        initiatorOctets;                /* 152-159 */

    uint64_t        reversePacketDeltaCount;        /* 160-167 */
    uint64_t        reverseOctetDeltaCount;         /* 168-175 */
    uint64_t        reversePacketTotalCount;        /* 176-183 */
    uint64_t        reverseOctetTotalCount;         /* 184-191 */
    uint64_t        responderPackets;               /* 192-199 */
    uint64_t        responderOctets;                /* 200-207 */

    /* Time can be represented in many different formats: */

    /* start time as NTP microseconds (RFC1305); may either have end
     * Time in same format or as an flowDurationMicroseconds value. */
    uint64_t        flowStartMicroseconds;          /* 208-215 */
    uint64_t        flowEndMicroseconds;            /* 216-223 */

    /* start time as NTP nanoseconds (RFC1305) */
    uint64_t        flowStartNanoseconds;           /* 224-231 */
    uint64_t        flowEndNanoseconds;             /* 232-239 */

    /* SysUpTime: used for flow{Start,End}SysUpTime calculations.
     * Needed to support Netflow v9 in particular. */
    uint64_t        systemInitTimeMilliseconds;     /* 240-247 */

    /* start time and end times as seconds since UNIX epoch. no
     * flowDuration field */
    uint32_t        flowStartSeconds;               /* 248-251 */
    uint32_t        flowEndSeconds;                 /* 252-255 */

    /* elapsed time as either microsec or millisec.  used when the
     * flowEnd time is not given. */
    uint32_t        flowDurationMicroseconds;       /* 256-259 */
    uint32_t        flowDurationMilliseconds;       /* 260-263 */

    /* start time as delta (negative microsec offsets) from the export
     * time; may either have end time in same format or a
     * flowDurationMicroseconds value */
    uint32_t        flowStartDeltaMicroseconds;     /* 264-267 */
    uint32_t        flowEndDeltaMicroseconds;       /* 268-271 */

    /* start time of reverse flow, as millisec offset from start time
     * of forward flow */
    uint32_t        reverseFlowDeltaMilliseconds;   /* 272-275 */

    /* Start and end time as delta from the system init time.  Needed
     * to support Netflow v9. */
    uint32_t        flowStartSysUpTime;             /* 276-279 */
    uint32_t        flowEndSysUpTime;               /* 280-283 */

    /* Flags for the reverse flow: */
    uint8_t         reverseTcpControlBits;          /* 284     */
    uint8_t         reverseInitialTCPFlags;         /* 285     */
    uint8_t         reverseUnionTCPFlags;           /* 286     */

    uint8_t         flowEndReason;                  /* 287     */

    /* Flow attribute flags */
    uint16_t        flowAttributes;                 /* 288-289 */
    uint16_t        reverseFlowAttributes;          /* 290-291 */

    /* vlan IDs */
    uint16_t        vlanId;                         /* 292-293 */
    uint16_t        postVlanId;                     /* 294-295 */
    uint16_t        reverseVlanId;                  /* 296-297 */
    uint16_t        reversePostVlanId;              /* 298-299 */

    /* ASN */
    uint32_t        bgpSourceAsNumber;              /* 300-303 */
    uint32_t        bgpDestinationAsNumber;         /* 304-307 */

    /* MPLS */
    uint32_t        mplsTopLabelIPv4Address;        /* 308-311 */
    uint8_t         mplsLabels[18];                 /* 312-329 */
    uint8_t         mplsTopLabelPrefixLength;       /* 330     */
    uint8_t         mplsTopLabelType;               /* 331     */

    /* Firewall events */
    uint8_t         firewallEvent;                  /* 332     */
    uint8_t         NF_F_FW_EVENT;                  /* 333     */
    uint16_t        NF_F_FW_EXT_EVENT;              /* 334-335 */

    /* ICMP */
    uint16_t        icmpTypeCodeIPv4;               /* 336-337 */
    uint8_t         icmpTypeIPv4;                   /* 338     */
    uint8_t         icmpCodeIPv4;                   /* 339     */
    uint16_t        icmpTypeCodeIPv6;               /* 340-341 */
    uint8_t         icmpTypeIPv6;                   /* 342     */
    uint8_t         icmpCodeIPv6;                   /* 343     */

    /* Collection time and Observation time */
    uint64_t        collectionTimeMilliseconds;     /* 344-351 */
    uint64_t        observationTimeMilliseconds;    /* 352-359 */
    uint64_t        observationTimeMicroseconds;    /* 360-367 */
    uint64_t        observationTimeNanoseconds;     /* 368-375 */
    uint32_t        observationTimeSeconds;         /* 376-379 */

    /* Flow Sampler ID */
    uint16_t        samplerId;                      /* 380-381 */

    /* Flow Direction */
    uint8_t         flowDirection;                  /* 382 */

    /* padding */
#if SKI_FIXREC_PADDING != 0
    uint8_t         pad[SKI_FIXREC_PADDING];      /* 383 */
#endif

    /* TCP flags from yaf (when it is run without --silk) */
    fbSubTemplateMultiList_t stml;

} ski_fixrec_t;

/* Support for reading TCP flags from an IPFIX subTemplateMultiList as
 * exported by YAF.  This has Template ID SKI_TCP_STML_TID.
 *
 * Keep this in sync with the ski_tcp_stml_t defined below. */
static fbInfoElementSpec_t ski_tcp_stml_spec[] = {
    { (char*)"initialTCPFlags",                    1, 0 },
    { (char*)"unionTCPFlags",                      1, 0 },
    { (char*)"reverseInitialTCPFlags",             1, 0 },
    { (char*)"reverseUnionTCPFlags",               1, 0 },
    FB_IESPEC_NULL
};

/* Keep in sync with the ski_tcp_stml_spec[] defined above. */
typedef struct ski_tcp_stml_st {
    uint8_t         initialTCPFlags;
    uint8_t         unionTCPFlags;
    uint8_t         reverseInitialTCPFlags;
    uint8_t         reverseUnionTCPFlags;
} ski_tcp_stml_t;


/*
 *    Define the list of information elements and the corresponding
 *    struct for reading NetFlowV9 Options Template records that
 *    contains sampling information.  These records use internal
 *    template ID SKI_NF9_SAMPLING_TID.
 */
#define SKI_NF9_SAMPLING_PADDING 4
static fbInfoElementSpec_t ski_nf9_sampling_spec[] = {
    { (char*)"samplingInterval",          4, 0 },    /* 34 */

    { (char*)"flowSamplerRandomInterval", 4, 1 },    /* 50, current fixbuf */
    { (char*)"samplerRandomInterval",     4, 2 },    /* 50, future fixbuf */

    { (char*)"samplingAlgorithm",         1, 0 },    /* 35 */

    { (char*)"flowSamplerMode",           1, 1 },    /* 49, current fixbuf */
    { (char*)"samplerMode",               1, 2 },    /* 49, future fixbuf */

    { (char*)"flowSamplerID",             2, 1 },    /* 48, current fixbuf */
    { (char*)"samplerId",                 2, 2 },    /* 48, future fixbuf */

#if SKI_NF9_SAMPLING_PADDING != 0
    { (char*)"paddingOctets",             SKI_NF9_SAMPLING_PADDING, 0 },
#endif
    FB_IESPEC_NULL
};

typedef struct ski_nf9_sampling_st {
    uint32_t    samplingInterval;
    uint32_t    samplerRandomInterval;
    uint8_t     samplingAlgorithm;
    uint8_t     samplerMode;
    uint16_t    samplerId;
#if SKI_NF9_SAMPLING_PADDING != 0
    uint8_t     paddingOctets[SKI_NF9_SAMPLING_PADDING];
#endif
} ski_nf9_sampling_t;


/* This lists statistics values that yaf may export. */
/* Keep this in sync with ski_yaf_stats_t defined in skipfix.h */
static fbInfoElementSpec_t ski_yaf_stats_option_spec[] = {
    { (char*)"systemInitTimeMilliseconds",         8, 0 },
    { (char*)"exportedFlowRecordTotalCount",       8, 0 },
    { (char*)"packetTotalCount",                   8, 0 },
    { (char*)"droppedPacketTotalCount",            8, 0 },
    { (char*)"ignoredPacketTotalCount",            8, 0 },
    { (char*)"notSentPacketTotalCount",            8, 0 },
    { (char*)"expiredFragmentCount",               4, 0 },
#if 0
    { (char*)"assembledFragmentCount",             4, 0 },
    { (char*)"flowTableFlushEventCount",           4, 0 },
    { (char*)"flowTablePeakCount",                 4, 0 },
    { (char*)"meanFlowRate",                       4, 0 },
    { (char*)"meanPacketRate",                     4, 0 },
    { (char*)"exporterIPv4Address",                4, 0 },
#endif  /* 0 */
    { (char*)"exportingProcessId",                 4, 0 },
#if SKI_YAF_STATS_PADDING != 0
    { (char*)"paddingOctets", SKI_YAF_STATS_PADDING,  0 },
#endif
    FB_IESPEC_NULL
};

/* Values for the flowEndReason. this first set is defined by the
 * IPFIX spec */
#define SKI_END_IDLE            1
#define SKI_END_ACTIVE          2
#define SKI_END_CLOSED          3
#define SKI_END_FORCED          4
#define SKI_END_RESOURCE        5

/* SiLK will ignore flows with a flowEndReason of
 * SKI_END_YAF_INTERMEDIATE_FLOW */
#define SKI_END_YAF_INTERMEDIATE_FLOW 0x1F

/* Mask for the values of flowEndReason: want to ignore the next bit */
#define SKI_END_MASK            0x1f

/* Bits from flowEndReason: whether flow is a continuation */
#define SKI_END_ISCONT          0x80


/* Bits from flowAttributes */
#define SKI_FLOW_ATTRIBUTE_UNIFORM_PACKET_SIZE 0x01

/* A FIELD_IDENT is a 64-bit value that contains an IPFIX enterpriseId
 * in the upper 32 bits and an IPFIX elementId in the lower 32 */
#define FIELD_IDENT(fi_enterprise, fi_element)                  \
    ((((uint64_t)(fi_enterprise)) << 32) | (fi_element))

/*
 *    The global 'elem' variable is to increase processing speed.
 *
 *    The skiRwNextRecord() function takes different actions depending
 *    on whether the template (fbTemplate_t) for the record it is
 *    processing contains various information elements.
 *
 *    The template is examined by skiTemplateCallbackCtx() when it is
 *    initially received, reducing the overhead of examining the
 *    template for every record (at the expenses of looking for
 *    elements which skiRwNextRecord() may never actually need).
 *    Overall this should be a benefit as long as the number of
 *    records received is much higher than the number of templates
 *    received (in the TCP case, the templates are only sent once).
 *
 *    The 'elem' variable is the single instance of the 'elem_st'
 *    struct.  For each of the information elements of interest, the
 *    'elem' variable specifies a bit position.  (The 'elem' variable
 *    provides a sort of enumeration.)
 *
 *    The TEMPLATE_SET_BIT() macro sets one bit on a bitmap.  The
 *    macro takes the bitmap and the member of the 'elem_st'
 *    structure whose bit position is to be set.
 *
 *    The TEMPLATE_GET_BIT() macro checks whether a bit is set on a
 *    bitmap.  It takes the bitmap and the member of the 'elem_st'
 *    structure whose bit position you want to examine.
 */
static const struct elem_st {
    /* either sourceIPv4Address or destinationIPv4Address */
    uint8_t     sourceIPv4Address;

    /* either sourceIPv6Address or destinationIPv6Address */
    uint8_t     sourceIPv6Address;

    uint8_t     NF_F_FW_EVENT;
    uint8_t     NF_F_FW_EXT_EVENT;
    uint8_t     collectionTimeMilliseconds;
    uint8_t     exportTimeSeconds;
    uint8_t     firewallEvent;
    uint8_t     flowDurationMicroseconds;
    uint8_t     flowDurationMilliseconds;
    uint8_t     flowEndDeltaMicroseconds;
    uint8_t     flowEndMicroseconds;
    uint8_t     flowEndMilliseconds;
    uint8_t     flowEndNanoseconds;
    uint8_t     flowEndSeconds;
    uint8_t     flowStartDeltaMicroseconds;
    uint8_t     flowStartMicroseconds;
    uint8_t     flowStartMilliseconds;
    uint8_t     flowStartNanoseconds;
    uint8_t     flowStartSeconds;
    uint8_t     flowStartSysUpTime;
    uint8_t     mplsTopLabelStackSection;
    uint8_t     observationTimeMicroseconds;
    uint8_t     observationTimeMilliseconds;
    uint8_t     observationTimeNanoseconds;
    uint8_t     observationTimeSeconds;
    uint8_t     postVlanId;
    uint8_t     reverseInitialTCPFlags;
    uint8_t     reverseTcpControlBits;
    uint8_t     reverseVlanId;
    uint8_t     systemInitTimeMilliseconds;

    /* both IE49,IE50 (samplerMode, samplerRandomInterval) are present */
    uint8_t     samplerMode;

    /* both IE35,IE34 (samplingAlgorithm, samplingInterval) are present */
    uint8_t     samplingAlgorithm;
} elem = {
    1,      /*  sourceIPv4Address or destinationIPv4Address */
    2,      /*  sourceIPv6Address or destinationIPv6Address */

    3,      /*  NF_F_FW_EVENT */
    4,      /*  NF_F_FW_EXT_EVENT */
    5,      /*  collectionTimeMilliseconds */
    6,      /*  exportTimeSeconds */
    7,      /*  firewallEvent */
    8,      /*  flowDurationMicroseconds */
    9,      /*  flowDurationMilliseconds */
    10,     /*  flowEndDeltaMicroseconds */
    11,     /*  flowEndMicroseconds */
    12,     /*  flowEndMilliseconds */
    13,     /*  flowEndNanoseconds */
    14,     /*  flowEndSeconds */
    15,     /*  flowStartDeltaMicroseconds */
    16,     /*  flowStartMicroseconds */
    17,     /*  flowStartMilliseconds */
    18,     /*  flowStartNanoseconds */
    19,     /*  flowStartSeconds */
    20,     /*  flowStartSysUpTime */
    21,     /*  mplsTopLabelStackSection */
    22,     /*  observationTimeMicroseconds */
    23,     /*  observationTimeMilliseconds */
    24,     /*  observationTimeNanoseconds */
    25,     /*  observationTimeSeconds */
    26,     /*  postVlanId */
    27,     /*  reverseInitialTCPFlags */
    28,     /*  reverseTcpControlBits */
    29,     /*  reverseVlanId */
    30,     /*  systemInitTimeMilliseconds */

    /* The following are only seen in options templates, so the bit
     * position here can repeat those above */

    1,      /*  samplerMode and samplerRandomInterval */
    2       /*  samplingAlgorithm and samplingInterval */
};

#define TEMPLATE_SET_BIT(tsb_bitmap, tsb_member)        \
    tsb_bitmap |= (1 << (elem. tsb_member ))

#define TEMPLATE_GET_BIT(tgb_bitmap, tgb_member)        \
    ((tgb_bitmap) & (1 << elem. tgb_member ))

#define ASSERT_IE_NAME_IS(aini_ie, aini_name)                           \
    assert(elem. aini_name                                              \
           && 0==strcmp((aini_ie)->ref.canon->ref.name, #aini_name))

/*
 *    There is a single infomation model.
 */
static fbInfoModel_t *ski_model = NULL;

/*
 *    When processing files with fixbuf, the session object
 *    (fbSession_t) is owned the reader/write buffer (fBuf_t).
 *
 *    When doing network processing, the fBuf_t does not own the
 *    session.  We use this global vector to maintain those session
 *    pointers so they can be freed at shutdown.
 */
static sk_vector_t *session_list = NULL;

/*
 *    If non-zero, print the templates when they arrive.  This can be
 *    set by defining the environment variable specified in
 *    SKI_ENV_PRINT_TEMPLATES.
 */
static int print_templates = 0;

/*
 *    The names of IE 48, 49, 50 used by fixbuf (flowSamplerFOO) do
 *    not match the names specified by IANA (samplerFOO).  At some
 *    point the names used by fixbuf will change, and this variable is
 *    for forward compatibility to determine which names fixbuf uses.
 *    It is set by skiInitialize().
 *
 *    Variables and structure members in this file use the IANA name.
 */
static uint32_t sampler_flags = 0;


/* FUNCTION DEFINITIONS */

/*
 *    Return a pointer to the single information model.  If necessary,
 *    create and initialize it.
 */
static fbInfoModel_t *
skiInfoModel(
    void)
{
    const char *env;

    if (!ski_model) {
        ski_model = skipfix_information_model_create(SK_INFOMODEL_UNIQUE);
        fbInfoModelAddElementArray(ski_model, ski_std_info_elements);

        env = getenv(SKI_ENV_PRINT_TEMPLATES);
        if (env && *env && strcmp("0", env)) {
            print_templates = 1;
        }
    }

    return ski_model;
}

/*
 *    Free the single information model.
 */
static void
skiInfoModelFree(
    void)
{
    skipfix_information_model_destroy(ski_model);
    ski_model = NULL;
}


void
skiInitialize(
    void)
{
    fbInfoModel_t *model;
    const fbInfoElementSpec_t *spec;
    uint32_t flags;

    model = skiInfoModel();
    flags = 0;

    for (spec = ski_nf9_sampling_spec; spec->name; ++spec) {
        if (0 == spec->flags) {
            assert(fbInfoModelGetElementByName(model, spec->name));
        }
        else if (fbInfoModelGetElementByName(model, spec->name)) {
            if (0 == flags) {
                flags = spec->flags;
            } else if (spec->flags != flags) {
                skAppPrintErr("Info Element '%s' is in model; flags = %u",
                              spec->name, flags);
                skAbort();
            }
        } else if (flags && spec->flags == flags) {
            skAppPrintErr("Info Element '%s' not in model; flags = %u",
                          spec->name, flags);
            skAbort();
        }
    }

    sampler_flags = flags;

    skiInfoModelFree();
}



/*
 *  skiPrintTemplate(session, template, template_id);
 *
 *    Function to print the contents of the 'template'.  The 'session'
 *    is used to get the domain which is printed with the
 *    'template_id'.
 *
 *    This function is normally invoked by the template callback when
 *    the 'print_templates' variable is true, and that variable is
 *    normally enabled by the environment variable named in
 *    SKI_ENV_PRINT_TEMPLATES.
 */
static void
skiPrintTemplate(
    fbSession_t        *session,
    fbTemplate_t       *tmpl,
    uint16_t            tid)
{
    fbInfoElement_t *ie;
    uint32_t i;
    uint32_t count;
    uint32_t domain;

    domain = fbSessionGetDomain(session);
    count = fbTemplateCountElements(tmpl);

    INFOMSG(("Domain 0x%04X, TemplateID 0x%04X,"
             " Contains %" PRIu32 " Elements, Enabled by %s"),
            domain, tid, count, SKI_ENV_PRINT_TEMPLATES);

    for (i = 0; i < count && (ie = fbTemplateGetIndexedIE(tmpl, i)); ++i) {
        if (0 == ie->ent) {
            INFOMSG(("Domain 0x%04X, TemplateID 0x%04X, Position %3u,"
                     " Length %5u, IE %11u, Name %s"),
                    domain, tid, i, ie->len, ie->num, ie->ref.canon->ref.name);
        } else {
            INFOMSG(("Domain 0x%04X, TemplateID 0x%04X, Position %3u,"
                     " Length %5u, IE %5u/%5u, Name %s"),
                    domain, tid, i,
                    ie->len, ie->ent, ie->num, ie->ref.canon->ref.name);
        }
    }
}


/*
 *    The skiTemplateCallbackCtx() callback is invoked whenever the
 *    session receives a new template.  This function must have the
 *    signature defined by the 'fbTemplateCtxCallback_fn' typedef.
 *    The callback set by calling fbSessionAddTemplateCtxCallback().
 *
 *    One purpose of the callback is the tell fixbuf how to process
 *    items in a subTemplateMultiList.  We tell fixbuf to map from
 *    the two templates that yaf uses for TCP flags (one of which has
 *    reverse elements and one of which does not) to the struct used
 *    in this file.
 *
 *    The callback also examines the template and sets a context
 *    pointer that contains high bits for certain information
 *    elements.  See the detailed comment above the "struct elem_st"
 *    definition.
 *
 *    Finally, if the SKI_ENV_PRINT_TEMPLATES environment variable is
 *    true, the templates are printed to the log file.
 */
static void
skiTemplateCallbackCtx(
    fbSession_t            *session,
    uint16_t                tid,
    fbTemplate_t           *tmpl,
    void                  **ctx,
    fbTemplateCtxFree_fn   *fn)
{
    unsigned int samplingAlgorithm;
    unsigned int samplerMode;
    fbInfoElement_t *ie;
    uintptr_t bmap;
    uint32_t count;
    uint32_t i;

    TRACEMSG(("Template callback called for Template ID 0x%04X [%p]",
              tid, tmpl));

    if (SKI_YAF_TCP_FLOW_TID == (tid & ~SKI_YAF_REVERSE_BIT)) {
        fbSessionAddTemplatePair(session, tid, SKI_TCP_STML_TID);
        *ctx = NULL;
        *fn = NULL;

    } else if (fbTemplateGetOptionsScope(tmpl)) {
        /* do not define any template pairs for this template */
        fbSessionAddTemplatePair(session, tid, 0);

        /* assume if it has the template ID used by the yaf stats
         * packet than that is what it is.  if not, check for
         * NetFlowV9 sampling values */
        if (tid == SKI_YAF_STATS_TID) {
            *ctx = NULL;
            *fn = NULL;
        } else {
            /* must check for multiple elements */
            samplingAlgorithm = samplerMode = 0;

            bmap = 0;

            count = fbTemplateCountElements(tmpl);
            for (i=0; i < count && (ie = fbTemplateGetIndexedIE(tmpl, i)); ++i)
            {
                switch (FIELD_IDENT(ie->ent, ie->num)) {
                  case  34:
                  case  35:
                    /* verify that both samplingInterval and
                     * samplingAlgorithm are present */
                    ++samplingAlgorithm;
                    if (2 == samplingAlgorithm) {
                        bmap |= 1;
                        TEMPLATE_SET_BIT(bmap, samplingAlgorithm);
                    }
                    break;
                  case  49:
                  case  50:
                    /* verify that both samplerMode and
                     * samplerRandomInterval are present */
                    ++samplerMode;
                    if (2 == samplerMode) {
                        bmap |= 1;
                        TEMPLATE_SET_BIT(bmap, samplerMode);
                    }
                    break;
                }
                TRACEMSG(("bmap = 0x%lx, IE = %s (%u/%u)",
                          bmap, ie->ref.canon->ref.name, ie->ent, ie->num));
            }

            if (0 == bmap) {
                *ctx = NULL;
                *fn = NULL;
            } else {
                *ctx = (void*)bmap;
                *fn = NULL;
                TRACEMSG(("Bitmap value for Template ID 0x%04X [%p]"
                          " was set to 0x%lx",
                          tid, (void*)tmpl, (unsigned long)bmap));
            }
        }
    } else {
        /* do not define any template pairs for this template */
        fbSessionAddTemplatePair(session, tid, 0);

        /* fill 'bmap' based on the elements in the template */
        bmap = 1;

        count = fbTemplateCountElements(tmpl);
        for (i = 0; i < count && (ie = fbTemplateGetIndexedIE(tmpl, i)); ++i) {
            switch (FIELD_IDENT(ie->ent, ie->num)) {
              case   8:
              case  12:
                /* sourceIPv4Address and destinationIPv4Address map to
                 * same position */
                TEMPLATE_SET_BIT(bmap, sourceIPv4Address);
                break;
              case  27:
              case  28:
                /* sourceIPv6Address and destinationIPv6Address map to
                 * same position */
                TEMPLATE_SET_BIT(bmap, sourceIPv6Address);
                break;

              case  22:
                ASSERT_IE_NAME_IS(ie, flowStartSysUpTime);
                TEMPLATE_SET_BIT(bmap, flowStartSysUpTime);
                break;
              case  59:
                ASSERT_IE_NAME_IS(ie, postVlanId);
                TEMPLATE_SET_BIT(bmap, postVlanId);
                break;
              case  70:
                ASSERT_IE_NAME_IS(ie, mplsTopLabelStackSection);
                TEMPLATE_SET_BIT(bmap, mplsTopLabelStackSection);
                break;
              case 150:
                ASSERT_IE_NAME_IS(ie, flowStartSeconds);
                TEMPLATE_SET_BIT(bmap, flowStartSeconds);
                break;
              case 151:
                ASSERT_IE_NAME_IS(ie, flowEndSeconds);
                TEMPLATE_SET_BIT(bmap, flowEndSeconds);
                break;
              case 152:
                ASSERT_IE_NAME_IS(ie, flowStartMilliseconds);
                TEMPLATE_SET_BIT(bmap, flowStartMilliseconds);
                break;
              case 153:
                ASSERT_IE_NAME_IS(ie, flowEndMilliseconds);
                TEMPLATE_SET_BIT(bmap, flowEndMilliseconds);
                break;
              case 154:
                ASSERT_IE_NAME_IS(ie, flowStartMicroseconds);
                TEMPLATE_SET_BIT(bmap, flowStartMicroseconds);
                break;
              case 155:
                ASSERT_IE_NAME_IS(ie, flowEndMicroseconds);
                TEMPLATE_SET_BIT(bmap, flowEndMicroseconds);
                break;
              case 156:
                ASSERT_IE_NAME_IS(ie, flowStartNanoseconds);
                TEMPLATE_SET_BIT(bmap, flowStartNanoseconds);
                break;
              case 157:
                ASSERT_IE_NAME_IS(ie, flowEndNanoseconds);
                TEMPLATE_SET_BIT(bmap, flowEndNanoseconds);
                break;
              case 158:
                ASSERT_IE_NAME_IS(ie, flowStartDeltaMicroseconds);
                TEMPLATE_SET_BIT(bmap, flowStartDeltaMicroseconds);
                break;
              case 159:
                ASSERT_IE_NAME_IS(ie, flowEndDeltaMicroseconds);
                TEMPLATE_SET_BIT(bmap, flowEndDeltaMicroseconds);
                break;
              case 160:
                ASSERT_IE_NAME_IS(ie, systemInitTimeMilliseconds);
                TEMPLATE_SET_BIT(bmap, systemInitTimeMilliseconds);
                break;
              case 161:
                ASSERT_IE_NAME_IS(ie, flowDurationMilliseconds);
                TEMPLATE_SET_BIT(bmap, flowDurationMilliseconds);
                break;
              case 162:
                ASSERT_IE_NAME_IS(ie, flowDurationMicroseconds);
                TEMPLATE_SET_BIT(bmap, flowDurationMicroseconds);
                break;
              case 233:
                ASSERT_IE_NAME_IS(ie, firewallEvent);
                TEMPLATE_SET_BIT(bmap, firewallEvent);
                break;
              case 258:
                ASSERT_IE_NAME_IS(ie, collectionTimeMilliseconds);
                TEMPLATE_SET_BIT(bmap, collectionTimeMilliseconds);
                break;
              case 322:
                ASSERT_IE_NAME_IS(ie, observationTimeSeconds);
                TEMPLATE_SET_BIT(bmap, observationTimeSeconds);
                break;
              case 323:
                ASSERT_IE_NAME_IS(ie, observationTimeMilliseconds);
                TEMPLATE_SET_BIT(bmap, observationTimeMilliseconds);
                break;
              case 324:
                ASSERT_IE_NAME_IS(ie, observationTimeMicroseconds);
                TEMPLATE_SET_BIT(bmap, observationTimeMicroseconds);
                break;
              case 325:
                ASSERT_IE_NAME_IS(ie, observationTimeNanoseconds);
                TEMPLATE_SET_BIT(bmap, observationTimeNanoseconds);
                break;

              case FB_CISCO_ASA_EVENT_XTRA:
                ASSERT_IE_NAME_IS(ie, NF_F_FW_EXT_EVENT);
                TEMPLATE_SET_BIT(bmap, NF_F_FW_EXT_EVENT);
                break;
              case FB_CISCO_ASA_EVENT_ID:
                ASSERT_IE_NAME_IS(ie, NF_F_FW_EVENT);
                TEMPLATE_SET_BIT(bmap, NF_F_FW_EVENT);

                /* REVERSE ELEMENTS */
              case FIELD_IDENT(FB_IE_PEN_REVERSE,   6):
                ASSERT_IE_NAME_IS(ie, reverseTcpControlBits);
                TEMPLATE_SET_BIT(bmap, reverseTcpControlBits);
                break;
              case FIELD_IDENT(FB_IE_PEN_REVERSE,  58):
                ASSERT_IE_NAME_IS(ie, reverseVlanId);
                TEMPLATE_SET_BIT(bmap, reverseVlanId);
                break;

                /* REVERSE CERT PRIVATE ENTERPRISE ELEMENTS */
              case FIELD_IDENT(IPFIX_CERT_PEN, 14 | FB_IE_VENDOR_BIT_REVERSE):
                ASSERT_IE_NAME_IS(ie, reverseInitialTCPFlags);
                TEMPLATE_SET_BIT(bmap, reverseInitialTCPFlags);
                break;
            }
            TRACEMSG(("bmap = 0x%lx, IE = %s (%u/%u)",
                      bmap, ie->ref.canon->ref.name, ie->ent, ie->num));
        }

        *ctx = (void*)bmap;
        *fn = NULL;
        TRACEMSG(("Bitmap value for Template ID 0x%04X [%p] was set to 0x%lx",
                  tid, (void*)tmpl, (unsigned long)bmap));
    }

    if (print_templates) {
        skiPrintTemplate(session, tmpl, tid);
    }
}


void
skiAddSessionCallback(
    fbSession_t        *session)
{
    fbSessionAddTemplateCtxCallback(session, &skiTemplateCallbackCtx);
}


static void
skiSessionsFree(
    void)
{
    size_t i;
    fbSession_t *session;

    if (session_list) {
        for (i = 0; i < skVectorGetCount(session_list); i++) {
            skVectorGetValue(&session, session_list, i);
            fbSessionFree(session);
        }
        skVectorDestroy(session_list);
        session_list = NULL;
    }
}


/*
 *    Initialize an fbSession object that reads from either the
 *    network or from a file.
 */
static int
skiSessionInitReader(
    fbSession_t        *session,
    GError            **err)
{
    fbInfoModel_t   *model = skiInfoModel();
    fbTemplate_t    *tmpl = NULL;

    /* Add the record template */
    tmpl = fbTemplateAlloc(model);
    if (!fbTemplateAppendSpecArray(tmpl, ski_fixrec_spec, sampler_flags,err))
    {
        goto ERROR;
    }
    if (!fbSessionAddTemplate(session, TRUE, SKI_FIXREC_TID, tmpl, err)) {
        goto ERROR;
    }

    /* Add the TCP record template */
    tmpl = fbTemplateAlloc(model);
    if (!fbTemplateAppendSpecArray(tmpl, ski_tcp_stml_spec, 0, err)) {
        goto ERROR;
    }
    if (!fbSessionAddTemplate(session, TRUE, SKI_TCP_STML_TID, tmpl, err)) {
        goto ERROR;
    }

    /* Add the yaf stats record template  */
    tmpl = fbTemplateAlloc(model);
    if (!fbTemplateAppendSpecArray(tmpl, ski_yaf_stats_option_spec, 0, err)) {
        goto ERROR;
    }
    if (!fbSessionAddTemplate(session, TRUE, SKI_YAF_STATS_TID, tmpl, err)) {
        goto ERROR;
    }

    /* Add the netflow v9 sampling template  */
    tmpl = fbTemplateAlloc(model);
    if (!fbTemplateAppendSpecArray(
            tmpl, ski_nf9_sampling_spec, sampler_flags, err))
    {
        goto ERROR;
    }
    if (!fbSessionAddTemplate(session, TRUE, SKI_NF9_SAMPLING_TID, tmpl, err)){
        goto ERROR;
    }

    return 1;

  ERROR:
    fbTemplateFreeUnused(tmpl);
    return 0;
}


void
skiTeardown(
    void)
{
    skiInfoModelFree();
    skiSessionsFree();
}


/* **************************************************************
 * *****  Support for reading/import
 */


fbListener_t *
skiCreateListener(
    fbConnSpec_t           *spec,
    fbListenerAppInit_fn    appinit,
    fbListenerAppFree_fn    appfree,
    GError                **err)
{
    fbInfoModel_t   *model;
    fbSession_t     *session = NULL;

    /* The session is not owned by the buffer or the listener, so
     * maintain a vector of them for later destruction. */
    if (!session_list) {
        session_list = skVectorNew(sizeof(fbSession_t *));
        if (session_list == NULL) {
            return NULL;
        }
    }
    model = skiInfoModel();
    if (model == NULL) {
        return NULL;
    }
    session = fbSessionAlloc(model);
    if (session == NULL) {
        return NULL;
    }
    /* Initialize session for reading */
    if (!skiSessionInitReader(session, err)) {
        fbSessionFree(session);
        return NULL;
    }
    if (skVectorAppendValue(session_list, &session) != 0) {
        fbSessionFree(session);
        return NULL;
    }

    /* Invoke a callback when a new template arrives that tells fixbuf
     * how to map from the subTemplateMultiList used by YAF for TCP
     * information to our internal structure. */
    skiAddSessionCallback(session);

    /* Allocate a listener */
    return fbListenerAlloc(spec, session, appinit, appfree, err);
}


fBuf_t *
skiCreateReadBufferForFP(
    FILE               *fp,
    GError            **err)
{
    fbInfoModel_t  *model = NULL;
    fbCollector_t  *collector = NULL;
    fbSession_t    *session = NULL;
    fBuf_t         *fbuf = NULL;

    model = skiInfoModel();
    if (NULL == model) {
        return NULL;
    }

    collector = fbCollectorAllocFP(NULL, fp);
    if (NULL == collector) {
        return NULL;
    }

    /* Allocate a session.  The session will be owned by the fbuf, so
     * don't save it for later freeing. */
    session = fbSessionAlloc(model);
    if (session == NULL) {
        return NULL;
    }
    /* Initialize session for reading */
    if (!skiSessionInitReader(session, err)) {
        fbSessionFree(session);
        return NULL;
    }

    /* Create a buffer with the session and the collector */
    fbuf = fBufAllocForCollection(session, collector);
    if (NULL == fbuf) {
        fbSessionFree(session);
        return NULL;
    }

    /* Make certain the fbuf has an internal template */
    if (!fBufSetInternalTemplate(fbuf, SKI_FIXREC_TID, err)) {
        fBufFree(fbuf);
        return NULL;
    }

    /* Invoke a callback when a new template arrives that tells fixbuf
     * how to map from the subTemplateMultiList used by YAF for TCP
     * information to our internal structure. */
    skiAddSessionCallback(session);

    return fbuf;
}


/*
 *    Convert the NTP timestamp (RFC1305) contained in 'ntp' to epoch
 *    milliseconds.  The 'is_micro' field should be 0 if the function
 *    is decoding dateTimeNanoseconds and non-zero when decoding
 *    dateTimeMicroseconds.
 *
 *    An NTP timestamp is a 64 bit value that has whole seconds in the
 *    upper 32 bits and fractional seconds in the lower 32 bits.  Each
 *    fractional second represents 1/(2^32)th of a second.
 *
 *    In addition, NTP uses an epoch time of Jan 1, 1900.
 *
 *    When the 'is_micro' flag is set, decoding must ignore the 11
 *    lowest bits of the fractional part of the timestamp.
 *
 *    If 'ntp' is 0, assume the element was not in the model and
 *    return 0.
 */
static uint64_t
skiNTPDecode(
    uint64_t            ntp,
    int                 is_micro)
{
    /* the UNIX epoch as a number of seconds since NTP epoch */
#define JAN_1970  UINT64_C(0x83AA7E80)

    double frac;
    uint64_t t;

    if (!ntp) {
        return 0;
    }
    /* handle fractional seconds; convert to milliseconds */
    frac = (1000.0 * (ntp & (is_micro ? UINT32_C(0xFFFFF800) : UINT32_MAX))
            / (double)UINT64_C(0x100000000));

    /* handle whole seconds, convert to milliseconds */
    t = ((ntp >> 32) - JAN_1970) * 1000;

    return t + (uint64_t)frac;
}


/* Print a message saying why a flow was ignored */
static void
skiFlowIgnored(
    const ski_fixrec_t     *fixrec,
    const char             *reason)
{
    char sipbuf[64];
    char dipbuf[64];

    if (!SK_IPV6_IS_ZERO(fixrec->sourceIPv6Address)) {
#ifdef SK_HAVE_INET_NTOP
        if (!inet_ntop(AF_INET6, &fixrec->sourceIPv6Address,
                       sipbuf, sizeof(sipbuf)))
#endif
        {
            strcpy(sipbuf, "unknown-v6");
        }
    } else {
        num2dot_r(fixrec->sourceIPv4Address, sipbuf);
    }
    if (!SK_IPV6_IS_ZERO(fixrec->destinationIPv6Address)) {
#ifdef SK_HAVE_INET_NTOP
        if (!inet_ntop(AF_INET6, &fixrec->destinationIPv6Address,
                       dipbuf, sizeof(dipbuf)))
#endif
        {
            strcpy(dipbuf, "unknown-v6");
        }
    } else {
        num2dot_r(fixrec->destinationIPv4Address, dipbuf);
    }

    INFOMSG(("IGNORED|%s|%s|%u|%u|%u|%" PRIu64 "|%" PRIu64 "|%s|"),
            sipbuf, dipbuf, fixrec->sourceTransportPort,
            fixrec->destinationTransportPort,fixrec->protocolIdentifier,
            ((fixrec->packetDeltaCount)
             ? fixrec->packetDeltaCount
             : ((fixrec->packetTotalCount)
                ? fixrec->packetTotalCount
                : fixrec->initiatorPackets)),
            ((fixrec->octetDeltaCount)
             ? fixrec->octetDeltaCount
             : ((fixrec->octetTotalCount)
                ? fixrec->octetTotalCount
                : fixrec->initiatorOctets)),
            reason);
}


/* get the type of the next record */
ski_rectype_t
skiGetNextRecordType(
    fBuf_t             *fbuf,
    GError            **err)
{
    fbTemplate_t *tmpl;
    uintptr_t bmap;
    uint16_t tid;

    tmpl = fBufNextCollectionTemplate(fbuf, &tid, err);
    if (tmpl == NULL) {
        return SKI_RECTYPE_ERROR;
    }

    /* Handle Records that use an Options Template */
    if (fbTemplateGetOptionsScope(tmpl)) {
        if (tid == SKI_YAF_STATS_TID) {
            return SKI_RECTYPE_STATS;
        }
        bmap = (uintptr_t)fbTemplateGetContext(tmpl);
        if (bmap & ((1 << elem.samplingAlgorithm)
                    | (1 << elem.samplerMode)))
        {
            return SKI_RECTYPE_NF9_SAMPLING;
        }
        return SKI_RECTYPE_UNKNOWN;
    }
    return SKI_RECTYPE_FLOW;
}

gboolean
skiYafNextStats(
    fBuf_t                     *fbuf,
    const skpc_probe_t  UNUSED(*probe),
    ski_yaf_stats_t            *stats,
    GError                    **err)
{
    size_t len;

    /* Set internal template to read an yaf stats record */
    if (!fBufSetInternalTemplate(fbuf, SKI_YAF_STATS_TID, err)) {
        return FALSE;
    }

    memset(stats, 0, sizeof(*stats));
    len = sizeof(*stats);

    if (!fBufNext(fbuf, (uint8_t *)stats, &len, err)) {
        return FALSE;
    }

    return TRUE;
}

gboolean
skiNextSamplingOptionsTemplate(
    fBuf_t                 *fbuf,
    const skpc_probe_t     *probe,
    GError                **err)
{
    fbTemplate_t *tmpl = NULL;
    ski_nf9_sampling_t rec;
    uintptr_t bmap;
    size_t len;

    /* Set internal template to read the options record */
    if (!fBufSetInternalTemplate(fbuf, SKI_NF9_SAMPLING_TID, err)) {
        return FALSE;
    }

    memset(&rec, 0, sizeof(rec));
    len = sizeof(rec);

    if (!fBufNext(fbuf, (uint8_t*)&rec, &len, err)) {
        return FALSE;
    }

    if (skpcProbeGetLogFlags(probe) & SOURCE_LOG_SAMPLING) {
        /* Get the template used for the last record */
        tmpl = fBufGetCollectionTemplate(fbuf, NULL);
        bmap = (uintptr_t)fbTemplateGetContext(tmpl);
        if (TEMPLATE_GET_BIT(bmap, samplingAlgorithm)) {
            INFOMSG("'%s': Sampling Algorithm %u; Sampling Interval %u",
                    skpcProbeGetName(probe), rec.samplingAlgorithm,
                    rec.samplingInterval);
        } else if (TEMPLATE_GET_BIT(bmap, samplerMode)) {
            INFOMSG(("'%s': Sampler Id %u; Sampler Mode %u;"
                     " Sampler Random Interval %u"),
                    skpcProbeGetName(probe), rec.samplerId,
                    rec.samplerMode, rec.samplerRandomInterval);
        }
    }
    return TRUE;
}

/* Get a record from the libfixbuf buffer 'fbuf' and fill in the
 * forward and reverse flow records, 'rec' and 'revRec'.  Return 1 if
 * the record is uni-flow, 2 if it is bi-flow, 0 if the record is to
 * be ignored, -1 if there is an error. */
int
skiRwNextRecord(
    fBuf_t                 *fbuf,
    const skpc_probe_t     *probe,
    skIPFIXSourceRecord_t  *forward_rec,
    skIPFIXSourceRecord_t  *reverse_rec,
    GError                **err)
{
    struct log_rec_time_st {
        /* "raw" start time from the record */
        uint64_t        start_val;
        /* name of the IE in the 'start_val' member, NULL if none */
        const char     *start_name;
        /* "raw" end time from the record */
        uint64_t        end_val;
        /* name of the IE in the 'end_val' member, NULL if none */
        const char     *end_name;
        /* "raw" duration time from the record */
        uint64_t        dur_val;
        /* name of the IE in the 'dur_val' member, NULL if none */
        const char     *dur_name;
        /* name of the IE holding the export time, NULL if none */
        const char     *export_name;
    } log_rec_time;
    char stime_buf[SKTIMESTAMP_STRLEN];
    fbTemplate_t *tmpl = NULL;
    fbSubTemplateMultiListEntry_t *stml;
    ski_fixrec_t fixrec;
    size_t len;
    uint16_t tid;
    uint64_t sTime, eTime;
    uint32_t duration;
    uint64_t pkts, bytes;
    uint64_t rev_pkts, rev_bytes;
    uint8_t tcp_state;
    uint8_t tcp_flags;
    int have_tcp_stml = 0;
    rwRec *rec;
    uintptr_t bmap;

    assert(forward_rec);

    rec = skIPFIXSourceRecordGetRwrec(forward_rec);

    /* Clear out the record */
    RWREC_CLEAR(rec);

    /* Set internal template to read an extended flow record */
    if (!fBufSetInternalTemplate(fbuf, SKI_FIXREC_TID, err)) {
        return -1;
    }

    /* Get the next record */
    len = sizeof(fixrec);
    if (!fBufNext(fbuf, (uint8_t *)&fixrec, &len, err)) {
        return -1;
    }

    if ((fixrec.flowEndReason & SKI_END_MASK) == SKI_END_YAF_INTERMEDIATE_FLOW)
    {
        TRACEMSG(("Ignored YAF intermediate uniflow"));
        return 0;
    }

    /* Get the template used for the last record */
    tmpl = fBufGetCollectionTemplate(fbuf, &tid);

    bmap = (uintptr_t)fbTemplateGetContext(tmpl);
    TRACEMSG(("Bitmap value for Template ID 0x%04X [%p] was read as 0x%lx",
              tid, (void*)tmpl, (unsigned long)bmap));

    /* Ignore records with no IPs. */
    if (TEMPLATE_GET_BIT(bmap, sourceIPv4Address)
        || TEMPLATE_GET_BIT(bmap, sourceIPv6Address))
    {
        /* we're good */
    } else if ((skpcProbeGetQuirks(probe) & SKPC_QUIRK_MISSING_IPS) == 0) {
        skiFlowIgnored(&fixrec, "No IP addresses");
        return 0;
    }

    /*
     *  Handle records that represent a "firewall event" when the
     *  SKPC_QUIRK_FW_EVENT quirks value is set on the probe.  When
     *  the quirk is not set, process the records normally.
     *
     *  This code changed in SiLK 3.8.0.  Prior to SiLK 3.8.0, all
     *  firewall event status messages were dropped.
     *
     *  It seems that every record from a Cisco ASA has NF_F_FW_EVENT
     *  and NF_F_FW_EXT_EVENT information elements, so ignoring flow
     *  records with these elements means ignoring all flow records.
     *
     *  firewallEvent is an official IPFIX information element, IE 233
     *
     *  NF_F_FW_EVENT is Cisco IE 40005
     *
     *  NF_F_FW_EXT_EVENT is Cisco IE 33002.
     *
     *  Note that the Cisco IE numbers cannot be used in IPFIX because
     *  IPFIX would treat them as "reverse" records.
     *
     *  References (October 2013):
     *  http://www.cisco.com/en/US/docs/security/asa/asa82/netflow/netflow.html#wp1028202
     *  http://www.cisco.com/en/US/docs/security/asa/asa84/system/netflow/netflow.pdf
     *
     *  Values for the NF_F_FW_EXT_EVENT depend on the values for the
     *  NF_F_FW_EVENT.  The following lists the FW_EVENT with
     *  sub-bullets for the NF_F_FW_EXT_EVENT.
     *
     *  0.  Ignore -- This value indicates that a field must be
     *      ignored.
     *
     *      0.  Ignore -- This value indicates that the field must be
     *          ignored.
     *
     *  1.  Flow created -- This value indicates that a new flow was
     *      created.
     *
     *  2.  Flow deleted -- This value indicates that a flow was
     *      deleted.
     *
     *    >2000.  Values above 2000 represent various reasons why a
     *            flow was terminated.
     *
     *  3.  Flow denied -- This value indicates that a flow was
     *      denied.
     *
     *    >1000.  Values above 1000 represent various reasons why a
     *            flow was denied.
     *
     *     1001.  A flow was denied by an ingress ACL.
     *
     *     1002.  A flow was denied by an egress ACL.
     *
     *     1003.  The ASA denied an attempt to connect to the (ASA's)
     *            interface service.
     *
     *     1004.  The flow was denied because the first packet on the
     *            TCP was not a TCP SYN packet.
     *
     *  5.  Flow updated -- This value indicates that a flow update
     *      timer went off or a flow was torn down.
     *
     *  The IPFIX values for the firewallEvent IE follow those for
     *  NF_F_FW_EVENT (with IPFIX providing no explanation as to what
     *  the values mean! --- some standard) and IPFIX adds the value:
     *
     *  4.  Flow alert.
     *
     *  PROCESSING RULES:
     *
     *  The term "ignore" below means that a log message is written
     *  and that no SiLK flow record is created.
     *
     *  Ignore flow records where the "flow ignore" event is present.
     *
     *  Treat records where "flow deleted" is specified as actual flow
     *  records to be processed and stored.
     *
     *  Ignore "flow created" events, since we will handle these flows
     *  when the "flow deleted" event occurs.  Also, a short-lived
     *  flow record may produce a "flow deleted" event without a "flow
     *  created" event.
     *
     *  For a "flow denied" event, write a special value into the SiLK
     *  Flow record that the writing thread can use to categorize the
     *  record as innull/outnull.
     *
     *  It is unclear how to handle "flow updated" events. If the
     *  record is only being updated, presumably SiLK will get a "flow
     *  deleted" event in the future.  However, if the flow is being
     *  torn down, will the ASA send a separate "flow deleted" event?
     *  For now (as of SiLK 3.8.0), ignore "flow updated" events.
     *
     *  Ignore "flow alert" events.
     *
     *
     *  Firewall events, byte and packet counts, and the Cisco ASA:
     *
     *  1.  Flow created events have a byte and packet count of 0;
     *  this is fine since we are ignoring these flows.
     *
     *  2.  Flow deinied events have a byte and packet count of 0.
     *  SiLK will ignore these flows unless we doctor them to have a
     *  non-zero byte and packet count, which we do when the ASA hack
     *  is enabled.
     *
     *  3.  Flow deleted events have a packet count of 0, but we have
     *  code below to work around that when the ASA hack is enabled.
     *  The flows usally have a non-zero byte count.  HOWEVER, some
     *  flow records have a 0-byte count, and it is unclear what to
     *  with those---currently they are ignored.
     */
    if ((skpcProbeGetQuirks(probe) & SKPC_QUIRK_FW_EVENT)
        && (bmap & ((1 << elem.firewallEvent)
                    | (1 << elem.NF_F_FW_EVENT)
                    | (1 << elem.NF_F_FW_EXT_EVENT))))
    {
        char msg[64];
        uint8_t event = (fixrec.firewallEvent
                         ? fixrec.firewallEvent : fixrec.NF_F_FW_EVENT);
        if (SKIPFIX_FW_EVENT_DELETED == event) {
            /* flow deleted */
            TRACEMSG((("Processing flow deleted event as actual flow record;"
                       " firewallEvent=%u, NF_F_FW_EVENT=%u,"
                       " NF_F_FW_EXT_EVENT=%u"),
                      fixrec.firewallEvent, fixrec.NF_F_FW_EVENT,
                      fixrec.NF_F_FW_EXT_EVENT));
        } else if (SKIPFIX_FW_EVENT_DENIED == event) {
            /* flow denied */
            TRACEMSG((("Processing flow denied event as actual flow record;"
                       " firewallEvent=%u, NF_F_FW_EVENT=%u,"
                       " NF_F_FW_EXT_EVENT=%u"),
                      fixrec.firewallEvent, fixrec.NF_F_FW_EVENT,
                      fixrec.NF_F_FW_EXT_EVENT));
            if (SKIPFIX_FW_EVENT_DENIED_CHECK_VALID(fixrec.NF_F_FW_EXT_EVENT)){
                rwRecSetMemo(rec, fixrec.NF_F_FW_EXT_EVENT);
            } else {
                rwRecSetMemo(rec, event);
            }
        } else {
            /* flow created, flow updated, flow alert, or something
             * unexpected */
            if (skpcProbeGetLogFlags(probe) & SOURCE_LOG_FIREWALL) {
                snprintf(msg, sizeof(msg), "firewallEvent=%u,extended=%u",
                         event, fixrec.NF_F_FW_EXT_EVENT);
                skiFlowIgnored(&fixrec, msg);
            }
            return 0;
        }
    }

    /* FIXME.  What if the record has a flowDirection field that is
     * set to egress (0x01)?  Shouldn't we handle that by reversing
     * the record?  Or has fixbuf done that for us? */

    /* Get the forward and reverse packet and byte counts (run the
     * Gauntlet of Volume). */
    pkts = ((fixrec.packetDeltaCount)
            ? fixrec.packetDeltaCount
            : ((fixrec.packetTotalCount)
               ? fixrec.packetTotalCount
               : fixrec.initiatorPackets));
    bytes = ((fixrec.octetDeltaCount)
             ? fixrec.octetDeltaCount
             : ((fixrec.octetTotalCount)
                ? fixrec.octetTotalCount
                : fixrec.initiatorOctets));

    rev_pkts = ((fixrec.reversePacketDeltaCount)
                ? fixrec.reversePacketDeltaCount
                : ((fixrec.reversePacketTotalCount)
                   ? fixrec.reversePacketTotalCount
                   : fixrec.responderPackets));
    rev_bytes = ((fixrec.reverseOctetDeltaCount)
                 ? fixrec.reverseOctetDeltaCount
                 : ((fixrec.reverseOctetTotalCount)
                    ? fixrec.reverseOctetTotalCount
                    : fixrec.responderOctets));

    if (0 == bytes && 0 == rev_bytes) {
        /* flow denied events from the Cisco ASA have zero in the
         * bytes and packets field */
        if ((skpcProbeGetQuirks(probe) & SKPC_QUIRK_FW_EVENT)
            && 0 == pkts
            && (SKIPFIX_FW_EVENT_DENIED == fixrec.NF_F_FW_EVENT
                || SKIPFIX_FW_EVENT_DENIED == fixrec.firewallEvent))
        {
            TRACEMSG(("Setting forward bytes and packets to 1"
                      " for denied firewall event"));
            bytes = 1;
            pkts = 1;
        } else {
            skiFlowIgnored(&fixrec, "no forward/reverse octets");
            return 0;
        }
    }

    if (0 == pkts && 0 == rev_pkts) {
        if ((skpcProbeGetQuirks(probe) & SKPC_QUIRK_ZERO_PACKETS) == 0) {
            /* Ignore records with no volume. */
            skiFlowIgnored(&fixrec, "no forward/reverse packets");
            return 0;
        }

        /* attempt to handle NetFlowV9 records from an ASA router that
         * have no packet count.  The code assumes all records from an
         * ASA have a byte count, though this is not always true. */
        if (bytes) {
            /* there is a forward byte count */
            if (0 == pkts) {
                TRACEMSG(("Setting forward packets to 1"));
                pkts = 1;
            }
        }
        if (rev_bytes) {
            /* there is a reverse byte count */
            if (0 == rev_pkts) {
                TRACEMSG(("Setting reverse packets to 1"));
                rev_pkts = 1;
            }
        }
    }

    /* If the TCP flags are in a subTemplateMultiList, copy them from
     * the list and into the record.  The fixbuf.stml gets initialized
     * by the call to fBufNext().*/
    stml = NULL;
    while ((stml = fbSubTemplateMultiListGetNextEntry(&fixrec.stml, stml))) {
        if (SKI_TCP_STML_TID != stml->tmplID) {
            fbSubTemplateMultiListEntryNextDataPtr(stml, NULL);
        } else {
            ski_tcp_stml_t *tcp = NULL;
            tcp = ((ski_tcp_stml_t*)
                   fbSubTemplateMultiListEntryNextDataPtr(stml, tcp));
            fixrec.initialTCPFlags = tcp->initialTCPFlags;
            fixrec.unionTCPFlags = tcp->unionTCPFlags;
            fixrec.reverseInitialTCPFlags = tcp->reverseInitialTCPFlags;
            fixrec.reverseUnionTCPFlags = tcp->reverseUnionTCPFlags;
            have_tcp_stml = 1;
        }
    }
    fbSubTemplateMultiListClear(&fixrec.stml);

    if (pkts && bytes) {
        /* We have forward information. */
        TRACEMSG(("Read a forward record"));

        /* Use the IPv6 addresses if they are present and either there
         * are no IPv4 addresses or the IPv6 addresses are non-zero. */
        if (TEMPLATE_GET_BIT(bmap, sourceIPv6Address)
            && (!TEMPLATE_GET_BIT(bmap, sourceIPv4Address)
                || !SK_IPV6_IS_ZERO(fixrec.sourceIPv6Address)
                || !SK_IPV6_IS_ZERO(fixrec.destinationIPv6Address)))
        {
            /* Values found in IPv6 addresses--use them */
            rwRecSetIPv6(rec);
            rwRecMemSetSIPv6(rec, &fixrec.sourceIPv6Address);
            rwRecMemSetDIPv6(rec, &fixrec.destinationIPv6Address);
            rwRecMemSetNhIPv6(rec, &fixrec.ipNextHopIPv6Address);
        } else {
            /* Take values from IPv4 */
            rwRecSetSIPv4(rec, fixrec.sourceIPv4Address);
            rwRecSetDIPv4(rec, fixrec.destinationIPv4Address);
            rwRecSetNhIPv4(rec, fixrec.ipNextHopIPv4Address);
        }

        /* Handle the Protocol and Ports */
        rwRecSetProto(rec, fixrec.protocolIdentifier);
        rwRecSetSPort(rec, fixrec.sourceTransportPort);
        rwRecSetDPort(rec, fixrec.destinationTransportPort);

        /* Check for ICMP Values */
        if (rwRecIsICMP(rec) && 0 == rwRecGetDPort(rec)) {
            if (rwRecIsIPv6(rec)) {
                if (fixrec.icmpTypeCodeIPv6) {
                    rwRecSetDPort(rec, fixrec.icmpTypeCodeIPv6);
                } else {
                    rwRecSetDPort(rec,
                                  fixrec.icmpTypeIPv6 | fixrec.icmpCodeIPv6);
                }
            } else {
                if (fixrec.icmpTypeCodeIPv4) {
                    rwRecSetDPort(rec, fixrec.icmpTypeCodeIPv4);
                } else {
                    rwRecSetDPort(rec,
                                  fixrec.icmpTypeIPv4 | fixrec.icmpCodeIPv4);
                }
            }
        }

        /* Handle the SNMP or VLAN interfaces */
        if (SKPC_IFVALUE_VLAN == skpcProbeGetInterfaceValueType(probe)) {
            rwRecSetInput(rec, fixrec.vlanId);
            rwRecSetOutput(rec, fixrec.postVlanId);
        } else {
            rwRecSetInput(rec, fixrec.ingressInterface);
            rwRecSetOutput(rec, fixrec.egressInterface);
        }

        /* Store volume */
        rwRecSetPkts(rec, pkts);
        rwRecSetBytes(rec, bytes);
    } else {
        if (0 == rev_pkts || 0 == rev_bytes) {
            skAbort();
        }

        /* We have no forward information, only reverse.  Write the
         * source and dest values from the IPFIX record to SiLK's dest
         * and source fields, respectively. */
        TRACEMSG(("Read a reversed record"));

        /* Store volume */
        rwRecSetPkts(rec, rev_pkts);
        rwRecSetBytes(rec, rev_bytes);

        /* This cannot be a bi-flow.  Clear rev_pkts and rev_bytes
         * variables now. We check this in the reverse_rec code
         * below. */
        rev_pkts = rev_bytes = 0;

        /* Handle the IP addresses */
        if (TEMPLATE_GET_BIT(bmap, sourceIPv6Address)
            && (!TEMPLATE_GET_BIT(bmap, sourceIPv4Address)
                || !SK_IPV6_IS_ZERO(fixrec.sourceIPv6Address)
                || !SK_IPV6_IS_ZERO(fixrec.destinationIPv6Address)))
        {
            /* Values found in IPv6 addresses--use them */
            rwRecSetIPv6(rec);
            rwRecMemSetSIPv6(rec, &fixrec.destinationIPv6Address);
            rwRecMemSetDIPv6(rec, &fixrec.sourceIPv6Address);
            rwRecMemSetNhIPv6(rec, &fixrec.ipNextHopIPv6Address);
        } else {
            /* Take values from IPv4 */
            rwRecSetSIPv4(rec, fixrec.destinationIPv4Address);
            rwRecSetDIPv4(rec, fixrec.sourceIPv4Address);
            rwRecSetNhIPv4(rec, fixrec.ipNextHopIPv4Address);
        }

        /* Handle the Protocol and Ports */
        rwRecSetProto(rec, fixrec.protocolIdentifier);
        if (!rwRecIsICMP(rec)) {
            rwRecSetSPort(rec, fixrec.destinationTransportPort);
            rwRecSetDPort(rec, fixrec.sourceTransportPort);
        } else {
            /* For an ICMP record, put whichever Port field is
             * non-zero into the record's dPort field */
            rwRecSetSPort(rec, 0);
            rwRecSetDPort(rec, (fixrec.destinationTransportPort
                                ? fixrec.destinationTransportPort
                                : fixrec.sourceTransportPort));

            /* Check for ICMP Values */
            if (0 == rwRecGetDPort(rec)) {
                if (rwRecIsIPv6(rec)) {
                    if (fixrec.icmpTypeCodeIPv6) {
                        rwRecSetDPort(rec, fixrec.icmpTypeCodeIPv6);
                    } else {
                        rwRecSetDPort(
                            rec, fixrec.icmpTypeIPv6 | fixrec.icmpCodeIPv6);
                    }
                } else {
                    if (fixrec.icmpTypeCodeIPv4) {
                        rwRecSetDPort(rec, fixrec.icmpTypeCodeIPv4);
                    } else {
                        rwRecSetDPort(
                            rec, fixrec.icmpTypeIPv4 | fixrec.icmpCodeIPv4);
                    }
                }
            }
        }

        /* Handle the SNMP or VLAN interfaces */
        if (SKPC_IFVALUE_VLAN == skpcProbeGetInterfaceValueType(probe)) {
            if (TEMPLATE_GET_BIT(bmap, reverseVlanId)) {
                /* If we have the reverse elements, use them */
                rwRecSetInput(rec, fixrec.reverseVlanId);
                rwRecSetOutput(rec, fixrec.reversePostVlanId);
            } else if (TEMPLATE_GET_BIT(bmap, postVlanId)) {
                /* If we have a single vlanId, set 'input' to that value;
                 * otherwise, set 'input' to postVlanId and 'output' to
                 * vlanId. */
                rwRecSetInput(rec, fixrec.postVlanId);
                rwRecSetOutput(rec, fixrec.vlanId);
            } else {
                /* we have a single vlanId, so don't swap the values */
                rwRecSetInput(rec, fixrec.vlanId);
            }
        } else {
            rwRecSetInput(rec, fixrec.egressInterface);
            rwRecSetOutput(rec, fixrec.ingressInterface);
        }
    }


    /* Run the Gauntlet of Time - convert all the various ways an IPFIX
     * record's time could be represented into start and elapsed times. */
    memset(&log_rec_time, 0, sizeof(log_rec_time));
    if (TEMPLATE_GET_BIT(bmap, flowStartSysUpTime)) {
        /* Times based on flow generator system uptimes (Netflow v9) */
        intmax_t uptime, difference;
        sktime_t export_msec;
        const char *rollover_first;
        const char *rollover_last = "";

        /* Set duration.  Our NetFlow v5 code checks the magnitude of
         * the difference between te eTime and sTime; this code is not
         * that complicated---we assume if eTime is less than sTime
         * then eTime has rolled over. */
        if (fixrec.flowStartSysUpTime <= fixrec.flowEndSysUpTime) {
            rwRecSetElapsed(rec, (fixrec.flowEndSysUpTime
                                  - fixrec.flowStartSysUpTime));
        } else {
            /* assume EndTime rolled-over and start did not */
            rwRecSetElapsed(rec, (ROLLOVER32 + fixrec.flowEndSysUpTime
                                  - fixrec.flowStartSysUpTime));
            rollover_last = ", assume flowEndSysUpTime rollover";
        }

        /* Set start time. */
        export_msec = sktimeCreate(fBufGetExportTime(fbuf), 0);
        if (!TEMPLATE_GET_BIT(bmap, systemInitTimeMilliseconds)) {
            /* we do not know when the router booted.  assume end-time
             * is same as the record's export time and set start-time
             * accordingly. */
            rwRecSetStartTime(rec, export_msec - rwRecGetElapsed(rec));
            if (skpcProbeGetLogFlags(probe) & SOURCE_LOG_TIMESTAMPS) {
                sktimestamp_r(stime_buf,rwRecGetStartTime(rec),SKTIMESTAMP_UTC);
                INFOMSG(("'%s': Set sTime=%sZ, dur=%.3fs from incoming record"
                         " flowStartSysUpTime=%" PRIu32
                         ", flowEndSysUpTime=%" PRIu32
                         ", no systemInitTimeMilliseconds"
                         ", set end to exportTimeSeconds=%" PRIu32 "%s"),
                        skpcProbeGetName(probe),
                        stime_buf, (double)rwRecGetElapsed(rec)/1000,
                        fixrec.flowStartSysUpTime, fixrec.flowEndSysUpTime,
                        fBufGetExportTime(fbuf), rollover_last);
            }
        } else {
            /* systemInitTimeMilliseconds is the absolute router boot
             * time (msec), and libfixbuf sets it by subtracting the
             * NFv9 uptime (msec) from the record's abolute export
             * time (sec). */
            uptime = export_msec - fixrec.systemInitTimeMilliseconds;
            difference = uptime - fixrec.flowStartSysUpTime;
            if (difference > MAXIMUM_FLOW_TIME_DEVIATION) {
                /* assume upTime is set before record is composed and
                 * that start-time has rolled over. */
                rwRecSetStartTime(rec, (fixrec.systemInitTimeMilliseconds
                                        + fixrec.flowStartSysUpTime
                                        + ROLLOVER32));
                rollover_first = ", assume flowStartSysUpTime rollover";
            } else if (-difference > MAXIMUM_FLOW_TIME_DEVIATION) {
                /* assume upTime is set after record is composed and
                 * that upTime has rolled over. */
                rwRecSetStartTime(rec, (fixrec.systemInitTimeMilliseconds
                                        + fixrec.flowStartSysUpTime
                                        - ROLLOVER32));
                rollover_first = ", assume sysUpTime rollover";
            } else {
                /* times look reasonable; assume no roll over */
                rwRecSetStartTime(rec, (fixrec.systemInitTimeMilliseconds
                                        + fixrec.flowStartSysUpTime));
                rollover_first = "";
            }
            if (skpcProbeGetLogFlags(probe) & SOURCE_LOG_TIMESTAMPS) {
                sktimestamp_r(stime_buf,rwRecGetStartTime(rec),SKTIMESTAMP_UTC);
                INFOMSG(("'%s': Set sTime=%sZ, dur=%.3fs from incoming record"
                         " flowStartSysUpTime=%" PRIu32
                         ", flowEndSysUpTime=%" PRIu32
                         ", systemInitTimeMilliseconds=%" PRIu64
                         ", exportTimeSeconds=%" PRIu32 "%s%s"),
                        skpcProbeGetName(probe),
                        stime_buf, (double)rwRecGetElapsed(rec)/1000,
                        fixrec.flowStartSysUpTime, fixrec.flowEndSysUpTime,
                        fixrec.systemInitTimeMilliseconds,
                        fBufGetExportTime(fbuf), rollover_first,rollover_last);
            }
        }
    } else {
        /* look for all possible start times.  consider changing this
         * to a switch(), but we would still need something like the
         * following if multiple times were set. */
        if (TEMPLATE_GET_BIT(bmap, flowStartMilliseconds)) {
            sTime = fixrec.flowStartMilliseconds;
            log_rec_time.start_val = fixrec.flowStartMilliseconds;
            log_rec_time.start_name = "flowStartMilliseconds";
        } else if (TEMPLATE_GET_BIT(bmap, flowStartSeconds)) {
            sTime = UINT64_C(1000) * (uint64_t)fixrec.flowStartSeconds;
            log_rec_time.start_val = fixrec.flowStartSeconds;
            log_rec_time.start_name = "flowStartSeconds";
        } else if (TEMPLATE_GET_BIT(bmap, flowStartMicroseconds)) {
            sTime = skiNTPDecode(fixrec.flowStartMicroseconds, 1);
            log_rec_time.start_val = fixrec.flowStartMicroseconds;
            log_rec_time.start_name = "flowStartMicroseconds";
        } else if (TEMPLATE_GET_BIT(bmap, flowStartNanoseconds)) {
            sTime = skiNTPDecode(fixrec.flowStartNanoseconds, 0);
            log_rec_time.start_val = fixrec.flowStartNanoseconds;
            log_rec_time.start_name = "flowStartNanoseconds";
        } else if (TEMPLATE_GET_BIT(bmap, flowStartDeltaMicroseconds)) {
            sTime = (fBufGetExportTime(fbuf) * 1000
                     - fixrec.flowStartDeltaMicroseconds / 1000);
            log_rec_time.start_val = fixrec.flowStartDeltaMicroseconds;
            log_rec_time.start_name = "flowStartDeltaMicroseconds";
            log_rec_time.export_name = "exportTimeSeconds";
        } else {
            sTime = 0;
        }

        /* look for all possible end times; if none found look for
         * collection/observation times */
        if (TEMPLATE_GET_BIT(bmap, flowEndMilliseconds)) {
            eTime = fixrec.flowEndMilliseconds;
            log_rec_time.end_val = fixrec.flowEndMilliseconds;
            log_rec_time.end_name = "flowEndMilliseconds";
        } else if (TEMPLATE_GET_BIT(bmap, flowEndSeconds)) {
            eTime = UINT64_C(1000) * (uint64_t)fixrec.flowEndSeconds;
            log_rec_time.end_val = fixrec.flowEndSeconds;
            log_rec_time.end_name = "flowEndSeconds";
        } else if (TEMPLATE_GET_BIT(bmap, flowEndMicroseconds)) {
            eTime = skiNTPDecode(fixrec.flowEndMicroseconds, 1);
            log_rec_time.end_val = fixrec.flowEndMicroseconds;
            log_rec_time.end_name = "flowEndMicroseconds";
        } else if (TEMPLATE_GET_BIT(bmap, flowEndNanoseconds)) {
            eTime = skiNTPDecode(fixrec.flowEndNanoseconds, 0);
            log_rec_time.end_val = fixrec.flowEndNanoseconds;
            log_rec_time.end_name = "flowEndNanoseconds";
        } else if (TEMPLATE_GET_BIT(bmap, flowEndDeltaMicroseconds)) {
            eTime = (fBufGetExportTime(fbuf) * 1000
                     - fixrec.flowEndDeltaMicroseconds / 1000);
            log_rec_time.end_val = fixrec.flowEndDeltaMicroseconds;
            log_rec_time.end_name = "flowEndDeltaMicroseconds";
            log_rec_time.export_name = "exportTimeSeconds";
        } else if (TEMPLATE_GET_BIT(bmap, collectionTimeMilliseconds)) {
            eTime = fixrec.collectionTimeMilliseconds;
            log_rec_time.end_val = fixrec.collectionTimeMilliseconds;
            log_rec_time.end_name = "collectionTimeMilliseconds";
        } else if (TEMPLATE_GET_BIT(bmap, observationTimeMilliseconds)) {
            eTime = fixrec.observationTimeMilliseconds;
            log_rec_time.end_val = fixrec.observationTimeMilliseconds;
            log_rec_time.end_name = "observationTimeMilliseconds";
        } else if (TEMPLATE_GET_BIT(bmap, observationTimeSeconds)) {
            eTime = UINT64_C(1000) * (uint64_t)fixrec.observationTimeSeconds;
            log_rec_time.end_val = fixrec.observationTimeSeconds;
            log_rec_time.end_name = "observationTimeSeconds";
        } else if (TEMPLATE_GET_BIT(bmap, observationTimeMicroseconds)) {
            eTime = skiNTPDecode(fixrec.observationTimeMicroseconds, 1);
            eTime = fixrec.observationTimeMicroseconds;
            log_rec_time.end_val = fixrec.observationTimeMicroseconds;
            log_rec_time.end_name = "observationTimeMicroseconds";
        } else if (TEMPLATE_GET_BIT(bmap, observationTimeNanoseconds)) {
            eTime = skiNTPDecode(fixrec.observationTimeNanoseconds, 0);
            log_rec_time.end_val = fixrec.observationTimeNanoseconds;
            log_rec_time.end_name = "observationTimeNanoseconds";
        } else {
            eTime = 0;
        }

        /* look for durations */
        if (TEMPLATE_GET_BIT(bmap, flowDurationMilliseconds)) {
            duration = fixrec.flowDurationMilliseconds;
            log_rec_time.dur_val = fixrec.flowDurationMilliseconds;
            log_rec_time.dur_name = "flowDurationMilliseconds";
        } else if (TEMPLATE_GET_BIT(bmap, flowDurationMicroseconds)) {
            duration = fixrec.flowDurationMicroseconds / 1000;
            log_rec_time.dur_val = fixrec.flowDurationMicroseconds;
            log_rec_time.dur_name = "flowDurationMicroseconds";
        } else {
            duration = 0;
        }

        /* set the record's time and print a log msg if requested */
        if (log_rec_time.start_name) {
            if (log_rec_time.dur_name) {
                /* have start and duration; use them */
                rwRecSetStartTime(rec, (sktime_t)sTime);
                rwRecSetElapsed(rec, duration);
                if (skpcProbeGetLogFlags(probe) & SOURCE_LOG_TIMESTAMPS) {
                    sktimestamp_r(stime_buf, sTime, SKTIMESTAMP_UTC);
                    if (log_rec_time.export_name) {
                        INFOMSG(("'%s': Set sTime=%sZ, dur=%.3fs"
                                 " from incoming record"
                                 " %s=%" PRIu64 ", %s=%" PRIu64
                                 ", %s=%" PRIu32),
                                skpcProbeGetName(probe),
                                stime_buf, (double)duration/1000,
                                log_rec_time.start_name,log_rec_time.start_val,
                                log_rec_time.dur_name, log_rec_time.dur_val,
                                log_rec_time.export_name,
                                fBufGetExportTime(fbuf));
                    } else {
                        INFOMSG(("'%s': Set sTime=%sZ, dur=%.3fs"
                                 " from incoming record"
                                 " %s=%" PRIu64 ", %s=%" PRIu64),
                                skpcProbeGetName(probe),
                                stime_buf, (double)duration/1000,
                                log_rec_time.start_name,log_rec_time.start_val,
                                log_rec_time.dur_name, log_rec_time.dur_val);
                    }
                }
            } else if (log_rec_time.end_name) {
                /* have start and end; use them */
                rwRecSetStartTime(rec, (sktime_t)sTime);
                if (eTime < sTime || (eTime > sTime + UINT32_MAX)) {
                    rwRecSetElapsed(rec, UINT32_MAX);
                } else {
                    rwRecSetElapsed(rec, (eTime - sTime));
                }
                if (skpcProbeGetLogFlags(probe) & SOURCE_LOG_TIMESTAMPS) {
                    sktimestamp_r(stime_buf, sTime, SKTIMESTAMP_UTC);
                    if (log_rec_time.export_name) {
                        INFOMSG(("'%s': Set sTime=%sZ, dur=%.3fs"
                                 " from incoming record"
                                 " %s=%" PRIu64 ", %s=%" PRIu64
                                 ", %s=%" PRIu32),
                                skpcProbeGetName(probe),
                                stime_buf, (double)rwRecGetElapsed(rec)/1000,
                                log_rec_time.start_name,log_rec_time.start_val,
                                log_rec_time.end_name, log_rec_time.end_val,
                                log_rec_time.export_name,
                                fBufGetExportTime(fbuf));
                    } else {
                        INFOMSG(("'%s': Set sTime=%sZ, dur=%.3fs"
                                 " from incoming record"
                                 " %s=%" PRIu64 ", %s=%" PRIu64),
                                skpcProbeGetName(probe),
                                stime_buf, (double)rwRecGetElapsed(rec)/1000,
                                log_rec_time.start_name,log_rec_time.start_val,
                                log_rec_time.end_name, log_rec_time.end_val);
                    }
                }
            } else {
                /* only have a start time; use it and set dur to 0 */
                rwRecSetStartTime(rec, (sktime_t)sTime);
                rwRecSetElapsed(rec, 0);
                if (skpcProbeGetLogFlags(probe) & SOURCE_LOG_TIMESTAMPS) {
                    sktimestamp_r(stime_buf, eTime, SKTIMESTAMP_UTC);
                    if (log_rec_time.export_name) {
                        INFOMSG(("'%s': Set sTime=%sZ, dur=%.3fs"
                                 " from incoming record"
                                 " %s=%" PRIu64 ", %s=%" PRIu32),
                                skpcProbeGetName(probe), stime_buf, 0.0,
                                log_rec_time.start_name,log_rec_time.start_val,
                                log_rec_time.export_name,
                                fBufGetExportTime(fbuf));
                    } else {
                        INFOMSG(("'%s': Set sTime=%sZ, dur=%.3fs"
                                 " from incoming record"
                                 " %s=%" PRIu64),
                                skpcProbeGetName(probe), stime_buf, 0.0,
                                log_rec_time.start_name,log_rec_time.start_val);
                    }
                }
            }
        } else if (log_rec_time.dur_name) {
            /* duration but no start time; is there an end time? */
            if (log_rec_time.end_name) {
                /* have dur and end; compute start */
                rwRecSetStartTime(rec, (sktime_t)(eTime - duration));
                rwRecSetElapsed(rec, duration);
                if (skpcProbeGetLogFlags(probe) & SOURCE_LOG_TIMESTAMPS) {
                    sktimestamp_r(stime_buf, rwRecGetStartTime(rec),
                                  SKTIMESTAMP_UTC);
                    if (log_rec_time.export_name) {
                        INFOMSG(("'%s': Set sTime=%sZ, dur=%.3fs"
                                 " from incoming record"
                                 " %s=%" PRIu64 ", %s=%" PRIu64
                                 ", %s=%" PRIu32),
                                skpcProbeGetName(probe),
                                stime_buf, (double)duration/1000,
                                log_rec_time.dur_name, log_rec_time.dur_val,
                                log_rec_time.end_name, log_rec_time.end_val,
                                log_rec_time.export_name,
                                fBufGetExportTime(fbuf));
                    } else {
                        INFOMSG(("'%s': Set sTime=%sZ, dur=%.3fs"
                                 " from incoming record"
                                 " %s=%" PRIu64 ", %s=%" PRIu64),
                                skpcProbeGetName(probe),
                                stime_buf, (double)duration/1000,
                                log_rec_time.dur_name, log_rec_time.dur_val,
                                log_rec_time.end_name, log_rec_time.end_val);
                    }
                }
            } else {
                /* only have a duration; use export time as end time */
                rwRecSetStartTime(
                    rec, (sktimeCreate(fBufGetExportTime(fbuf),0) - duration));
                rwRecSetElapsed(rec, duration);
                if (skpcProbeGetLogFlags(probe) & SOURCE_LOG_TIMESTAMPS) {
                    sktimestamp_r(stime_buf, rwRecGetStartTime(rec),
                                  SKTIMESTAMP_UTC);
                    INFOMSG(("'%s': Set sTime=%sZ, dur=%.3fs"
                             " from incoming record"
                             " %s=%" PRIu64
                             ", set end to exportTimeSeconds=%" PRIu32),
                            skpcProbeGetName(probe),
                            stime_buf, (double)duration/1000,
                            log_rec_time.dur_name, log_rec_time.dur_val,
                            fBufGetExportTime(fbuf));
                }
            }
        } else if (log_rec_time.end_name) {
            /* only have an end time; use it as start time and set dur
             * to 0 */
            rwRecSetStartTime(rec, (sktime_t)eTime);
            rwRecSetElapsed(rec, 0);
            if (skpcProbeGetLogFlags(probe) & SOURCE_LOG_TIMESTAMPS) {
                sktimestamp_r(stime_buf, eTime, SKTIMESTAMP_UTC);
                if (log_rec_time.export_name) {
                    INFOMSG(("'%s': Set sTime=%sZ, dur=%.3fs"
                             " from incoming record"
                             " %s=%" PRIu64 ", %s=%" PRIu32),
                            skpcProbeGetName(probe), stime_buf, 0.0,
                            log_rec_time.end_name, log_rec_time.end_val,
                            log_rec_time.export_name, fBufGetExportTime(fbuf));
                } else {
                    INFOMSG(("'%s': Set sTime=%sZ, dur=%.3fs"
                             " from incoming record"
                             " %s=%" PRIu64),
                            skpcProbeGetName(probe), stime_buf, 0.0,
                            log_rec_time.end_name, log_rec_time.end_val);
                }
            }
        } else {
            /* no times, set start to export time and set dur to 0 */
            rwRecSetStartTime(rec, sktimeCreate(fBufGetExportTime(fbuf), 0));
            rwRecSetElapsed(rec, 0);
            if (skpcProbeGetLogFlags(probe) & SOURCE_LOG_TIMESTAMPS) {
                sktimestamp_r(stime_buf,rwRecGetStartTime(rec),SKTIMESTAMP_UTC);
                INFOMSG(("'%s': Set sTime=%sZ, dur=%.3fs based on"
                         " exportTimeSeconds=%" PRIu32),
                        skpcProbeGetName(probe), stime_buf, 0.0,
                        fBufGetExportTime(fbuf));
            }
        }
    }

    /* Copy the remainder of the record */
    rwRecSetFlowType(rec, fixrec.silkFlowType);
    rwRecSetSensor(rec, fixrec.silkFlowSensor);
    rwRecSetApplication(rec, fixrec.silkAppLabel);

    tcp_state = fixrec.silkTCPState;
    tcp_flags = (fixrec.initialTCPFlags | fixrec.unionTCPFlags);

    /* Ensure the SK_TCPSTATE_EXPANDED bit is properly set. */
    if (tcp_flags && IPPROTO_TCP == rwRecGetProto(rec)) {
        /* Flow is TCP and init|session flags had a value. */
        rwRecSetFlags(rec, tcp_flags);
        rwRecSetInitFlags(rec, fixrec.initialTCPFlags);
        rwRecSetRestFlags(rec, fixrec.unionTCPFlags);
        tcp_state |= SK_TCPSTATE_EXPANDED;
    } else {
        /* clear bit when not TCP or no separate init/session flags */
        tcp_state &= ~SK_TCPSTATE_EXPANDED;
        /* use whatever all-flags we were given; leave initial-flags
         * and session-flags unset */
        rwRecSetFlags(rec, fixrec.tcpControlBits);
    }

    /* Process the flowEndReason and flowAttributes unless one of
     * those bits is already set (via silkTCPState). */
    if (!(tcp_state
          & (SK_TCPSTATE_FIN_FOLLOWED_NOT_ACK | SK_TCPSTATE_TIMEOUT_KILLED
             | SK_TCPSTATE_TIMEOUT_STARTED | SK_TCPSTATE_UNIFORM_PACKET_SIZE)))
    {
        /* Note active timeout */
        if ((fixrec.flowEndReason & SKI_END_MASK) == SKI_END_ACTIVE) {
            tcp_state |= SK_TCPSTATE_TIMEOUT_KILLED;
        }
        /* Note continuation */
        if (fixrec.flowEndReason & SKI_END_ISCONT) {
            tcp_state |= SK_TCPSTATE_TIMEOUT_STARTED;
        }
        /* Note flows with records of uniform size */
        if (fixrec.flowAttributes & SKI_FLOW_ATTRIBUTE_UNIFORM_PACKET_SIZE) {
            tcp_state |= SK_TCPSTATE_UNIFORM_PACKET_SIZE;
        }
        rwRecSetTcpState(rec, tcp_state);
    }

    rwRecSetTcpState(rec, tcp_state);

    /* Handle the reverse record if the caller provided one and if
     * there is one in the IPFIX record, which is indicated by the
     * value of 'rev_bytes'.*/
    if (0 == rev_bytes) {
        /* No data for reverse direction; just clear the record. */
        if (reverse_rec) {
            rwRec *revRec;
            revRec = skIPFIXSourceRecordGetRwrec(reverse_rec);
            RWREC_CLEAR(revRec);
        }
    } else if (reverse_rec) {
        rwRec *revRec;
        revRec = skIPFIXSourceRecordGetRwrec(reverse_rec);

        /* We have data for reverse direction. */
        TRACEMSG(("Handling reverse side of bi-flow"));

#define COPY_FORWARD_REC_TO_REVERSE 1
#if COPY_FORWARD_REC_TO_REVERSE
        /* Initialize the reverse record with the forward
         * record  */
        RWREC_COPY(revRec, rec);
#else
        /* instead of copying the forward record and changing
         * nearly everything, we could just set these fields on
         * the reverse record. */
        rwRecSetProto(revRec, fixrec.protocolIdentifier);
        rwRecSetFlowType(revRec, fixrec.silkFlowType);
        rwRecSetSensor(revRec, fixrec.silkFlowSensor);
        rwRecSetTcpState(revRec, fixrec.silkTCPState);
        rwRecSetApplication(revRec, fixrec.silkAppLabel);
        /* does using the forward nexthop IP for the reverse
         * record make any sense?  Shouldn't we check for a
         * reverse next hop address? */
        if (rwRecIsIPv6(rec)) {
            rwRecSetIPv6(revRec);
            rwRecMemSetNhIPv6(revRec, &fixrec.ipNextHopIPv6Address);
        } else {
            rwRecSetNhIPv4(revRec, &fixrec.ipNextHopIPv4Address);
        }
#endif  /* #else clause of #if COPY_FORWARD_REC_TO_REVERSE */

        /* Reverse the IPs */
        if (rwRecIsIPv6(rec)) {
            rwRecMemSetSIPv6(revRec, &fixrec.destinationIPv6Address);
            rwRecMemSetDIPv6(revRec, &fixrec.sourceIPv6Address);
        } else {
            rwRecSetSIPv4(revRec, fixrec.destinationIPv4Address);
            rwRecSetDIPv4(revRec, fixrec.sourceIPv4Address);
        }

        /* Reverse the ports unless this is an ICMP record */
        if (!rwRecIsICMP(rec)) {
            rwRecSetSPort(revRec, rwRecGetDPort(rec));
            rwRecSetDPort(revRec, rwRecGetSPort(rec));
        }

        /* Reverse the SNMP or VLAN interfaces */
        if (SKPC_IFVALUE_VLAN != skpcProbeGetInterfaceValueType(probe)) {
            rwRecSetInput(revRec, rwRecGetOutput(rec));
            rwRecSetOutput(revRec, rwRecGetInput(rec));
        } else if (TEMPLATE_GET_BIT(bmap, reverseVlanId)) {
            /* Reverse VLAN values exist.  Use them */
            rwRecSetInput(rec, fixrec.reverseVlanId);
            rwRecSetOutput(rec, fixrec.reversePostVlanId);
        } else if (TEMPLATE_GET_BIT(bmap, postVlanId)) {
            /* Reverse the forward values */
            rwRecSetInput(rec, fixrec.postVlanId);
            rwRecSetOutput(rec, fixrec.vlanId);
        } else {
            /* we have a single vlanId, so don't swap the values */
            rwRecSetInput(rec, fixrec.vlanId);
        }

        /* Set volume.  We retrieved them above */
        rwRecSetPkts(revRec, rev_pkts);
        rwRecSetBytes(revRec, rev_bytes);

        /* Calculate reverse start time from reverse RTT */

        /* Reverse flow's start time must be increased and its
         * duration decreased by its offset from the forward
         * record  */
        rwRecSetStartTime(revRec, (rwRecGetStartTime(rec)
                                   + fixrec.reverseFlowDeltaMilliseconds));
        rwRecSetElapsed(revRec, (rwRecGetElapsed(rec)
                                 - fixrec.reverseFlowDeltaMilliseconds));

        /* Note: the value of the 'tcp_state' variable from above is
         * what is in rwRecGetTcpState(revRec). */

        /* Get reverse TCP flags from the IPFIX record if they are
         * available.  Otherwise, leave the flags unchanged (using
         * those from the forward direction). */
        tcp_flags =(fixrec.reverseInitialTCPFlags|fixrec.reverseUnionTCPFlags);

        if (tcp_flags && IPPROTO_TCP == rwRecGetProto(rec)) {
            /* Flow is TCP and init|session has a value. */
            TRACEMSG(("Using reverse TCP flags (initial|session)"));
            rwRecSetFlags(revRec, tcp_flags);
            rwRecSetInitFlags(revRec, fixrec.reverseInitialTCPFlags);
            rwRecSetRestFlags(revRec, fixrec.reverseUnionTCPFlags);
            tcp_state |= SK_TCPSTATE_EXPANDED;
        } else if (TEMPLATE_GET_BIT(bmap, reverseTcpControlBits)) {
            /* Use whatever is in all-flags; clear any init/session
             * flags we got from the forward rec. */
            TRACEMSG(("Using reverse TCP flags (all only)"));
            rwRecSetFlags(revRec, fixrec.reverseTcpControlBits);
            rwRecSetInitFlags(revRec, 0);
            rwRecSetRestFlags(revRec, 0);
            tcp_state &= ~SK_TCPSTATE_EXPANDED;
        } else if (have_tcp_stml
                   || (TEMPLATE_GET_BIT(bmap, reverseInitialTCPFlags)))
        {
            /* If a reverseInitialTCPFlags Element existed on the
             * template; use it even though its value is 0. */
            TRACEMSG(("Setting all TCP flags to 0"));
            rwRecSetFlags(revRec, 0);
            rwRecSetInitFlags(revRec, 0);
            rwRecSetRestFlags(revRec, 0);
            tcp_state &= ~SK_TCPSTATE_EXPANDED;
        }
        /* else leave the flags unchanged */

        /* Handle reverse flow attributes */
        if (fixrec.reverseFlowAttributes
            & SKI_FLOW_ATTRIBUTE_UNIFORM_PACKET_SIZE)
        {
            /* ensure it is set */
            tcp_state |= SK_TCPSTATE_UNIFORM_PACKET_SIZE;
        } else {
            /* ensure it it not set */
            tcp_state &= ~SK_TCPSTATE_UNIFORM_PACKET_SIZE;
        }

        rwRecSetTcpState(revRec, tcp_state);
    }

    /* all done */
    return ((rev_bytes > 0) ? 2 : 1);
}


void
skiCheckDataStructure(
    FILE               *fh)
{
    unsigned long pos;

#define PRINT_TITLE(s_)                                         \
    fprintf(fh, "===> %s\n%5s|%5s|%5s|%5s|%5s|%s\n", #s_,       \
            "begin", "end", "size", "alerr", "hole", "member")

#define PRINT_OFFSET(pos_, s_, mem_)                                    \
    {                                                                   \
        s_ x;                                                           \
        unsigned long off_ = (unsigned long)offsetof(s_, mem_);         \
        unsigned long sz_  = (unsigned long)sizeof(x.mem_);             \
        unsigned long end_ = off_ + sz_ - 1;                            \
        int align_ = ((off_ % sz_) == 0);                               \
        int hole_ = (pos_ != off_);                                     \
        pos_ += sz_;                                                    \
        fprintf(fh, "%5lu|%5lu|%5lu|%5s|%5s|%s\n",                      \
                off_, end_, sz_, (align_ ? "" : "alerr"),               \
                (hole_ ? "hole" : ""), #mem_);                          \
    }

    pos = 0;
    PRINT_TITLE(ski_fixrec_t);
    PRINT_OFFSET(pos, ski_fixrec_t, flowStartMilliseconds);
    PRINT_OFFSET(pos, ski_fixrec_t, flowEndMilliseconds);
    PRINT_OFFSET(pos, ski_fixrec_t, sourceIPv6Address);
    PRINT_OFFSET(pos, ski_fixrec_t, destinationIPv6Address);
    PRINT_OFFSET(pos, ski_fixrec_t, sourceIPv4Address);
    PRINT_OFFSET(pos, ski_fixrec_t, destinationIPv4Address);
    PRINT_OFFSET(pos, ski_fixrec_t, sourceTransportPort);
    PRINT_OFFSET(pos, ski_fixrec_t, destinationTransportPort);
    PRINT_OFFSET(pos, ski_fixrec_t, ipNextHopIPv4Address);
    PRINT_OFFSET(pos, ski_fixrec_t, ipNextHopIPv6Address);
    PRINT_OFFSET(pos, ski_fixrec_t, ingressInterface);
    PRINT_OFFSET(pos, ski_fixrec_t, egressInterface);
    PRINT_OFFSET(pos, ski_fixrec_t, packetDeltaCount);
    PRINT_OFFSET(pos, ski_fixrec_t, octetDeltaCount);
    PRINT_OFFSET(pos, ski_fixrec_t, protocolIdentifier);
    PRINT_OFFSET(pos, ski_fixrec_t, silkFlowType);
    PRINT_OFFSET(pos, ski_fixrec_t, silkFlowSensor);
    PRINT_OFFSET(pos, ski_fixrec_t, tcpControlBits);
    PRINT_OFFSET(pos, ski_fixrec_t, initialTCPFlags);
    PRINT_OFFSET(pos, ski_fixrec_t, unionTCPFlags);
    PRINT_OFFSET(pos, ski_fixrec_t, silkTCPState);
    PRINT_OFFSET(pos, ski_fixrec_t, silkAppLabel);
    PRINT_OFFSET(pos, ski_fixrec_t, sourceMacAddress);
    PRINT_OFFSET(pos, ski_fixrec_t, destinationMacAddress);
    PRINT_OFFSET(pos, ski_fixrec_t, ipClassOfService);
    PRINT_OFFSET(pos, ski_fixrec_t, reverseIpClassOfService);
    PRINT_OFFSET(pos, ski_fixrec_t, packetTotalCount);
    PRINT_OFFSET(pos, ski_fixrec_t, octetTotalCount);
    PRINT_OFFSET(pos, ski_fixrec_t, initiatorPackets);
    PRINT_OFFSET(pos, ski_fixrec_t, initiatorOctets);
    PRINT_OFFSET(pos, ski_fixrec_t, reversePacketDeltaCount);
    PRINT_OFFSET(pos, ski_fixrec_t, reverseOctetDeltaCount);
    PRINT_OFFSET(pos, ski_fixrec_t, reversePacketTotalCount);
    PRINT_OFFSET(pos, ski_fixrec_t, reverseOctetTotalCount);
    PRINT_OFFSET(pos, ski_fixrec_t, responderPackets);
    PRINT_OFFSET(pos, ski_fixrec_t, responderOctets);
    PRINT_OFFSET(pos, ski_fixrec_t, flowStartMicroseconds);
    PRINT_OFFSET(pos, ski_fixrec_t, flowEndMicroseconds);
    PRINT_OFFSET(pos, ski_fixrec_t, flowStartNanoseconds);
    PRINT_OFFSET(pos, ski_fixrec_t, flowEndNanoseconds);
    PRINT_OFFSET(pos, ski_fixrec_t, systemInitTimeMilliseconds);
    PRINT_OFFSET(pos, ski_fixrec_t, flowStartSeconds);
    PRINT_OFFSET(pos, ski_fixrec_t, flowEndSeconds);
    PRINT_OFFSET(pos, ski_fixrec_t, flowDurationMicroseconds);
    PRINT_OFFSET(pos, ski_fixrec_t, flowDurationMilliseconds);
    PRINT_OFFSET(pos, ski_fixrec_t, flowStartDeltaMicroseconds);
    PRINT_OFFSET(pos, ski_fixrec_t, flowEndDeltaMicroseconds);
    PRINT_OFFSET(pos, ski_fixrec_t, reverseFlowDeltaMilliseconds);
    PRINT_OFFSET(pos, ski_fixrec_t, flowStartSysUpTime);
    PRINT_OFFSET(pos, ski_fixrec_t, flowEndSysUpTime);
    PRINT_OFFSET(pos, ski_fixrec_t, reverseTcpControlBits);
    PRINT_OFFSET(pos, ski_fixrec_t, reverseInitialTCPFlags);
    PRINT_OFFSET(pos, ski_fixrec_t, reverseUnionTCPFlags);
    PRINT_OFFSET(pos, ski_fixrec_t, flowEndReason);
    PRINT_OFFSET(pos, ski_fixrec_t, flowAttributes);
    PRINT_OFFSET(pos, ski_fixrec_t, reverseFlowAttributes);
    PRINT_OFFSET(pos, ski_fixrec_t, vlanId);
    PRINT_OFFSET(pos, ski_fixrec_t, postVlanId);
    PRINT_OFFSET(pos, ski_fixrec_t, reverseVlanId);
    PRINT_OFFSET(pos, ski_fixrec_t, reversePostVlanId);
    PRINT_OFFSET(pos, ski_fixrec_t, bgpSourceAsNumber);
    PRINT_OFFSET(pos, ski_fixrec_t, bgpDestinationAsNumber);
    PRINT_OFFSET(pos, ski_fixrec_t, mplsTopLabelIPv4Address);
    PRINT_OFFSET(pos, ski_fixrec_t, mplsLabels);
    PRINT_OFFSET(pos, ski_fixrec_t, mplsTopLabelPrefixLength);
    PRINT_OFFSET(pos, ski_fixrec_t, mplsTopLabelType);
    PRINT_OFFSET(pos, ski_fixrec_t, firewallEvent);
    PRINT_OFFSET(pos, ski_fixrec_t, NF_F_FW_EVENT);
    PRINT_OFFSET(pos, ski_fixrec_t, NF_F_FW_EXT_EVENT);
    PRINT_OFFSET(pos, ski_fixrec_t, icmpTypeCodeIPv4);
    PRINT_OFFSET(pos, ski_fixrec_t, icmpTypeIPv4);
    PRINT_OFFSET(pos, ski_fixrec_t, icmpCodeIPv4);
    PRINT_OFFSET(pos, ski_fixrec_t, icmpTypeCodeIPv6);
    PRINT_OFFSET(pos, ski_fixrec_t, icmpTypeIPv6);
    PRINT_OFFSET(pos, ski_fixrec_t, icmpCodeIPv6);
    PRINT_OFFSET(pos, ski_fixrec_t, collectionTimeMilliseconds);
    PRINT_OFFSET(pos, ski_fixrec_t, observationTimeMilliseconds);
    PRINT_OFFSET(pos, ski_fixrec_t, observationTimeMicroseconds);
    PRINT_OFFSET(pos, ski_fixrec_t, observationTimeNanoseconds);
    PRINT_OFFSET(pos, ski_fixrec_t, observationTimeSeconds);
    PRINT_OFFSET(pos, ski_fixrec_t, samplerId);
    PRINT_OFFSET(pos, ski_fixrec_t, flowDirection);
#if SKI_FIXREC_PADDING != 0
    PRINT_OFFSET(pos, ski_fixrec_t, pad);
#endif

    pos = 0;
    PRINT_TITLE(ski_yaf_stats_t);
    PRINT_OFFSET(pos, ski_yaf_stats_t, systemInitTimeMilliseconds);
    PRINT_OFFSET(pos, ski_yaf_stats_t, exportedFlowRecordTotalCount);
    PRINT_OFFSET(pos, ski_yaf_stats_t, packetTotalCount);
    PRINT_OFFSET(pos, ski_yaf_stats_t, droppedPacketTotalCount);
    PRINT_OFFSET(pos, ski_yaf_stats_t, ignoredPacketTotalCount);
    PRINT_OFFSET(pos, ski_yaf_stats_t, notSentPacketTotalCount);
    PRINT_OFFSET(pos, ski_yaf_stats_t, expiredFragmentCount);
#if 0
    PRINT_OFFSET(pos, ski_yaf_stats_t, assembledFragmentCount);
    PRINT_OFFSET(pos, ski_yaf_stats_t, flowTableFlushEventCount);
    PRINT_OFFSET(pos, ski_yaf_stats_t, flowTablePeakCount);
    PRINT_OFFSET(pos, ski_yaf_stats_t, meanFlowRate);
    PRINT_OFFSET(pos, ski_yaf_stats_t, meanPacketRate);
    PRINT_OFFSET(pos, ski_yaf_stats_t, exporterIPv4Address);
#endif  /* 0 */
    PRINT_OFFSET(pos, ski_yaf_stats_t, exportingProcessId);
#if SKI_YAF_STATS_PADDING != 0
    PRINT_OFFSET(pos, ski_yaf_stats_t, pad);
#endif
}


/*
** Local Variables:
** mode:c
** indent-tabs-mode:nil
** c-basic-offset:4
** End:
*/
