/*
** Copyright (C) 2007-2017 by Carnegie Mellon University.
**
** @OPENSOURCE_LICENSE_START@
** See license information in ../../LICENSE.txt
** @OPENSOURCE_LICENSE_END@
*/

/*
**  rwrec.c
**
**    Functions to get/set values on the rwRec structure.
**
**    Usually these are not used, since we prefer to use macros to get
**    and set values on rwRec; however some functionality is complex
**    enough to require a function, particular those dealing with IPv6
**    addresses.
**
**    In addition, these functions are used when RWREC_OPAQUE is 1.
**
*/

#define SOURCE_FILE_RWREC_C 1
#define RWREC_DEFINE_BODY 1
#include <silk/silk.h>

RCSIDENT("$SiLK: rwrec.c efd886457770 2017-06-21 18:43:23Z mthomas $");

#include <silk/rwrec.h>
#include <silk/skipaddr.h>
#include <silk/skstringmap.h>
#include <silk/utils.h>


/* FUNCTION DEFINITIONS */

/*
 *    Helper function to mask an IP on an rwRec with an skipaddr_t.
 */
static void
recApplyMaskIPAddr(
    rwRec              *r,
    skIPUnion_t        *ip,
    const skipaddr_t   *mask_addr)
{
    skIPUnion_t tmp;

    if (rwRecIsIPv6(r)) {
        if (mask_addr->ip_is_v6) {
            /* both are IPv6 */
            skIPUnionApplyMaskV6(ip, mask_addr->ip_ip.ipu_ipv6);
            return;
        }
        /* must convert mask_addr to V6 */
        skIPUnion4to6(&(mask_addr->ip_ip), &tmp);
        skIPUnionApplyMaskV6(ip, tmp.ipu_ipv6);
        return;
    }
    if (mask_addr->ip_is_v6) {
        /* Record is IPv4 and 'mask_addr' is IPv6. if bytes 10 and 11
         * of 'mask_addr' are 0xFFFF, then an IPv4 address will
         * result; otherwise, we must convert the record to IPv6 and
         * we'll get something strange */
        if (memcmp(&mask_addr->ip_ip.ipu_ipv6[10], &sk_ipv6_v4inv6[10], 2)
            == 0)
        {
            uint32_t mask_v4;
            memcpy(&mask_v4, &mask_addr->ip_ip.ipu_ipv6[12], 4);
            skIPUnionApplyMaskV4(ip, ntohl(mask_v4));
            return;
        }
        rwRecConvertToIPv6(r);
        skIPUnionApplyMaskV6(ip, mask_addr->ip_ip.ipu_ipv6);
        return;
    }
    /* both addresses are IPv4 */
    skIPUnionApplyMaskV4(ip, mask_addr->ip_ip.ipu_ipv4);
}


void
rwRecApplyMaskNhIP(
    rwRec              *r,
    const skipaddr_t   *mask_addr)
{
    recApplyMaskIPAddr(r, &r->nhIP, mask_addr);
}

void
rwRecApplyMaskSIP(
    rwRec              *r,
    const skipaddr_t   *mask_addr)
{
    recApplyMaskIPAddr(r, &r->sIP, mask_addr);
}

void
rwRecApplyMaskDIP(
    rwRec              *r,
    const skipaddr_t   *mask_addr)
{
    recApplyMaskIPAddr(r, &r->dIP, mask_addr);
}


int
rwRecConvertToIPv4(
    rwRec              *r)
{
    uint32_t            ipv4;

    if (!SK_IPV6_IS_V4INV6(r->sIP.ipu_ipv6)
        || !SK_IPV6_IS_V4INV6(r->dIP.ipu_ipv6)
        || (!SK_IPV6_IS_V4INV6(r->nhIP.ipu_ipv6)
            && !SK_IPV6_IS_ZERO(r->nhIP.ipu_ipv6)))
    {
        return -1;
    }

    MEMCPY32(&ipv4, &(r->sIP.ipu_ipv6[12]));
    r->sIP.ipu_ipv4 = ntohl(ipv4);

    MEMCPY32(&ipv4, &(r->dIP.ipu_ipv6[12]));
    r->dIP.ipu_ipv4 = ntohl(ipv4);

    MEMCPY32(&ipv4, &(r->nhIP.ipu_ipv6[12]));
    r->nhIP.ipu_ipv4 = ntohl(ipv4);

    r->tcp_state &= 0x7F;

    return 0;
}


int
skRwrecAppendFieldsToStringMap(
    sk_stringmap_t     *str_map)
{
    /*
     * This struct holds the field names and their IDs.
     *
     * Names that map to the same ID must be put together, with the
     * name that you want to use for the title first, then any aliases
     * afterward.
     *
     * NOTE!! We are assuming that the stringmap code leaves things in
     * the order we insert them and doesn't re-arrange them.  Since
     * that code uses a linked-list, we are safe for now.
     */
    static const sk_stringmap_entry_t rwrec_fields[] = {
        {"sIP",          RWREC_FIELD_SIP,
         "Source IP address", NULL},
        {"1",            RWREC_FIELD_SIP,               NULL, NULL},
        {"dIP",          RWREC_FIELD_DIP,
         "Destination IP address", NULL},
        {"2",            RWREC_FIELD_DIP,               NULL, NULL},
        {"sPort",        RWREC_FIELD_SPORT,
         "Source port for TCP, UDP, or equivalent", NULL},
        {"3",            RWREC_FIELD_SPORT,             NULL, NULL},
        {"dPort",        RWREC_FIELD_DPORT,
         "Destination port for TCP, UDP, or equivalent", NULL},
        {"4",            RWREC_FIELD_DPORT,             NULL, NULL},
        {"protocol",     RWREC_FIELD_PROTO,
         "IP protocol", NULL},
        {"5",            RWREC_FIELD_PROTO,             NULL, NULL},
        {"packets",      RWREC_FIELD_PKTS,
         "Number of packets in the flow", NULL},
        {"pkts",         RWREC_FIELD_PKTS,              NULL, NULL},
        {"6",            RWREC_FIELD_PKTS,              NULL, NULL},
        {"bytes",        RWREC_FIELD_BYTES,
         "Number of octets (bytes) in the flow", NULL},
        {"7",            RWREC_FIELD_BYTES,             NULL, NULL},
        {"flags",        RWREC_FIELD_FLAGS,
         "Bit-wise OR of TCP flags over all packets [FSRPAUEC]", NULL},
        {"8",            RWREC_FIELD_FLAGS,             NULL, NULL},
        {"sTime",        RWREC_FIELD_STIME,
         "Starting time of the flow", NULL},
        {"9",            RWREC_FIELD_STIME,             NULL, NULL},
        {"duration",     RWREC_FIELD_ELAPSED,
         "Duration of the flow", NULL},
        {"10",           RWREC_FIELD_ELAPSED,           NULL, NULL},
        {"eTime",        RWREC_FIELD_ETIME,
         "Ending time of the flow", NULL},
        {"11",           RWREC_FIELD_ETIME,             NULL, NULL},
        {"sensor",       RWREC_FIELD_SID,
         "Name or ID of the sensor as assigned by rwflowpack", NULL},
        {"12",           RWREC_FIELD_SID,               NULL, NULL},
        {"in",           RWREC_FIELD_INPUT,
         "Router SNMP input interface or vlanId", NULL},
        {"13",           RWREC_FIELD_INPUT,             NULL, NULL},
        {"out",          RWREC_FIELD_OUTPUT,
         "Router SNMP output interface or postVlanId", NULL},
        {"14",           RWREC_FIELD_OUTPUT,            NULL, NULL},
        {"nhIP",         RWREC_FIELD_NHIP,
         "Router next-hop IP address", NULL},
        {"15",           RWREC_FIELD_NHIP,              NULL, NULL},
        {"initialFlags", RWREC_FIELD_INIT_FLAGS,
         "TCP flags on first packet in the flow", NULL},
        {"26",           RWREC_FIELD_INIT_FLAGS,        NULL, NULL},
        {"sessionFlags", RWREC_FIELD_REST_FLAGS,
         "Bit-wise OR of TCP flags over second through final packet", NULL},
        {"27",           RWREC_FIELD_REST_FLAGS,        NULL, NULL},
        {"attributes",   RWREC_FIELD_TCP_STATE,
         "Flow attributes set by flow generator [SFTC]", NULL},
        {"28",           RWREC_FIELD_TCP_STATE,         NULL, NULL},
        {"application",  RWREC_FIELD_APPLICATION,
         "Guess as to content of flow (appLabel)", NULL},
        {"29",           RWREC_FIELD_APPLICATION,       NULL, NULL},
        {"class",        RWREC_FIELD_FTYPE_CLASS,
         "Class of the sensor as assigned by rwflowpack", NULL},
        {"20",           RWREC_FIELD_FTYPE_CLASS,       NULL, NULL},
        {"type",         RWREC_FIELD_FTYPE_TYPE,
         "Type within the class as assigned by rwflowpack", NULL},
        {"21",           RWREC_FIELD_FTYPE_TYPE,        NULL, NULL},
        {"iType",        RWREC_FIELD_ICMP_TYPE,
         "ICMP type value for ICMP or ICMPv6 flows; empty otherwise", NULL},
        {"iCode",        RWREC_FIELD_ICMP_CODE,
         "ICMP code value for ICMP or ICMPv6 flows; empty otherwise", NULL},

        /* Do not add the following since the "icmp" prefix cause
         * conflicts with "icmpTypeCode" */
        /* {"icmpType",     RWREC_FIELD_ICMP_TYPE,         NULL, NULL}, */
        /* {"icmpCode",     RWREC_FIELD_ICMP_CODE,         NULL, NULL}, */

        SK_STRINGMAP_SENTINEL
    };
    sk_stringmap_status_t sm_err;

    assert(str_map);

    /* add entries */
    sm_err = (skStringMapAddEntries(
                  str_map, (sizeof(rwrec_fields)/sizeof(rwrec_fields[0]) - 1),
                  rwrec_fields));
    if (SKSTRINGMAP_OK != sm_err) {
        return -1;
    }
    return 0;
}


/*
** Local Variables:
** mode:c
** indent-tabs-mode:nil
** c-basic-offset:4
** End:
*/
