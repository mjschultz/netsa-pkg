/*
** Copyright (C) 2006-2017 by Carnegie Mellon University.
**
** @OPENSOURCE_LICENSE_START@
** See license information in ../../LICENSE.txt
** @OPENSOURCE_LICENSE_END@
*/

/*
**  ipfixsource.c
**
**    Interface to pull flows from IPFIX/NetFlowV9/sFlow streams.
**
*/

#include <silk/silk.h>

RCSIDENT("$SiLK: rwflowpack-ipfixsource.c efd886457770 2017-06-21 18:43:23Z mthomas $");

#include <silk/redblack.h>
#include <silk/skfixstream.h>
#include <silk/skipfixcert.h>
#include <silk/skschema.h>
#include <silk/skpolldir.h>
#include "rwflowpack_priv.h"

#ifdef  SKIPFIXSOURCE_TRACE_LEVEL
#define TRACEMSG_LEVEL SKIPFIXSOURCE_TRACE_LEVEL
#endif
#define TRACEMSG(lvl, msg)                      \
    TRACEMSG_TO_TRACEMSGLVL(lvl, msg)
#include <silk/sktracemsg.h>


/*
 *  **********************************************************************
 *  **********************************************************************
 *
 *  skipfix.c
 *
 */


/* LOCAL DEFINES AND TYPEDEFS */

/* Whether to export information elements in IPFIX files we write */
#ifndef SKIPFIX_EXPORT_ELEMENTS
#define SKIPFIX_EXPORT_ELEMENTS 0
#endif

/* The IPFIX Private Enterprise Number for CERT */
#define IPFIX_CERT_PEN  6871

/* Extenal Template ID used for SiLK Flows written by rwsilk2ipfix. */
#define SKI_RWREC_TID           0xAFEA

/* Internal Template ID for extended SiLK flows. */
#define SKI_EXTRWREC_TID        0xAFEB

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


/* Compatilibity for libfixbuf < 1.4.0 */
#ifndef FB_IE_INIT_FULL

#define FB_IE_INIT_FULL(_name_, _ent_, _num_, _len_, _flags_, _min_, _max_, _type_, _desc_) \
    FB_IE_INIT(_name_, _ent_, _num_, _len_, _flags_)

#define FB_IE_QUANTITY          0
#define FB_IE_TOTALCOUNTER      0
#define FB_IE_DELTACOUNTER      0
#define FB_IE_IDENTIFIER        0
#define FB_IE_FLAGS             0
#define FB_IE_LIST              0

#define FB_UNITS_BITS           0
#define FB_UNITS_OCTETS         0
#define FB_UNITS_PACKETS        0
#define FB_UNITS_FLOWS          0
#define FB_UNITS_SECONDS        0
#define FB_UNITS_MILLISECONDS   0
#define FB_UNITS_MICROSECONDS   0
#define FB_UNITS_NANOSECONDS    0
#define FB_UNITS_WORDS          0
#define FB_UNITS_MESSAGES       0
#define FB_UNITS_HOPS           0
#define FB_UNITS_ENTRIES        0

#endif  /* FB_IE_INIT_FULL */


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


/* Bytes of padding to add to ski_yaf_stats to get a multiple of
 * 64bits */
#define SKI_YAF_STATS_PADDING  0

/* FIXME: How to handle yaf stats packets in this New World Order? */
/* ski_yaf_stats_t is the record type for yaf statistics; these values
 * are based on the yaf 2.3.0 manual page. */
struct ski_yaf_stats_st {
    /* The time in milliseconds of the last (re-)initialization of
     * yaf.  IE 161, 8 octets */
    uint64_t    systemInitTimeMilliseconds;

    /* Total amount of exported flows from yaf start time.  IE 42, 8
     * octets */
    uint64_t    exportedFlowRecordTotalCount;

    /* Total amount of packets processed by yaf from yaf start time.
     * IE 86, 8 octets */
    uint64_t    packetTotalCount;

    /* Total amount of dropped packets according to statistics given
     * by libpcap, libdag, or libpcapexpress.  IE 135, 8 octets */
    uint64_t    droppedPacketTotalCount;

    /* Total amount of packets ignored by the yaf packet decoder, such
     * as unsupported packet types and incomplete headers, from yaf
     * start time.  IE 164, 8 octets */
    uint64_t    ignoredPacketTotalCount;

    /* Total amount of packets rejected by yaf because they were
     * received out of sequence.  IE 167, 8 octets */
    uint64_t    notSentPacketTotalCount;

    /* Total amount of fragments that have been expired since yaf
     * start time.  CERT (PEN 6871) IE 100, 4 octets */
    uint32_t    expiredFragmentCount;

#if 0
    /* Total number of packets that been assembled from a series of
     * fragments since yaf start time. CERT (PEN 6871) IE 101, 4
     * octets */
    uint32_t    assembledFragmentCount;

    /* Total number of times the yaf flow table has been flushed since
     * yaf start time.  CERT (PEN 6871) IE 104, 4 octets */
    uint32_t    flowTableFlushEventCount;

    /* The maximum number of flows in the yaf flow table at any one
     * time since yaf start time.  CERT (PEN 6871) IE 105, 4 octets */
    uint32_t    flowTablePeakCount;

    /* The mean flow rate of the yaf flow sensor since yaf start time,
     * rounded to the nearest integer.  CERT (PEN 6871) IE 102, 4
     * octets */
    uint32_t    meanFlowRate;

    /* The mean packet rate of the yaf flow sensor since yaf start
     * time, rounded to the nearest integer.  CERT (PEN 6871) IE 103,
     * 4 octets */
    uint32_t    meanPacketRate;

    /* The IPv4 Address of the yaf flow sensor.  IE 130, 4 octets */
    uint32_t    exporterIPv4Address;
#endif  /* 0 */

    /* The following is not currently used, but it is here for
     * alignment purposes. */
    /* Set the ID of the yaf flow sensor by giving a value to
     * --observation-domain.  The default is 0.   IE 144, 4 octets */
    uint32_t    exportingProcessId;

#if SKI_YAF_STATS_PADDING != 0
    uint8_t     pad[SKI_YAF_STATS_PADDING];
#endif
};
typedef struct ski_yaf_stats_st ski_yaf_stats_t;


/* ipfix_tmpl_rec_t */
struct ipfix_tmpl_rec_st {
    rwRec               rec;
    size_t              len;
    /* location of the flowStartMilliseconds field in 'rec' */
    const sk_field_t   *stime;
    /* location of the flowEndMilliseconds field in 'rec' */
    const sk_field_t   *etime;
    /* location of potential start-time, end-time, and
     * router-boot-time fields in 'rec' */
    const sk_field_t   *times[3];
    uint16_t            tid;
};
typedef struct ipfix_tmpl_rec_st ipfix_tmpl_rec_t;


/* tmpl_to_schema_ctx_t */
struct tmpl_to_schema_ctx_st {
    sk_schema_timemap_t    *timemap;
    sk_fixrec_t             rec;
    size_t                  len;
    uint16_t                tid;
};
typedef struct tmpl_to_schema_ctx_st tmpl_to_schema_ctx_t;

/*
 *     ipfix_to_schema_fields_t is a struct containing all fields that
 *     fixrec_pack_record() might want to consider when converting an
 *     IPFIX rwrec to a traditional rwrec.
 *
 *     This struct must be kept in sync with the
 *     ipfix_to_schema_fieldlist array.  To facilitate this, both this
 *     structure and the named array use identical SK_ENTRY() clauses
 *     with a suitable redefinition of the SK_ENTRY() macro.
 */
#define SK_ENTRY(name, pen, id) const sk_field_t *name;
typedef struct ipfix_to_schema_fields_st {
    SK_ENTRY(octetDeltaCount,          0, 1)
    SK_ENTRY(packetDeltaCount,         0, 2)
    SK_ENTRY(protocolIdentifier,       0, 4)
    SK_ENTRY(tcpControlBits,           0, 6)
    SK_ENTRY(sourceTransportPort,      0, 7)
    SK_ENTRY(sourceIPv4Address,        0, 8)
    SK_ENTRY(ingressInterface,         0, 10)
    SK_ENTRY(destinationTransportPort, 0, 11)
    SK_ENTRY(destinationIPv4Address,   0, 12)
    SK_ENTRY(egressInterface,          0, 14)
    SK_ENTRY(ipNextHopIPv4Address,     0, 15)
    SK_ENTRY(sourceIPv6Address,        0, 27)
    SK_ENTRY(destinationIPv6Address,   0, 28)
    SK_ENTRY(icmpTypeCodeIPv4,         0, 32)
    SK_ENTRY(ipNextHopIPv6Address,     0, 62)
    SK_ENTRY(octetTotalCount,          0, 85)
    SK_ENTRY(packetTotalCount,         0, 86)
    SK_ENTRY(flowEndReason,            0, 136)
    SK_ENTRY(icmpTypeCodeIPv6,         0, 139)
    SK_ENTRY(flowStartMilliseconds,    0, 152)
    SK_ENTRY(flowEndMilliseconds,      0, 153)
    SK_ENTRY(icmpTypeIPv4,             0, 176)
    SK_ENTRY(icmpCodeIPv4,             0, 177)
    SK_ENTRY(icmpTypeIPv6,             0, 178)
    SK_ENTRY(icmpCodeIPv6,             0, 179)
    SK_ENTRY(initiatorOctets,          0, 231)
    SK_ENTRY(initiatorPackets,         0, 298)
    SK_ENTRY(initialTCPFlags,          IPFIX_CERT_PEN, 14)
    SK_ENTRY(unionTCPFlags,            IPFIX_CERT_PEN, 15)
    SK_ENTRY(silkFlowType,             IPFIX_CERT_PEN, 30)
    SK_ENTRY(silkFlowSensor,           IPFIX_CERT_PEN, 31)
    SK_ENTRY(silkTCPState,             IPFIX_CERT_PEN, 32)
    SK_ENTRY(silkAppLabel,             IPFIX_CERT_PEN, 33)
    SK_ENTRY(flowAttributes,           IPFIX_CERT_PEN, 40)

    /* Reverse elements */
    SK_ENTRY(reverseOctetDeltaCount,   FB_IE_PEN_REVERSE, 1)
    SK_ENTRY(reversePacketDeltaCount,  FB_IE_PEN_REVERSE, 2)
    SK_ENTRY(reverseTcpControlBits,    FB_IE_PEN_REVERSE, 6)
    SK_ENTRY(reverseOctetTotalCount,   FB_IE_PEN_REVERSE, 85)
    SK_ENTRY(reversePacketTotalCount,  FB_IE_PEN_REVERSE, 86)
    SK_ENTRY(responderOctets,          0, 232)
    SK_ENTRY(responderPackets,         0, 299)
    SK_ENTRY(reverseInitialTCPFlags,
             IPFIX_CERT_PEN, 14 | FB_IE_VENDOR_BIT_REVERSE)
    SK_ENTRY(reverseUnionTCPFlags,
             IPFIX_CERT_PEN, 15 | FB_IE_VENDOR_BIT_REVERSE)
    SK_ENTRY(reverseFlowDeltaMilliseconds,
             IPFIX_CERT_PEN, 21)
    SK_ENTRY(reverseFlowAttributes,
             IPFIX_CERT_PEN, 40 | FB_IE_VENDOR_BIT_REVERSE)
} ipfix_to_schema_fields_t;
#undef SK_ENTRY

/*
 *     ipfix_to_schema_fieldlist is an array containing the field
 *     identifiers of the fields in the ipfix_to_schema_fields_t
 *     struct, in order.
 *
 *     This array must be kept in sync with the
 *     ipfix_to_schema_fields_t struct.  To facilitate this, both this
 *     array and the named struct use identical SK_ENTRY() clauses
 *     with a suitable redefinition of the SK_ENTRY() macro.
 */
#define SK_ENTRY(name, pen, id) SK_FIELD_IDENT_CREATE(pen, id),
#define FIELDLIST_SENTINEL SK_FIELD_IDENT_CREATE(0, 0)
static sk_field_ident_t ipfix_to_schema_fieldlist[] = {
    SK_ENTRY(octetDeltaCount,          0, 1)
    SK_ENTRY(packetDeltaCount,         0, 2)
    SK_ENTRY(protocolIdentifier,       0, 4)
    SK_ENTRY(tcpControlBits,           0, 6)
    SK_ENTRY(sourceTransportPort,      0, 7)
    SK_ENTRY(sourceIPv4Address,        0, 8)
    SK_ENTRY(ingressInterface,         0, 10)
    SK_ENTRY(destinationTransportPort, 0, 11)
    SK_ENTRY(destinationIPv4Address,   0, 12)
    SK_ENTRY(egressInterface,          0, 14)
    SK_ENTRY(ipNextHopIPv4Address,     0, 15)
    SK_ENTRY(sourceIPv6Address,        0, 27)
    SK_ENTRY(destinationIPv6Address,   0, 28)
    SK_ENTRY(icmpTypeCodeIPv4,         0, 32)
    SK_ENTRY(ipNextHopIPv6Address,     0, 62)
    SK_ENTRY(octetTotalCount,          0, 85)
    SK_ENTRY(packetTotalCount,         0, 86)
    SK_ENTRY(flowEndReason,            0, 136)
    SK_ENTRY(icmpTypeCodeIPv6,         0, 139)
    SK_ENTRY(flowStartMilliseconds,    0, 152)
    SK_ENTRY(flowEndMilliseconds,      0, 153)
    SK_ENTRY(icmpTypeIPv4,             0, 176)
    SK_ENTRY(icmpCodeIPv4,             0, 177)
    SK_ENTRY(icmpTypeIPv6,             0, 178)
    SK_ENTRY(icmpCodeIPv6,             0, 179)
    SK_ENTRY(initiatorOctets,          0, 231)
    SK_ENTRY(initiatorPackets,         0, 298)
    SK_ENTRY(initialTCPFlags,          IPFIX_CERT_PEN, 14)
    SK_ENTRY(unionTCPFlags,            IPFIX_CERT_PEN, 15)
    SK_ENTRY(silkFlowType,             IPFIX_CERT_PEN, 30)
    SK_ENTRY(silkFlowSensor,           IPFIX_CERT_PEN, 31)
    SK_ENTRY(silkTCPState,             IPFIX_CERT_PEN, 32)
    SK_ENTRY(silkAppLabel,             IPFIX_CERT_PEN, 33)
    SK_ENTRY(flowAttributes,           IPFIX_CERT_PEN, 40)

    /* Reverse elements */
    SK_ENTRY(reverseOctetDeltaCount,   FB_IE_PEN_REVERSE, 1)
    SK_ENTRY(reversePacketDeltaCount,  FB_IE_PEN_REVERSE, 2)
    SK_ENTRY(reverseTcpControlBits,    FB_IE_PEN_REVERSE, 6)
    SK_ENTRY(reverseOctetTotalCount,   FB_IE_PEN_REVERSE, 85)
    SK_ENTRY(reversePacketTotalCount,  FB_IE_PEN_REVERSE, 86)
    SK_ENTRY(responderOctets,          0, 232)
    SK_ENTRY(responderPackets,         0, 299)
    SK_ENTRY(reverseInitialTCPFlags,
             IPFIX_CERT_PEN, 14 | FB_IE_VENDOR_BIT_REVERSE)
    SK_ENTRY(reverseUnionTCPFlags,
             IPFIX_CERT_PEN, 15 | FB_IE_VENDOR_BIT_REVERSE)
    SK_ENTRY(reverseFlowDeltaMilliseconds,
             IPFIX_CERT_PEN, 21)
    SK_ENTRY(reverseFlowAttributes,
             IPFIX_CERT_PEN, 40 | FB_IE_VENDOR_BIT_REVERSE)

    FIELDLIST_SENTINEL
};
#undef SK_ENTRY


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
 *    Whether to consider the source port when determining whether UDP
 *    streams from a single host are the same stream.
 */
static int consider_sport = 1;

/*
 *    Identifier to get the schema context that is used when packing a
 *    record.
 */
static sk_schema_ctx_ident_t packrec_ctx_ident = SK_SCHEMA_CTX_IDENT_INVALID;


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
    if (ski_model) {
        skipfix_information_model_destroy(ski_model);
        ski_model = NULL;
    }
}

#if 0
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
 *
 *    This function requires libfixbuf 1.4.0 or later.
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
                    domain, tid, i, ie->len, ie->ent, ie->num,
                    ie->ref.canon->ref.name);
        }
    }
}
#endif  /* 0 */


/*
 *  ipfixIpfixTemplateCallbackFree(cur_rec, ipfix);
 *
 *    Free the structure that holds the current record.
 *
 *    This function is called by libfixbuf when a template is
 *    destroyed.  This function is registered with fixbuf by
 *    ipfixIpfixTemplateCallbackHandler().
 */
static void
skiTemplateCallbackFree(
    void               *v_tmpl_ctx,
    void        UNUSED(*app_ctx))
{
    tmpl_to_schema_ctx_t *tmpl_ctx = (tmpl_to_schema_ctx_t*)v_tmpl_ctx;

    if (tmpl_ctx) {
        TRACEMSG(2, ("Freeing schema %p on template_ctx %p, tid 0x%04X",
                     sk_fixrec_get_schema(&tmpl_ctx->rec), tmpl_ctx,
                     tmpl_ctx->tid));
        sk_fixrec_destroy(&tmpl_ctx->rec);
        sk_schema_timemap_destroy(tmpl_ctx->timemap);
        free(tmpl_ctx);
    }
}


/*
 *  ipfixIpfixTemplateCallbackHandler(session, tid, tmpl, ipfix, &cur_rec, free_fn);
 *
 *    Create an object to hold the current record that matches the
 *    template 'tmpl' having the template ID 'tid' owned by 'session'.
 *
 *    The object is returned to the caller in the memory referenced by
 *    'cur_rec'.  The 'free_fn' is the function to deallocate that
 *    structure.
 *
 *    This function is called by libfixbuf when a new template is
 *    noticed.  The function is registered with fixbuf by
 *    fbSessionAddTemplateCtxCallback2().
 */
static void
skiTemplateCallbackCtx(
    fbSession_t                    *session,
    uint16_t                        etid,
    fbTemplate_t                   *tmpl,
    void                    UNUSED(*v_base),
    void                          **v_ctx,
    fbTemplateCtxFree2_fn          *ctx_free_fn)
{
    ipfix_to_schema_fields_t *fields;
    const sk_field_t **field_array;
    sk_field_ident_t *ident;
    tmpl_to_schema_ctx_t *tmpl_ctx;
    sk_schema_t *schema;
    fbTemplate_t *schema_tmpl;
    GError *err = NULL;

    /* ignore this template if it is for sending custom IPFIX
     * elements */
    if (fbInfoModelTypeInfoRecord(tmpl)) {
        return;
    }

    if (sk_schema_create_from_template(
            &schema, fbSessionGetInfoModel(session), tmpl))
    {
        skAbort();
    }
    sk_schema_set_tid(schema, etid);

    tmpl_ctx = sk_alloc(tmpl_to_schema_ctx_t);

    /* add support for normalizing the time fields */
    sk_schema_timemap_create(&tmpl_ctx->timemap, schema);

#if 0
    /* FIXME: we want to invoke the schema callback function that
     * exists on the ipfix-source.  however, due to the completely
     * bizarre way that fixbuf handles UDP connections, there is no
     * way to pass the source object into the template-set-callback
     * function because it is only after you READ DATA from the fbuf
     * that the connection object is properly set.  And of course,
     * once you have READ DATA, any templates in that first packet
     * will have already been processed and setting the callback at
     * that point is useless. */

    /* call the new-schema callback function if it is set */
    if (ipfix->ipfix.schema_cb_fn) {
        ipfix->ipfix.schema_cb_fn(
            schema, etid, (void*)ipfix->ipfix.schema_cb_data);
    }
#endif  /* 0 */

    if (sk_schema_freeze(schema)) {
        sk_schema_timemap_destroy(tmpl_ctx->timemap);
        sk_schema_destroy(schema);
        skAbort();
    }

    sk_schema_get_template(schema, &schema_tmpl, &tmpl_ctx->tid);

    /* add internal template */
    if (!fbSessionAddTemplate(session, TRUE, tmpl_ctx->tid, schema_tmpl, &err))
    {
        g_clear_error(&err);
        sk_schema_timemap_destroy(tmpl_ctx->timemap);
        sk_schema_destroy(schema);
        skAbort();
    }

    tmpl_ctx->len = sk_schema_get_record_length(schema);
    sk_fixrec_init(&tmpl_ctx->rec, schema);

    /* set the context used to convert an IPFIX record to an rwRec */
    fields = sk_alloc(ipfix_to_schema_fields_t);
    field_array = (const sk_field_t **)fields;
    for (ident = ipfix_to_schema_fieldlist;
         *ident != FIELDLIST_SENTINEL;
         ++ident)
    {
        *field_array = sk_schema_get_field_by_ident(schema, *ident, NULL);
        ++field_array;
    }
    sk_schema_set_context(schema, packrec_ctx_ident, (void *)fields, free);

    TRACEMSG(2, ("Created schema %p on template_ctx %p, tid 0x%04X",
                 schema, tmpl_ctx, etid));

    /* since 'schema' is going out of scope and 'tmpl_ctx->rec' now
     * has a reference to it, reduce its reference count */
    sk_schema_destroy(schema);

    *v_ctx = (void*)tmpl_ctx;
    *ctx_free_fn = &skiTemplateCallbackFree;
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


static void
skiTeardown(
    void)
{
    skiInfoModelFree();
    skiSessionsFree();
}


/* **************************************************************
 * *****  Support for reading/import
 */


static fbListener_t *
skiCreateListener(
    fbConnSpec_t           *spec,
    fbListenerAppInit_fn    appinit,
    fbListenerAppFree_fn    appfree,
    void                   *tmpl_app_ctx,
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
    if (skVectorAppendValue(session_list, &session) != 0) {
        fbSessionFree(session);
        return NULL;
    }

    /* One-time initialization of packrec_ctx_ident */
    if (packrec_ctx_ident == SK_SCHEMA_CTX_IDENT_INVALID) {
        sk_schema_context_ident_create(&packrec_ctx_ident);
    }

    /* Invoke a callback when a new template arrives that tells fixbuf
     * how to map from the subTemplateMultiList used by YAF for TCP
     * information to our internal structure. */
    fbSessionAddTemplateCtxCallback2(
        session, &skiTemplateCallbackCtx, tmpl_app_ctx);

    /* Allocate a listener */
    return fbListenerAlloc(spec, session, appinit, appfree, err);
}


/*
 *  **********************************************************************
 *  **********************************************************************
 *
 *  ipfixsource.c
 *
 */

/*
 *  Logging messages for function entry/return.
 *
 *    Define the macro SKIPFIXSOURCE_ENTRY_RETURN to trace entry to
 *    and return from the functions in this file.
 *
 *    Developers must use "TRACE_ENTRY" at the beginning of every
 *    function.  Use "TRACE_RETURN(x);" for functions that return the
 *    value "x", and use "TRACE_RETURN;" for functions that have no
 *    return value.
 */
/* #define SKIPFIXSOURCE_ENTRY_RETURN 1 */

#ifndef SKIPFIXSOURCE_ENTRY_RETURN
#  define TRACE_ENTRY
#  define TRACE_RETURN       return
#else
/*
 *  this macro is used when the extra-level debugging statements write
 *  to the log, since we do not want the result of the log's printf()
 *  to trash the value of 'errno'
 */
#define WRAP_ERRNO(x)                                           \
    do { int _saveerrno = errno; x; errno = _saveerrno; }       \
    while (0)

#if defined(SK_HAVE_C99___FUNC__)

#  define TRACE_ENTRY                                   \
    WRAP_ERRNO(DEBUGMSG("Entering %s", __func__))
#  define TRACE_RETURN                                                  \
    WRAP_ERRNO(DEBUGMSG("Exiting %s [%d]", __func__, __LINE__)); return

#else

#  define TRACE_ENTRY                                                   \
    WRAP_ERRNO(DEBUGMSG("Entering function %s:%d", __FILE__, __LINE__))
#  define TRACE_RETURN                                                  \
    WRAP_ERRNO(DEBUGMSG("Exiting function %s:%d", __FILE__, __LINE__)); return

#endif
#endif  /* SKIPFIXSOURCE_ENTRY_RETURN */


/*
 *  IMPLEMENTATION NOTES
 *
 *  Each probe is represented by a single sk_conv_ipfix_t object.
 *
 *  For probes that process file-based IPFIX sources, the
 *  sk_conv_ipfix_t object contains an fBuf_t object.  When the caller
 *  invokes skIPFIXSourceGetGeneric(), the next record is read from
 *  the fBuf_t and the record is returned.  For consistency with
 *  network processing (described next), the file-based
 *  sk_conv_ipfix_t has an ipfix_net_base_t object, but that object
 *  does little for file-based sources.
 *
 *  For probes that process network-based IPFIX sources, the
 *  combination of the following four values must be unique: protocol,
 *  listen-on-port, listen-as-address, accept-from-host.  (Note that
 *  an ADDR_ANY value for listen-as-address or accept-from-host
 *  matches all other addresses.)
 *
 *  Each sk_conv_ipfix_t references an ipfix_net_base_t object.
 *  Each unique listen-as-address/listen-to-port/protocol triple is
 *  handled by a single fbListener_t object, which is contained in the
 *  ipfix_net_base_t object.  When two sk_conv_ipfix_t's differ
 *  only by their accept-from-host addreses, the sk_conv_ipfix_t's
 *  reference the same ipfix_net_base_t object.  The
 *  ipfix_net_base_t objects contain a reference-count.  The
 *  ipfix_net_base_t is destroyed when the last sk_conv_ipfix_t
 *  referring to it is destroyed.
 *
 *  An skIPFIXConnection_t represents a connection, which is one of
 *  two things: In the TCP case, a connection is equivalent to a TCP
 *  connection.  In the UDP case, a connection is a given set of IPFIX
 *  or NFv9 UDP packets sent from a given address, to a given address,
 *  on a given port, with a given domain ID.  The skIPFIXConnection_t
 *  object is ipfixsource's way of mapping to the fbSession_t object
 *  in libfixbuf.
 *
 *  There can be multiple active connections on a probe---consider a
 *  probe that collects from two machines that load-balance.  In the
 *  code, this is represented by having each skIPFIXConnection_t
 *  object point to its sk_conv_ipfix_t.  As described below, the
 *  skIPFIXConnection_t is stored as the context pointer on the
 *  libfixbuf fbCollector_t object.
 *
 *  When a new TCP connection arrives or if a new UDP connection is
 *  seen and we are using a fixbuf that supports multi-UDP, the
 *  fixbufConnect() callback function first determines whether the
 *  peer is allowed to connect.  If the peer is allowed, the function
 *  sets the context pointer for the fbCollector_t object to the a new
 *  skIPFIXConnection_t object which contains statistics information
 *  for the connection and the sk_conv_ipfix_t object associated with
 *  the connection.  These skIPFIXConnection_t objects are destroyed
 *  in the fixbufDisconnect() callback.
 *
 *  When a new UDP peer sends data to the listener, the actual address
 *  is not known until the underlying recvmesg() call itself, rather
 *  than in an accept()-like call similar to TCP.  What this means is
 *  that in this scenario the fixbufConnect() appInit function is not
 *  called until a call to fBufNext() or fBufNextCollectionTemplate()
 *  is called.
 *
 *  There is a similar fixbufConnectUDP() function to handle UDP
 *  connections when libfixbuf does not support multi-UDP.  However,
 *  the fundamental difference is this: TCP connections are associated
 *  with a new fbCollector_t at connection time.  Non-multi-UDP
 *  connections are associated with a new fbCollector_t during the
 *  fbListenerAlloc() call.
 *
 *  FIXBUF API ISSUE: The source objects connected to the
 *  fbCollector_t objects have to be passed to the
 *  fixbufConnect*() calls via global objects---newly created
 *  sources are put into a red-black tree; the call to
 *  fixbufConnect*() attempts to find the value in the red-black tree.
 *  It would have made more sense if fbListenerAlloc() took a
 *  caller-specified context pointer which would get passed to the
 *  fbListenerAppInit_fn() and fbListenerAppFree_fn() functions.
 *
 *  There is one ipfix_net_base_reader() thread per
 *  ipfix_net_base_t object.
 *  This thread loops around fbListenerWait() returning fBuf_t
 *  objects.  The underlying skIPFIXConnection_t containing the source
 *  information is grabbed from the fBuf_t's collector.  The
 *  fBufNext() is used to read the data from the fBuf_t and this data
 *  is associated with the given source by either inserting it into
 *  the source's circular buffer, or by adding the stats information
 *  to the source.  Then we loop back determining any new connection
 *  and dealing with the next piece of data until the fBuf_t empties.
 *  We then return to fbListenerWait() to get the next fBuf_t.

 *  Since there is one thread per listener, if one source attached to
 *  a listener blocks due to the circular buffer becoming full, all
 *  sources attached to the listener will block as well.  Solving this
 *  problem would involve more threads, and moving away from the
 *  fbListenerWait() method of doing things.  We could instead have a
 *  separate thread per connection.  This would require us to handle
 *  the connections (bind/listen/accept) ourselves, and then create
 *  fBufs from the resulting file descriptors.
 */


/* LOCAL DEFINES AND TYPEDEFS */

/*
 *    The NetFlowV9/IPFIX standard stays that a 'stream' is unique if
 *    the source-address and domain are unique.  SiLK violates the
 *    standard in that it also treats the sending port as part of the
 *    unique 'stream' key.
 *
 *    To have SiLK follow the standard---that is, to treat UDP packets
 *    coming from the same source address but different source ports
 *    as being part of the same protocol stream, set the following
 *    environment variable prior to invoking rwflowpack or flowcap.
 */
#define SK_IPFIX_UDP_IGNORE_SOURCE_PORT "SK_IPFIX_UDP_IGNORE_SOURCE_PORT"

/* error codes used in callback that fixbuf calls */
#define SK_IPFIXSOURCE_DOMAIN  g_quark_from_string("silkError")
#define SK_IPFIX_ERROR_CONN    1


/**
 *    An IPFIX source is a flow record source based on IPFIX or
 *    NetFlow V9 records.  Once created, records can be requested of
 *    it via a pull mechanism.
 */
typedef struct sk_conv_ipfix_st sk_conv_ipfix_t;


/* Various macros for handling yaf stats */

/* compute difference of fields at current time 'stats' and previous
 * time 'last'; last and stats are ski_yaf_stats_t structures */
#define STAT_REC_FIELD_DIFF(last, stats, field) \
    (stats.field - last.field)

/* update current counts on sk_conv_ipfix_t 'source' with the values
 * at current time 'stats' compared with those at previous time
 * 'last'; last and stats are ski_yaf_stats_t structures */
#define INCORPORATE_STAT_RECORD(source, last, stats)                    \
    pthread_mutex_lock(&source->stats_mutex);                           \
    source->saw_yaf_stats_pkt = 1;                                      \
    if (stats.systemInitTimeMilliseconds != last.systemInitTimeMilliseconds) \
    {                                                                   \
        memset(&(last), 0, sizeof(last));                               \
    }                                                                   \
    source->yaf_dropped_packets +=                                      \
        STAT_REC_FIELD_DIFF(last, stats, droppedPacketTotalCount);      \
    source->yaf_ignored_packets +=                                      \
        STAT_REC_FIELD_DIFF(last, stats, ignoredPacketTotalCount);      \
    source->yaf_notsent_packets +=                                      \
        STAT_REC_FIELD_DIFF(last, stats, notSentPacketTotalCount);      \
    source->yaf_expired_fragments +=                                    \
        STAT_REC_FIELD_DIFF(last, stats, expiredFragmentCount);         \
    source->yaf_processed_packets +=                                    \
        STAT_REC_FIELD_DIFF(last, stats, packetTotalCount);             \
    source->yaf_exported_flows +=                                       \
        STAT_REC_FIELD_DIFF(last, stats, exportedFlowRecordTotalCount); \
    last = stats;                                                       \
    pthread_mutex_unlock(&source->stats_mutex);

#define TRACEMSG_YAF_STATS(source, stats)               \
    TRACEMSG(1, (("'%s': "                              \
                 "inittime %" PRIu64                    \
                 ", dropped %" PRIu64                   \
                 ", ignored %" PRIu64                   \
                 ", notsent %" PRIu64                   \
                 ", expired %" PRIu32                   \
                 ", pkttotal %" PRIu64                  \
                 ", exported %" PRIu64),                \
                 (source)->name,                        \
                 (stats).systemInitTimeMilliseconds,    \
                 (stats).droppedPacketTotalCount,       \
                 (stats).ignoredPacketTotalCount,       \
                 (stats).notSentPacketTotalCount,       \
                 (stats).expiredFragmentCount,          \
                 (stats).packetTotalCount,              \
                 (stats).exportedFlowRecordTotalCount))


/*
 *  SILK_PROTO_TO_FIXBUF_TRANSPORT(silk_proto, &fb_trans);
 *
 *    Set the fbTransport_t value in the memory referenced by
 *    'fb_trans' based on the SiLK protocol value 'silk_proto'.
 */
#define SILK_PROTO_TO_FIXBUF_TRANSPORT(silk_proto, fb_trans)    \
    switch (silk_proto) {                                       \
      case SKPC_PROTO_SCTP:                                     \
        *(fb_trans) = FB_SCTP;                                  \
        break;                                                  \
      case SKPC_PROTO_TCP:                                      \
        *(fb_trans) = FB_TCP;                                   \
        break;                                                  \
      case SKPC_PROTO_UDP:                                      \
        *(fb_trans) = FB_UDP;                                   \
        break;                                                  \
      default:                                                  \
        skAbortBadCase(silk_proto);                             \
    }


/* The sk_conv_ipfix_t represents a single converter, as mapped to
 * be a single probe. */
struct sk_conv_ipfix_st {
    /* for yaf sources, packets dropped by libpcap, libdag,
     * libpcapexpress.  For NetFlowV9/sFlow sources, number of packets
     * that were missed. */
    uint64_t                yaf_dropped_packets;

    /* packets ignored by yaf (unsupported packet types; bad headers) */
    uint64_t                yaf_ignored_packets;

    /* packets rejected by yaf due to being out-of-sequence */
    uint64_t                yaf_notsent_packets;

    /* packet fragments expired by yaf (e.g., never saw first frag) */
    uint64_t                yaf_expired_fragments;

    /* packets processed by yaf */
    uint64_t                yaf_processed_packets;

    /* exported flow record count */
    uint64_t                yaf_exported_flows;

    /* these next values are based on records the ipfixsource gets
     * from skipfix */
    uint64_t                forward_flows;
    uint64_t                reverse_flows;
    uint64_t                ignored_flows;

    /* mutex to protect access to the above statistics */
    pthread_mutex_t         stats_mutex;

    const sk_schema_t      *prev_schema;

    /* for NetFlowV9/sFlow sources, a red-black tree of
     * skIPFIXConnection_t objects that currently point to this
     * sk_conv_ipfix_t, keyed by the skIPFIXConnection_t pointer. */
    struct rbtree          *connections;

    /* count of skIPFIXConnection_t's associated with this source */
    uint32_t                connection_count;

    /* Whether this source has received a STATS packet from yaf.  The
     * yaf stats are only written to the log once a stats packet has
     * been received.  */
    unsigned                saw_yaf_stats_pkt   :1;
};
/* typedef struct sk_conv_ipfix_st sk_conv_ipfix_t;  // libflowsource.h */

/* Data for "active" connections */
typedef struct skIPFIXConnection_st {
    skpc_probe_t       *probe;
    ski_yaf_stats_t     last_yaf_stats;
    /* Address of the host that contacted us */
    sk_sockaddr_t       peer_addr;
    size_t              peer_len;
    /* The observation domain id. */
    uint32_t            ob_domain;
} skIPFIXConnection_t;




/* The ipfix_net_base_t object represents a single listening port
 * or file */
struct ipfix_net_base_st {
    sk_coll_thread_t    t;

    /* address we are listening to. This is an array to support a
     * hostname that maps to multiple IPs (e.g. IPv4 and IPv6). */
    const sk_sockaddr_array_t *listen_address;

    /* name of address:port to bind to */
    const char             *name;

    /* when a probe does not have an accept-from-host clause, any peer
     * may connect, and there is a one-to-one mapping between a source
     * object and a base object.  The 'any' member points to the
     * source, and the 'peer2probe' member must be NULL. */
    skpc_probe_t       *any;

    /* if there is an accept-from clause, the 'peer2probe' red-black
     * tree maps the address of the peer to a particular source object
     * (via 'ipfix_peer2probe_t' objects), and the 'any' member must
     * be NULL. */
    struct rbtree      *peer2probe;

    /* the listener object from libfixbuf */
    fbListener_t       *listener;

    /* the probe from which this base is started */
    const skpc_probe_t *start_from;

    /* A count of sources associated with this base object */
    uint32_t            refcount;

    uint16_t            protocol;

    /* whether the base is a UDP connection */
    unsigned            is_udp    : 1;
};
typedef struct ipfix_net_base_st ipfix_net_base_t;

/*
 *    ipfix_peer2probe_t maps from an socket address to a probe.
 *
 *    The 'peer2probe' member of 'ipfix_net_base_t' is a red-black
 *    tree whose data members are defined by the following structure,
 *    a 'ipfix_peer2probe_t' object.
 *
 *    The tree is used when multiple sources listen on the same port
 *    and the accept-from-host addresses are used to choose the source
 *    based on the peer address of the sender.
 *
 *    The 'peer2probe' tree uses the ipfix_peer2probe_compare()
 *    comparison function.
 */
struct ipfix_peer2probe_st {
    const sk_sockaddr_t *addr;
    skpc_probe_t        *probe;
};
typedef struct ipfix_peer2probe_st ipfix_peer2probe_t;


/* LOCAL VARIABLE DEFINITIONS */

/* Mutex around calls to skiCreateListener. */
static pthread_mutex_t create_listener_mutex = PTHREAD_MUTEX_INITIALIZER;

/*
 *    When a connection arrives, the fixbufConnect() callback is invoked
 *    with the fbListener_t where the connection arrived.  The
 *    listener_to_base mapping (red-black tree that holds
 *    'ipfix_net_base_t' objects) is used to map from the fbListener_t to
 *    the ipfix_net_base_t object.
 *
 *    The tree uses listener_to_base_compare() as its comparitor.
 */
static struct listener_to_base_st {
    /* Mutex around listener_to_base tree and count. */
    pthread_mutex_t     mutex;

    /* Map from listeners to ipfix_net_base_t objects.  Objects in
     * rbtree are ipfix_net_base_t pointers. */
    struct rbtree      *map;

    /* Count of items in the red-black tree */
    uint32_t            count;
} listener_to_base =
    { PTHREAD_MUTEX_INITIALIZER, NULL, 0};


/* FUNCTION DEFINITIONS */

/*
 *     The listener_to_base_compare() function is used as the
 *     comparison function for the listener_to_base red-black
 *     tree.  Stores objects of type ipfix_net_base_t, orders by
 *     the fbListener_t pointer value on the base.
 */
static int
listener_to_base_compare(
    const void         *va,
    const void         *vb,
    const void  UNUSED(*ctx))
{
    const fbListener_t *a = ((const ipfix_net_base_t *)va)->listener;
    const fbListener_t *b = ((const ipfix_net_base_t *)vb)->listener;
    return ((a < b) ? -1 : (a > b));
}

/**
 *    Search for a base object that does not yet have an fbListener_t
 *    associated with it by searching based on the 'listen_address'
 *    and protocol.
 *
 *    If a base is found, store its location in 'base_ret' and return
 *    1.  If no existing base object is found, return 0.
 *
 *    If an existing base object is found but its addresses do not
 *    match exactly, return -1.
 */
static int
listener_to_base_search(
    ipfix_net_base_t          **base_ret,
    const sk_sockaddr_array_t  *listen_address,
    uint16_t                    protocol)
{
    const ipfix_net_base_t *base;
    RBLIST *iter;

    TRACE_ENTRY;

    assert(base_ret);
    assert(listen_address);

    *base_ret = NULL;

    pthread_mutex_lock(&listener_to_base.mutex);
    if (NULL == listener_to_base.map) {
        assert(0 == listener_to_base.count);
        pthread_mutex_unlock(&listener_to_base.mutex);
        TRACE_RETURN(0);
    }

    /* Loop through all current bases, and compare based on the
     * listen_address and the protocol */
    iter = rbopenlist(listener_to_base.map);
    while ((base = (ipfix_net_base_t *)rbreadlist(iter)) != NULL) {
        if (base->protocol == protocol
            && skSockaddrArrayMatches(base->listen_address,
                                      listen_address, 0))
        {
            /* Found a match.  'base' is now set to the matching
             * base */
            break;
        }
    }
    rbcloselist(iter);

    *base_ret = (ipfix_net_base_t *)base;
    TRACE_RETURN(NULL != base);
}

/**
 *    Add 'base' to the listener_to_base mapping.
 */
static int
listener_to_base_insert(
    const ipfix_net_base_t *base)
{
    const ipfix_net_base_t *b;

    assert(base);
    assert(base->listener);

    pthread_mutex_lock(&listener_to_base.mutex);
    if (listener_to_base.map == NULL) {
        listener_to_base.map = rbinit(listener_to_base_compare,NULL);
        if (NULL == listener_to_base.map) {
            pthread_mutex_unlock(&listener_to_base.mutex);
            return -1;
        }
    }
    b = ((ipfix_net_base_t *)rbsearch(base, listener_to_base.map));
    if (base != b) {
        pthread_mutex_unlock(&listener_to_base.mutex);
        return -1;
    }
    ++listener_to_base.count;
    TRACEMSG(3, ("listener_to_base.count is %" PRIu32, listener_to_base.count));
    pthread_mutex_unlock(&listener_to_base.mutex);
    return 0;
}

/**
 *    Remove 'base' to the listener_to_base mapping.
 */
static void
listener_to_base_remove(
    const ipfix_net_base_t *base)
{
    const void *b;

    pthread_mutex_lock(&listener_to_base.mutex);
    if (listener_to_base.map) {
        if (listener_to_base.count && base && base->listener) {
            b = rbdelete(base, listener_to_base.map);
            if (b) {
                assert(b == base);
                --listener_to_base.count;
                TRACEMSG(3, ("listener_to_base.count is %" PRIu32,
                             listener_to_base.count));
            }
        }
        if (0 == listener_to_base.count) {
            /* When the last base is removed, destroy the global base
             * list, and call the teardown function for the libskipfix
             * library to free any global objects allocated there. */
            rbdestroy(listener_to_base.map);
            listener_to_base.map = NULL;
        skiTeardown();
        }
    }
    pthread_mutex_unlock(&listener_to_base.mutex);
}


/*
 *     The ipfix_peer2probe_compare() function is used as the
 *     comparison function for the ipfix_net_base_t's red-black tree,
 *     peer2probe.
 *
 *     The tree stores ipfix_peer2probe_t objects, keyed by
 *     sk_sockaddr_t address of the accepted peers.
 */
static int
ipfix_peer2probe_compare(
    const void         *va,
    const void         *vb,
    const void  UNUSED(*ctx))
{
    const sk_sockaddr_t *a = ((const ipfix_peer2probe_t *)va)->addr;
    const sk_sockaddr_t *b = ((const ipfix_peer2probe_t *)vb)->addr;

    return skSockaddrCompare(a, b, SK_SOCKADDRCOMP_NOPORT);
}


/*
 *     The pointer_cmp() function is used compare skIPFIXConnection_t
 *     pointers in the 'connections' red-black tree on sk_conv_ipfix_t
 *     objects.
 */
static int
pointer_cmp(
    const void         *va,
    const void         *vb,
    const void  UNUSED(*ctx))
{
    return ((va < vb) ? -1 : (va > vb));
}

static void
ipfixFirstFieldIpaddr(
    const sk_field_t  **fptr,
    skipaddr_t         *addr,
    const sk_fixrec_t  *rwrec,
    ...) SK_CHECK_SENTINEL;
static uint64_t
ipfixFirstFieldUint64(
    const sk_field_t  **fptr,
    const sk_fixrec_t  *rwrec,
    ...) SK_CHECK_SENTINEL;

/*
 *  ipfixFirstFieldIpaddr(&fptr, &addr, rwrec, &fielda, &fieldb, ..., NULL)
 *
 *    Return in 'addr' the first non-zero ip address of the given
 *    pointers to ip address fields in 'rwrec'.  The field selected
 *    will be returned in 'fptr', if non-NULL.
 */
static void
ipfixFirstFieldIpaddr(
    const sk_field_t  **fptr,
    skipaddr_t         *addr,
    const sk_fixrec_t  *rwrec,
    ...)
{
    va_list ap;
    const sk_field_t **f;

    va_start(ap, rwrec);
    if (fptr) {
        *fptr = NULL;
    }
    while ((f = va_arg(ap, const sk_field_t **))) {
        if (*f) {
            sk_fixrec_get_ip_address(rwrec, *f, addr);
            if (!skipaddrIsZero(addr)) {
                if (fptr) {
                    *fptr = *f;
                }
                goto END;
            }
        }
    }
  END:
    va_end(ap);
}


/*
 *  uint64_t = ipfixFirstFieldUint64(&fptr, rwrec,
 *                                    &fielda, &fieldb, ..., NULL)
 *
 *    Return the first non-zero value of the given pointers to numeric
 *    fields in 'rwrec'.  The field selected will be returned in
 *    'fptr', if non-NULL.
 */
static uint64_t
ipfixFirstFieldUint64(
    const sk_field_t  **fptr,
    const sk_fixrec_t  *rwrec,
    ...)
{
    va_list ap;
    uint64_t retval = 0;
    const sk_field_t **f;

    va_start(ap, rwrec);
    if (fptr) {
        *fptr = NULL;
    }
    while ((f = va_arg(ap, const sk_field_t **))) {
        if (*f) {
            sk_fixrec_get_unsigned(rwrec, *f, &retval);
            if (retval != 0) {
                if (fptr) {
                    *fptr = *f;
                }
                goto END;
            }
        }
    }
  END:
    va_end(ap);
    return retval;
}

static int
fixrec_pack_record(
    skpc_probe_t       *probe,
    const sk_fixrec_t  *fixrec)
{
    rwRec fwd_rec;
    rwRec rev_rec;
    const sk_schema_t *schema;
    ipfix_to_schema_fields_t *fields;
    uint8_t tcp_state;
    uint8_t tcp_flags, init_flags, rest_flags;
    sktime_t stime;
    uint8_t u8;
    uint16_t u16;
    uint32_t u32;
    uint32_t u64;
    sktime_t t;
    skipaddr_t source, dest, nhip;
    const sk_field_t *source_field, *dest_field, *nhip_field;

    schema = sk_fixrec_get_schema(fixrec);

    rwRecInitialize(&fwd_rec, NULL);

    /* Get or build the field context from the schema */
    fields = ((ipfix_to_schema_fields_t *)
              sk_schema_get_context(schema, packrec_ctx_ident));

    skipaddrClear(&source);
    ipfixFirstFieldIpaddr(&source_field, &source, fixrec,
                           &fields->sourceIPv6Address,
                           &fields->sourceIPv4Address, NULL);

    skipaddrClear(&dest);
    ipfixFirstFieldIpaddr(&dest_field, &dest, fixrec,
                           &fields->destinationIPv6Address,
                           &fields->destinationIPv4Address, NULL);

    skipaddrClear(&nhip);
    ipfixFirstFieldIpaddr(&nhip_field, &nhip, fixrec,
                           &fields->ipNextHopIPv6Address,
                           &fields->ipNextHopIPv4Address, NULL);

    /* If any IP address is V6, all must be. */
    if (skipaddrIsV6(&source) || skipaddrIsV6(&dest) || skipaddrIsV6(&nhip))
    {
        if (source_field == NULL) {
            skipaddrSetV6(&source, sk_ipv6_zero);
        }
        if (dest_field == NULL) {
            skipaddrSetV6(&dest, sk_ipv6_zero);
        }
        if (nhip_field == NULL) {
            skipaddrSetV6(&nhip, sk_ipv6_zero);
        }
    }

    /* Forward record */

    /* SIP */
    rwRecMemSetSIP(&fwd_rec, &source);

    /* DIP */
    rwRecMemSetDIP(&fwd_rec, &dest);

    /* NHIP */
    rwRecMemSetNhIP(&fwd_rec, &nhip);

    /* proto */
    if (fields->protocolIdentifier) {
        sk_fixrec_get_unsigned8(fixrec, fields->protocolIdentifier, &u8);
        rwRecSetProto(&fwd_rec, u8);
    }

    /* Handle the ports (if they exist), then the ICMP type/code.
     * This should end up working even if dport is used as the
     * ICMP type/code */

    /* sport */
    if (fields->sourceTransportPort) {
        sk_fixrec_get_unsigned16(
            fixrec, fields->sourceTransportPort, &u16);
        rwRecSetSPort(&fwd_rec, u16);
    }

    /* dport */
    if (fields->destinationTransportPort) {
        sk_fixrec_get_unsigned16(
            fixrec, fields->destinationTransportPort, &u16);
        rwRecSetDPort(&fwd_rec, u16);
    }

    /* icmp type/code */
    if (rwRecIsICMP(&fwd_rec)) {
        if (rwRecIsIPv6(&fwd_rec)) {
            if (fields->icmpTypeCodeIPv6) {
                sk_fixrec_get_unsigned16(
                    fixrec, fields->icmpTypeCodeIPv6, &u16);
                rwRecSetIcmpTypeAndCode(&fwd_rec, u16);
            } else {
                if (fields->icmpTypeIPv6) {
                    sk_fixrec_get_unsigned8(
                        fixrec, fields->icmpTypeIPv6, &u8);
                    rwRecSetIcmpType(&fwd_rec, u8);
                }
                if (fields->icmpCodeIPv6) {
                    sk_fixrec_get_unsigned8(
                        fixrec, fields->icmpCodeIPv6, &u8);
                    rwRecSetIcmpCode(&fwd_rec, u8);
                }
            }
        } else {
            if (fields->icmpTypeCodeIPv4) {
                sk_fixrec_get_unsigned16(fixrec, fields->icmpTypeCodeIPv4,
                                         &u16);
                rwRecSetIcmpTypeAndCode(&fwd_rec, u16);
            } else {
                if (fields->icmpTypeIPv4) {
                    sk_fixrec_get_unsigned8(
                        fixrec, fields->icmpTypeIPv4, &u8);
                    rwRecSetIcmpType(&fwd_rec, u8);
                }
                if (fields->icmpCodeIPv4) {
                    sk_fixrec_get_unsigned8(
                        fixrec, fields->icmpCodeIPv4, &u8);
                    rwRecSetIcmpCode(&fwd_rec, u8);
                }
            }
        }
    }

    /* input */
    if (fields->ingressInterface) {
        sk_fixrec_get_unsigned32(fixrec, fields->ingressInterface, &u32);
        rwRecSetInput(&fwd_rec, u32);
    }

    /* output */
    if (fields->egressInterface) {
        sk_fixrec_get_unsigned32(fixrec, fields->egressInterface, &u32);
        rwRecSetOutput(&fwd_rec, u32);
    }

    /* packets */
    u64 = ipfixFirstFieldUint64(
        NULL, fixrec, &fields->packetDeltaCount, &fields->packetTotalCount,
        &fields->initiatorPackets, NULL);
    rwRecSetPkts(&fwd_rec, u64);

    /* bytes */
    u64 = ipfixFirstFieldUint64(
        NULL, fixrec, &fields->octetDeltaCount, &fields->octetTotalCount,
        &fields->initiatorOctets, NULL);
    rwRecSetBytes(&fwd_rec, u64);

    /* stime */
    if (fields->flowStartMilliseconds) {
        sk_fixrec_get_datetime(fixrec, fields->flowStartMilliseconds, &stime);
        rwRecSetStartTime(&fwd_rec, stime);
    }

    /* etime */
    if (fields->flowEndMilliseconds) {
        sk_fixrec_get_datetime(fixrec, fields->flowEndMilliseconds, &t);
        rwRecSetElapsed(&fwd_rec, t - stime);
    }

    /* flowtype */
    if (fields->silkFlowType) {
        sk_fixrec_get_unsigned8(fixrec, fields->silkFlowType, &u8);
        rwRecSetFlowType(&fwd_rec, u8);
    }

    /* sensor */
    if (fields->silkFlowSensor) {
        sk_fixrec_get_unsigned16(fixrec, fields->silkFlowSensor, &u16);
        rwRecSetSensor(&fwd_rec, u16);
    }

    /* application */
    if (fields->silkAppLabel) {
        sk_fixrec_get_unsigned16(fixrec, fields->silkAppLabel, &u16);
        rwRecSetApplication(&fwd_rec, u16);
    }

    /* tcpstate */
    tcp_state = 0;
    if (fields->silkTCPState) {
        sk_fixrec_get_unsigned8(fixrec, fields->silkTCPState, &tcp_state);
    }

    /* flags */
    tcp_flags = init_flags = rest_flags = 0;
    if (fields->initialTCPFlags) {
        sk_fixrec_get_unsigned8(fixrec, fields->initialTCPFlags, &init_flags);
        tcp_flags |= init_flags;
    }
    if (fields->unionTCPFlags) {
        sk_fixrec_get_unsigned8(fixrec, fields->unionTCPFlags, &rest_flags);
        tcp_flags |= rest_flags;
    }
    if (tcp_flags && IPPROTO_TCP == rwRecGetProto(&fwd_rec)) {
        rwRecSetFlags(&fwd_rec, tcp_flags);
        rwRecSetInitFlags(&fwd_rec, init_flags);
        rwRecSetRestFlags(&fwd_rec, rest_flags);
        tcp_state |= SK_TCPSTATE_EXPANDED;
    } else {
        if (fields->tcpControlBits) {
            sk_fixrec_get_unsigned8(fixrec, fields->tcpControlBits, &u8);
            rwRecSetFlags(&fwd_rec, u8);
        }
        tcp_state &= ~SK_TCPSTATE_EXPANDED;
    }

    /* Process the flowEndReason and flowAttributes unless one of
     * those bits is already set (via silkTCPState). */
    if (!(tcp_state
          & (SK_TCPSTATE_FIN_FOLLOWED_NOT_ACK | SK_TCPSTATE_TIMEOUT_KILLED
             | SK_TCPSTATE_TIMEOUT_STARTED
             | SK_TCPSTATE_UNIFORM_PACKET_SIZE)))
    {
        if (fields->flowEndReason) {
            sk_fixrec_get_unsigned8(fixrec, fields->flowEndReason, &u8);
            if ((u8 & SKI_END_MASK) == SKI_END_ACTIVE) {
                tcp_state |= SK_TCPSTATE_TIMEOUT_KILLED;
            }
            if (u8 & SKI_END_ISCONT) {
                tcp_state |= SK_TCPSTATE_TIMEOUT_STARTED;
            }
        }
        if (fields->flowAttributes) {
            sk_fixrec_get_unsigned16(fixrec, fields->flowAttributes, &u16);
            if (u16 & SKI_FLOW_ATTRIBUTE_UNIFORM_PACKET_SIZE) {
                tcp_state |= SK_TCPSTATE_UNIFORM_PACKET_SIZE;
            }
        }
    }

    rwRecSetTcpState(&fwd_rec, tcp_state);

    if (!(fields->reverseOctetTotalCount
          || fields->reversePacketDeltaCount
          || fields->reverseTcpControlBits
          || fields->reverseOctetTotalCount
          || fields->reversePacketTotalCount
          || fields->responderOctets
          || fields->responderPackets
          || fields->reverseInitialTCPFlags
          || fields->reverseUnionTCPFlags
          || fields->reverseFlowDeltaMilliseconds))
    {
        /* No reverse record */
        /* Pack the forward record */
        probe->incoming_rec = (void *)fixrec;
        return skpcProbePackRecord(probe, &fwd_rec, NULL);
    }

    /* Output a reverse record */
    memcpy(&rev_rec, &fwd_rec, sizeof(rwRec));

    /* Swap IP addresses */
    rwRecMemSetSIP(&rev_rec, &dest);
    rwRecMemSetDIP(&rev_rec, &source);

    /* Swap Ports */
    u16 = rwRecGetDPort(&fwd_rec);
    if (!rwRecIsICMP(&fwd_rec) || u16 == 0) {
        /* Swap ports if not ICMP or dPort is 0 */
        rwRecSetDPort(&rev_rec, rwRecGetSPort(&fwd_rec));
        rwRecSetSPort(&rev_rec, u16);
    } else {
        /* ICMP.  Set sPort to 0 */
        rwRecSetSPort(&rev_rec, 0);
    }

    /* Swap interfaces */
    rwRecSetInput(&rev_rec, rwRecGetOutput(&fwd_rec));
    rwRecSetOutput(&rev_rec, rwRecGetInput(&fwd_rec));

    /* packets */
    u64 = ipfixFirstFieldUint64(
        NULL, fixrec, &fields->reversePacketDeltaCount,
        &fields->reversePacketTotalCount,
        &fields->responderPackets, NULL);
    rwRecSetPkts(&rev_rec, u64);

    /* bytes */
    u64 = ipfixFirstFieldUint64(
        NULL, fixrec, &fields->reverseOctetDeltaCount,
        &fields->reverseOctetTotalCount,
        &fields->responderOctets, NULL);
    rwRecSetBytes(&rev_rec, u64);

    /* times */
    if (fields->reverseFlowDeltaMilliseconds) {
        sk_fixrec_get_unsigned32(
            fixrec, fields->reverseFlowDeltaMilliseconds, &u32);
    } else {
        u32 = 0;
    }
    rwRecSetStartTime(&rev_rec, stime + u32);
    rwRecSetElapsed(&rev_rec, rwRecGetElapsed(&fwd_rec) - u32);

    /* flags */
    init_flags = rest_flags = 0;
    if (fields->reverseInitialTCPFlags) {
        sk_fixrec_get_unsigned8(
            fixrec, fields->reverseInitialTCPFlags, &init_flags);
    }
    if (fields->reverseUnionTCPFlags) {
        sk_fixrec_get_unsigned8(
            fixrec, fields->reverseUnionTCPFlags, &rest_flags);
    }
    tcp_flags = init_flags | rest_flags;
    if (tcp_flags && IPPROTO_TCP == rwRecGetProto(&fwd_rec)) {
        rwRecSetFlags(&rev_rec, tcp_flags);
        rwRecSetInitFlags(&rev_rec, init_flags);
        rwRecSetRestFlags(&rev_rec, rest_flags);
        tcp_state |= SK_TCPSTATE_EXPANDED;
    } else if (fields->reverseTcpControlBits) {
        sk_fixrec_get_unsigned8(fixrec, fields->reverseTcpControlBits, &u8);
        rwRecSetFlags(&rev_rec, u8);
        rwRecSetInitFlags(&rev_rec, 0);
        rwRecSetRestFlags(&rev_rec, 0);
        tcp_state &= ~SK_TCPSTATE_EXPANDED;
    }

    if (fields->reverseFlowAttributes) {
        sk_fixrec_get_unsigned16(fixrec, fields->reverseFlowAttributes, &u16);
        if (u16 & SKI_FLOW_ATTRIBUTE_UNIFORM_PACKET_SIZE) {
            tcp_state |= SK_TCPSTATE_UNIFORM_PACKET_SIZE;
        } else {
            tcp_state &= ~SK_TCPSTATE_UNIFORM_PACKET_SIZE;
        }
    }

    rwRecSetTcpState(&rev_rec, tcp_state);

    /* Pack the records */
    probe->incoming_rec = (void *)fixrec;
    return skpcProbePackRecord(probe, &fwd_rec, &rev_rec);
}


static void
ipfix_stream_new_schema_callback(
    sk_schema_t        *schema,
    uint16_t            tid,
    void               *cb_data)
{
    ipfix_to_schema_fields_t *fields;
    const sk_field_t **field_array;
    sk_field_ident_t *ident;

    (void)tid;
    (void)cb_data;

    /* set the context used to convert an IPFIX record to an rwRec */
    fields = sk_alloc(ipfix_to_schema_fields_t);
    field_array = (const sk_field_t **)fields;
    for (ident = ipfix_to_schema_fieldlist;
         *ident != FIELDLIST_SENTINEL;
         ++ident)
    {
        *field_array = sk_schema_get_field_by_ident(schema, *ident, NULL);
        ++field_array;
    }
    sk_schema_set_context(schema, packrec_ctx_ident, (void *)fields, free);
}


int
sk_conv_ipfix_stream(
    skpc_probe_t       *probe,
    skstream_t         *stream)
{
    const sk_fixrec_t *fixrec;
    sk_fixstream_t *fixstream;
    int move_to_error_dir;
    ssize_t rv;

    TRACE_ENTRY;

    if ((rv = sk_fixstream_create(&fixstream))
        || (rv = sk_fixstream_set_stream(fixstream, stream))
        || (rv = sk_fixstream_set_schema_cb(
                fixstream, ipfix_stream_new_schema_callback, NULL))
        || (rv = sk_fixstream_open(fixstream)))
    {
        WARNINGMSG("%s", sk_fixstream_strerror(fixstream));
        sk_fixstream_destroy(&fixstream);
        TRACE_RETURN(1);
    }

    move_to_error_dir = 0;
    while ((rv = sk_fixstream_read_record(fixstream, &fixrec))
           == SKSTREAM_OK)
    {
        rv = fixrec_pack_record(probe, fixrec);
        if (-1 == rv) {
            move_to_error_dir = 1;
            rv = SKSTREAM_ERR_EOF;
            break;
        }
    }
    if (SKSTREAM_ERR_EOF != rv) {
        move_to_error_dir = 1;
        NOTICEMSG("%s", sk_fixstream_strerror(fixstream));
    }

    INFOMSG("'%s': Processed file '%s'",
            skpcProbeGetName(probe), skStreamGetPathname(stream));

    TRACE_RETURN(move_to_error_dir);
}


#if 0
/*
 *    This code allows us to treat UDP streams from the same host but
 *    arriving from different source-ports as a single stream.
 *
 *    The code treats UDP streams from the same address but on
 *    different ports as different streams unless the environment
 *    variable named by SK_IPFIX_UDP_IGNORE_SOURCE_PORT is set to a
 *    non-zero value.
 */
static void
ipfix_ignore_source_port(
    void)
{
    const char *env;

    env = getenv(SK_IPFIX_UDP_IGNORE_SOURCE_PORT);
    if (NULL != env && *env && *env != '0') {
        consider_sport = 0;
    }
    consider_sport = 1;
}
#endif


/*
 *     The fixbufConnect() function is passed to fbListenerAlloc() as
 *     its 'appinit' callback (fbListenerAppInit_fn) for TCP sources
 *     and UDP sources if libfixbuf supports multi-UDP (v1.2.0 or later).
 *     This function is called from within the fbListenerWait() call
 *     when a new connection to the listening socket is made.  (In
 *     addition, for UDP sources, it is called directly by
 *     fbListenerAlloc() with a NULL peer.)
 *
 *     Its primary purposes are to accept/reject the connection,
 *     create an skIPFIXConnection_t, and set the the collector's
 *     context to the skIPFIXConnection_t.  The skIPFIXConnection_t
 *     remembers the peer information, contains the stats for this
 *     connection, and references the source object.
 */
static gboolean
fixbufConnect(
    fbListener_t           *listener,
    void                  **out_coll_ctx,
    int              UNUSED(fd),
    struct sockaddr        *peer,
    size_t                  peerlen,
    GError                **err)
{
    fbCollector_t *collector;
    char addr_buf[2 * SK_NUM2DOT_STRLEN];
    ipfix_net_base_t target_base;
    ipfix_net_base_t *base;
    const ipfix_peer2probe_t *found_peer;
    ipfix_peer2probe_t target_peer;
    skpc_probe_t *probe;
    sk_conv_ipfix_t *source;
    skIPFIXConnection_t *conn = NULL;
    sk_sockaddr_t addr;
    gboolean retval = 0;

    TRACE_ENTRY;

    if (peer == NULL) {
        /* This function is being called for a UDP listener at init
         * time.  Ignore this. */
        TRACE_RETURN(1);
    }
    if (peerlen > sizeof(addr)) {
        TRACEMSG(1, (("ipfixsource rejected connection:"
                      " peerlen too large: %" SK_PRIuZ " > %" SK_PRIuZ),
                     peerlen, sizeof(addr)));
        g_set_error(err, SK_IPFIXSOURCE_DOMAIN, SK_IPFIX_ERROR_CONN,
                    ("peerlen unexpectedly large: %" SK_PRIuZ), peerlen);
        TRACE_RETURN(0);
    }

    memcpy(&addr.sa, peer, peerlen);
    skSockaddrString(addr_buf, sizeof(addr_buf), &addr);

    TRACEMSG(3, (("ipfixsource processing connection from '%s'"), addr_buf));

    /* Find the ipfix_net_base_t object associated with this
     * listener */
    target_base.listener = listener;
    base = ((ipfix_net_base_t *)rbfind(&target_base, listener_to_base.map));
    if (base == NULL) {
        TRACEMSG(1, (("ipfixsource rejected connection from '%s':"
                      " unable to find base given listener"), addr_buf));
        g_set_error(err, SK_IPFIXSOURCE_DOMAIN, SK_IPFIX_ERROR_CONN,
                    "Unable to find base for listener");
        TRACE_RETURN(0);
    }

    if (base->any) {
        /* When there is no accept-from address on the probe, there is
         * a one-to-one mapping between source and base, and all
         * connections are permitted. */
        probe = base->any;
    } else {
        /* Using the address of the incoming connection, search for
         * the source object associated with this address. */
        assert(base->peer2probe);
        target_peer.addr = &addr;
        found_peer = ((const ipfix_peer2probe_t*)
                      rbfind(&target_peer, base->peer2probe));
        if (NULL == found_peer) {
            /* Reject hosts that do not appear in accept-from-host */
            TRACEMSG(1, (("ipfixsource rejected connection from '%s':"
                          " host prohibited"), addr_buf));
            g_set_error(err, SK_IPFIXSOURCE_DOMAIN, SK_IPFIX_ERROR_CONN,
                        "Connection prohibited from %s", addr_buf);
            goto END;
        }
        probe = found_peer->probe;
    }

    source = (sk_conv_ipfix_t *)probe->converter;
    conn = sk_alloc(skIPFIXConnection_t);

    /* If this is an NetFlowV9/sFLow probe, store the
     * skIPFIXConnection_t in the red-black tree on the source so we
     * can log about missing NetFlowV9/sFlow packets. */
    if (source->connections) {
        skIPFIXConnection_t *found_conn;

        pthread_mutex_lock(&source->stats_mutex);
        found_conn = ((skIPFIXConnection_t*)
                      rbsearch(conn, source->connections));
        pthread_mutex_unlock(&source->stats_mutex);
        if (found_conn != conn) {
            TRACEMSG(1, (("ipfixsource rejected connection from '%s':"
                          " unable to store connection on source"), addr_buf));
            g_set_error(err, SK_IPFIXSOURCE_DOMAIN, SK_IPFIX_ERROR_CONN,
                        "Unable to store connection on source");
            free(conn);
            goto END;
        }
    }

    /* Update the skIPFIXConnection_t with the information necessary
     * to provide a useful log message at disconnect.  This info is
     * also used to get NetFlowV9/sFlow missed packets. */
    if (peerlen <= sizeof(conn->peer_addr)) {
        memcpy(&conn->peer_addr.sa, peer, peerlen);
        conn->peer_len = peerlen;
    }

    TRACEMSG(4, ("Creating new conn = %p for probe = %p, source = %p",
                 conn, probe, source));

    /* Set the skIPFIXConnection_t to point to the source, increment
     * the source's connection_count, and set the context pointer to
     * the connection.  */
    conn->probe = probe;
    ++source->connection_count;
    retval = 1;
    *out_coll_ctx = conn;

    /* Get the domain (also needed for NetFlowV9/sFlow missed pkts).
     * In the TCP case, the collector does not exist yet, and the
     * following call returns false. */
    if (fbListenerGetCollector(listener, &collector, NULL)) {
        conn->ob_domain = fbCollectorGetObservationDomain(collector);
        INFOMSG("'%s': accepted connection from %s, domain 0x%04x",
                skpcProbeGetName(probe), addr_buf, conn->ob_domain);
    } else {
        INFOMSG("'%s': accepted connection from %s",
                skpcProbeGetName(probe), addr_buf);
    }

  END:
    pthread_mutex_unlock(&base->t.mutex);
    TRACE_RETURN(retval);
}


/*
 *     The fixbufDisconnect() function is passed to fbListenerAlloc()
 *     as its 'appfree' callback (fbListenerAppFree_fn).  This
 *     function is called by fBufFree().  The argument to this
 *     function is the context (the skIPFIXConnection_t) that was set
 *     by fixbufConnect().
 *
 *     The function decrefs the source and frees it if the
 *     connection_count hits zero and the source has been asked to be
 *     destroyed.  It then frees the connection object.
 */
static void
fixbufDisconnect(
    void               *ctx)
{
    skIPFIXConnection_t *conn = (skIPFIXConnection_t *)ctx;
    sk_conv_ipfix_t *source;

    TRACE_ENTRY;

    if (conn == NULL) {
        TRACE_RETURN;
    }
    source = (sk_conv_ipfix_t *)conn->probe->converter;

    TRACEMSG(3, (("fixbufDisconnection connection_count = %" PRIu32),
                 source->connection_count));

    /* Remove the connection from the source. */
    --source->connection_count;
    if (source->connections) {
        pthread_mutex_lock(&source->stats_mutex);
        rbdelete(conn, source->connections);
        pthread_mutex_unlock(&source->stats_mutex);
    }

    /* For older fixbuf, only TCP connections contain the peer addr */
    if (conn->peer_len) {
        char addr_buf[2 * SK_NUM2DOT_STRLEN];

        skSockaddrString(addr_buf, sizeof(addr_buf), &conn->peer_addr);
        if (conn->ob_domain) {
            INFOMSG("'%s': noticed disconnect by %s, domain 0x%04x",
                    skpcProbeGetName(conn->probe), addr_buf, conn->ob_domain);
        } else {
            INFOMSG("'%s': noticed disconnect by %s",
                    skpcProbeGetName(conn->probe), addr_buf);
        }
    }

    TRACEMSG(4, ("Destroying conn = %p for probe = %p, source = %p",
                 conn, conn->probe, source));

    free(conn);
    TRACE_RETURN;
}


/*
 *    THREAD ENTRY POINT
 *
 *    The ipfix_net_base_reader() function is the main thread for
 *    listening to
 *    data from a single fbListener_t object.  It is passed the
 *    ipfix_net_base_t object containing that fbListener_t object.
 *    This thread is started from the ipfixSourceCreateFromSockaddr()
 *    function.
 */
static void *
ipfix_net_base_reader(
    void               *v_base)
{
    ipfix_net_base_t *base = (ipfix_net_base_t *)v_base;
    skIPFIXConnection_t *conn = NULL;
    sk_conv_ipfix_t *source;
    skpc_probe_t *probe = NULL;
    GError *err = NULL;
    fBuf_t *fbuf = NULL;
    tmpl_to_schema_ctx_t *tmpl_ctx;
    fbTemplate_t *tmpl;
    size_t len;
    uint16_t tid;
    int rv;

    TRACE_ENTRY;

    assert(base != NULL);

    /* Communicate that the thread has started */
    pthread_mutex_lock(&base->t.mutex);
    if (STARTING != base->t.status) {
        goto END;
    }
    base->t.status = STARTED;
    pthread_cond_signal(&base->t.cond);
    pthread_mutex_unlock(&base->t.mutex);

    DEBUGMSG("fixbuf listener started for %s", base->name);

    TRACEMSG(3, ("base %p started for %s", base, base->name));

    /* Main loop */
    for (;;) {
        /* to be pedantic, we should lock the mutex while checking the
         * value; however, that is probably not needed here since any
         * partial value still indicates we want to exit the loop */
        if (STARTED != base->t.status) {
            break;
        }

        /* wait for a new connection; this while() is not a loop since
         * there is a "break;" just before the closing brace */
        conn = NULL;
        while ((fbuf = fbListenerWait(base->listener, &err))) {
            /* Make sure the fbuf is in manual mode.  Manual mode is
             * required to multiplex among multiple collectors using
             * fbListenerWait().  Without this, fBufNext() blocks once
             * the buffer is empty until it has messages again.
             * Instead, we want to switch to a different fbuf once we
             * read all records in the current buffer. */
            fBufSetAutomaticMode(fbuf, 0);

            /* get the first template; for a UDP message, this is
             * where the appInit callback is invoked */
            tmpl = fBufNextCollectionTemplate(fbuf, &tid, &err);
            if (!tmpl) {
                break;
            }

            /* Get the connection data associated with the
             * fbCollector_t for this fBuf_t object */
            conn = ((skIPFIXConnection_t *)
                    fbCollectorGetContext(fBufGetCollector(fbuf)));
            if (conn == NULL) {
                /* If conn is NULL, we must have rejected a UDP
                 * connection from the appInit function. */
                TRACEMSG(2, ("<UNKNOWN>: SKI_RECTYPE_ERROR"));
                break;
            }
            probe = conn->probe;
            assert(probe);
            source = (sk_conv_ipfix_t *)probe->converter;
            assert(source);

            TRACEMSG(5, ("'%s': conn = %p, probe = %p, source = %p, fbuf = %p",
                         skpcProbeGetName(probe), conn, probe, source, fbuf));

            /* NOTE: While processing a message, we do not check
             * whether the base has been told to stop processing
             * records.  Perhaps we should. */
            do {
                tmpl_ctx = (tmpl_to_schema_ctx_t*)fbTemplateGetContext(tmpl);
                if (NULL == tmpl_ctx) {
                    skAbort();
                }
                sk_fixrec_clear(&tmpl_ctx->rec);
                if (tmpl_ctx->rec.schema != source->prev_schema) {
                    if (!fBufSetInternalTemplate(fbuf, tmpl_ctx->tid, &err)) {
                        break;
                    }
                    source->prev_schema = tmpl_ctx->rec.schema;
                }
                len = tmpl_ctx->len;
                if (!fBufNext(fbuf, tmpl_ctx->rec.data, &len, &err)) {
                    break;
                }
                tmpl_ctx->rec.flags |= SK_FIXREC_FIXBUF_VARDATA;

                /*
                 *  FIXME: What to do now?  Do we save this record into a
                 *  circular buffer, or do we call the pack function to
                 *  pack the record?
                 *
                 *  If we save into the circular buffer, we need to worry
                 *  about copying the record and any list/varlen fields it
                 *  contains.  If we call the pack function, all sources
                 *  that listen on this source's port are blocked until
                 *  the pack function returns.
                 *
                 *  Ideally, I wish we could spawn a short-lived thread to
                 *  handle packing of this single record, but that is not
                 *  possible, and there would still be the question of
                 *  ownership of the record and its data...
                 *
                 *  Given the way fixbuf works, if the record has any
                 *  list/varlen data we MUST either copy the record of
                 *  compete our processing of it before moving the fbuf to
                 *  the next template.
                 */

                /* For now, call the packer */
                rv = fixrec_pack_record(probe, &tmpl_ctx->rec);
                if (-1 == rv) {
                    /* FIXME; currently error is ignored. */
                }
            } while ((tmpl = fBufNextCollectionTemplate(fbuf, &tid, &err)));

            break;
        }
        /* we have an error.  maybe an end-of-message after processing
         * a record or an error from the fbListenerWait() call */

        /* Handle FB_ERROR_NLREAD and FB_ERROR_EOM returned by
         * fBufNext() in the same way as when they are returned by
         * fbListenerWait().
         *
         * FB_ERROR_NLREAD is also returned when a previously rejected
         * UDP client attempts to send more data. */
        if (g_error_matches(err, FB_ERROR_DOMAIN, FB_ERROR_NLREAD)
            || g_error_matches(err, FB_ERROR_DOMAIN, FB_ERROR_EOM))
        {
            TRACEMSG(1, ("'%s': Ignoring %s: %s",
                         (conn ? skpcProbeGetName(probe) : "<UNKNOWN>"),
                         ((FB_ERROR_EOM == err->code)
                          ? "end-of-message" : "interrupted read"),
                         err->message));
            /* Do not free the fbuf here.  The fbuf is owned by the
             * listener, and will be freed when the listener is freed.
             * Calling fBufFree() here would cause fixbuf to forget
             * the current template, which would cause it to ignore
             * records until a new template is transmitted. */
            g_clear_error(&err);
            continue;
        }

        /* SK_IPFIX_ERROR_CONN indicates that a new UDP "connection"
         * was rejected by the appInit function in a multi-UDP
         * libfixbuf session.  Do not free the fbuf since we do not
         * have a connection yet; wait for another connection. */
        if (g_error_matches(err, SK_IPFIXSOURCE_DOMAIN, SK_IPFIX_ERROR_CONN)) {
            INFOMSG("Closing connection: %s", err->message);
            g_clear_error(&err);
            continue;
        }
#if 0
        if (g_error_matches(err,SK_IPFIXSOURCE_DOMAIN,SK_IPFIX_ERROR_CONN))
        {
            /* the callback rejected the connection (TCP only) */
            DEBUGMSG("fixbuf listener rejected connection: %s",
                     err->message);
            g_clear_error(&err);
            continue;
        }
#endif  /* 0 */

        /* The remainder of the code in this while() block assumes
         * that 'source' is valid, which is only true if 'conn' is
         * non-NULL.  Trap that here, just in case. */
        if (NULL == conn) {
            if (NULL == err) {
                /* give up when error code is unknown */
                NOTICEMSG("'<UNKNOWN>': fixbuf listener shutting down:"
                          " unknown error from fBufNext");
                break;
            }
            DEBUGMSG("Ignoring packet: %s (d=%" PRIu32 ",c=%" PRId32 ")",
                     err->message, (uint32_t)err->domain, (int32_t)err->code);
            g_clear_error(&err);
            continue;
        }

        /* FB_ERROR_NETFLOWV9 indicates an anomalous netflow v9
         * record; these do not disturb fixbuf state, and so should be
         * ignored. */
        if (g_error_matches(err, FB_ERROR_DOMAIN, FB_ERROR_NETFLOWV9)) {
            DEBUGMSG("'%s': Ignoring NetFlowV9 record: %s",
                     skpcProbeGetName(probe), err->message);
            g_clear_error(&err);
            continue;
        }

        /* FB_ERROR_SFLOW indicates an anomalous sFlow
         * record; these do not disturb fixbuf state, and so should be
         * ignored. */
        if (g_error_matches(err, FB_ERROR_DOMAIN, FB_ERROR_SFLOW)) {
            DEBUGMSG("'%s': Ignoring sFlow record: %s",
                     skpcProbeGetName(probe), err->message);
            g_clear_error(&err);
            continue;
        }

        /* FB_ERROR_TMPL indicates a set references a template ID for
         * which there is no template.  Log and continue. */
        if (g_error_matches(err, FB_ERROR_DOMAIN, FB_ERROR_TMPL)) {
            DEBUGMSG("'%s': Ignoring data set: %s",
                     skpcProbeGetName(probe), err->message);
            g_clear_error(&err);
            continue;
        }

        /* FB_ERROR_IPFIX indicates invalid IPFIX.  We could simply
         * choose to log and continue; instead we choose to log, close
         * the connection, and continue. */
        if (g_error_matches(err, FB_ERROR_DOMAIN, FB_ERROR_IPFIX)) {
            if (base->is_udp) {
                DEBUGMSG("'%s': Ignoring invalid IPFIX: %s",
                         skpcProbeGetName(probe), err->message);
            } else {
                INFOMSG("'%s': Closing connection; received invalid IPFIX: %s",
                        skpcProbeGetName(probe), err->message);
                fBufFree(fbuf);
                fbuf = NULL;
            }
            g_clear_error(&err);
            continue;
        }

        /* FB_ERROR_EOF indicates that the connection associated with
         * this fBuf_t object has finished.  In this case, free the
         * fBuf_t object to close the connection.  Do not free the
         * fBuf_t for UDP connections, since these UDP-based fBuf_t
         * objects are freed with the listener. */
        if (g_error_matches(err, FB_ERROR_DOMAIN, FB_ERROR_EOF)) {
            if (!base->is_udp) {
                INFOMSG("'%s': Closing connection: %s",
                        skpcProbeGetName(probe), err->message);
                fBufFree(fbuf);
                fbuf = NULL;
            }
            g_clear_error(&err);
            continue;
        }

        /* Handle an unexpected error generated by fixbuf */
        if (err && err->domain == FB_ERROR_DOMAIN) {
            if (base->is_udp) {
                DEBUGMSG(("'%s': Ignoring UDP packet: %s"
                          " (d=%" PRIu32 ",c=%" PRId32 ")"),
                         skpcProbeGetName(probe), err->message,
                         (uint32_t)err->domain, (int32_t)err->code);
            } else {
                INFOMSG(("'%s': Closing connection: %s"
                         " (d=%" PRIu32 ",c=%" PRId32 ")"),
                        skpcProbeGetName(probe), err->message,
                        (uint32_t)err->domain, (int32_t)err->code);
                fBufFree(fbuf);
                fbuf = NULL;
            }
            g_clear_error(&err);
            continue;
        }

        /* In the event of an unhandled error, end the thread. */
        if (NULL == err) {
            NOTICEMSG(("'%s': fixbuf listener shutting down:"
                       " unknown error from fBufNext"),
                      probe ? skpcProbeGetName(probe) : "<UNKNOWN>");
        } else {
            NOTICEMSG(("'%s': fixbuf listener shutting down: %s"
                       " (d=%" PRIu32 ",c=%" PRId32 ")"),
                      skpcProbeGetName(probe), err->message,
                      (uint32_t)err->domain, (int32_t)err->code);
            g_clear_error(&err);
        }
        break;
    }

    TRACEMSG(3, ("base %p exited while() loop", base));

    /* Free the fbuf if it exists.  (If it's UDP, it will be freed by
     * the destruction of the listener below.) */
    if (fbuf && !base->is_udp) {
        TRACEMSG(3, ("base %p calling fBufFree", base));
        fBufFree(fbuf);
    }

    pthread_mutex_lock(&base->t.mutex);

  END:
    base->t.status = STOPPED;
    pthread_cond_broadcast(&base->t.cond);

    /* Destroy the fbListener_t object.  This destroys the fbuf if the
     * stream is UDP. */
    TRACEMSG(3, ("base %p calling fbListenerFree", base));
    fbListenerFree(base->listener);
    base->listener = NULL;
    pthread_mutex_unlock(&base->t.mutex);

    /* Notify skIPFIXSourceDestroy() that the thread is ending */
    DEBUGMSG("fixbuf listener ended for %s.", base->name);

    decrement_thread_count(1);

    TRACE_RETURN(NULL);
}


static void
ipfix_net_base_stop(
    ipfix_net_base_t   *base)
{
    ASSERT_MUTEX_LOCKED(&base->t.mutex);

    TRACEMSG(3, ("base %p status is %d", base, base->t.status));

    switch (base->t.status) {
      case UNKNONWN:
        skAbortBadCase(base->t.status);
      case CREATED:
        base->t.status = JOINED;
        return;
      case JOINED:
      case STOPPED:
        return;
      case STARTING:
      case STARTED:
        base->t.status = STOPPING;
        assert(base->listener);
        TRACEMSG(3, ("base %p calling fbListenerInterrupt", base));
        /* Unblock the fbListenerWait() call */
        fbListenerInterrupt(base->listener);
        /* FALLTHROUGH */
      case STOPPING:
        while (STOPPED != base->t.status) {
            TRACEMSG(3, ("base %p waiting for status to be STOPPED", base));
            pthread_cond_wait(&base->t.cond, &base->t.mutex);
        }
        break;
    }
}

static void
ipfix_net_base_destroy(
    ipfix_net_base_t   *base)
{
    TRACE_ENTRY;

    if (NULL == base) {
        TRACE_RETURN;
    }

    pthread_mutex_lock(&base->t.mutex);
    assert(base->refcount == 0);

    TRACEMSG(3, ("base %p source_count is %u", base, base->refcount));

    ipfix_net_base_stop(base);
    if (JOINED != base->t.status) {
        /* Reap thread */
        TRACEMSG(3, ("base %p joining its thread", base));
        pthread_join(base->t.thread, NULL);
    }

    if (base->listener) {
        fbListenerFree(base->listener);
    }

    /* Free peer2probe tree */
    if (base->peer2probe) {
        ipfix_peer2probe_t *addr;
        RBLIST *iter;

        iter = rbopenlist(base->peer2probe);
        if (iter != NULL) {
            while ((addr = (ipfix_peer2probe_t *)rbreadlist(iter)) != NULL) {
                free(addr);
            }
            rbcloselist(iter);
        }
        rbdestroy(base->peer2probe);
        base->peer2probe = NULL;
    }

    listener_to_base_remove(base);

    pthread_cond_destroy(&base->t.cond);
    pthread_mutex_unlock(&base->t.mutex);
    pthread_mutex_destroy(&base->t.mutex);

    TRACEMSG(3, ("base %p is free", base));
    free(base);

    TRACE_RETURN;
}



/**
 *    Create a base object, open and bind its sockets, but do not
 *    start its thread.
 *
 *    The probe parameter is here to provide information needed while
 *    creating the base, such as the protocol and the type of data to
 *    be collected.
 */
static ipfix_net_base_t *
ipfix_net_base_create(
    const sk_sockaddr_array_t  *listen_address,
    const skpc_probe_t         *probe)
{
    fbConnSpec_t connspec;
    ipfix_net_base_t *base;
    char port_string[7];
    GError *err;

    TRACE_ENTRY;

    assert(listen_address);
    assert(probe);

    /* create the base object */
    base = sk_alloc(ipfix_net_base_t);

    pthread_mutex_init(&base->t.mutex, NULL);
    pthread_cond_init(&base->t.cond, NULL);
    base->t.thread = pthread_self();
    base->t.status = CREATED;

    base->name = skSockaddrArrayGetHostPortPair(listen_address);
    base->listen_address = listen_address;
    base->protocol = skpcProbeGetProtocol(probe);
    base->is_udp = (base->protocol == SKPC_PROTO_UDP);

    /* Fill in the connspec in order to create a listener */
    memset(&connspec, 0, sizeof(connspec));
    SILK_PROTO_TO_FIXBUF_TRANSPORT(base->protocol, &connspec.transport);
    connspec.host = (char*)skSockaddrArrayGetHostname(listen_address);
    if (sk_sockaddr_array_anyhostname == connspec.host) {
        connspec.host = NULL;
    }
    snprintf(port_string, sizeof(port_string), "%d",
             skSockaddrGetPort(skSockaddrArrayGet(listen_address, 0)));
    connspec.svc = port_string;
    DEBUGMSG("connspec: %s:%s/%d",
             connspec.host ? connspec.host : "NULL", connspec.svc,
             connspec.transport);

    err = NULL;
    base->listener = skiCreateListener(
        &connspec, fixbufConnect, fixbufDisconnect, base, &err);
    if (base->listener == NULL) {
        pthread_mutex_unlock(&create_listener_mutex);
        goto ERROR;
    }

    if (base->is_udp) {
        fbCollector_t *collector;

        if (!fbListenerGetCollector(base->listener, &collector, &err)) {
            goto ERROR;
        }
        /* Enable the multi-UDP support in libfixbuf. */
        fbCollectorSetUDPMultiSession(collector, 1);
        fbCollectorManageUDPStreamByPort(collector, consider_sport);

        /* If this is a Netflow v9 source or an sFlow source, tell
         * the collector. */
        switch (skpcProbeGetType(probe)) {
          case PROBE_ENUM_IPFIX:
            break;
          case PROBE_ENUM_NETFLOW_V9:
            if (!fbCollectorSetNetflowV9Translator(collector, &err)) {
                goto ERROR;
            }
            break;
          case PROBE_ENUM_SFLOW:
            if (!fbCollectorSetSFlowTranslator(collector, &err)) {
                goto ERROR;
            }
            break;
          default:
            skAbortBadCase(skpcProbeGetType(probe));
        }
    }

    /* create a mapping between the listener to the base.  the mapping
     * is used by the fixbufConnect() callback to get the probe */
    if (listener_to_base_insert(base)) {
        goto ERROR;
    }

    TRACE_RETURN(base);

  ERROR:
    if (err) {
        ERRMSG("%s", err->message);
        g_clear_error(&err);
    }
    ipfix_net_base_destroy(base);
    TRACE_RETURN(NULL);
}


static int
ipfix_net_base_start(
    ipfix_net_base_t   *base)
{
    int rv;

    assert(base);
    assert(base->listener);

    /* start the collection thread */
    pthread_mutex_lock(&base->t.mutex);
    base->t.status = STARTING;
    increment_thread_count();
    rv = skthread_create(
        base->name, &base->t.thread, &ipfix_net_base_reader, (void*)base);
    if (rv) {
        base->t.thread = pthread_self();
        base->t.status = JOINED;
        pthread_mutex_unlock(&base->t.mutex);
        WARNINGMSG("Unable to spawn new collection thread for '%s': %s",
                   base->name, strerror(rv));
        decrement_thread_count(0);
    }

    /* wait for the thread to finish initializing before returning. */
    while (STARTING == base->t.status) {
        pthread_cond_wait(&base->t.cond, &base->t.mutex);
    }

    /* return success if thread started */
    rv = ((STARTED == base->t.status) ? 0 : -1);
    pthread_mutex_unlock(&base->t.mutex);
    TRACE_RETURN(rv);
}


/**
 *    If 'probe' does not have an accept from clause, set 'base' as
 *    the network-collector for 'probe', set the 'any' and
 *    'start_from' members of 'base' to 'probe', and return.
 *
 *    Otherwise, add 'probe' to the mapping (red-black tree) on 'base'
 *    that maps from accept-from addresses to probes, creating the
 *    red-black tree if it does not exist.
 *
 *    If the 'start_from' member of 'base' is NULL, set it to 'probe'.
 *
 *    This is a helper function for sk_coll_ipfix_create().
 */
static int
sk_coll_ipfix_create_helper(
    skpc_probe_t         *probe,
    ipfix_net_base_t     *base)
{
    const sk_sockaddr_array_t **accept_from;
    const ipfix_peer2probe_t *found;
    ipfix_peer2probe_t *addr_src;
    size_t accept_from_count;
    size_t i;
    size_t j;

    TRACE_ENTRY;

    assert(probe);
    assert(base);

    /* get data we need from the probe */
    accept_from = NULL;
    accept_from_count = skpcProbeGetAcceptFromHost(probe, &accept_from);

    if (NULL == accept_from) {
        /* source accepts packets from any address.  By definition
         * there is a one-to-one mapping between source and base; this
         * must be a newly created base. */
        if (base->any || base->peer2probe || base->refcount) {
            CRITMSG("Expected unused base object for promiscuous source");
            skAbort();
        }

        /* update the pointers: probe to base and base to probe */
        probe->coll.network = base;
        base->any = probe;

        base->start_from = probe;
        ++base->refcount;

        TRACE_RETURN(0);
    }

    /* otherwise, we need to update the base so that it knows packets
     * coming from each of the 'accept_from' addresses on 'probe'
     * should be processed by that probe */
    if (base->any) {
        CRITMSG("Base object is promiscuous and source is not");
        skAbort();
    }
    /* create the mapping if it does not exist */
    if (NULL == base->peer2probe) {
        base->peer2probe = rbinit(&ipfix_peer2probe_compare, NULL);
        if (NULL == base->peer2probe) {
            skAppPrintOutOfMemory("Red black tree");
            goto ERROR;
        }
        assert(0 == base->refcount);
        assert(NULL == base->start_from);
    }

    for (j = 0; j < accept_from_count; ++j) {
        for (i = 0; i < skSockaddrArrayGetSize(accept_from[j]); ++i) {
            /* create the mapping between this accept_from and the
             * probe */
            addr_src = sk_alloc(ipfix_peer2probe_t);
            addr_src->probe = probe;
            addr_src->addr = skSockaddrArrayGet(accept_from[j], i);

            /* add the accept_from to the tree */
            found = ((const ipfix_peer2probe_t*)
                     rbsearch(addr_src, base->peer2probe));
            if (found != addr_src) {
                if (found && (found->probe == addr_src->probe)) {
                    /* duplicate address, same connection */
                    free(addr_src);
                    continue;
                }
                /* memory error adding to tree */
                free(addr_src);
                goto ERROR;
            }
        }
    }

#if 0
/* DEBUG_ACCEPT_FROM */
    {
        char addr_buf[2 * SK_NUM2DOT_STRLEN];
        RBLIST *iter;

        iter = rbopenlist(base->peer2probe);
        while ((addr_src = (ipfix_peer2probe_t *)rbreadlist(iter)) != NULL) {
            skSockaddrString(addr_buf, sizeof(addr_buf), addr_src->addr);
            DEBUGMSG("Base '%s' accepts packets from '%s'",
                     base->name, addr_buf);
        }
        rbcloselist(iter);
    }
#endif  /* DEBUG_ACCEPT_FROM */

    /* update the probe to point to this base */
    probe->coll.network = base;

    /* start the base when this probe's collector starts */
    if (NULL == base->start_from) {
        assert(0 == base->refcount);
        base->start_from = probe;
    }

    ++base->refcount;

    TRACE_RETURN(0);

  ERROR:
    TRACE_RETURN(-1);
}


/**
 *    Create a new network collector object and store that object on
 *    the probe.
 *
 *    This function either creates a ipfix_net_base_t object or finds
 *    an existing one that is listening on the same port as 'probe'.
 *    Once the base object exists, call sk_coll_ipfix_create_helper()
 *    to connect the base and the probe.
 */
int
sk_coll_ipfix_create(
    skpc_probe_t       *probe)
{
    const sk_sockaddr_array_t *listen_address;
    ipfix_net_base_t *base;
    int base_search;

    TRACE_ENTRY;

    assert(probe);
    assert(PROBE_ENUM_IPFIX == skpcProbeGetType(probe)
           || PROBE_ENUM_NETFLOW_V9 == skpcProbeGetType(probe)
           || PROBE_ENUM_SFLOW == skpcProbeGetType(probe));
    assert(SKPROBE_COLL_NETWORK == probe->coll_type);
    assert(NULL == probe->coll.network);

    /* This must be a network-based probe */
    if (-1 == skpcProbeGetListenOnSockaddr(probe, &listen_address)) {
        CRITMSG("Cannot get listen address");
        skAbort();
    }

    /* search the existing bases to see if we have already created a
     * base that will listen on this port and protocol */
    base_search = listener_to_base_search(&base, listen_address,
                                          skpcProbeGetProtocol(probe));
    if (-1 == base_search) {
        /* address mismatch */
        goto ERROR;
    }
    if (0 == base_search) {
        /* no existing base was found, so create one */
        base = ipfix_net_base_create(listen_address, probe);
        if (base == NULL) {
            goto ERROR;
        }
    }

    /* create a mapping between the base and the probe */
    if (sk_coll_ipfix_create_helper(probe, base)) {
        goto ERROR;
    }

    /* successful */
    TRACE_RETURN(0);

  ERROR:
    /* on error, destroy the base if this function created it.  */
    if (0 == base_search && base) {
        base->t.status = JOINED;
        ipfix_net_base_destroy(base);
    }
    TRACE_RETURN(-1);
}


int
sk_coll_ipfix_start(
    skpc_probe_t       *probe)
{
    ipfix_net_base_t *base;

    TRACE_ENTRY;

    assert(probe);
    assert(PROBE_ENUM_IPFIX == skpcProbeGetType(probe)
           || PROBE_ENUM_NETFLOW_V9 == skpcProbeGetType(probe)
           || PROBE_ENUM_SFLOW == skpcProbeGetType(probe));
    assert(NULL == skpcProbeGetPollDirectory(probe)
           && NULL == skpcProbeGetFileSource(probe));
    assert(0 == skpcProbeGetListenOnSockaddr(probe, NULL));

    base = (ipfix_net_base_t *)probe->coll.network;
    assert(base);
    assert(base->start_from);

    if (base->start_from == probe) {
        TRACE_RETURN(ipfix_net_base_start(base));
    }
    TRACE_RETURN(0);
}


/*
 *    Stops processing of packets.  This will cause a call to any
 *    skIPFIXSourceGetGeneric() function to stop blocking.  Meant to
 *    be used as a prelude to skIPFIXSourceDestroy() in threaded code.
 */
void
sk_coll_ipfix_stop(
    skpc_probe_t       *probe)
{
    ipfix_net_base_t *base;

    TRACE_ENTRY;

    assert(probe);
    assert(PROBE_ENUM_IPFIX == skpcProbeGetType(probe)
           || PROBE_ENUM_NETFLOW_V9 == skpcProbeGetType(probe)
           || PROBE_ENUM_SFLOW == skpcProbeGetType(probe));
    assert(0 == skpcProbeGetListenOnSockaddr(probe, NULL));

    base = (ipfix_net_base_t *)probe->coll.network;
    if (base) {
        pthread_mutex_lock(&base->t.mutex);
        ipfix_net_base_stop(base);
        pthread_mutex_unlock(&base->t.mutex);
    }

    TRACE_RETURN;
}


/*
 *    Destroys a IPFIX source.
 */
void
sk_coll_ipfix_destroy(
    skpc_probe_t       *probe)
{
    ipfix_net_base_t *base;

    TRACE_ENTRY;

    assert(probe);
    assert(PROBE_ENUM_IPFIX == skpcProbeGetType(probe)
           || PROBE_ENUM_NETFLOW_V9 == skpcProbeGetType(probe)
           || PROBE_ENUM_SFLOW == skpcProbeGetType(probe));

    base = (ipfix_net_base_t *)probe->coll.network;
    if (NULL == base) {
        TRACE_RETURN;
    }

    pthread_mutex_lock(&base->t.mutex);
    ipfix_net_base_stop(base);

    if (base->refcount > 1) {
        --base->refcount;
        pthread_mutex_unlock(&base->t.mutex);
        probe->coll.network = NULL;
        return;
    }

    if (base->any) {
        /* there should be a one-to-one mapping between the base and
         * the probe */
        assert(skpcProbeGetAcceptFromHost(probe, NULL) == 0);
        assert(base->any == probe);
        assert(base->start_from == probe);
        base->any = NULL;
    }

    if (base->refcount != 1) {
        ERRMSG("Unexpected reference count %" PRIu32, base->refcount);
    }
    base->refcount = 0;

    pthread_mutex_unlock(&base->t.mutex);
    ipfix_net_base_destroy(base);
    probe->coll.network = NULL;

    TRACE_RETURN;
}


/*
 *    Creates a IPFIX source based on an skpc_probe_t.
 */
int
sk_conv_ipfix_create(
    skpc_probe_t       *probe)
{
    sk_conv_ipfix_t *source;

    TRACE_ENTRY;

    /* One-time initialization of packrec_ctx_ident */
    if (packrec_ctx_ident == SK_SCHEMA_CTX_IDENT_INVALID) {
        sk_schema_context_ident_create(&packrec_ctx_ident);
    }

    assert(probe);
    assert(PROBE_ENUM_IPFIX == skpcProbeGetType(probe)
           || PROBE_ENUM_NETFLOW_V9 == skpcProbeGetType(probe)
           || PROBE_ENUM_SFLOW == skpcProbeGetType(probe));

    if (probe->converter) {
        TRACE_RETURN(0);
    }

    /* Create and initialize source */
    source = sk_alloc(sk_conv_ipfix_t);

    pthread_mutex_init(&source->stats_mutex, NULL);

    /* if probe is NetFlowv9 or sFlow, create a red-black tree used to
     * report missing packets */
    if (PROBE_ENUM_NETFLOW_V9 == skpcProbeGetType(probe)
        || PROBE_ENUM_SFLOW == skpcProbeGetType(probe))
    {
        assert(SKPC_PROTO_UDP == skpcProbeGetProtocol(probe));
        assert(SKPROBE_COLL_NETWORK == probe->coll_type);

        source->connections = rbinit(pointer_cmp, NULL);
        if (NULL == source->connections) {
            free(source);
            TRACE_RETURN(-1);
        }
    }

    probe->converter = source;
    TRACE_RETURN(0);
}


/*
 *    Destroys a IPFIX source.
 */
void
sk_conv_ipfix_destroy(
    skpc_probe_t       *probe)
{
    sk_conv_ipfix_t *source;

    TRACE_ENTRY;

    assert(probe);
    assert(PROBE_ENUM_IPFIX == skpcProbeGetType(probe)
           || PROBE_ENUM_NETFLOW_V9 == skpcProbeGetType(probe)
           || PROBE_ENUM_SFLOW == skpcProbeGetType(probe));

    source = (sk_conv_ipfix_t*)probe->converter;
    if (NULL == source) {
        TRACE_RETURN;
    }
    probe->converter = NULL;

    if (source->connections) {
        rbdestroy(source->connections);
    }
    free(source);

    TRACE_RETURN;
}


void
sk_conv_ipfix_log_stats(
    skpc_probe_t       *probe)
{
    fbCollector_t *collector = NULL;
    sk_conv_ipfix_t *source;
    ipfix_net_base_t *base;
    GError *err = NULL;

    TRACE_ENTRY;

    assert(probe);
    assert(PROBE_ENUM_IPFIX == skpcProbeGetType(probe)
           || PROBE_ENUM_NETFLOW_V9 == skpcProbeGetType(probe)
           || PROBE_ENUM_SFLOW == skpcProbeGetType(probe));

    source = (sk_conv_ipfix_t*)probe->converter;
    if (NULL == source) {
        TRACE_RETURN;
    }
    base = (ipfix_net_base_t *)probe->coll.network;

    pthread_mutex_lock(&source->stats_mutex);

    /* print log message giving the current statistics on the
     * sk_conv_ipfix_t pointer 'source' */
        if (source->saw_yaf_stats_pkt) {
            /* IPFIX from yaf: print the stats */
            INFOMSG(("'%s': forward %" PRIu64
                     ", reverse %" PRIu64
                     ", ignored %" PRIu64
                     "; yaf: recs %" PRIu64
                     ", pkts %" PRIu64
                     ", dropped-pkts %" PRIu64
                     ", ignored-pkts %" PRIu64
                     ", bad-sequence-pkts %" PRIu64
                     ", expired-frags %" PRIu64),
                    skpcProbeGetName(probe),
                    source->forward_flows,
                    source->reverse_flows,
                    source->ignored_flows,
                    source->yaf_exported_flows,
                    source->yaf_processed_packets,
                    source->yaf_dropped_packets,
                    source->yaf_ignored_packets,
                    source->yaf_notsent_packets,
                    source->yaf_expired_fragments);

        } else if (!source->connections
                   || !base
                   || !base->listener)
        {
            /* no data or other IPFIX; print count of SiLK flows
             * created */
            INFOMSG(("'%s': forward %" PRIu64
                     ", reverse %" PRIu64
                     ", ignored %" PRIu64),
                    skpcProbeGetName(probe),
                    source->forward_flows,
                    source->reverse_flows,
                    source->ignored_flows);

        } else if (!fbListenerGetCollector(base->listener,
                                           &collector, &err))
        {
            /* sFlow or NetFlowV9, but no collector */
            DEBUGMSG("'%s': Unable to get collector for source: %s",
                     skpcProbeGetName(probe), err->message);
            g_clear_error(&err);

            INFOMSG(("'%s': forward %" PRIu64
                     ", reverse %" PRIu64
                     ", ignored %" PRIu64),
                    skpcProbeGetName(probe),
                    source->forward_flows,
                    source->reverse_flows,
                    source->ignored_flows);

        } else {
            /* sFlow or NetFlowV9 */
            skIPFIXConnection_t *conn;
            RBLIST *iter;
            uint64_t prev;

            iter = rbopenlist(source->connections);
            while ((conn = (skIPFIXConnection_t *)rbreadlist(iter)) != NULL) {
                /* store the previous number of dropped NF9/sFlow packets
                 * and get the new number of dropped packets. */
                prev = conn->last_yaf_stats.droppedPacketTotalCount;
                if (skpcProbeGetType(probe) == PROBE_ENUM_SFLOW) {
                    conn->last_yaf_stats.droppedPacketTotalCount
                        = fbCollectorGetSFlowMissed(
                            collector, &conn->peer_addr.sa, conn->peer_len,
                            conn->ob_domain);
                } else {
                    conn->last_yaf_stats.droppedPacketTotalCount
                        = fbCollectorGetNetflowMissed(
                            collector, &conn->peer_addr.sa, conn->peer_len,
                            conn->ob_domain);
                }
                if (prev > conn->last_yaf_stats.droppedPacketTotalCount) {
                    /* assume a new collector */
                    TRACEMSG(4, (("Assuming new collector: NF9 loss dropped"
                                  " old = %" PRIu64 ", new = %" PRIu64),
                                 prev,
                                 conn->last_yaf_stats.droppedPacketTotalCount));
                    prev = 0;
                }
                source->yaf_dropped_packets
                    += conn->last_yaf_stats.droppedPacketTotalCount - prev;
            }
            rbcloselist(iter);

            INFOMSG(("'%s': forward %" PRIu64
                     ", reverse %" PRIu64
                     ", ignored %" PRIu64
                     ", %s: missing-pkts %" PRIu64),
                    skpcProbeGetName(probe),
                    source->forward_flows,
                    source->reverse_flows,
                    source->ignored_flows,
                    ((skpcProbeGetType(probe) == PROBE_ENUM_SFLOW)
                     ? "sflow" : "nf9"),
                    source->yaf_dropped_packets);
        }

    /* reset (set to zero) statistics on the sk_conv_ipfix_t
     * 'source' */
        source->yaf_dropped_packets = 0;
        source->yaf_ignored_packets = 0;
        source->yaf_notsent_packets = 0;
        source->yaf_expired_fragments = 0;
        source->yaf_processed_packets = 0;
        source->yaf_exported_flows = 0;
        source->forward_flows = 0;
        source->reverse_flows = 0;
        source->ignored_flows = 0;

    pthread_mutex_unlock(&source->stats_mutex);
    TRACE_RETURN;
}


/*
** Local Variables:
** mode:c
** indent-tabs-mode:nil
** c-basic-offset:4
** End:
*/
