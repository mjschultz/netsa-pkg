/*
 ** fbinfomodel.c
 ** IPFIX Information Model and IE storage management
 **
 ** ------------------------------------------------------------------------
 ** Copyright (C) 2006-2015 Carnegie Mellon University. All Rights Reserved.
 ** ------------------------------------------------------------------------
 ** Authors: Brian Trammell
 ** ------------------------------------------------------------------------
 ** @OPENSOURCE_HEADER_START@
 ** Use of the libfixbuf system and related source code is subject to the terms
 ** of the following licenses:
 **
 ** GNU Lesser GPL (LGPL) Rights pursuant to Version 2.1, February 1999
 ** Government Purpose License Rights (GPLR) pursuant to DFARS 252.227.7013
 **
 ** NO WARRANTY
 **
 ** ANY INFORMATION, MATERIALS, SERVICES, INTELLECTUAL PROPERTY OR OTHER
 ** PROPERTY OR RIGHTS GRANTED OR PROVIDED BY CARNEGIE MELLON UNIVERSITY
 ** PURSUANT TO THIS LICENSE (HEREINAFTER THE "DELIVERABLES") ARE ON AN
 ** "AS-IS" BASIS. CARNEGIE MELLON UNIVERSITY MAKES NO WARRANTIES OF ANY
 ** KIND, EITHER EXPRESS OR IMPLIED AS TO ANY MATTER INCLUDING, BUT NOT
 ** LIMITED TO, WARRANTY OF FITNESS FOR A PARTICULAR PURPOSE,
 ** MERCHANTABILITY, INFORMATIONAL CONTENT, NONINFRINGEMENT, OR ERROR-FREE
 ** OPERATION. CARNEGIE MELLON UNIVERSITY SHALL NOT BE LIABLE FOR INDIRECT,
 ** SPECIAL OR CONSEQUENTIAL DAMAGES, SUCH AS LOSS OF PROFITS OR INABILITY
 ** TO USE SAID INTELLECTUAL PROPERTY, UNDER THIS LICENSE, REGARDLESS OF
 ** WHETHER SUCH PARTY WAS AWARE OF THE POSSIBILITY OF SUCH DAMAGES.
 ** LICENSEE AGREES THAT IT WILL NOT MAKE ANY WARRANTY ON BEHALF OF
 ** CARNEGIE MELLON UNIVERSITY, EXPRESS OR IMPLIED, TO ANY PERSON
 ** CONCERNING THE APPLICATION OF OR THE RESULTS TO BE OBTAINED WITH THE
 ** DELIVERABLES UNDER THIS LICENSE.
 **
 ** Licensee hereby agrees to defend, indemnify, and hold harmless Carnegie
 ** Mellon University, its trustees, officers, employees, and agents from
 ** all claims or demands made against them (and any related losses,
 ** expenses, or attorney's fees) arising out of, or relating to Licensee's
 ** and/or its sub licensees' negligent use or willful misuse of or
 ** negligent conduct or willful misconduct regarding the Software,
 ** facilities, or other rights or assistance granted by Carnegie Mellon
 ** University under this License, including, but not limited to, any
 ** claims of product liability, personal injury, death, damage to
 ** property, or violation of any laws or regulations.
 **
 ** Carnegie Mellon University Software Engineering Institute authored
 ** documents are sponsored by the U.S. Department of Defense under
 ** Contract FA8721-05-C-0003. Carnegie Mellon University retains
 ** copyrights in all material produced under this contract. The U.S.
 ** Government retains a non-exclusive, royalty-free license to publish or
 ** reproduce these documents, or allow others to do so, for U.S.
 ** Government purposes only pursuant to the copyright license under the
 ** contract clause at 252.227.7013.
 **
 ** @OPENSOURCE_HEADER_END@
 ** ------------------------------------------------------------------------
 */

#define _FIXBUF_SOURCE_
#include <fixbuf/private.h>


#define CERT_PEN 6871

struct fbInfoModel_st {
    GHashTable          *ie_table;
    GHashTable          *ie_byname;
    GStringChunk        *ie_names;
    GStringChunk        *ie_desc;
    GPtrArray           *ie_list;
};

static fbInfoElement_t defaults[] = {
    FB_IE_INIT_FULL("octetDeltaCount", 0, 1, 8,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE | FB_IE_DELTACOUNTER |
                    FB_UNITS_OCTETS, 0, 0, FB_UINT_64, NULL),
    FB_IE_INIT_FULL("packetDeltaCount", 0, 2, 8,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE | FB_IE_DELTACOUNTER |
                    FB_UNITS_PACKETS, 0, 0, FB_UINT_64, NULL),
    FB_IE_INIT_FULL("deltaFlowCount", 0, 3, 8,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE | FB_IE_DELTACOUNTER |
                    FB_UNITS_FLOWS, 0, 0, FB_UINT_64, NULL),
    FB_IE_INIT_FULL("protocolIdentifier", 0, 4, 1,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE | FB_IE_IDENTIFIER,
                    0, 0, FB_UINT_8, NULL),
    FB_IE_INIT_FULL("ipClassOfService", 0, 5, 1,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE | FB_IE_IDENTIFIER,
                    0, 0, FB_UINT_8, NULL),
    FB_IE_INIT_FULL("tcpControlBits", 0, 6, 1,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE | FB_IE_FLAGS, 0, 0,
                    FB_UINT_8, NULL),
    FB_IE_INIT_FULL("sourceTransportPort", 0, 7, 2,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE | FB_IE_IDENTIFIER,
                    0, 0, FB_UINT_16, NULL),
    FB_IE_INIT_FULL("sourceIPv4Address", 0, 8, 4,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE | FB_IE_DEFAULT,
                    0, 0, FB_IP4_ADDR, NULL),
    FB_IE_INIT_FULL("sourceIPv4PrefixLength", 0, 9, 1,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE |FB_UNITS_BITS, 0, 32,
                    FB_UINT_8, NULL),
    FB_IE_INIT_FULL("ingressInterface", 0, 10, 4,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE | FB_IE_IDENTIFIER,
                    0, 0, FB_UINT_32, NULL),
    FB_IE_INIT_FULL("destinationTransportPort", 0, 11, 2,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE | FB_IE_IDENTIFIER,
                    0, 0, FB_UINT_16, NULL),
    FB_IE_INIT_FULL("destinationIPv4Address", 0, 12, 4,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE | FB_IE_DEFAULT,
                    0, 0, FB_IP4_ADDR, NULL),
    FB_IE_INIT_FULL("destinationIPv4PrefixLength", 0, 13, 1,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE | FB_UNITS_BITS, 0, 32,
                    FB_UINT_8, NULL),
    FB_IE_INIT_FULL("egressInterface", 0, 14, 4,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE | FB_IE_IDENTIFIER,
                    0, 0, FB_UINT_32, NULL),
    FB_IE_INIT_FULL("ipNextHopIPv4Address", 0, 15, 4,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE | FB_IE_DEFAULT,
                    0, 0, FB_IP4_ADDR, NULL),
    FB_IE_INIT_FULL("bgpSourceAsNumber", 0, 16, 4,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE | FB_IE_IDENTIFIER,
                    0, 0, FB_UINT_32, NULL),
    FB_IE_INIT_FULL("bgpDestinationAsNumber", 0, 17, 4,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE | FB_IE_IDENTIFIER,
                    0, 0, FB_UINT_32, NULL),
    FB_IE_INIT_FULL("bgpNextHopIPv4Address", 0, 18, 4,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE | FB_IE_DEFAULT,
                    0, 0, FB_IP4_ADDR, NULL),
    FB_IE_INIT_FULL("postMCastPacketDeltaCount", 0, 19, 8,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE | FB_IE_DELTACOUNTER,
                    0, 0, FB_UINT_64, NULL),
    FB_IE_INIT_FULL("postMCastOctetDeltaCount", 0, 20, 8,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE | FB_UNITS_PACKETS |
                    FB_IE_DELTACOUNTER, 0, 0, FB_UINT_64, NULL),
    FB_IE_INIT_FULL("flowEndSysUpTime", 0, 21, 4,
                    FB_IE_F_ENDIAN| FB_IE_F_REVERSIBLE | FB_UNITS_MILLISECONDS,
                    0, 0, FB_UINT_32, NULL),
    FB_IE_INIT_FULL("flowStartSysUpTime", 0, 22, 4,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE |FB_UNITS_MILLISECONDS,
                    0, 0, FB_UINT_32, NULL),
    FB_IE_INIT_FULL("postOctetDeltaCount", 0, 23, 8,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE | FB_UNITS_OCTETS |
                    FB_IE_DELTACOUNTER, 0, 0, FB_UINT_64, NULL ),
    FB_IE_INIT_FULL("postPacketDeltaCount", 0, 24, 8,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE | FB_UNITS_PACKETS |
                    FB_IE_DELTACOUNTER, 0, 0, FB_UINT_64, NULL),
    FB_IE_INIT_FULL("minimumIpTotalLength", 0, 25, 8,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE | FB_UNITS_OCTETS,
                    0, 0, FB_UINT_64, NULL),
    FB_IE_INIT_FULL("maximumIpTotalLength", 0, 26, 8,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE | FB_UNITS_OCTETS,
                    0, 0, FB_UINT_64, NULL),
    FB_IE_INIT_FULL("sourceIPv6Address", 0, 27, 16,
                    FB_IE_F_REVERSIBLE | FB_IE_DEFAULT, 0, 0,
                    FB_IP6_ADDR, NULL),
    FB_IE_INIT_FULL("destinationIPv6Address", 0, 28, 16,
                    FB_IE_F_REVERSIBLE | FB_IE_DEFAULT, 0, 0,
                    FB_IP6_ADDR, NULL),
    FB_IE_INIT_FULL("sourceIPv6PrefixLength", 0, 29, 1,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE | FB_UNITS_BITS,
                    0, 128, FB_UINT_8, NULL),
    FB_IE_INIT_FULL("destinationIPv6PrefixLength", 0, 30, 1,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE | FB_UNITS_BITS,
                    0, 128, FB_UINT_8, NULL),
    FB_IE_INIT_FULL("flowLabelIPv6", 0, 31, 4,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE | FB_IE_IDENTIFIER,
                    0, 0, FB_UINT_32, NULL),
    FB_IE_INIT_FULL("icmpTypeCodeIPv4", 0, 32, 2,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE | FB_IE_IDENTIFIER,
                    0, 0, FB_UINT_16, NULL),
    FB_IE_INIT_FULL("igmpType", 0, 33, 1,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE | FB_IE_IDENTIFIER,
                    0, 0, FB_UINT_8, NULL),
    /* NetFlow Compatibility 34-35*/
    FB_IE_INIT_FULL("samplingInterval", 0, 34, 4,
                    FB_IE_F_ENDIAN | FB_IE_QUANTITY | FB_UNITS_PACKETS,
                    0, 0, FB_UINT_32, NULL),
    FB_IE_INIT_FULL("samplingAlgorithm", 0, 35, 1, FB_IE_F_ENDIAN | FB_IE_FLAGS,
                    0, 0, FB_UINT_8, NULL),
    FB_IE_INIT_FULL("flowActiveTimeout", 0, 36, 2,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE | FB_UNITS_SECONDS,
                    0, 0, FB_UINT_16, NULL),
    FB_IE_INIT_FULL("flowIdleTimeout", 0, 37, 2,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE | FB_UNITS_SECONDS,
                    0, 0, FB_UINT_16, NULL),
    /* NetFlow Compatibility 38-39 */
    FB_IE_INIT_FULL("engineType", 0, 38, 1, FB_IE_F_ENDIAN | FB_IE_FLAGS,
                    0, 0, FB_UINT_8, NULL),
    FB_IE_INIT_FULL("engineId", 0, 39, 1, FB_IE_F_ENDIAN | FB_IE_IDENTIFIER,
                    0, 0, FB_UINT_8, NULL),
    FB_IE_INIT_FULL("exportedOctetTotalCount", 0, 40, 8,
                    FB_IE_F_ENDIAN | FB_UNITS_SECONDS | FB_IE_TOTALCOUNTER,
                    0, 0, FB_UINT_64, NULL),
    FB_IE_INIT_FULL("exportedMessageTotalCount", 0, 41, 8,
                    FB_IE_F_ENDIAN | FB_UNITS_MESSAGES | FB_IE_TOTALCOUNTER,
                    0, 0, FB_UINT_64, NULL),
    FB_IE_INIT_FULL("exportedFlowRecordTotalCount", 0, 42, 8,
                    FB_IE_F_ENDIAN | FB_UNITS_FLOWS | FB_IE_TOTALCOUNTER, 0, 0,
                    FB_UINT_64, NULL),
    FB_IE_INIT_FULL("ipv4RouterSc", 0, 43, 4,
                    FB_IE_F_ENDIAN | FB_IE_DEFAULT, 0, 0, FB_IP4_ADDR, NULL),
    FB_IE_INIT_FULL("sourceIPv4Prefix", 0, 44, 4,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE | FB_IE_DEFAULT, 0, 0,
                    FB_IP4_ADDR, NULL),
    FB_IE_INIT_FULL("destinationIPv4Prefix", 0, 45, 4,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE | FB_IE_DEFAULT, 0, 0,
                    FB_IP4_ADDR, NULL),
    FB_IE_INIT_FULL("mplsTopLabelType", 0, 46, 1,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE | FB_IE_IDENTIFIER,
                    0, 0, FB_UINT_8, NULL),
    FB_IE_INIT_FULL("mplsTopLabelIPv4Address", 0, 47, 4,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE | FB_IE_DEFAULT,
                    0, 0, FB_IP4_ADDR, NULL),
    /* NetFlow Compatibility 48-50*/
    FB_IE_INIT_FULL("flowSamplerID", 0, 48, 1,
                    FB_IE_F_ENDIAN | FB_IE_IDENTIFIER,
                    0, 0, FB_UINT_8, NULL),
    FB_IE_INIT_FULL("flowSamplerMode", 0, 49, 1, FB_IE_F_ENDIAN | FB_IE_FLAGS,
                    0, 0, FB_UINT_8, NULL),
    FB_IE_INIT_FULL("flowSamplerRandomInterval", 0, 50, 4,
                    FB_IE_F_ENDIAN | FB_IE_QUANTITY | FB_UNITS_PACKETS,
                    0, 0, FB_UINT_32, NULL),
    FB_IE_INIT_FULL("classId", 0, 51, 1, FB_IE_F_ENDIAN | FB_IE_IDENTIFIER,
                    0, 0, FB_UINT_8, NULL),
    FB_IE_INIT_FULL("minimumTTL", 0, 52, 1,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE | FB_UNITS_HOPS, 0, 0,
                    FB_UINT_8, NULL),
    FB_IE_INIT_FULL("maximumTTL", 0, 53, 1,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE | FB_UNITS_HOPS, 0, 0,
                    FB_UINT_8, NULL),
    FB_IE_INIT_FULL("fragmentIdentification", 0, 54, 4,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE | FB_IE_IDENTIFIER,
                    0, 0, FB_UINT_32, NULL),
    FB_IE_INIT_FULL("postIpClassOfService", 0, 55, 1,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE | FB_IE_IDENTIFIER,
                    0, 0, FB_UINT_8, NULL),
    FB_IE_INIT_FULL("sourceMacAddress", 0, 56, 6,
                    FB_IE_F_REVERSIBLE | FB_IE_DEFAULT, 0, 0,
                    FB_MAC_ADDR, NULL),
    FB_IE_INIT_FULL("postDestinationMacAddress", 0, 57, 6,
                    FB_IE_F_REVERSIBLE | FB_IE_DEFAULT, 0, 0,
                    FB_MAC_ADDR, NULL),
    FB_IE_INIT_FULL("vlanId", 0, 58, 2,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE | FB_IE_IDENTIFIER, 0,
                    0, FB_UINT_16, NULL),
    FB_IE_INIT_FULL("postVlanId", 0, 59, 2,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE | FB_IE_IDENTIFIER, 0,
                    0, FB_UINT_16, NULL),
    FB_IE_INIT_FULL("ipVersion", 0, 60, 1,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE | FB_IE_IDENTIFIER, 0,
                    0, FB_UINT_8, NULL),
    FB_IE_INIT_FULL("flowDirection", 0, 61, 1,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE | FB_IE_IDENTIFIER, 0,
                    0, FB_UINT_8, NULL),
    FB_IE_INIT_FULL("ipNextHopIPv6Address", 0, 62, 16,
                    FB_IE_F_REVERSIBLE | FB_IE_DEFAULT, 0, 0,
                    FB_IP6_ADDR, NULL),
    FB_IE_INIT_FULL("bgpNextHopIPv6Address", 0, 63, 16,
                    FB_IE_F_REVERSIBLE | FB_IE_DEFAULT, 0, 0,
                    FB_IP6_ADDR, NULL),
    FB_IE_INIT_FULL("ipv6ExtensionHeaders", 0, 64, 4,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE | FB_IE_FLAGS, 0, 0,
                    FB_UINT_32, NULL),
    FB_IE_INIT_FULL("mplsTopLabelStackSection", 0, 70, FB_IE_VARLEN,
                    FB_IE_F_REVERSIBLE | FB_IE_DEFAULT, 0, 0,
                    FB_OCTET_ARRAY, NULL),
    FB_IE_INIT_FULL("mplsLabelStackSection2", 0, 71, FB_IE_VARLEN,
                    FB_IE_F_REVERSIBLE | FB_IE_DEFAULT, 0, 0,
                    FB_OCTET_ARRAY, NULL),
    FB_IE_INIT_FULL("mplsLabelStackSection3", 0, 72, FB_IE_VARLEN,
                    FB_IE_F_REVERSIBLE | FB_IE_DEFAULT, 0, 0,
                    FB_OCTET_ARRAY, NULL),
    FB_IE_INIT_FULL("mplsLabelStackSection4", 0, 73, FB_IE_VARLEN,
                    FB_IE_F_REVERSIBLE | FB_IE_DEFAULT, 0, 0,
                    FB_OCTET_ARRAY, NULL),
    FB_IE_INIT_FULL("mplsLabelStackSection5", 0, 74, FB_IE_VARLEN,
                    FB_IE_F_REVERSIBLE | FB_IE_DEFAULT, 0, 0,
                    FB_OCTET_ARRAY, NULL),
    FB_IE_INIT_FULL("mplsLabelStackSection6", 0, 75, FB_IE_VARLEN,
                    FB_IE_F_REVERSIBLE | FB_IE_DEFAULT, 0, 0,
                    FB_OCTET_ARRAY, NULL),
    FB_IE_INIT_FULL("mplsLabelStackSection7", 0, 76, FB_IE_VARLEN,
                    FB_IE_F_REVERSIBLE | FB_IE_DEFAULT, 0, 0,
                    FB_OCTET_ARRAY, NULL),
    FB_IE_INIT_FULL("mplsLabelStackSection8", 0, 77, FB_IE_VARLEN,
                    FB_IE_F_REVERSIBLE | FB_IE_DEFAULT, 0, 0,
                    FB_OCTET_ARRAY, NULL),
    FB_IE_INIT_FULL("mplsLabelStackSection9", 0, 78, FB_IE_VARLEN,
                    FB_IE_F_REVERSIBLE | FB_IE_DEFAULT, 0, 0,
                    FB_OCTET_ARRAY, NULL),
    FB_IE_INIT_FULL("mplsLabelStackSection10", 0, 79, FB_IE_VARLEN,
                    FB_IE_F_REVERSIBLE | FB_IE_DEFAULT, 0, 0,
                    FB_OCTET_ARRAY, NULL),
    FB_IE_INIT_FULL("destinationMacAddress", 0, 80, 6,
                    FB_IE_F_REVERSIBLE | FB_IE_DEFAULT, 0, 0,
                    FB_MAC_ADDR, NULL),
    FB_IE_INIT_FULL("postSourceMacAddress", 0, 81, 6,
                    FB_IE_F_REVERSIBLE | FB_IE_DEFAULT, 0, 0,
                    FB_MAC_ADDR, NULL),
    FB_IE_INIT_FULL("interfaceName", 0, 82, FB_IE_VARLEN,
                    FB_IE_F_REVERSIBLE, 0, 0, FB_STRING, NULL),
    FB_IE_INIT_FULL("interfaceDescription", 0, 83, FB_IE_VARLEN,
                    FB_IE_F_REVERSIBLE, 0, 0, FB_STRING, NULL),
    /* NetFlow Compatibility */
    FB_IE_INIT_FULL("samplerName", 0, 84, FB_IE_VARLEN,
                    FB_IE_IDENTIFIER, 0, 0, FB_STRING, NULL),
    FB_IE_INIT_FULL("octetTotalCount", 0, 85, 8,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE | FB_IE_TOTALCOUNTER,
                    0, 0, FB_UINT_64, NULL),
    FB_IE_INIT_FULL("packetTotalCount", 0, 86, 8,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE | FB_IE_TOTALCOUNTER,
                    0, 0, FB_UINT_64, NULL),
    FB_IE_INIT_FULL("flagsAndSamplerId", 0, 87, 4,
                    FB_IE_F_ENDIAN | FB_IE_IDENTIFIER, 0, 0, FB_UINT_32, NULL),
    FB_IE_INIT_FULL("fragmentOffset", 0, 88, 2,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE | FB_IE_IDENTIFIER,
                    0, 0, FB_UINT_16, NULL),
    /* NETFLOW COMPATIBILITY */
    FB_IE_INIT_FULL("forwardingStatus", 0, 89, 1, FB_IE_F_ENDIAN | FB_IE_FLAGS,
                    0, 0, FB_UINT_8, NULL),
    FB_IE_INIT_FULL("mplsVpnRouteDistinguisher", 0, 90, FB_IE_VARLEN,
                    FB_IE_F_REVERSIBLE | FB_IE_DEFAULT, 0, 0,
                    FB_OCTET_ARRAY, NULL),
    FB_IE_INIT_FULL("mplsTopLabelPrefixLength", 0, 91, 1,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE | FB_IE_IDENTIFIER |
                    FB_UNITS_BITS, 0, 32, FB_UINT_8, NULL),
    /* NetFlow Compatibility */
    FB_IE_INIT_FULL("srcTrafficIndex", 0, 92, 4, FB_IE_F_ENDIAN | FB_IE_IDENTIFIER,
                    0, 0, FB_UINT_32, NULL),
    FB_IE_INIT_FULL("dstTrafficIndex", 0, 93, 4, FB_IE_F_ENDIAN | FB_IE_IDENTIFIER,
                    0, 0, FB_UINT_32, NULL),
    FB_IE_INIT_FULL("applicationDescription", 0, 94, FB_IE_VARLEN,
                    FB_IE_DEFAULT, 0, 0, FB_STRING, NULL),
    FB_IE_INIT_FULL("applicationId", 0, 95, FB_IE_VARLEN, FB_IE_IDENTIFIER,
                    0, 0, FB_OCTET_ARRAY, NULL),
    FB_IE_INIT_FULL("applicationName", 0, 96, FB_IE_VARLEN, FB_IE_DEFAULT,
                    0, 0, FB_STRING, NULL),
    FB_IE_INIT_FULL("postIpDiffServCodePoint", 0, 98, 1,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE | FB_IE_IDENTIFIER,
                    0, 63, FB_UINT_8, NULL),
    FB_IE_INIT_FULL("multicastReplicationFactor", 0, 99, 4,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE | FB_IE_QUANTITY,
                    0, 0, FB_UINT_32, NULL),
    FB_IE_INIT_FULL("className", 0, 100, FB_IE_VARLEN,
                    0, 0, 0, FB_STRING, NULL),
    FB_IE_INIT_FULL("classificationEngineId", 0, 101, 1,
                    FB_IE_F_ENDIAN | FB_IE_IDENTIFIER, 0, 0, FB_UINT_8, NULL),
    FB_IE_INIT_FULL("layer2packetSectionOffset", 0, 102, 2,
                    FB_IE_F_ENDIAN | FB_IE_QUANTITY, 0, 0, FB_UINT_16, NULL),
    FB_IE_INIT_FULL("layer2packetSectionSize", 0, 103, 2,
                    FB_IE_F_ENDIAN | FB_IE_QUANTITY, 0, 0, FB_UINT_16, NULL),
    FB_IE_INIT_FULL("layer2packetSectionData", 0, 104, FB_IE_VARLEN,
                    0, 0, 0, FB_OCTET_ARRAY, NULL),
    FB_IE_INIT_FULL("bgpNextAdjacentAsNumber", 0, 128, 4,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE | FB_IE_IDENTIFIER,
                    0, 0, FB_UINT_32, NULL),
    FB_IE_INIT_FULL("bgpPrevAdjacentAsNumber", 0, 129, 4,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE | FB_IE_IDENTIFIER,
                    0, 0, FB_UINT_32, NULL),
    FB_IE_INIT_FULL("exporterIPv4Address", 0, 130, 4,
                    FB_IE_F_ENDIAN | FB_IE_DEFAULT | FB_IE_DEFAULT, 0, 0,
                    FB_IP4_ADDR, NULL),
    FB_IE_INIT_FULL("exporterIPv6Address", 0, 131, 16, FB_IE_DEFAULT,
                    0, 0, FB_IP6_ADDR, NULL),
    FB_IE_INIT_FULL("droppedOctetDeltaCount", 0, 132, 8,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE | FB_IE_DELTACOUNTER |
                    FB_UNITS_OCTETS, 0, 0, FB_UINT_64, NULL),
    FB_IE_INIT_FULL("droppedPacketDeltaCount", 0, 133, 8,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE | FB_IE_DELTACOUNTER |
                    FB_UNITS_PACKETS, 0, 0, FB_UINT_64, NULL),
    FB_IE_INIT_FULL("droppedOctetTotalCount", 0, 134, 8,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE | FB_IE_TOTALCOUNTER |
                    FB_UNITS_OCTETS, 0, 0, FB_UINT_64, NULL),
    FB_IE_INIT_FULL("droppedPacketTotalCount", 0, 135, 8,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE | FB_IE_TOTALCOUNTER |
                    FB_UNITS_PACKETS, 0, 0, FB_UINT_64, NULL),
    FB_IE_INIT_FULL("flowEndReason", 0, 136, 1,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE | FB_IE_IDENTIFIER,
                    0, 0, FB_UINT_8, NULL),
    FB_IE_INIT_FULL("commonPropertiesId", 0, 137, 8,
                    FB_IE_F_ENDIAN | FB_IE_IDENTIFIER, 0, 0, FB_UINT_64, NULL),
    FB_IE_INIT_FULL("observationPointId", 0, 138, 4,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE | FB_IE_IDENTIFIER,
                    0, 0, FB_UINT_32, NULL),
    FB_IE_INIT_FULL("icmpTypeCodeIPv6", 0, 139, 2,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE | FB_IE_IDENTIFIER,
                    0, 0, FB_UINT_16, NULL),
    FB_IE_INIT_FULL("mplsTopLabelIPv6Address", 0, 140, 16,
                    FB_IE_F_REVERSIBLE | FB_IE_DEFAULT, 0, 0,
                    FB_IP6_ADDR, NULL),
    FB_IE_INIT_FULL("lineCardId", 0, 141, 4,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE | FB_IE_IDENTIFIER,
                    0, 0, FB_UINT_32, NULL),
    FB_IE_INIT_FULL("portId", 0, 142, 4,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE | FB_IE_IDENTIFIER,
                    0, 0, FB_UINT_32, NULL),
    FB_IE_INIT_FULL("meteringProcessId", 0, 143, 4,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE | FB_IE_IDENTIFIER,
                    0, 0, FB_UINT_32, NULL),
    FB_IE_INIT_FULL("exportingProcessId", 0, 144, 4,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE | FB_IE_IDENTIFIER,
                    0, 0, FB_UINT_32, NULL),
    FB_IE_INIT_FULL("templateId", 0, 145, 2,
                    FB_IE_F_ENDIAN | FB_IE_IDENTIFIER, 0, 0, FB_UINT_16, NULL),
    FB_IE_INIT_FULL("wlanChannelId", 0, 146, 1,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE | FB_IE_IDENTIFIER,
                    0, 0, FB_UINT_8, NULL),
    FB_IE_INIT_FULL("wlanSSID", 0, 147, FB_IE_VARLEN, FB_IE_F_REVERSIBLE,
                    0, 0, FB_STRING, NULL),
    FB_IE_INIT_FULL("flowId", 0, 148, 8, FB_IE_F_ENDIAN | FB_IE_IDENTIFIER,
                    0, 0, FB_UINT_64, NULL),
    FB_IE_INIT_FULL("observationDomainId", 0, 149, 4,
                    FB_IE_F_ENDIAN | FB_IE_IDENTIFIER, 0, 0, FB_UINT_32, NULL),
    FB_IE_INIT_FULL("flowStartSeconds", 0, 150, 4,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE | FB_UNITS_SECONDS,
                    0, 0, FB_DT_SEC, NULL),
    FB_IE_INIT_FULL("flowEndSeconds", 0, 151, 4,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE | FB_UNITS_SECONDS,
                    0, 0, FB_DT_SEC, NULL),
    FB_IE_INIT_FULL("flowStartMilliseconds", 0, 152, 8,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE | FB_UNITS_MILLISECONDS,
                    0, 0, FB_DT_MILSEC, NULL),
    FB_IE_INIT_FULL("flowEndMilliseconds", 0, 153, 8,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE |FB_UNITS_MILLISECONDS,
                    0, 0, FB_DT_MILSEC, NULL),
    FB_IE_INIT_FULL("flowStartMicroseconds", 0, 154, 8,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE |FB_UNITS_MICROSECONDS,
                    0, 0, FB_DT_MICROSEC, NULL),
    FB_IE_INIT_FULL("flowEndMicroseconds", 0, 155, 8,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE |FB_UNITS_MICROSECONDS,
                    0,0, FB_DT_MICROSEC, NULL),
    FB_IE_INIT_FULL("flowStartNanoseconds", 0, 156, 8,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE | FB_UNITS_NANOSECONDS,
                    0, 0, FB_DT_NANOSEC, NULL),
    FB_IE_INIT_FULL("flowEndNanoseconds", 0, 157, 8,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE | FB_UNITS_NANOSECONDS,
                    0, 0, FB_DT_NANOSEC, NULL),
    FB_IE_INIT_FULL("flowStartDeltaMicroseconds", 0, 158, 4,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE| FB_UNITS_MICROSECONDS,
                    0, 0, FB_DT_MICROSEC, NULL),
    FB_IE_INIT_FULL("flowEndDeltaMicroseconds", 0, 159, 4,
                    FB_IE_F_ENDIAN| FB_IE_F_REVERSIBLE | FB_UNITS_MICROSECONDS,
                    0, 0, FB_DT_MICROSEC, NULL),
    FB_IE_INIT_FULL("systemInitTimeMilliseconds", 0, 160, 8,
                    FB_IE_F_ENDIAN| FB_IE_F_REVERSIBLE | FB_UNITS_MILLISECONDS,
                    0, 0, FB_DT_MILSEC, NULL),
    FB_IE_INIT_FULL("flowDurationMilliseconds", 0, 161, 4,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE | FB_IE_QUANTITY |
                    FB_UNITS_MILLISECONDS, 0, 0, FB_UINT_32, NULL),
    FB_IE_INIT_FULL("flowDurationMicroseconds", 0, 162, 4,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE |FB_UNITS_MICROSECONDS,
                    0, 0, FB_UINT_32, NULL),
    FB_IE_INIT_FULL("observedFlowTotalCount", 0, 163, 8,
                    FB_IE_F_ENDIAN | FB_IE_TOTALCOUNTER | FB_UNITS_FLOWS, 0, 0,
                    FB_UINT_64, NULL),
    FB_IE_INIT_FULL("ignoredPacketTotalCount", 0, 164, 8,
                    FB_IE_F_ENDIAN | FB_IE_TOTALCOUNTER | FB_UNITS_PACKETS,
                    0, 0, FB_UINT_64, NULL),
    FB_IE_INIT_FULL("ignoredOctetTotalCount", 0, 165, 8,
                    FB_IE_F_ENDIAN | FB_IE_TOTALCOUNTER | FB_UNITS_OCTETS,
                    0, 0, FB_UINT_64, NULL),
    FB_IE_INIT_FULL("notSentFlowTotalCount", 0, 166, 8,
                    FB_IE_F_ENDIAN | FB_IE_TOTALCOUNTER | FB_UNITS_FLOWS, 0, 0,
                    FB_UINT_64, NULL),
    FB_IE_INIT_FULL("notSentPacketTotalCount", 0, 167, 8,
                    FB_IE_F_ENDIAN | FB_IE_TOTALCOUNTER | FB_UNITS_PACKETS,
                    0, 0, FB_UINT_64, NULL),
    FB_IE_INIT_FULL("notSentOctetTotalCount", 0, 168, 8,
                    FB_IE_F_ENDIAN | FB_IE_TOTALCOUNTER | FB_UNITS_OCTETS,
                    0, 0, FB_UINT_64, NULL),
    FB_IE_INIT_FULL("destinationIPv6Prefix", 0, 169, 16, FB_IE_F_REVERSIBLE,
                    0, 0, FB_IP6_ADDR, NULL),
    FB_IE_INIT_FULL("sourceIPv6Prefix", 0, 170, 16, FB_IE_F_REVERSIBLE, 0, 0,
                    FB_IP6_ADDR, NULL),
    FB_IE_INIT_FULL("postOctetTotalCount", 0, 171, 8,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE | FB_IE_TOTALCOUNTER |
                    FB_UNITS_OCTETS, 0, 0, FB_UINT_64, NULL),
    FB_IE_INIT_FULL("postPacketTotalCount", 0, 172, 8,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE | FB_IE_TOTALCOUNTER |
                    FB_UNITS_PACKETS, 0, 0, FB_UINT_64, NULL),
    FB_IE_INIT_FULL("flowKeyIndicator", 0, 173, 8,
                    FB_IE_F_ENDIAN | FB_IE_FLAGS, 0, 0, FB_UINT_64, NULL),
    FB_IE_INIT_FULL("postMCastPacketTotalCount", 0, 174, 8,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE | FB_IE_TOTALCOUNTER |
                    FB_UNITS_PACKETS, 0, 0, FB_UINT_64, NULL),
    FB_IE_INIT_FULL("postMCastOctetTotalCount", 0, 175, 8,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE | FB_IE_TOTALCOUNTER |
                    FB_UNITS_OCTETS, 0, 0, FB_UINT_64, NULL),
    FB_IE_INIT_FULL("icmpTypeIPv4", 0, 176, 1,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE | FB_IE_IDENTIFIER,
                    0, 0, FB_UINT_8, NULL),
    FB_IE_INIT_FULL("icmpCodeIPv4", 0, 177, 1,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE | FB_IE_IDENTIFIER,
                    0, 0, FB_UINT_8, NULL),
    FB_IE_INIT_FULL("icmpTypeIPv6", 0, 178, 1,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE | FB_IE_IDENTIFIER,
                    0, 0, FB_UINT_8, NULL),
    FB_IE_INIT_FULL("icmpCodeIPv6", 0, 179, 1,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE | FB_IE_IDENTIFIER,
                    0, 0, FB_UINT_8, NULL),
    FB_IE_INIT_FULL("udpSourcePort", 0, 180, 2,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE | FB_IE_IDENTIFIER,
                    0, 0, FB_UINT_16, NULL),
    FB_IE_INIT_FULL("udpDestinationPort", 0, 181, 2,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE | FB_IE_IDENTIFIER,
                    0, 0, FB_UINT_16, NULL),
    FB_IE_INIT_FULL("tcpSourcePort", 0, 182, 2,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE | FB_IE_IDENTIFIER,
                    0, 0, FB_UINT_16, NULL),
    FB_IE_INIT_FULL("tcpDestinationPort", 0, 183, 2,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE | FB_IE_IDENTIFIER,
                    0, 0, FB_UINT_16, NULL),
    FB_IE_INIT_FULL("tcpSequenceNumber", 0, 184, 4,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE, 0, 0,
                    FB_UINT_32, NULL),
    FB_IE_INIT_FULL("tcpAcknowledgementNumber", 0, 185, 4,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE, 0, 0,
                    FB_UINT_32, NULL),
    FB_IE_INIT_FULL("tcpWindowSize", 0, 186, 2,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE, 0, 0,
                    FB_UINT_16, NULL),
    FB_IE_INIT_FULL("tcpUrgentPointer", 0, 187, 2,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE, 0, 0,
                    FB_UINT_16, NULL),
    FB_IE_INIT_FULL("tcpHeaderLength", 0, 188, 1,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE | FB_UNITS_OCTETS,
                    0, 0, FB_UINT_8, NULL),
    FB_IE_INIT_FULL("ipHeaderLength", 0, 189, 1,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE | FB_UNITS_OCTETS,
                    0, 0, FB_UINT_8, NULL),
    FB_IE_INIT_FULL("totalLengthIPv4", 0, 190, 2,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE | FB_UNITS_OCTETS,
                    0, 0, FB_UINT_16, NULL),
    FB_IE_INIT_FULL("payloadLengthIPv6", 0, 191, 2,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE | FB_UNITS_OCTETS,
                    0, 0, FB_UINT_16, NULL),
    FB_IE_INIT_FULL("ipTTL", 0, 192, 1,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE | FB_UNITS_HOPS, 0, 0,
                    FB_UINT_8, NULL),
    FB_IE_INIT_FULL("nextHeaderIPv6", 0, 193, 1,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE, 0, 0,FB_UINT_8, NULL),
    FB_IE_INIT_FULL("mplsPayloadLength", 0, 194, 4,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE | FB_UNITS_OCTETS,
                    0, 0, FB_UINT_32, NULL),
    FB_IE_INIT_FULL("ipDiffServCodePoint", 0, 195, 1,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE | FB_IE_IDENTIFIER,
                    0, 63, FB_UINT_8, NULL),
    FB_IE_INIT_FULL("ipPrecedence", 0, 196, 1,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE | FB_IE_IDENTIFIER,
                    0, 7, FB_UINT_8, NULL),
    FB_IE_INIT_FULL("fragmentFlags", 0, 197, 1,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE | FB_IE_FLAGS, 0, 0,
                    FB_UINT_8, NULL),
    FB_IE_INIT_FULL("octetDeltaSumOfSquares", 0, 198, 8,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE, 0, 0,
                    FB_UINT_64, NULL),
    FB_IE_INIT_FULL("octetTotalSumOfSquares", 0, 199, 8,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE | FB_UNITS_OCTETS,
                    0, 0, FB_UINT_64, NULL),
    FB_IE_INIT_FULL("mplsTopLabelTTL", 0, 200, 1,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE | FB_UNITS_HOPS, 0, 0,
                    FB_UINT_8, NULL),
    FB_IE_INIT_FULL("mplsLabelStackLength", 0, 201, 4,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE | FB_UNITS_OCTETS,
                    0, 0, FB_UINT_32, NULL),
    FB_IE_INIT_FULL("mplsLabelStackDepth", 0, 202, 4,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE | FB_UNITS_ENTRIES,
                    0, 0, FB_UINT_32, NULL),
    FB_IE_INIT_FULL("mplsTopLabelExp", 0, 203, 1,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE | FB_IE_FLAGS, 0, 0,
                    FB_UINT_8, NULL),
    FB_IE_INIT_FULL("ipPayloadLength", 0, 204, 4,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE | FB_UNITS_OCTETS,
                    0, 0, FB_UINT_32, NULL),
    FB_IE_INIT_FULL("udpMessageLength", 0, 205, 2,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE | FB_UNITS_OCTETS,
                    0, 0, FB_UINT_16, NULL),
    FB_IE_INIT_FULL("isMulticast", 0, 206, 1,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE | FB_IE_FLAGS, 0, 0,
                    FB_UINT_8, NULL),
    FB_IE_INIT_FULL("ipv4IHL", 0, 207, 1,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE | FB_UNITS_WORDS, 0, 0,
                    FB_UINT_8, NULL),
    FB_IE_INIT_FULL("ipv4Options", 0, 208, 4,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE | FB_IE_FLAGS, 0, 0,
                    FB_UINT_32, NULL),
    FB_IE_INIT_FULL("tcpOptions", 0, 209, 8,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE | FB_IE_FLAGS, 0, 0,
                    FB_UINT_64, NULL),
    FB_IE_INIT_FULL("paddingOctets", 0, 210, FB_IE_VARLEN, FB_IE_DEFAULT, 0, 0,
                    FB_OCTET_ARRAY, NULL),
    FB_IE_INIT_FULL("collectorIPv4Address", 0, 211, 4,
                    FB_IE_F_ENDIAN | FB_IE_DEFAULT, 0, 0,
                    FB_IP4_ADDR, NULL),
    FB_IE_INIT_FULL("collectorIPv6Address", 0, 212, 16,
                    FB_IE_DEFAULT, 0, 0, FB_IP6_ADDR, NULL),
    FB_IE_INIT_FULL("exportInterface", 0, 213, 4,
                    FB_IE_F_ENDIAN | FB_IE_IDENTIFIER, 0, 0, FB_UINT_32, NULL),
    FB_IE_INIT_FULL("exportProtocolVersion", 0, 214, 1,
                    FB_IE_F_ENDIAN | FB_IE_IDENTIFIER, 0, 0, FB_UINT_8, NULL),
    FB_IE_INIT_FULL("exportTransportProtocol", 0, 215, 1,
                    FB_IE_F_ENDIAN | FB_IE_IDENTIFIER, 0, 0, FB_UINT_8, NULL),
    FB_IE_INIT_FULL("collectorTransportPort", 0, 216, 2,
                    FB_IE_F_ENDIAN | FB_IE_IDENTIFIER, 0, 0, FB_UINT_16, NULL),
    FB_IE_INIT_FULL("exporterTransportPort", 0, 217, 2,
                    FB_IE_F_ENDIAN | FB_IE_IDENTIFIER, 0, 0, FB_UINT_16, NULL),
    FB_IE_INIT_FULL("tcpSynTotalCount", 0, 218, 8,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE | FB_IE_TOTALCOUNTER |
                    FB_UNITS_PACKETS, 0, 0, FB_UINT_64, NULL),
    FB_IE_INIT_FULL("tcpFinTotalCount", 0, 219, 8,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE | FB_IE_TOTALCOUNTER |
                    FB_UNITS_PACKETS, 0, 0, FB_UINT_64, NULL),
    FB_IE_INIT_FULL("tcpRstTotalCount", 0, 220, 8,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE | FB_IE_TOTALCOUNTER |
                    FB_UNITS_PACKETS, 0, 0, FB_UINT_64, NULL),
    FB_IE_INIT_FULL("tcpPshTotalCount", 0, 221, 8,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE | FB_IE_TOTALCOUNTER |
                    FB_UNITS_PACKETS, 0, 0, FB_UINT_64, NULL),
    FB_IE_INIT_FULL("tcpAckTotalCount", 0, 222, 8,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE | FB_IE_TOTALCOUNTER |
                    FB_UNITS_PACKETS, 0, 0, FB_UINT_64, NULL),
    FB_IE_INIT_FULL("tcpUrgTotalCount", 0, 223, 8,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE | FB_IE_TOTALCOUNTER |
                    FB_UNITS_PACKETS, 0, 0, FB_UINT_64, NULL),
    FB_IE_INIT_FULL("ipTotalLength", 0, 224, 8,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE | FB_UNITS_OCTETS,
                    0, 0, FB_UINT_64, NULL),
    FB_IE_INIT_FULL("postNATSourceIPv4Address", 0, 225, 4,
                    FB_IE_F_ENDIAN | FB_IE_DEFAULT, 0, 0,
                    FB_IP4_ADDR, NULL),
    FB_IE_INIT_FULL("postNATDestinationIPv4Address", 0, 226, 4,
                    FB_IE_F_ENDIAN | FB_IE_DEFAULT, 0, 0,
                    FB_IP4_ADDR, NULL),
    FB_IE_INIT_FULL("postNAPTSourceTransportPort", 0, 227, 2,
                    FB_IE_F_ENDIAN | FB_IE_IDENTIFIER, 0, 0, FB_UINT_16, NULL),
    FB_IE_INIT_FULL("postNAPTDestinationTransportPort", 0, 228, 2,
                    FB_IE_F_ENDIAN | FB_IE_IDENTIFIER, 0, 0, FB_UINT_16, NULL),
    FB_IE_INIT_FULL("natOriginatingAddressRealm", 0, 229, 1,
                    FB_IE_F_ENDIAN | FB_IE_FLAGS, 0, 0, FB_UINT_8, NULL),
    FB_IE_INIT_FULL("natEvent", 0, 230, 1, FB_IE_F_ENDIAN, 0, 0,
                    FB_UINT_8, NULL),
    FB_IE_INIT_FULL("initiatorOctets", 0, 231, 8,
                    FB_IE_F_ENDIAN | FB_UNITS_OCTETS, 0, 0, FB_UINT_64, NULL),
    FB_IE_INIT_FULL("responderOctets", 0, 232, 8,
                    FB_IE_F_ENDIAN | FB_UNITS_OCTETS, 0, 0, FB_UINT_64, NULL),
    FB_IE_INIT_FULL("firewallEvent", 0, 233, 1, FB_IE_F_ENDIAN, 0, 0,
                    FB_UINT_8, NULL),
    FB_IE_INIT_FULL("ingressVRFID", 0, 234, 4, FB_IE_F_ENDIAN, 0, 0,
                    FB_UINT_32, NULL),
    FB_IE_INIT_FULL("egressVRFID", 0, 235, 4, FB_IE_F_ENDIAN, 0, 0,
                    FB_UINT_32, NULL),
    FB_IE_INIT_FULL("VRFname", 0, 236, FB_IE_VARLEN, FB_IE_F_REVERSIBLE,
                    0, 0, FB_STRING, NULL),
    FB_IE_INIT_FULL("postMplsTopLabelExp", 0, 237, 1,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE | FB_IE_FLAGS, 0, 0,
                    FB_UINT_8, NULL),
    FB_IE_INIT_FULL("tcpWindowScale", 0, 238, 2,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE, 0, 0,
                    FB_UINT_16, NULL),
    FB_IE_INIT_FULL("biflowDirection", 0, 239, 1,
                    FB_IE_F_ENDIAN | FB_IE_IDENTIFIER, 0, 0, FB_UINT_8, NULL),
    FB_IE_INIT_FULL("ethernetHeaderLength", 0, 240, 1,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE | FB_IE_IDENTIFIER |
                    FB_UNITS_OCTETS, 0, 0, FB_UINT_8, NULL),
    FB_IE_INIT_FULL("ethernetPayloadLength", 0, 241, 2,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE| FB_IE_IDENTIFIER |
                    FB_UNITS_OCTETS, 0, 0, FB_UINT_16, NULL),
    FB_IE_INIT_FULL("ethernetTotalLength", 0, 242, 2,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE | FB_IE_IDENTIFIER |
                    FB_UNITS_OCTETS, 0, 0, FB_UINT_16, NULL),
    FB_IE_INIT_FULL("dot1qVlanId", 0, 243, 2,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE | FB_IE_IDENTIFIER |
                    FB_UNITS_OCTETS, 0, 0, FB_UINT_16, NULL),
    FB_IE_INIT_FULL("dot1qPriority", 0, 244, 1,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE | FB_IE_IDENTIFIER,
                    0, 0, FB_UINT_8, NULL),
    FB_IE_INIT_FULL("dot1qCustomerVlanId", 0, 245, 2,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE | FB_IE_IDENTIFIER,
                    0, 0, FB_UINT_16, NULL),
    FB_IE_INIT_FULL("dot1qCustomerPriority", 0, 246, 1,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE | FB_IE_IDENTIFIER,
                    0, 0, FB_UINT_8, NULL),
    FB_IE_INIT_FULL("metroEvcId", 0, 247, FB_IE_VARLEN, FB_IE_F_REVERSIBLE,
                    0, 0, FB_STRING, NULL),
    FB_IE_INIT_FULL("metroEvcType", 0, 248, 1,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE | FB_IE_IDENTIFIER,
                    0, 0, FB_UINT_8, NULL),
    FB_IE_INIT_FULL("pseudoWireId", 0, 249, 4,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE | FB_IE_IDENTIFIER,
                    0, 0, FB_UINT_32, NULL),
    FB_IE_INIT_FULL("pseudoWireType", 0, 250, 2,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE | FB_IE_IDENTIFIER,
                    0, 0, FB_UINT_16, NULL),
    FB_IE_INIT_FULL("pseudoWireControlWord", 0, 251, 4,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE | FB_IE_IDENTIFIER,
                    0, 0, FB_UINT_32, NULL),
    FB_IE_INIT_FULL("ingressPhysicalInterface", 0, 252, 4,
                    FB_IE_F_ENDIAN | FB_IE_IDENTIFIER, 0, 0, FB_UINT_32, NULL),
    FB_IE_INIT_FULL("egressPhysicalInterface", 0, 253, 4,
                    FB_IE_F_ENDIAN | FB_IE_IDENTIFIER, 0, 0, FB_UINT_32, NULL),
    FB_IE_INIT_FULL("postDot1qVlanId", 0, 254, 2,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE | FB_IE_IDENTIFIER,
                    0, 0, FB_UINT_16, NULL),
    FB_IE_INIT_FULL("postDot1qCustomerVlanId", 0, 255, 2,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE | FB_IE_IDENTIFIER, 0,
                    0, FB_UINT_16, NULL),
    FB_IE_INIT_FULL("ethernetType", 0, 256, 2,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE | FB_IE_IDENTIFIER,
                    0, 0, FB_UINT_16, NULL),
    FB_IE_INIT_FULL("postIpPrecedence", 0, 257, 1,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE | FB_IE_IDENTIFIER,
                    0, 7, FB_UINT_8, NULL),
    FB_IE_INIT_FULL("collectionTimeMilliseconds", 0, 258, 8,
                    FB_IE_F_ENDIAN | FB_UNITS_MILLISECONDS,
                    0, 0, FB_DT_MILSEC, NULL),
    FB_IE_INIT_FULL("exportSctpStreamId", 0, 259, 2,
                    FB_IE_F_ENDIAN | FB_IE_F_ENDIAN, 0, 0, FB_UINT_16, NULL),
    FB_IE_INIT_FULL("maxExportSeconds", 0, 260, 4,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE | FB_UNITS_SECONDS,
                    0, 0, FB_DT_SEC, NULL),
    FB_IE_INIT_FULL("maxFlowEndSeconds", 0, 261, 4,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE | FB_UNITS_SECONDS,
                    0, 0, FB_DT_SEC, NULL),
    FB_IE_INIT_FULL("messageMD5Checksum", 0, 262, FB_IE_VARLEN, FB_IE_DEFAULT,
                    0, 0, FB_OCTET_ARRAY, NULL),
    FB_IE_INIT_FULL("messageScope", 0, 263, 1, FB_IE_F_ENDIAN, 0, 0, FB_UINT_8,
                    NULL),
    FB_IE_INIT_FULL("minExportSeconds", 0, 264, 4,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE | FB_UNITS_SECONDS,
                    0, 0, FB_DT_SEC, NULL),
    FB_IE_INIT_FULL("minFlowStartSeconds", 0, 265, 4,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE | FB_UNITS_SECONDS,
                    0, 0, FB_DT_SEC, NULL),
    FB_IE_INIT_FULL("opaqueOctets", 0, 266, FB_IE_VARLEN,
                    FB_IE_F_REVERSIBLE | FB_IE_DEFAULT,
                    0, 0, FB_OCTET_ARRAY, NULL),
    FB_IE_INIT_FULL("sessionScope", 0, 267, 1,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE, 0, 0,
                    FB_UINT_8, NULL),
    FB_IE_INIT_FULL("maxFlowEndMicroseconds", 0, 268, 8,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE |FB_UNITS_MICROSECONDS,
                    0, 0, FB_DT_MICROSEC, NULL),
    FB_IE_INIT_FULL("maxFlowEndMilliseconds", 0, 269, 8,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE |FB_UNITS_MILLISECONDS,
                    0, 0, FB_DT_MILSEC, NULL),
    FB_IE_INIT_FULL("maxFlowEndNanoseconds", 0, 270, 8,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE |FB_UNITS_NANOSECONDS,
                    0, 0, FB_DT_NANOSEC, NULL),
    FB_IE_INIT_FULL("minFlowStartMicroseconds", 0, 271, 8,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE |FB_UNITS_MICROSECONDS,
                    0, 0, FB_DT_MICROSEC, NULL),
    FB_IE_INIT_FULL("minFlowStartMilliseconds", 0, 272, 8,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE |FB_UNITS_MILLISECONDS,
                    0, 0, FB_DT_MILSEC, NULL),
    FB_IE_INIT_FULL("minFlowStartNanoseconds", 0, 273, 8,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE | FB_UNITS_NANOSECONDS,
                    0, 0, FB_DT_NANOSEC, NULL),
    FB_IE_INIT_FULL("collectorCertificate", 0, 274, FB_IE_VARLEN, FB_IE_DEFAULT,
                    0, 0, FB_OCTET_ARRAY, NULL),
    FB_IE_INIT_FULL("exporterCertificate", 0, 275, FB_IE_VARLEN, FB_IE_DEFAULT,
                    0, 0, FB_OCTET_ARRAY, NULL),
    FB_IE_INIT_FULL("dataRecordsReliability", 0, 276, 1,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE | FB_IE_DEFAULT,
                    0, 0, FB_BOOL, NULL),
    FB_IE_INIT_FULL("observationPointType", 0, 277, 1,
                    FB_IE_F_ENDIAN | FB_IE_IDENTIFIER, 0, 0, FB_UINT_8, NULL),
    FB_IE_INIT_FULL("newConnectionDeltaCount", 0, 278, 4,
                    FB_IE_F_ENDIAN | FB_IE_DELTACOUNTER, 0, 0,
                    FB_UINT_32, NULL),
    FB_IE_INIT_FULL("connectionSumDurationSeconds", 0, 279, 8,
                    FB_IE_F_ENDIAN | FB_UNITS_SECONDS, 0, 0, FB_UINT_64, NULL),
    FB_IE_INIT_FULL("connectionTransactionId", 0, 280, 8,
                    FB_IE_F_ENDIAN | FB_IE_IDENTIFIER, 0, 0, FB_UINT_64, NULL),
    FB_IE_INIT_FULL("postNATSourceIPv6Address", 0, 281, 16,
                    FB_IE_F_REVERSIBLE, 0, 0, FB_IP6_ADDR, NULL),
    FB_IE_INIT_FULL("postNATDestinationIPv6Address", 0, 282, 16,
                    FB_IE_F_REVERSIBLE, 0, 0, FB_IP6_ADDR, NULL),
    FB_IE_INIT_FULL("natPoolId", 0, 283, 4,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE | FB_IE_IDENTIFIER,
                    0, 0, FB_UINT_32, NULL),
    FB_IE_INIT_FULL("natPoolName", 0, 284, FB_IE_VARLEN, FB_IE_F_REVERSIBLE,
                    0, 0, FB_STRING, NULL),
    FB_IE_INIT_FULL("anonymizationFlags", 0, 285, 2,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE | FB_IE_FLAGS, 0, 0,
                    FB_UINT_16, NULL),
    FB_IE_INIT_FULL("anonymizationTechnique", 0, 286, 2,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE | FB_IE_IDENTIFIER,
                    0, 0, FB_UINT_16, NULL),
    FB_IE_INIT_FULL("informationElementIndex", 0, 287, 2,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE | FB_IE_IDENTIFIER,
                    0, 0, FB_UINT_16, NULL),
    FB_IE_INIT_FULL("p2pTechnology", 0, 288, FB_IE_VARLEN, FB_IE_F_REVERSIBLE,
                    0, 0, FB_STRING, NULL),
    FB_IE_INIT_FULL("tunnelTechnology", 0, 289, FB_IE_VARLEN,
                    FB_IE_F_REVERSIBLE, 0, 0, FB_STRING, NULL),
    FB_IE_INIT_FULL("encryptedTechnology", 0, 290, FB_IE_VARLEN,
                    FB_IE_F_REVERSIBLE, 0, 0, FB_STRING, NULL),
    FB_IE_INIT_FULL("basicList", 0, FB_IE_BASIC_LIST, FB_IE_VARLEN, FB_IE_LIST,
                    0, 0, FB_BASIC_LIST, NULL),
    FB_IE_INIT_FULL("subTemplateList", 0, FB_IE_SUBTEMPLATE_LIST, FB_IE_VARLEN,
                    FB_IE_LIST, 0, 0, FB_SUB_TMPL_LIST, NULL),
    FB_IE_INIT_FULL("subTemplateMultiList", 0, FB_IE_SUBTEMPLATE_MULTILIST,
                    FB_IE_VARLEN, FB_IE_LIST, 0, 0,
                    FB_SUB_TMPL_MULTI_LIST, NULL),
    FB_IE_INIT_FULL("bgpValidityState", 0, 294, 1,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE | FB_IE_IDENTIFIER,
                    0, 0, FB_UINT_8, NULL),
    FB_IE_INIT_FULL("IPSecSPI", 0, 295, 4,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE | FB_IE_IDENTIFIER,
                    0, 0, FB_UINT_32, NULL),
    FB_IE_INIT_FULL("greKey", 0, 296, 4,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE | FB_IE_IDENTIFIER,
                    0, 0, FB_UINT_32, NULL),
    FB_IE_INIT_FULL("natType", 0, 297, 1,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE | FB_IE_IDENTIFIER,
                    0, 0, FB_UINT_8, NULL),
    FB_IE_INIT_FULL("initiatorPackets", 0, 298, 8,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE | FB_IE_IDENTIFIER |
                    FB_UNITS_PACKETS, 0, 0, FB_UINT_64, NULL),
    FB_IE_INIT_FULL("responderPackets", 0, 299, 8,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE | FB_IE_IDENTIFIER |
                    FB_UNITS_PACKETS, 0, 0, FB_UINT_64, NULL),
    FB_IE_INIT_FULL("observationDomainName", 0, 300, FB_IE_VARLEN,
                    FB_IE_DEFAULT, 0, 0, FB_STRING, NULL),
    FB_IE_INIT_FULL("selectionSequenceId", 0, 301, 8,
                    FB_IE_F_ENDIAN | FB_IE_IDENTIFIER, 0, 0, FB_UINT_64, NULL),
    FB_IE_INIT_FULL("selectorId", 0, 302, 8, FB_IE_F_ENDIAN | FB_IE_IDENTIFIER,
                    0, 0, FB_UINT_64, NULL),
    FB_IE_INIT_FULL("informationElementId", 0, 303, 2,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE | FB_IE_IDENTIFIER, 0,
                    0, FB_UINT_16, NULL),
    FB_IE_INIT_FULL("selectorAlgorithm", 0, 304, 2,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE | FB_IE_IDENTIFIER, 0,
                    0, FB_UINT_16, NULL),
    FB_IE_INIT_FULL("samplingPacketInterval", 0, 305, 4,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE | FB_IE_QUANTITY |
                    FB_UNITS_PACKETS, 0, 0, FB_UINT_32, NULL),
    FB_IE_INIT_FULL("samplingPacketSpace", 0, 306, 4,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE | FB_IE_QUANTITY |
                    FB_UNITS_PACKETS, 0, 0, FB_UINT_32, NULL),
    FB_IE_INIT_FULL("samplingTimeInterval", 0, 307, 4, FB_IE_F_ENDIAN |
                    FB_IE_F_REVERSIBLE |FB_IE_QUANTITY | FB_UNITS_MICROSECONDS,
                    0, 0, FB_UINT_32, NULL),
    FB_IE_INIT_FULL("samplingTimeSpace", 0, 308, 4, FB_IE_F_ENDIAN |
                    FB_IE_F_REVERSIBLE| FB_IE_QUANTITY | FB_UNITS_MICROSECONDS,
                    0, 0, FB_UINT_32, NULL),
    FB_IE_INIT_FULL("samplingSize", 0, 309, 4,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE | FB_IE_QUANTITY |
                    FB_UNITS_PACKETS, 0, 0, FB_UINT_32, NULL),
    FB_IE_INIT_FULL("samplingPopulation", 0, 310, 4,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE | FB_IE_QUANTITY |
                    FB_UNITS_PACKETS, 0, 0, FB_UINT_32, NULL),
    FB_IE_INIT_FULL("samplingProbability", 0, 311, 8,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE | FB_IE_QUANTITY, 0, 0,
                    FB_FLOAT_64, NULL),
    FB_IE_INIT_FULL("dataLinkFrameSize", 0, 312, 2,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE, 0, 0,
                    FB_UINT_16, NULL),
    FB_IE_INIT_FULL("ipHeaderPacketSection", 0, 313, FB_IE_VARLEN,
                    FB_IE_F_REVERSIBLE | FB_IE_DEFAULT, 0, 0,
                    FB_OCTET_ARRAY, NULL),
    FB_IE_INIT_FULL("ipPayloadPacketSection", 0, 314, FB_IE_VARLEN,
                    FB_IE_F_REVERSIBLE | FB_IE_DEFAULT, 0, 0,
                    FB_OCTET_ARRAY, NULL),
    FB_IE_INIT_FULL("dataLinkFrameSection", 0, 315, FB_IE_VARLEN,
                    FB_IE_F_REVERSIBLE | FB_IE_DEFAULT, 0, 0,
                    FB_OCTET_ARRAY, NULL),
    FB_IE_INIT_FULL("mplsLabelStackSection", 0, 316, FB_IE_VARLEN,
                    FB_IE_F_REVERSIBLE | FB_IE_DEFAULT, 0, 0,
                    FB_OCTET_ARRAY, NULL),
    FB_IE_INIT_FULL("mplsPayloadPacketSection", 0, 317, FB_IE_VARLEN,
                    FB_IE_F_REVERSIBLE | FB_IE_DEFAULT, 0, 0,
                    FB_OCTET_ARRAY, NULL),
    FB_IE_INIT_FULL("selectorIdTotalPktsObserved", 0, 318, 8,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE | FB_IE_TOTALCOUNTER |
                    FB_UNITS_PACKETS, 0, 0, FB_UINT_64, NULL),
    FB_IE_INIT_FULL("selectorIdTotalPktsSelected", 0, 319, 8,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE | FB_IE_TOTALCOUNTER |
                    FB_UNITS_PACKETS, 0, 0, FB_UINT_64, NULL),
    FB_IE_INIT_FULL("absoluteError", 0, 320, 8,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE | FB_IE_QUANTITY, 0, 0,
                    FB_FLOAT_64, NULL),
    FB_IE_INIT_FULL("relativeError", 0, 321, 8,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE | FB_IE_QUANTITY, 0, 0,
                    FB_FLOAT_64, NULL),
    FB_IE_INIT_FULL("observationTimeSeconds", 0, 322, 4,
                    FB_IE_F_ENDIAN | FB_IE_DEFAULT | FB_UNITS_SECONDS, 0, 0,
                    FB_DT_SEC, NULL),
    FB_IE_INIT_FULL("observationTimeMilliseconds", 0, 323, 8,
                    FB_IE_F_ENDIAN | FB_IE_DEFAULT | FB_UNITS_MILLISECONDS,
                    0, 0, FB_DT_MILSEC, NULL),
    FB_IE_INIT_FULL("observationTimeMicroseconds", 0, 324, 8,
                    FB_IE_F_ENDIAN | FB_IE_DEFAULT | FB_UNITS_MICROSECONDS,
                    0, 0, FB_DT_MICROSEC, NULL),
    FB_IE_INIT_FULL("observationTimeNanoseconds", 0, 325, 8,
                    FB_IE_F_ENDIAN | FB_IE_DEFAULT | FB_UNITS_NANOSECONDS,
                    0, 0, FB_DT_NANOSEC, NULL),
    FB_IE_INIT_FULL("digestHashValue", 0, 326, 8,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE | FB_IE_QUANTITY, 0, 0,
                    FB_UINT_64, NULL),
    FB_IE_INIT_FULL("hashIPPayloadOffset", 0, 327, 8,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE | FB_IE_QUANTITY, 0, 0,
                    FB_UINT_64, NULL),
    FB_IE_INIT_FULL("hashIPPayloadSize", 0, 328, 8,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE | FB_IE_QUANTITY, 0, 0,
                    FB_UINT_64, NULL),
    FB_IE_INIT_FULL("hashOutputRangeMin", 0, 329, 8,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE | FB_IE_QUANTITY, 0, 0,
                    FB_UINT_64, NULL),
    FB_IE_INIT_FULL("hashOutputRangeMax", 0, 330, 8,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE | FB_IE_QUANTITY, 0, 0,
                    FB_UINT_64, NULL),
    FB_IE_INIT_FULL("hashSelectedRangeMin", 0, 331, 8,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE | FB_IE_QUANTITY, 0, 0,
                    FB_UINT_64, NULL),
    FB_IE_INIT_FULL("hashSelectedRangeMax", 0, 332, 8,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE | FB_IE_QUANTITY, 0, 0,
                    FB_UINT_64, NULL),
    FB_IE_INIT_FULL("hashDigestOutput", 0, 333, 1,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE | FB_IE_DEFAULT, 0, 0,
                    FB_BOOL, NULL),
    FB_IE_INIT_FULL("hashInitialiserValue", 0, 334, 8,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE | FB_IE_QUANTITY, 0, 0,
                    FB_UINT_64, NULL),
    FB_IE_INIT_FULL("selectorName", 0, 335, FB_IE_VARLEN, FB_IE_F_NONE, 0, 0,
                    FB_STRING, NULL),
    FB_IE_INIT_FULL("upperCILimit", 0, 336, 8, FB_IE_F_ENDIAN | FB_IE_QUANTITY,
                    0, 0, FB_FLOAT_64, NULL),
    FB_IE_INIT_FULL("lowerCILimit", 0, 337, 8, FB_IE_F_ENDIAN | FB_IE_QUANTITY,
                    0, 0, FB_FLOAT_64, NULL),
    FB_IE_INIT_FULL("confidenceLevel", 0, 338, 8,
                    FB_IE_F_ENDIAN | FB_IE_QUANTITY, 0, 0, FB_FLOAT_64, NULL),
    FB_IE_INIT_FULL("informationElementDataType", 0, 339, 1, FB_IE_F_ENDIAN,
                    0, 0, FB_UINT_8, NULL),
    FB_IE_INIT_FULL("informationElementDescription", 0, 340, FB_IE_VARLEN,
                    FB_IE_F_NONE, 0, 0, FB_STRING, NULL),
    FB_IE_INIT_FULL("informationElementName", 0, 341, FB_IE_VARLEN,
                    FB_IE_F_NONE, 0, 0, FB_STRING, NULL),
    FB_IE_INIT_FULL("informationElementRangeBegin", 0, 342, 8,
                    FB_IE_F_ENDIAN | FB_IE_QUANTITY, 0, 0, FB_UINT_64, NULL),
    FB_IE_INIT_FULL("informationElementRangeEnd", 0, 343, 8,
                    FB_IE_F_ENDIAN | FB_IE_QUANTITY, 0, 0, FB_UINT_64, NULL),
    FB_IE_INIT_FULL("informationElementSemantics", 0, 344, 1,
                    FB_IE_F_ENDIAN, 0, 0, FB_UINT_8, NULL),
    FB_IE_INIT_FULL("informationElementUnits", 0, 345, 2,
                    FB_IE_F_ENDIAN, 0, 0, FB_UINT_16, NULL),
    FB_IE_INIT_FULL("privateEnterpriseNumber", 0, 346, 4,
                    FB_IE_F_ENDIAN | FB_IE_IDENTIFIER, 0, 0, FB_UINT_32, NULL),
    FB_IE_INIT_FULL("virtualStationInterfaceId", 0, 347, FB_IE_VARLEN,
                    FB_IE_DEFAULT, 0, 0, FB_OCTET_ARRAY, NULL),
    FB_IE_INIT_FULL("virtualStationInterfaceName", 0, 348, FB_IE_VARLEN,
                    FB_IE_F_NONE, 0, 0, FB_STRING, NULL),
    FB_IE_INIT_FULL("virtualStationUUID", 0, 349, FB_IE_VARLEN,
                    FB_IE_DEFAULT, 0, 0, FB_OCTET_ARRAY, NULL),
    FB_IE_INIT_FULL("virtualStationName", 0, 350, FB_IE_VARLEN,
                    FB_IE_F_NONE, 0, 0, FB_STRING, NULL),
    FB_IE_INIT_FULL("layer2SegmentId", 0, 351, 8,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE | FB_IE_IDENTIFIER,
                    0, 0, FB_UINT_64, NULL),
    FB_IE_INIT_FULL("layer2OctetDeltaCount", 0, 352, 8,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE | FB_IE_DELTACOUNTER |
                    FB_UNITS_OCTETS, 0, 0, FB_UINT_64, NULL),
    FB_IE_INIT_FULL("layer2OctetTotalCount", 0, 353, 8,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE | FB_IE_TOTALCOUNTER |
                    FB_UNITS_OCTETS, 0, 0, FB_UINT_64, NULL),
    FB_IE_INIT_FULL("ingressUnicastPacketTotalCount", 0, 354, 8,
                    FB_IE_F_ENDIAN | FB_IE_TOTALCOUNTER | FB_UNITS_PACKETS,
                    0, 0, FB_UINT_64, NULL),
    FB_IE_INIT_FULL("ingressMulticastPacketTotalCount", 0, 355, 8,
                    FB_IE_F_ENDIAN | FB_IE_TOTALCOUNTER | FB_UNITS_PACKETS,
                    0, 0, FB_UINT_64, NULL),
    FB_IE_INIT_FULL("ingressBroadcastPacketTotalCount", 0, 356, 8,
                    FB_IE_F_ENDIAN | FB_IE_TOTALCOUNTER | FB_UNITS_PACKETS,
                    0, 0, FB_UINT_64, NULL),
    FB_IE_INIT_FULL("egressUnicastPacketTotalCount", 0, 357, 8,
                    FB_IE_F_ENDIAN | FB_IE_TOTALCOUNTER | FB_UNITS_PACKETS,
                    0, 0, FB_UINT_64, NULL),
    FB_IE_INIT_FULL("egressBroadcastPacketTotalCount", 0, 358, 8,
                    FB_IE_F_ENDIAN | FB_IE_TOTALCOUNTER | FB_UNITS_PACKETS,
                    0, 0, FB_UINT_64, NULL),
    FB_IE_INIT_FULL("monitoringIntervalStartMilliSeconds", 0, 359, 8,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE |FB_UNITS_MILLISECONDS,
                    0, 0, FB_DT_MILSEC, NULL),
    FB_IE_INIT_FULL("monitoringIntervalEndMilliSeconds", 0, 360, 8,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE |FB_UNITS_MILLISECONDS,
                    0, 0, FB_DT_MILSEC, NULL),
    FB_IE_INIT_FULL("portRangeStart", 0, 361, 2,
                    FB_IE_F_ENDIAN | FB_IE_IDENTIFIER, 0, 0, FB_UINT_16, NULL),
    FB_IE_INIT_FULL("portRangeEnd", 0, 362, 2,
                    FB_IE_F_ENDIAN | FB_IE_IDENTIFIER, 0, 0, FB_UINT_16, NULL),
    FB_IE_INIT_FULL("portRangeStepSize", 0, 363, 2,
                    FB_IE_F_ENDIAN | FB_IE_IDENTIFIER, 0, 0, FB_UINT_16, NULL),
    FB_IE_INIT_FULL("portRangeNumPorts", 0, 364, 2,
                    FB_IE_F_ENDIAN | FB_IE_IDENTIFIER, 0, 0, FB_UINT_16, NULL),
    FB_IE_INIT_FULL("staMacAddress", 0, 365, 6,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE | FB_IE_IDENTIFIER,
                    0, 0, FB_MAC_ADDR, NULL),
    FB_IE_INIT_FULL("staIPv4Address", 0, 366, 4,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE | FB_IE_IDENTIFIER,
                    0, 0, FB_IP4_ADDR, NULL),
    FB_IE_INIT_FULL("wtpMacAddress", 0, 367, 4,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE | FB_IE_IDENTIFIER,
                    0, 0, FB_MAC_ADDR, NULL),
    FB_IE_INIT_FULL("ingressInterfaceType", 0, 368, 4,
                    FB_IE_F_ENDIAN | FB_IE_IDENTIFIER, 0, 0,
                    FB_UINT_32, NULL),
    FB_IE_INIT_FULL("egressInterfaceType", 0, 369, 4,
                    FB_IE_F_ENDIAN | FB_IE_IDENTIFIER, 0, 0,
                    FB_UINT_32, NULL),
    FB_IE_INIT_FULL("rtpSequenceNumber", 0, 370, 2,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE,
                    0, 0, FB_UINT_16, NULL),
    FB_IE_INIT_FULL("userName", 0, 371, FB_IE_VARLEN,
                    FB_IE_F_REVERSIBLE | FB_IE_DEFAULT,
                    0, 0, FB_STRING, NULL),
    FB_IE_INIT_FULL("applicationCategoryName", 0, 372, FB_IE_VARLEN,
                    FB_IE_DEFAULT, 0, 0, FB_STRING, NULL),
    FB_IE_INIT_FULL("applicationSubCategoryName", 0, 373, FB_IE_VARLEN,
                    FB_IE_DEFAULT, 0, 0, FB_STRING, NULL),
    FB_IE_INIT_FULL("applicationGroupName", 0, 374, FB_IE_VARLEN,
                    FB_IE_DEFAULT, 0, 0, FB_STRING, NULL),
    FB_IE_INIT_FULL("originalFlowsPresent", 0, 375, 8,
                    FB_IE_F_ENDIAN | FB_IE_DELTACOUNTER | FB_UNITS_FLOWS,
                    0, 0, FB_UINT_64, NULL),
    FB_IE_INIT_FULL("originalFlowsInitiated", 0, 376, 8,
                    FB_IE_F_ENDIAN | FB_IE_DELTACOUNTER | FB_UNITS_FLOWS,
                    0, 0, FB_UINT_64, NULL),
    FB_IE_INIT_FULL("originalFlowsCompleted", 0, 377, 8,
                    FB_IE_F_ENDIAN | FB_IE_DELTACOUNTER | FB_UNITS_FLOWS,
                    0, 0, FB_UINT_64, NULL),
    FB_IE_INIT_FULL("distinctCountOfSourceIPAddress", 0, 378, 8,
                    FB_IE_F_ENDIAN | FB_IE_TOTALCOUNTER,
                    0, 0, FB_UINT_64, NULL),
    FB_IE_INIT_FULL("distinctCountOfDestinationIPAddress", 0, 379, 8,
                    FB_IE_F_ENDIAN | FB_IE_TOTALCOUNTER,
                    0, 0, FB_UINT_64, NULL),
    FB_IE_INIT_FULL("distinctCountOfSourceIPv4Address", 0, 380, 4,
                    FB_IE_F_ENDIAN | FB_IE_TOTALCOUNTER,
                    0, 0, FB_UINT_32, NULL),
    FB_IE_INIT_FULL("distinctCountOfDestinationIPv4Address", 0, 381, 4,
                    FB_IE_F_ENDIAN | FB_IE_TOTALCOUNTER,
                    0, 0, FB_UINT_32, NULL),
    FB_IE_INIT_FULL("distinctCountOfSourceIPv6Address", 0, 382, 8,
                    FB_IE_F_ENDIAN | FB_IE_TOTALCOUNTER,
                    0, 0, FB_UINT_64, NULL),
    FB_IE_INIT_FULL("distinctCountOfDestinationIPv6Address", 0, 383, 8,
                    FB_IE_F_ENDIAN | FB_IE_TOTALCOUNTER,
                    0, 0, FB_UINT_64, NULL),
    FB_IE_INIT_FULL("valueDistributionMethod", 0, 384, 1,
                    FB_IE_F_ENDIAN | FB_IE_DEFAULT, 0, 0, FB_UINT_8, NULL),
    FB_IE_INIT_FULL("rfc3550JitterMilliseconds", 0, 385, 4,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE | FB_IE_QUANTITY,
                    0, 0, FB_DT_MILSEC, NULL),
    FB_IE_INIT_FULL("rfc3550JitterMicroseconds", 0, 386, 4,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE | FB_IE_QUANTITY,
                    0, 0, FB_DT_MICROSEC, NULL),
    FB_IE_INIT_FULL("rfc3550JitterNanoseconds", 0, 387, 4,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE | FB_IE_QUANTITY,
                    0, 0, FB_DT_NANOSEC, NULL),
    FB_IE_INIT_FULL("dot1qDEI", 0, 388, 1,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE | FB_IE_DEFAULT, 0, 0,
                    FB_BOOL, NULL),
    FB_IE_INIT_FULL("dot1qCustomerDEI",0, 389,1,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE | FB_IE_DEFAULT, 0, 0,
                    FB_BOOL, NULL),
    FB_IE_INIT_FULL("flowSelectorAlgorithm", 0, 390, 2,
                    FB_IE_F_ENDIAN | FB_IE_IDENTIFIER | FB_IE_DEFAULT, 0, 0,
                    FB_UINT_16, NULL),
    FB_IE_INIT_FULL("flowSelectedOctetDeltaCount", 0, 391, 8,
                    FB_IE_F_ENDIAN | FB_UNITS_OCTETS, 0, 0,
                    FB_UINT_64, NULL),
    FB_IE_INIT_FULL("flowSelectedPacketDeltaCount", 0, 392, 8,
                    FB_IE_F_ENDIAN | FB_UNITS_PACKETS, 0, 0,
                    FB_UINT_64, NULL),
    FB_IE_INIT_FULL("flowSelectedFlowDeltaCount", 0, 393, 8,
                    FB_IE_F_ENDIAN | FB_UNITS_FLOWS, 0, 0,
                    FB_UINT_64, NULL),
    FB_IE_INIT_FULL("selectorIDTotalFlowsObserved", 0, 394, 8,
                    FB_IE_F_ENDIAN | FB_UNITS_FLOWS, 0, 0,
                    FB_UINT_64, NULL),
    FB_IE_INIT_FULL("selectorIDTotalFlowsSelected", 0, 395, 8,
                    FB_IE_F_ENDIAN | FB_UNITS_FLOWS, 0, 0,
                    FB_UINT_64, NULL),
    FB_IE_INIT_FULL("samplingFlowInterval", 0, 396, 8,
                    FB_IE_F_ENDIAN | FB_UNITS_FLOWS, 0, 0,
                    FB_UINT_64, NULL),
    FB_IE_INIT_FULL("samplingFlowSpacing", 0, 397, 8,
                    FB_IE_F_ENDIAN | FB_UNITS_FLOWS, 0, 0,
                    FB_UINT_64, NULL),
    FB_IE_INIT_FULL("flowSamplingTimeInterval", 0, 398, 8,
                    FB_IE_F_ENDIAN | FB_UNITS_MICROSECONDS, 0, 0,
                    FB_UINT_64, NULL),
    FB_IE_INIT_FULL("flowSamplingTimeSpacing", 0, 399, 8,
                    FB_IE_F_ENDIAN | FB_UNITS_MICROSECONDS, 0, 0,
                    FB_UINT_64, NULL),
    FB_IE_INIT_FULL("hashFlowDomain", 0, 400, 2,
                    FB_IE_F_ENDIAN | FB_IE_IDENTIFIER, 0, 0,
                    FB_UINT_16, NULL),
    FB_IE_INIT_FULL("transportOctetDeltaCount", 0, 401, 8,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE | FB_IE_DELTACOUNTER
                    | FB_UNITS_OCTETS, 0, 0, FB_UINT_64, NULL),
    FB_IE_INIT_FULL("transportPacketDeltaCount", 0, 402, 8,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE | FB_IE_DELTACOUNTER
                    | FB_UNITS_OCTETS, 0, 0, FB_UINT_64, NULL),
    FB_IE_INIT_FULL("originalExporterIPv4Address", 0, 403, 4,
                    FB_IE_F_ENDIAN, 0, 0, FB_IP4_ADDR, NULL),
    FB_IE_INIT_FULL("originalExporterIPv6Address", 0, 404, 16,
                    FB_IE_F_ENDIAN, 0, 0, FB_IP6_ADDR, NULL),
    FB_IE_INIT_FULL("originalObservationDomainId", 0, 405, 4,
                    FB_IE_F_ENDIAN | FB_IE_IDENTIFIER, 0, 0, FB_UINT_32, NULL),
    FB_IE_INIT_FULL("intermediateProcessId", 0, 406, 4,
                    FB_IE_F_ENDIAN | FB_IE_IDENTIFIER, 0, 0, FB_UINT_32, NULL),
    FB_IE_INIT_FULL("ignoredDataRecordTotalCount", 0, 407, 8,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE| FB_IE_TOTALCOUNTER,
                    0, 0, FB_UINT_64, NULL),
    FB_IE_INIT_FULL("dataLinkFrameType", 0, 408, 8,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE | FB_IE_FLAGS,
                    0, 0, FB_UINT_64, NULL),
    FB_IE_INIT_FULL("sectionOffset", 0, 409, 2,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE | FB_IE_QUANTITY,
                    0, 0, FB_UINT_16, NULL),
    FB_IE_INIT_FULL("sectionExportedOctets", 0, 410, 2,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE | FB_IE_QUANTITY,
                    0, 0, FB_UINT_16, NULL),
    FB_IE_INIT_FULL("dot1qServiceInstanceTag", 0, 411, FB_IE_VARLEN,
                    FB_IE_F_REVERSIBLE | FB_IE_IDENTIFIER, 0, 0,
                    FB_OCTET_ARRAY, NULL),
    FB_IE_INIT_FULL("dot1qServiceInstanceId", 0, 412, 4,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE | FB_IE_IDENTIFIER,
                    0, 0, FB_UINT_32, NULL),
    FB_IE_INIT_FULL("dot1qServiceInstancePriority", 0, 413, 1,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE | FB_IE_IDENTIFIER,
                    0, 0, FB_UINT_8, NULL),
    FB_IE_INIT_FULL("dot1qCustomerSourceMacAddress", 0, 414, 6,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE | FB_IE_DEFAULT,
                    0, 0, FB_MAC_ADDR, NULL),
    FB_IE_INIT_FULL("dot1qCustomerDestinationMacAddress", 0,415, 6,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE | FB_IE_DEFAULT,
                    0, 0, FB_MAC_ADDR, NULL),
    /* 416 was duplicate of 352 */
    FB_IE_INIT_FULL("postLayer2OctetDeltaCount", 0, 417, 8,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE | FB_IE_DELTACOUNTER |
                    FB_UNITS_OCTETS, 0, 0, FB_UINT_64, NULL),
    FB_IE_INIT_FULL("postMCastLayer2OctetDeltaCount", 0, 418, 8,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE | FB_IE_DELTACOUNTER |
                    FB_UNITS_OCTETS, 0, 0, FB_UINT_64, NULL),
    /* 419 was duplicate of 353 */
    FB_IE_INIT_FULL("postLayer2OctetTotalCount", 0, 420, 8,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE | FB_IE_TOTALCOUNTER |
                    FB_UNITS_OCTETS, 0, 0, FB_UINT_64, NULL),
    FB_IE_INIT_FULL("postMCastLayer2OctetTotalCount", 0, 421, 8,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE | FB_IE_TOTALCOUNTER |
                    FB_UNITS_OCTETS, 0, 0, FB_UINT_64, NULL),
    FB_IE_INIT_FULL("minimumLayer2TotalLength", 0, 422, 8,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE | FB_UNITS_OCTETS,
                    0, 0, FB_UINT_64, NULL),
    FB_IE_INIT_FULL("maximumLayer2TotalLength", 0, 423, 8,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE | FB_UNITS_OCTETS,
                    0, 0, FB_UINT_64, NULL),
    FB_IE_INIT_FULL("droppedLayer2OctetDeltaCount", 0, 424, 8,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE | FB_IE_DELTACOUNTER |
                    FB_UNITS_OCTETS, 0, 0, FB_UINT_64, NULL),
    FB_IE_INIT_FULL("droppedLayer2OctetTotalCount", 0, 425, 8,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE | FB_IE_TOTALCOUNTER |
                    FB_UNITS_OCTETS, 0, 0, FB_UINT_64, NULL),
    FB_IE_INIT_FULL("ignoredLayer2OctetTotalCount", 0, 426, 8,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE | FB_IE_TOTALCOUNTER |
                    FB_UNITS_OCTETS, 0, 0, FB_UINT_64, NULL),
    FB_IE_INIT_FULL("notSentLayer2OctetTotalCount", 0, 427, 8,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE | FB_IE_TOTALCOUNTER |
                    FB_UNITS_OCTETS, 0, 0, FB_UINT_64, NULL),
    FB_IE_INIT_FULL("layer2OctetDeltaSumOfSquares", 0, 428, 8,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE | FB_IE_DELTACOUNTER |
                    FB_UNITS_OCTETS, 0, 0, FB_UINT_64, NULL),
    FB_IE_INIT_FULL("layer2OctetTotalSumOfSquares", 0, 429, 8,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE | FB_IE_TOTALCOUNTER |
                    FB_UNITS_OCTETS, 0, 0, FB_UINT_64, NULL),
    FB_IE_INIT_FULL("layer2FrameDeltaCount", 0, 430, 8,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE | FB_IE_DELTACOUNTER |
                    FB_UNITS_FRAMES, 0, 0, FB_UINT_64, NULL),
    FB_IE_INIT_FULL("layer2FrameTotalCount", 0, 431, 8,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE | FB_IE_TOTALCOUNTER |
                    FB_UNITS_FRAMES, 0, 0, FB_UINT_64, NULL),
    FB_IE_INIT_FULL("pseudoWireDestinationIPv4Address", 0, 432, 4,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE | FB_IE_DEFAULT,
                    0, 0, FB_IP4_ADDR, NULL),
    FB_IE_INIT_FULL("ignoredLayer2FrameTotalCount", 0, 433, 8,
                    FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE | FB_IE_TOTALCOUNTER |
                    FB_UNITS_FRAMES, 0, 0, FB_UINT_64, NULL),
    FB_IE_INIT_FULL("NF_F_FW_EXT_EVENT", 0, FB_CISCO_ASA_EVENT_XTRA, 2,
                    FB_IE_F_ENDIAN | FB_IE_IDENTIFIER, 0, 0, FB_UINT_16, NULL),
    FB_IE_INIT_FULL("NF_F_FW_EVENT", 0, FB_CISCO_ASA_EVENT_ID, 1,
                    FB_IE_F_ENDIAN | FB_IE_IDENTIFIER, 0, 0, FB_UINT_8, NULL),
    FB_IE_INIT_FULL("ciscoNetflowGeneric", 0, FB_CISCO_GENERIC, 8,
                    FB_IE_F_ENDIAN, 0, 0, FB_UINT_64, NULL),

    /* IEs in the CERT_PEN space */

    FB_IE_INIT_FULL("templateName", CERT_PEN, 1000, FB_IE_VARLEN,
                    FB_IE_F_NONE, 0, 0, FB_STRING, NULL),
    FB_IE_INIT_FULL("templateDescription", CERT_PEN, 1001, FB_IE_VARLEN,
                    FB_IE_F_NONE, 0, 0, FB_STRING, NULL),
    FB_IE_NULL
};

static fbInfoElementSpec_t ie_type_spec[] = {
    {"privateEnterpriseNumber",         0, 0 },
    {"informationElementId",            0, 0 },
    {"informationElementDataType",      0, 0 },
    {"informationElementSemantics",     0, 0 },
    {"informationElementUnits",         0, 0 },
    {"paddingOctets",                   6, 1 },
    {"informationElementRangeBegin",    0, 0 },
    {"informationElementRangeEnd",      0, 0 },
    {"informationElementName",          0, 0 },
    {"informationElementDescription",   0, 0 },
    FB_IESPEC_NULL
};


uint32_t            fbInfoElementHash(
    fbInfoElement_t     *ie)
{
    return ((ie->ent & 0x0000ffff) << 16) | (ie->num << 2) | (ie->midx << 4);
}

gboolean            fbInfoElementEqual(
    const fbInfoElement_t   *a,
    const fbInfoElement_t   *b)
{
    return ((a->ent == b->ent) && (a->num == b->num) && (a->midx == b->midx));
}

void                fbInfoElementDebug(
    gboolean            tmpl,
    fbInfoElement_t     *ie)
{
    if (ie->len == FB_IE_VARLEN) {
        fprintf(stderr, "VL %02x %08x:%04x %2u (%s)\n",
                    ie->flags, ie->ent, ie->num, ie->midx,
                    tmpl ? ie->ref.canon->ref.name : ie->ref.name);
    } else {
        fprintf(stderr, "%2u %02x %08x:%04x %2u (%s)\n",
                    ie->len, ie->flags, ie->ent, ie->num, ie->midx,
                    tmpl ? ie->ref.canon->ref.name : ie->ref.name);
    }
}

static void         fbInfoElementFree(
    fbInfoElement_t     *ie)
{
    g_slice_free(fbInfoElement_t, ie);
}

fbInfoModel_t       *fbInfoModelAlloc()
{
    fbInfoModel_t       *model = NULL;

    /* Create an information model */
    model = g_slice_new0(fbInfoModel_t);

    /* Allocate information element tables */
    model->ie_table = g_hash_table_new_full(
            (GHashFunc)fbInfoElementHash, (GEqualFunc)fbInfoElementEqual,
            NULL, (GDestroyNotify)fbInfoElementFree);

    model->ie_byname = g_hash_table_new(g_str_hash, g_str_equal);

    model->ie_list = g_ptr_array_new();

    /* Allocate information element name chunk */
    model->ie_names = g_string_chunk_new(64);
    model->ie_desc = g_string_chunk_new(128);

    /* Add IETF information elements to the information model */
    fbInfoModelAddElementArray(model, defaults);

    /* Return the new information model */
    return model;
}

void                fbInfoModelFree(
    fbInfoModel_t       *model)
{
    g_hash_table_destroy(model->ie_byname);
    g_string_chunk_free(model->ie_names);
    g_string_chunk_free(model->ie_desc);
    g_hash_table_destroy(model->ie_table);
    g_ptr_array_free(model->ie_list, TRUE);
    g_slice_free(fbInfoModel_t, model);
}

static void         fbInfoModelReversifyName(
    const char          *fwdname,
    char                *revname,
    size_t              revname_sz)
 {
    /* paranoid string copy */
    strncpy(revname + FB_IE_REVERSE_STRLEN, fwdname, revname_sz - FB_IE_REVERSE_STRLEN - 1);
    revname[revname_sz - 1] = (char)0;

    /* uppercase first char */
    revname[FB_IE_REVERSE_STRLEN] = toupper(revname[FB_IE_REVERSE_STRLEN]);

    /* prepend reverse */
    memcpy(revname, FB_IE_REVERSE_STR, FB_IE_REVERSE_STRLEN);
}

#define FB_IE_REVERSE_BUFSZ 256

void                fbInfoModelAddElement(
    fbInfoModel_t       *model,
    fbInfoElement_t     *ie)
{
    fbInfoElement_t     *model_ie = NULL;
    fbInfoElement_t     *found;
    char                revname[FB_IE_REVERSE_BUFSZ];

    /* Allocate a new information element */
    model_ie = g_slice_new0(fbInfoElement_t);

    /* Copy external IE to model IE */

    model_ie->ref.name = g_string_chunk_insert(model->ie_names, ie->ref.name);
    model_ie->midx = 0;
    model_ie->ent = ie->ent;
    model_ie->num = ie->num;
    model_ie->len = ie->len;
    model_ie->flags = ie->flags;
    model_ie->min = ie->min;
    model_ie->max = ie->max;
    model_ie->type = ie->type;
    if (ie->description) {
        model_ie->description = g_string_chunk_insert(model->ie_desc,
                                                      ie->description);
    }

    /* Insert model IE into tables */
    if ((found = g_hash_table_lookup(model->ie_table, model_ie))) {
        /* replace it if already exists */
        /* insert will replace but only will replace the value, not the key */
        /* since insert frees the value and the key and the value are the */
        /* same - this creates a problem.  g_hash_table_replace replaces */
        /* the key and the value */
        g_hash_table_replace(model->ie_table, model_ie, model_ie);
    } else {
        g_hash_table_insert(model->ie_table, model_ie, model_ie);
    }

    if ((found = g_hash_table_lookup(model->ie_byname, model_ie->ref.name))) {
        g_ptr_array_remove(model->ie_list, found);
    }
    g_ptr_array_add(model->ie_list, model_ie);
    g_hash_table_insert(model->ie_byname, (char *)model_ie->ref.name,model_ie);

    /* Short circuit if not reversible or not IANA-managed */
    if (!(ie->flags & FB_IE_F_REVERSIBLE)) {
        return;
    }

    /* Allocate a new reverse information element */
    model_ie = g_slice_new0(fbInfoElement_t);

    /* Generate reverse name */
    fbInfoModelReversifyName(ie->ref.name, revname, sizeof(revname));

    /* Copy external IE to reverse model IE */
    model_ie->ref.name = g_string_chunk_insert(model->ie_names, revname);
    model_ie->midx = 0;
    model_ie->ent = ie->ent ? ie->ent : FB_IE_PEN_REVERSE;
    model_ie->num = ie->ent ? ie->num | FB_IE_VENDOR_BIT_REVERSE : ie->num;
    model_ie->len = ie->len;
    model_ie->flags = ie->flags;
    model_ie->min = ie->min;
    model_ie->max = ie->max;
    model_ie->type = ie->type;

    /* Insert model IE into tables */
    g_hash_table_insert(model->ie_table, model_ie, model_ie);
    if ((found = g_hash_table_lookup(model->ie_byname, model_ie->ref.name))) {
        g_ptr_array_remove(model->ie_list, found);
    }
    g_ptr_array_add(model->ie_list, model_ie);
    g_hash_table_insert(model->ie_byname, (char *)model_ie->ref.name,model_ie);
}

void                fbInfoModelAddElementArray(
    fbInfoModel_t       *model,
    fbInfoElement_t     *ie)
{
    for (; ie->ref.name; ie++) fbInfoModelAddElement(model, ie);
}

const fbInfoElement_t     *fbInfoModelGetElement(
    fbInfoModel_t       *model,
    fbInfoElement_t     *ex_ie)
{
    return g_hash_table_lookup(model->ie_table, ex_ie);
}

gboolean            fbInfoElementCopyToTemplate(
    fbInfoModel_t       *model,
    fbInfoElement_t     *ex_ie,
    fbInfoElement_t     *tmpl_ie)
{
    const fbInfoElement_t     *model_ie = NULL;

    /* Look up information element in the model */
    model_ie = fbInfoModelGetElement(model, ex_ie);
    if (!model_ie) {
        /* Information element not in model. Note it's alien and add it. */
        ex_ie->ref.name = g_string_chunk_insert(model->ie_names,
                                                "_alienInformationElement");
        ex_ie->flags |= FB_IE_F_ALIEN;
        fbInfoModelAddElement(model, ex_ie);
        model_ie = fbInfoModelGetElement(model, ex_ie);
        g_assert(model_ie);
    }

    /* Refer to canonical IE in the model */
    tmpl_ie->ref.canon = model_ie;

    /* Copy model IE to template IE */
    tmpl_ie->midx = 0;
    tmpl_ie->ent = model_ie->ent;
    tmpl_ie->num = model_ie->num;
    tmpl_ie->len = ex_ie->len;
    tmpl_ie->flags = model_ie->flags;
    tmpl_ie->type = model_ie->type;
    tmpl_ie->min = model_ie->min;
    tmpl_ie->max = model_ie->max;
    tmpl_ie->description = model_ie->description;

    /* All done */
    return TRUE;
}

const fbInfoElement_t     *fbInfoModelGetElementByName(
    fbInfoModel_t       *model,
    const char          *name)
{
    return g_hash_table_lookup(model->ie_byname, name);
}

const fbInfoElement_t    *fbInfoModelGetElementByID(
    fbInfoModel_t      *model,
    uint16_t           id,
    uint32_t           ent)
{

    fbInfoElement_t tempElement;

    tempElement.midx = 0;
    tempElement.ent = ent;
    tempElement.num = id;

    return fbInfoModelGetElement(model, &tempElement);
}

gboolean            fbInfoElementCopyToTemplateByName(
    fbInfoModel_t       *model,
    const char          *name,
    uint16_t            len_override,
    fbInfoElement_t     *tmpl_ie)
{
    const fbInfoElement_t     *model_ie = NULL;

    /* Look up information element in the model */
    model_ie = fbInfoModelGetElementByName(model, name);
    if (!model_ie) return FALSE;

    /* Refer to canonical IE in the model */
    tmpl_ie->ref.canon = model_ie;

    /* Copy model IE to template IE */
    tmpl_ie->midx = 0;
    tmpl_ie->ent = model_ie->ent;
    tmpl_ie->num = model_ie->num;
    tmpl_ie->len = len_override ? len_override : model_ie->len;
    tmpl_ie->flags = model_ie->flags;
    tmpl_ie->type = model_ie->type;
    tmpl_ie->min = model_ie->min;
    tmpl_ie->max = model_ie->max;
    tmpl_ie->description = model_ie->description;

    /* All done */
    return TRUE;
}

fbTemplate_t *fbInfoElementAllocTypeTemplate(
    fbInfoModel_t          *model,
    GError                 **err)
{
    fbTemplate_t *tmpl = NULL;

    tmpl = fbTemplateAlloc(model);

    if (!fbTemplateAppendSpecArray(tmpl, ie_type_spec, 0xffffffff, err))
        return NULL;

    fbTemplateSetOptionsScope(tmpl, 2);

    return tmpl;

}

gboolean fbInfoElementWriteOptionsRecord(
    fBuf_t                  *fbuf,
    const fbInfoElement_t   *model_ie,
    uint16_t                tid,
    GError                  **err)
{

    fbInfoElementOptRec_t   rec;

    if (model_ie == NULL) {
        g_set_error(err, FB_ERROR_DOMAIN, FB_ERROR_NOELEMENT,
                    "Invalid [NULL] Information Element");
        return FALSE;
    }

    rec.ie_range_begin = model_ie->min;
    rec.ie_range_end = model_ie->max;
    rec.ie_pen = model_ie->ent;
    rec.ie_units = FB_IE_UNITS(model_ie->flags);
    rec.ie_semantic = FB_IE_SEMANTIC(model_ie->flags);
    rec.ie_id = model_ie->num;
    rec.ie_type = model_ie->type;
    memset(rec.padding, 0, sizeof(rec.padding));
    rec.ie_name.buf = (uint8_t *)model_ie->ref.name;
    rec.ie_name.len = strlen(model_ie->ref.name);
    rec.ie_desc.buf = (uint8_t *)model_ie->description;
    if (model_ie->description) {
        rec.ie_desc.len = strlen(model_ie->description);
    } else {
        rec.ie_desc.len = 0;
    }

    if (!fBufSetExportTemplate(fbuf, tid, err)) {
        return FALSE;
    }

    if (!fBufSetInternalTemplate(fbuf, tid, err)) {
        return FALSE;
    }

    if (!fBufAppend(fbuf, (uint8_t *)&rec, sizeof(rec), err)) {
        return FALSE;
    }

    return TRUE;
}

gboolean fbInfoElementAddOptRecElement(
    fbInfoModel_t           *model,
    fbInfoElementOptRec_t   *rec)
{
    fbInfoElement_t     ie;
    char                name[500];
    char                description[4096];

    if (rec->ie_pen != 0) {

        ie.min = rec->ie_range_begin;
        ie.max = rec->ie_range_end;
        ie.ent = rec->ie_pen;
        ie.num = rec->ie_id;
        ie.type = rec->ie_type;
        strncpy(name, (char *)rec->ie_name.buf, rec->ie_name.len);
        name[rec->ie_name.len] = '\0';
        ie.ref.name = name;
        strncpy(description, (char *)rec->ie_desc.buf, rec->ie_desc.len);
        description[rec->ie_desc.len] = '\0';
        ie.description = description;
        ie.flags = 0;
        ie.flags |= rec->ie_units << 16;
        ie.flags |= rec->ie_semantic << 8;

        /* length is inferred from data type */
        switch (ie.type) {
          case FB_OCTET_ARRAY:
          case FB_STRING:
          case FB_BASIC_LIST:
          case FB_SUB_TMPL_LIST:
          case FB_SUB_TMPL_MULTI_LIST:
            ie.len = FB_IE_VARLEN;
            break;
          case FB_UINT_8:
          case FB_INT_8:
          case FB_BOOL:
            ie.len = 1;
            break;
          case FB_UINT_16:
          case FB_INT_16:
            ie.len = 2;
            break;
          case FB_UINT_32:
          case FB_INT_32:
          case FB_DT_SEC:
          case FB_FLOAT_32:
          case FB_IP4_ADDR:
            ie.len = 4;
            break;
          case FB_MAC_ADDR:
            ie.len = 6;
            break;
          case FB_UINT_64:
          case FB_INT_64:
          case FB_DT_MILSEC:
          case FB_DT_MICROSEC:
          case FB_DT_NANOSEC:
          case FB_FLOAT_64:
            ie.len = 8;
            break;
          case FB_IP6_ADDR:
            ie.len = 16;
            break;
          default:
            g_warning("Adding element %s with invalid data type [%d]", name,
                      rec->ie_type);
            ie.len = FB_IE_VARLEN;
        }

        fbInfoModelAddElement(model, &ie);
        return TRUE;
    }

    return FALSE;
}

gboolean fbInfoModelTypeInfoRecord(
    fbTemplate_t            *tmpl)
{
    /* ignore padding. */
    if (fbTemplateContainsAllFlaggedElementsByName(tmpl, ie_type_spec, 0)) {
        return TRUE;
    }

    return FALSE;
}

guint fbInfoModelCountElements(
    const fbInfoModel_t *model)
{
    return model->ie_list->len;
}

void fbInfoModelIterInit(
    fbInfoModelIter_t   *iter,
    const fbInfoModel_t *model)
{
    iter->model = model;
    iter->index = 0;
}

const fbInfoElement_t *fbInfoModelIterNext(
    fbInfoModelIter_t *iter)
{
    if (iter->index >= iter->model->ie_list->len) {
        return NULL;
    }
    return g_ptr_array_index(iter->model->ie_list, iter->index++);
}

const fbInfoElement_t     *fbInfoModelAddAlienElement(
    fbInfoModel_t       *model,
    fbInfoElement_t     *ex_ie)
{
    const fbInfoElement_t     *model_ie = NULL;

    if (ex_ie == NULL) {
        return NULL;
    }
    /* Information element not in model. Note it's alien and add it. */
    ex_ie->ref.name = g_string_chunk_insert(model->ie_names,
                                            "_alienInformationElement");
    ex_ie->flags |= FB_IE_F_ALIEN;
    fbInfoModelAddElement(model, ex_ie);
    model_ie = fbInfoModelGetElement(model, ex_ie);
    g_assert(model_ie);

    return model_ie;
}
