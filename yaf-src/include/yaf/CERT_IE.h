/*
 *
 ** @file CERT_IE.h
 ** Definition of the CERT "standard" information elements extension to
 ** the IETF standard RFC 5102 information elements
 **
 ** ------------------------------------------------------------------------
 ** Copyright (C) 2009-2015 Carnegie Mellon University. All Rights Reserved.
 ** ------------------------------------------------------------------------
 ** Authors: Brian Trammell, Chris Inacio, Emily Ecoff <ecoff@cert.org>
 ** <netsa-help@cert.org>
 ** ------------------------------------------------------------------------
 ** Use of the YAF system and related source code is subject to the terms
 ** of the following licenses:
 **
 ** GNU Public License (GPL) Rights pursuant to Version 2, June 1991
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
 ** ------------------------------------------------------------------------
 */


#ifndef CERT_IE_H_
#define CERT_IE_H_

#define NONE FB_IE_F_NONE
#define ER FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE

/**
 * IPFIX information elements in 6871/CERT_PEN space for YAF
 * these elements are included within the capabilities of YAF
 * primarily, but may be used within other CERT software as
 * well
 */

static fbInfoElement_t yaf_info_elements[] = {
    FB_IE_INIT_FULL("initialTCPFlags", CERT_PEN, 14, 1, ER | FB_IE_FLAGS,
                    0, 0, FB_UINT_8, NULL),
    FB_IE_INIT_FULL("unionTCPFlags", CERT_PEN, 15, 1, ER | FB_IE_FLAGS, 0, 0,
                    FB_UINT_8, NULL),
    FB_IE_INIT_FULL("payload", CERT_PEN, 18, FB_IE_VARLEN, FB_IE_F_REVERSIBLE,
                    0, 0, FB_OCTET_ARRAY, NULL),
    FB_IE_INIT_FULL("reverseFlowDeltaMilliseconds", CERT_PEN, 21, 4,
                    FB_IE_F_ENDIAN | FB_IE_QUANTITY | FB_UNITS_MILLISECONDS,
                    0, 0, FB_UINT_32, NULL),
    FB_IE_INIT_FULL("silkAppLabel", CERT_PEN, 33, 2,
               FB_IE_F_ENDIAN | FB_IE_IDENTIFIER, 0, 0, FB_UINT_16, NULL),
    FB_IE_INIT_FULL("payloadEntropy", CERT_PEN, 35, 1, ER, 0, 0,
                    FB_UINT_8, NULL),
    FB_IE_INIT_FULL("osName", CERT_PEN, 36, FB_IE_VARLEN, FB_IE_F_REVERSIBLE,
                    0, 0, FB_STRING, NULL),
    FB_IE_INIT_FULL("osVersion",CERT_PEN, 37, FB_IE_VARLEN, FB_IE_F_REVERSIBLE,
                    0, 0, FB_STRING, NULL),
    FB_IE_INIT_FULL("firstPacketBanner", CERT_PEN, 38, FB_IE_VARLEN,
                    FB_IE_F_REVERSIBLE, 0, 0, FB_OCTET_ARRAY, NULL),
    FB_IE_INIT_FULL("secondPacketBanner", CERT_PEN, 39, FB_IE_VARLEN,
                    FB_IE_F_REVERSIBLE, 0, 0, FB_OCTET_ARRAY, NULL),
    FB_IE_INIT_FULL("flowAttributes", CERT_PEN, 40, 2, ER | FB_IE_FLAGS, 0, 0,
                    FB_UINT_16, NULL),
    FB_IE_INIT_FULL("osFingerPrint",CERT_PEN, 107, FB_IE_VARLEN,
                    FB_IE_F_REVERSIBLE, 0, 0, FB_STRING, NULL),
    FB_IE_INIT_FULL("expiredFragmentCount", CERT_PEN, 100, 4,
                    FB_IE_F_ENDIAN | FB_IE_TOTALCOUNTER | FB_UNITS_PACKETS,
                    0, 0, FB_UINT_32, NULL),
    FB_IE_INIT_FULL("assembledFragmentCount", CERT_PEN, 101, 4,
                    FB_IE_F_ENDIAN | FB_IE_TOTALCOUNTER | FB_UNITS_PACKETS,
                    0, 0, FB_UINT_32, NULL),
    FB_IE_INIT_FULL("meanFlowRate", CERT_PEN, 102, 4,
                    FB_IE_F_ENDIAN | FB_UNITS_FLOWS, 0, 0, FB_UINT_32, NULL),
    FB_IE_INIT_FULL("meanPacketRate", CERT_PEN, 103, 4,
                    FB_IE_F_ENDIAN | FB_UNITS_PACKETS, 0, 0, FB_UINT_32, NULL),
    FB_IE_INIT_FULL("flowTableFlushEventCount", CERT_PEN, 104, 4,
                    FB_IE_F_ENDIAN | FB_UNITS_FLOWS | FB_IE_TOTALCOUNTER,
                    0, 0, FB_UINT_32, NULL),
    FB_IE_INIT_FULL("flowTablePeakCount", CERT_PEN, 105, 4,
                    FB_IE_F_ENDIAN | FB_UNITS_FLOWS, 0, 0, FB_UINT_32, NULL),
    FB_IE_INIT_FULL("yafFlowKeyHash", CERT_PEN, 106, 4,
                    FB_IE_F_ENDIAN | FB_IE_IDENTIFIER, 0, 0, FB_UINT_32, NULL),
    FB_IE_INIT_FULL("mptcpInitialDataSequenceNumber", CERT_PEN, 289, 8,
                    FB_IE_F_ENDIAN, 0, 0, FB_UINT_64, NULL),
    FB_IE_INIT_FULL("mptcpReceiverToken", CERT_PEN, 290, 4,
                    FB_IE_F_ENDIAN | FB_IE_IDENTIFIER, 0, 0, FB_UINT_32, NULL),
    FB_IE_INIT_FULL("mptcpMaximumSegmentSize", CERT_PEN, 291, 2,
                    FB_IE_F_ENDIAN , 0, 0, FB_UINT_16, NULL),
    FB_IE_INIT_FULL("mptcpAddressID", CERT_PEN, 292, 1,
                    FB_IE_F_ENDIAN | FB_IE_IDENTIFIER, 0, 0, FB_UINT_8, NULL),
    FB_IE_INIT_FULL("mptcpFlags", CERT_PEN, 293, 1,
                    FB_IE_F_ENDIAN | FB_IE_FLAGS, 0, 0, FB_UINT_8, NULL),
    /* flow stats */
    FB_IE_INIT_FULL("smallPacketCount", CERT_PEN, 500, 4,
                    ER | FB_IE_TOTALCOUNTER | FB_UNITS_PACKETS, 0, 0,
                    FB_UINT_32, NULL),
    FB_IE_INIT_FULL("nonEmptyPacketCount", CERT_PEN, 501, 4,
                    ER | FB_IE_TOTALCOUNTER | FB_UNITS_PACKETS, 0, 0,
                    FB_UINT_32, NULL),
    FB_IE_INIT_FULL("dataByteCount", CERT_PEN, 502, 8,
                    ER | FB_IE_TOTALCOUNTER | FB_UNITS_OCTETS, 0, 0,
                    FB_UINT_64,NULL),
    FB_IE_INIT_FULL("averageInterarrivalTime", CERT_PEN, 503, 8,
                    ER | FB_UNITS_MILLISECONDS, 0, 0, FB_UINT_64, NULL),
    FB_IE_INIT_FULL("standardDeviationInterarrivalTime", CERT_PEN, 504, 8,
                    ER | FB_UNITS_MILLISECONDS, 0, 0, FB_UINT_64, NULL),
    FB_IE_INIT_FULL("firstNonEmptyPacketSize", CERT_PEN, 505, 2,
                    ER | FB_IE_QUANTITY | FB_UNITS_OCTETS, 0, 0,
                    FB_UINT_16, NULL),
    FB_IE_INIT_FULL("maxPacketSize", CERT_PEN, 506, 2,
                    ER | FB_IE_QUANTITY | FB_UNITS_OCTETS, 0, 0,
                    FB_UINT_16, NULL),
    FB_IE_INIT_FULL("firstEightNonEmptyPacketDirections", CERT_PEN, 507, 1,
                    ER | FB_IE_FLAGS, 0, 0, FB_UINT_8, NULL),
    FB_IE_INIT_FULL("standardDeviationPayloadLength", CERT_PEN, 508, 2,
                    ER | FB_UNITS_OCTETS, 0, 0, FB_UINT_16, NULL),
    FB_IE_INIT_FULL("tcpUrgentCount", CERT_PEN, 509, 4,
                    ER | FB_IE_TOTALCOUNTER | FB_UNITS_PACKETS, 0, 0,
                    FB_UINT_32, NULL),
    FB_IE_INIT_FULL("largePacketCount", CERT_PEN, 510, 4,
                    ER | FB_IE_TOTALCOUNTER | FB_UNITS_PACKETS, 0, 0,
                    FB_UINT_32, NULL),
    FB_IE_NULL
};

/* IE numbers 110-299 */

#if YAF_ENABLE_HOOKS

static fbInfoElement_t yaf_dpi_info_elements[] = {
    FB_IE_INIT_FULL("httpServerString", CERT_PEN, 110, FB_IE_VARLEN, NONE,
                    0, 0, FB_STRING, NULL),
    FB_IE_INIT_FULL("httpUserAgent", CERT_PEN, 111, FB_IE_VARLEN, NONE,
                    0, 0, FB_STRING, NULL),
    FB_IE_INIT_FULL("httpGet", CERT_PEN, 112, FB_IE_VARLEN, NONE,
                    0, 0, FB_STRING, NULL),
    FB_IE_INIT_FULL("httpConnection", CERT_PEN, 113, FB_IE_VARLEN, NONE,
                    0, 0, FB_STRING, NULL),
    FB_IE_INIT_FULL("httpVersion", CERT_PEN, 114, FB_IE_VARLEN, NONE,
                    0, 0, FB_STRING, NULL),
    FB_IE_INIT_FULL("httpReferer", CERT_PEN, 115, FB_IE_VARLEN, NONE,
                    0, 0, FB_STRING, NULL),
    FB_IE_INIT_FULL("httpLocation", CERT_PEN, 116, FB_IE_VARLEN, NONE,
                    0, 0, FB_STRING, NULL),
    FB_IE_INIT_FULL("httpHost", CERT_PEN, 117, FB_IE_VARLEN, NONE,
                    0, 0, FB_STRING, NULL),
    FB_IE_INIT_FULL("httpContentLength", CERT_PEN, 118, FB_IE_VARLEN, NONE,
                    0, 0, FB_STRING, NULL),
    FB_IE_INIT_FULL("httpAge", CERT_PEN, 119, FB_IE_VARLEN, NONE,
                    0, 0, FB_STRING, NULL),
    FB_IE_INIT_FULL("httpAccept", CERT_PEN, 120, FB_IE_VARLEN, NONE,
                    0, 0, FB_STRING, NULL),
    FB_IE_INIT_FULL("httpAcceptLanguage", CERT_PEN, 121, FB_IE_VARLEN, NONE,
                    0, 0, FB_STRING, NULL),
    FB_IE_INIT_FULL("httpContentType", CERT_PEN, 122, FB_IE_VARLEN, NONE,
                    0, 0, FB_STRING, NULL),
    FB_IE_INIT_FULL("httpResponse", CERT_PEN, 123, FB_IE_VARLEN, NONE,
                    0, 0, FB_STRING, NULL),
    FB_IE_INIT_FULL("httpCookie", CERT_PEN, 220, FB_IE_VARLEN, NONE,
                    0, 0, FB_STRING, NULL),
    FB_IE_INIT_FULL("httpSetCookie", CERT_PEN, 221, FB_IE_VARLEN, NONE,
                    0, 0, FB_STRING, NULL),
    FB_IE_INIT_FULL("httpAuthorization", CERT_PEN, 252, FB_IE_VARLEN, NONE,
                    0, 0, FB_STRING, NULL),
    FB_IE_INIT_FULL("httpVia", CERT_PEN, 253, FB_IE_VARLEN, NONE,
                    0, 0, FB_STRING, NULL),
    FB_IE_INIT_FULL("httpX-Forwarded-For", CERT_PEN, 254, FB_IE_VARLEN, NONE,
                    0, 0, FB_STRING, NULL),
    FB_IE_INIT_FULL("httpRefresh", CERT_PEN, 256, FB_IE_VARLEN, NONE,
                    0, 0, FB_STRING, NULL),
    /* http mobile fields - turned off by default */
    FB_IE_INIT_FULL("httpIMEI", CERT_PEN, 257, FB_IE_VARLEN, NONE,
                    0, 0, FB_STRING, NULL),
    FB_IE_INIT_FULL("httpIMSI", CERT_PEN, 258, FB_IE_VARLEN, NONE,
                    0, 0, FB_STRING, NULL),
    FB_IE_INIT_FULL("httpMSISDN", CERT_PEN, 259, FB_IE_VARLEN, NONE,
                    0, 0, FB_STRING, NULL),
    FB_IE_INIT_FULL("httpSubscriber", CERT_PEN, 260, FB_IE_VARLEN, NONE,
                    0, 0, FB_STRING, NULL),
    /* http extra fields - turned off by default */
    FB_IE_INIT_FULL("httpExpires", CERT_PEN, 255, FB_IE_VARLEN, NONE,
                    0, 0, FB_STRING, NULL),
    FB_IE_INIT_FULL("httpAcceptCharset", CERT_PEN, 261, FB_IE_VARLEN, NONE,
                    0, 0, FB_STRING, NULL),
    FB_IE_INIT_FULL("httpAcceptEncoding", CERT_PEN, 262, FB_IE_VARLEN, NONE,
                    0, 0, FB_STRING, NULL),
    FB_IE_INIT_FULL("httpAllow", CERT_PEN, 263, FB_IE_VARLEN, NONE,
                    0, 0, FB_STRING, NULL),
    FB_IE_INIT_FULL("httpDate", CERT_PEN, 264, FB_IE_VARLEN, NONE,
                    0, 0, FB_STRING, NULL),
    FB_IE_INIT_FULL("httpExpect", CERT_PEN, 265, FB_IE_VARLEN, NONE,
                    0, 0, FB_STRING, NULL),
    FB_IE_INIT_FULL("httpFrom", CERT_PEN, 266, FB_IE_VARLEN, NONE,
                    0, 0, FB_STRING, NULL),
    FB_IE_INIT_FULL("httpProxyAuthentication", CERT_PEN, 267, FB_IE_VARLEN,
                    NONE, 0, 0, FB_STRING, NULL),
    FB_IE_INIT_FULL("httpUpgrade", CERT_PEN, 268, FB_IE_VARLEN, NONE,
                    0, 0, FB_STRING, NULL),
    FB_IE_INIT_FULL("httpWarning", CERT_PEN, 269, FB_IE_VARLEN, NONE,
                    0, 0, FB_STRING, NULL),
    FB_IE_INIT_FULL("httpDNT", CERT_PEN, 270, FB_IE_VARLEN, NONE,
                    0, 0, FB_STRING, NULL),
    FB_IE_INIT_FULL("httpX-Forwarded-Proto", CERT_PEN, 271, FB_IE_VARLEN,
                    NONE, 0, 0, FB_STRING, NULL),
    FB_IE_INIT_FULL("httpX-Forwarded-Host", CERT_PEN, 272, FB_IE_VARLEN, NONE,
                    0, 0, FB_STRING, NULL),
    FB_IE_INIT_FULL("httpX-Forwarded-Server", CERT_PEN, 273, FB_IE_VARLEN,
                    NONE, 0, 0, FB_STRING, NULL),
    FB_IE_INIT_FULL("httpX-DeviceID", CERT_PEN, 274, FB_IE_VARLEN, NONE,
                    0, 0, FB_STRING, NULL),
    FB_IE_INIT_FULL("httpX-Profile", CERT_PEN, 275, FB_IE_VARLEN, NONE,
                    0, 0, FB_STRING, NULL),
    FB_IE_INIT_FULL("httpLastModified", CERT_PEN, 276, FB_IE_VARLEN, NONE,
                    0, 0, FB_STRING, NULL),
    FB_IE_INIT_FULL("httpContentEncoding", CERT_PEN, 277, FB_IE_VARLEN, NONE,
                    0, 0, FB_STRING, NULL),
    FB_IE_INIT_FULL("httpContentLanguage", CERT_PEN, 278, FB_IE_VARLEN, NONE,
                    0, 0, FB_STRING, NULL),
    FB_IE_INIT_FULL("httpContentLocation", CERT_PEN, 279, FB_IE_VARLEN, NONE,
                    0, 0, FB_STRING, NULL),
    FB_IE_INIT_FULL("httpX-UA-Compatible", CERT_PEN, 280, FB_IE_VARLEN, NONE,
                    0, 0, FB_STRING, NULL),
    /* POP3 IEs */
    FB_IE_INIT_FULL("pop3TextMessage", CERT_PEN, 124, FB_IE_VARLEN, NONE,
                    0, 0, FB_STRING, NULL),
    /* IRC IEs */
    FB_IE_INIT_FULL("ircTextMessage", CERT_PEN, 125, FB_IE_VARLEN, NONE,
                    0, 0, FB_STRING, NULL),
    /* TFTP IEs */
    FB_IE_INIT_FULL("tftpFilename", CERT_PEN, 126, FB_IE_VARLEN, NONE,
                    0, 0, FB_STRING, NULL),
    FB_IE_INIT_FULL("tftpMode", CERT_PEN, 127, FB_IE_VARLEN, NONE,
                    0, 0, FB_STRING, NULL),
    /* SLP IEs */
    FB_IE_INIT_FULL("slpVersion", CERT_PEN, 128, 1, FB_IE_F_ENDIAN,
                    0, 0, FB_UINT_8, NULL),
    FB_IE_INIT_FULL("slpMessageType", CERT_PEN, 129, 1, FB_IE_F_ENDIAN,
                    0, 0, FB_UINT_8, NULL),
    FB_IE_INIT_FULL("slpString", CERT_PEN, 130, FB_IE_VARLEN, NONE,
                    0, 0, FB_STRING, NULL),
    /* FTP IEs */
    FB_IE_INIT_FULL("ftpReturn", CERT_PEN, 131, FB_IE_VARLEN, NONE,
                    0, 0, FB_STRING, NULL),
    FB_IE_INIT_FULL("ftpUser", CERT_PEN, 132, FB_IE_VARLEN, NONE,
                    0, 0, FB_STRING, NULL),
    FB_IE_INIT_FULL("ftpPass", CERT_PEN,133, FB_IE_VARLEN, NONE,
                    0, 0, FB_STRING, NULL),
    FB_IE_INIT_FULL("ftpType", CERT_PEN,134, FB_IE_VARLEN, NONE,
                    0, 0, FB_STRING, NULL),
    FB_IE_INIT_FULL("ftpRespCode", CERT_PEN,135, FB_IE_VARLEN, NONE,
                    0, 0, FB_STRING, NULL),
    /* IMAP IEs */
    FB_IE_INIT_FULL("imapCapability", CERT_PEN, 136, FB_IE_VARLEN, NONE,
                    0, 0, FB_STRING, NULL),
    FB_IE_INIT_FULL("imapLogin", CERT_PEN, 137, FB_IE_VARLEN, NONE,
                    0, 0, FB_STRING, NULL),
    FB_IE_INIT_FULL("imapStartTLS", CERT_PEN, 138, FB_IE_VARLEN, NONE,
                    0, 0, FB_STRING, NULL),
    FB_IE_INIT_FULL("imapAuthenticate", CERT_PEN, 139, FB_IE_VARLEN, NONE,
                    0, 0, FB_STRING, NULL),
    FB_IE_INIT_FULL("imapCommand", CERT_PEN, 140, FB_IE_VARLEN, NONE,
                    0, 0, FB_STRING, NULL),
    FB_IE_INIT_FULL("imapExists", CERT_PEN, 141, FB_IE_VARLEN, NONE,
                    0, 0, FB_STRING, NULL),
    FB_IE_INIT_FULL("imapRecent", CERT_PEN, 142, FB_IE_VARLEN, NONE,
                    0, 0, FB_STRING, NULL),
    /* rtsp IEs */
    FB_IE_INIT_FULL("rtspURL", CERT_PEN, 143, FB_IE_VARLEN, NONE,
                    0, 0, FB_STRING, NULL),
    FB_IE_INIT_FULL("rtspVersion", CERT_PEN, 144, FB_IE_VARLEN, NONE,
                    0, 0, FB_STRING, NULL),
    FB_IE_INIT_FULL("rtspReturnCode", CERT_PEN, 145, FB_IE_VARLEN, NONE,
                    0, 0, FB_STRING, NULL),
    FB_IE_INIT_FULL("rtspContentLength", CERT_PEN, 146, FB_IE_VARLEN, NONE,
                    0, 0, FB_STRING, NULL),
    FB_IE_INIT_FULL("rtspCommand", CERT_PEN, 147, FB_IE_VARLEN, NONE,
                    0, 0, FB_STRING, NULL),
    FB_IE_INIT_FULL("rtspContentType", CERT_PEN, 148, FB_IE_VARLEN, NONE,
                    0, 0, FB_STRING, NULL),
    FB_IE_INIT_FULL("rtspTransport", CERT_PEN, 149, FB_IE_VARLEN, NONE,
                    0, 0, FB_STRING, NULL),
    FB_IE_INIT_FULL("rtspCSeq", CERT_PEN, 150, FB_IE_VARLEN, NONE,
                    0, 0, FB_STRING, NULL),
    FB_IE_INIT_FULL("rtspLocation", CERT_PEN, 151, FB_IE_VARLEN, NONE,
                    0, 0, FB_STRING, NULL),
    FB_IE_INIT_FULL("rtspPacketsReceived", CERT_PEN, 152, FB_IE_VARLEN, NONE,
                    0, 0, FB_STRING, NULL),
    FB_IE_INIT_FULL("rtspUserAgent", CERT_PEN, 153, FB_IE_VARLEN, NONE,
                    0, 0, FB_STRING, NULL),
    FB_IE_INIT_FULL("rtspJitter", CERT_PEN, 154, FB_IE_VARLEN, NONE,
                    0, 0, FB_STRING, NULL),
    /* sip IEs */
    FB_IE_INIT_FULL("sipInvite", CERT_PEN, 155, FB_IE_VARLEN, NONE,
                    0, 0, FB_STRING, NULL),
    FB_IE_INIT_FULL("sipCommand", CERT_PEN, 156, FB_IE_VARLEN, NONE,
                    0, 0, FB_STRING, NULL),
    FB_IE_INIT_FULL("sipVia", CERT_PEN, 157, FB_IE_VARLEN, NONE,
                    0, 0, FB_STRING, NULL),
    FB_IE_INIT_FULL("sipMaxForwards", CERT_PEN, 158, FB_IE_VARLEN, NONE,
                    0, 0, FB_STRING, NULL),
    FB_IE_INIT_FULL("sipAddress", CERT_PEN, 159, FB_IE_VARLEN, NONE,
                    0, 0, FB_STRING, NULL),
    FB_IE_INIT_FULL("sipContentLength", CERT_PEN, 160, FB_IE_VARLEN, NONE,
                    0, 0, FB_STRING, NULL),
    FB_IE_INIT_FULL("sipUserAgent", CERT_PEN, 161, FB_IE_VARLEN, NONE,
                    0, 0, FB_STRING, NULL),
    /* smtp IEs */
    FB_IE_INIT_FULL("smtpHello", CERT_PEN, 162, FB_IE_VARLEN, NONE,
                    0, 0, FB_STRING, NULL),
    FB_IE_INIT_FULL("smtpFrom", CERT_PEN, 163, FB_IE_VARLEN, NONE,
                    0, 0, FB_STRING, NULL),
    FB_IE_INIT_FULL("smtpTo", CERT_PEN, 164, FB_IE_VARLEN, NONE,
                    0, 0, FB_STRING, NULL),
    FB_IE_INIT_FULL("smtpContentType", CERT_PEN, 165, FB_IE_VARLEN, NONE,
                    0, 0, FB_STRING, NULL),
    FB_IE_INIT_FULL("smtpSubject", CERT_PEN, 166, FB_IE_VARLEN, NONE,
                    0, 0, FB_STRING, NULL),
    FB_IE_INIT_FULL("smtpFilename", CERT_PEN, 167, FB_IE_VARLEN, NONE,
                    0, 0, FB_STRING, NULL),
    FB_IE_INIT_FULL("smtpContentDisposition", CERT_PEN, 168, FB_IE_VARLEN,
                    NONE, 0, 0, FB_STRING, NULL),
    FB_IE_INIT_FULL("smtpResponse", CERT_PEN, 169, FB_IE_VARLEN, NONE,
                    0, 0, FB_STRING, NULL),
    FB_IE_INIT_FULL("smtpEnhanced", CERT_PEN, 170, FB_IE_VARLEN, NONE,
                    0, 0, FB_STRING, NULL),
    FB_IE_INIT_FULL("smtpSize", CERT_PEN, 222, FB_IE_VARLEN, NONE,
                    0, 0, FB_STRING, NULL),
    FB_IE_INIT_FULL("smtpDate", CERT_PEN, 251, FB_IE_VARLEN, NONE,
                    0, 0, FB_STRING, NULL),
    /* ssh IEs */
    FB_IE_INIT_FULL("sshVersion", CERT_PEN, 171, FB_IE_VARLEN, NONE,
                    0, 0, FB_STRING, NULL),
    /* nntp IEs */
    FB_IE_INIT_FULL("nntpResponse", CERT_PEN, 172, FB_IE_VARLEN, NONE,
                    0, 0, FB_STRING, NULL),
    FB_IE_INIT_FULL("nntpCommand", CERT_PEN, 173, FB_IE_VARLEN, NONE,
                    0, 0, FB_STRING, NULL),
    /* dns IEs */
    FB_IE_INIT_FULL("dnsQueryResponse", CERT_PEN, 174, 1, FB_IE_F_ENDIAN,
                    0, 0, FB_UINT_8, NULL),
    FB_IE_INIT_FULL("dnsQRType", CERT_PEN, 175, 2, FB_IE_F_ENDIAN,
                    0, 0, FB_UINT_16, NULL),
    FB_IE_INIT_FULL("dnsAuthoritative", CERT_PEN, 176, 1, FB_IE_F_ENDIAN,
                    0, 0, FB_UINT_8, NULL),
    FB_IE_INIT_FULL("dnsNXDomain", CERT_PEN, 177, 1, FB_IE_F_ENDIAN,
                    0, 0, FB_UINT_8, NULL),
    FB_IE_INIT_FULL("dnsRRSection", CERT_PEN, 178, 1, FB_IE_F_ENDIAN,
                    0, 0, FB_UINT_8, NULL),
    FB_IE_INIT_FULL("dnsQName", CERT_PEN, 179, FB_IE_VARLEN, NONE,
                    0, 0, FB_STRING, NULL),
    FB_IE_INIT_FULL("dnsCName", CERT_PEN, 180, FB_IE_VARLEN, NONE,
                    0, 0, FB_STRING, NULL),
    FB_IE_INIT_FULL("dnsMXPreference", CERT_PEN, 181, 2, FB_IE_F_ENDIAN,
                    0, 0, FB_UINT_16, NULL),
    FB_IE_INIT_FULL("dnsMXExchange", CERT_PEN, 182, FB_IE_VARLEN, NONE,
                    0, 0, FB_STRING, NULL),
    FB_IE_INIT_FULL("dnsNSDName", CERT_PEN, 183, FB_IE_VARLEN, NONE,
                    0, 0, FB_STRING, NULL),
    FB_IE_INIT_FULL("dnsPTRDName", CERT_PEN, 184, FB_IE_VARLEN, NONE,
                    0, 0, FB_STRING, NULL),
    FB_IE_INIT_FULL("dnsTTL", CERT_PEN, 199, 4, FB_IE_F_ENDIAN,
                    0, 0, FB_UINT_32, NULL),
    FB_IE_INIT_FULL("dnsTXTData", CERT_PEN, 208, FB_IE_VARLEN, NONE,
                    0, 0, FB_STRING, NULL),
    FB_IE_INIT_FULL("dnsSOASerial", CERT_PEN, 209, 4, FB_IE_F_ENDIAN,
                    0, 0, FB_UINT_32, NULL),
    FB_IE_INIT_FULL("dnsSOARefresh", CERT_PEN, 210, 4, FB_IE_F_ENDIAN,
                    0, 0, FB_UINT_32, NULL),
    FB_IE_INIT_FULL("dnsSOARetry", CERT_PEN, 211, 4, FB_IE_F_ENDIAN,
                    0, 0, FB_UINT_32, NULL),
    FB_IE_INIT_FULL("dnsSOAExpire", CERT_PEN, 212, 4, FB_IE_F_ENDIAN,
                    0, 0, FB_UINT_32, NULL),
    FB_IE_INIT_FULL("dnsSOAMinimum", CERT_PEN, 213, 4, FB_IE_F_ENDIAN,
                    0, 0, FB_UINT_32, NULL),
    FB_IE_INIT_FULL("dnsSOAMName", CERT_PEN, 214, FB_IE_VARLEN, NONE,
                    0, 0, FB_STRING, NULL),
    FB_IE_INIT_FULL("dnsSOARName", CERT_PEN, 215, FB_IE_VARLEN, NONE,
                    0, 0, FB_STRING, NULL),
    FB_IE_INIT_FULL("dnsSRVPriority", CERT_PEN, 216, 2, FB_IE_F_ENDIAN,
                    0, 0, FB_UINT_16, NULL),
    FB_IE_INIT_FULL("dnsSRVWeight", CERT_PEN, 217, 2, FB_IE_F_ENDIAN,
                    0, 0, FB_UINT_16, NULL),
    FB_IE_INIT_FULL("dnsSRVPort", CERT_PEN, 218, 2, FB_IE_F_ENDIAN,
                    0, 0, FB_UINT_16, NULL),
    FB_IE_INIT_FULL("dnsSRVTarget", CERT_PEN, 219, FB_IE_VARLEN, NONE,
                    0, 0, FB_STRING, NULL),
    FB_IE_INIT_FULL("dnsID", CERT_PEN, 226, 2, FB_IE_F_ENDIAN,
                    0, 0, FB_UINT_16, NULL),
    /* dnssec IEs */
    FB_IE_INIT_FULL("dnsAlgorithm", CERT_PEN, 227, 1, FB_IE_F_ENDIAN,
                    0, 0, FB_UINT_8, NULL),
    FB_IE_INIT_FULL("dnsKeyTag", CERT_PEN, 228, 2, FB_IE_F_ENDIAN,
                    0, 0, FB_UINT_16, NULL),
    FB_IE_INIT_FULL("dnsSigner", CERT_PEN, 229, FB_IE_VARLEN, NONE,
                    0, 0, FB_STRING, NULL),
    FB_IE_INIT_FULL("dnsSignature", CERT_PEN, 230, FB_IE_VARLEN, NONE,
                    0, 0, FB_OCTET_ARRAY, NULL),
    FB_IE_INIT_FULL("dnsDigest", CERT_PEN, 231, FB_IE_VARLEN, NONE,
                    0, 0, FB_OCTET_ARRAY, NULL),
    FB_IE_INIT_FULL("dnsPublicKey", CERT_PEN, 232, FB_IE_VARLEN, NONE,
                    0, 0, FB_OCTET_ARRAY, NULL),
    FB_IE_INIT_FULL("dnsSalt", CERT_PEN, 233, FB_IE_VARLEN, NONE,
                    0, 0, FB_OCTET_ARRAY, NULL),
    FB_IE_INIT_FULL("dnsHashData", CERT_PEN, 234, FB_IE_VARLEN, NONE,
                    0, 0, FB_OCTET_ARRAY, NULL),
    FB_IE_INIT_FULL("dnsIterations", CERT_PEN, 235, 2, FB_IE_F_ENDIAN,
                    0, 0, FB_UINT_16, NULL),
    FB_IE_INIT_FULL("dnsSignatureExpiration", CERT_PEN, 236, 4,
                    FB_IE_F_ENDIAN, 0, 0, FB_UINT_32, NULL),
    FB_IE_INIT_FULL("dnsSignatureInception", CERT_PEN, 237, 4, FB_IE_F_ENDIAN,
                    0, 0, FB_UINT_32, NULL),
    FB_IE_INIT_FULL("dnsDigestType", CERT_PEN, 238, 1, FB_IE_F_ENDIAN,
                    0, 0, FB_UINT_8, NULL),
    FB_IE_INIT_FULL("dnsLabels", CERT_PEN, 239, 1, FB_IE_F_ENDIAN,
                    0, 0, FB_UINT_8, NULL),
    FB_IE_INIT_FULL("dnsTypeCovered", CERT_PEN, 240, 2, FB_IE_F_ENDIAN,
                    0, 0, FB_UINT_16, NULL),
    FB_IE_INIT_FULL("dnsFlags", CERT_PEN, 241, 2,
                    FB_IE_F_ENDIAN | FB_IE_FLAGS, 0, 0, FB_UINT_16, NULL),
    /* ssl IEs */
    FB_IE_INIT_FULL("sslCipher", CERT_PEN, 185, 4, FB_IE_F_ENDIAN,
                    0, 0, FB_UINT_32, NULL),
    FB_IE_INIT_FULL("sslClientVersion", CERT_PEN, 186, 1, FB_IE_F_ENDIAN,
                    0, 0, FB_UINT_8, NULL),
    FB_IE_INIT_FULL("sslServerCipher", CERT_PEN, 187, 4, FB_IE_F_ENDIAN,
                    0, 0, FB_UINT_32, NULL),
    FB_IE_INIT_FULL("sslCompressionMethod", CERT_PEN, 188, 1, FB_IE_F_ENDIAN,
                    0, 0, FB_UINT_8, NULL),
    FB_IE_INIT_FULL("sslCertVersion", CERT_PEN, 189, 1, FB_IE_F_ENDIAN,
                    0, 0, FB_UINT_8, NULL),
    FB_IE_INIT_FULL("sslCertSignature", CERT_PEN, 190, FB_IE_VARLEN, NONE,
                    0, 0, FB_OCTET_ARRAY, NULL),
    FB_IE_INIT_FULL("sslCertSerialNumber", CERT_PEN, 244, FB_IE_VARLEN, NONE,
                    0, 0, FB_OCTET_ARRAY, NULL),
    FB_IE_INIT_FULL("sslObjectType", CERT_PEN, 245, 1, FB_IE_F_ENDIAN,
                    0, 0, FB_UINT_8, NULL),
    FB_IE_INIT_FULL("sslObjectValue", CERT_PEN, 246, FB_IE_VARLEN, NONE,
                    0, 0, FB_OCTET_ARRAY, NULL),
    FB_IE_INIT_FULL("sslCertValidityNotBefore", CERT_PEN, 247, FB_IE_VARLEN,
                    NONE, 0, 0, FB_STRING, NULL),
    FB_IE_INIT_FULL("sslCertValidityNotAfter", CERT_PEN, 248, FB_IE_VARLEN,
                    NONE, 0, 0, FB_STRING, NULL),
    FB_IE_INIT_FULL("sslPublicKeyAlgorithm", CERT_PEN, 249, FB_IE_VARLEN,
                    NONE, 0, 0, FB_OCTET_ARRAY, NULL),
    FB_IE_INIT_FULL("sslPublicKeyLength", CERT_PEN, 250, 2, FB_IE_F_ENDIAN,
                    0, 0, FB_UINT_16, NULL),
    FB_IE_INIT_FULL("sslServerName", CERT_PEN, 294, FB_IE_VARLEN, NONE,
                     0, 0, FB_STRING, NULL),
    FB_IE_INIT_FULL("sslCertificateHash", CERT_PEN, 295, FB_IE_VARLEN, NONE,
                    0, 0, FB_OCTET_ARRAY, NULL),
    FB_IE_INIT_FULL("sslCertificate", CERT_PEN, 296, FB_IE_VARLEN, NONE,
                    0, 0, FB_OCTET_ARRAY, NULL),
    /* mysql IEs */
    FB_IE_INIT_FULL("mysqlUsername", CERT_PEN, 223, FB_IE_VARLEN, NONE,
                    0, 0, FB_STRING, NULL),
    FB_IE_INIT_FULL("mysqlCommandCode", CERT_PEN, 224, 1, FB_IE_F_ENDIAN,
                    0, 0, FB_UINT_8, NULL),
    FB_IE_INIT_FULL("mysqlCommandText", CERT_PEN, 225, FB_IE_VARLEN, NONE,
                    0, 0, FB_STRING, NULL),
    /* dnp3.0 IEs */
    FB_IE_INIT_FULL("dnp3SourceAddress", CERT_PEN, 281, 2, FB_IE_F_ENDIAN,
                    0, 0, FB_UINT_16, NULL),
    FB_IE_INIT_FULL("dnp3DestinationAddress", CERT_PEN, 282, 2,
                    FB_IE_F_ENDIAN, 0, 0, FB_UINT_16, NULL),
    FB_IE_INIT_FULL("dnp3Function", CERT_PEN, 283, 1, FB_IE_F_ENDIAN,
                    0, 0, FB_UINT_8, NULL),
    FB_IE_INIT_FULL("dnp3ObjectData", CERT_PEN, 284, FB_IE_VARLEN, NONE,
                    0, 0, FB_OCTET_ARRAY, NULL),
    FB_IE_INIT_FULL("modbusData", CERT_PEN, 285, FB_IE_VARLEN, NONE,
                    0, 0, FB_OCTET_ARRAY, NULL),
    FB_IE_INIT_FULL("ethernetIPData", CERT_PEN, 286, FB_IE_VARLEN, NONE,
                    0, 0, FB_OCTET_ARRAY, NULL),
    FB_IE_INIT_FULL("rtpPayloadType", CERT_PEN, 287, 1, ER,
                    0, 0, FB_UINT_8, NULL),
    FB_IE_INIT_FULL("sslRecordVersion", CERT_PEN, 288, 2, FB_IE_F_ENDIAN,
                    0, 0, FB_UINT_16, NULL),
    FB_IE_NULL
};

static fbInfoElement_t yaf_dhcp_info_elements[] = {
    FB_IE_INIT_FULL("dhcpFingerPrint", CERT_PEN, 242, FB_IE_VARLEN,
                    FB_IE_F_REVERSIBLE, 0, 0, FB_STRING, NULL),
    FB_IE_INIT_FULL("dhcpVendorCode", CERT_PEN, 243, FB_IE_VARLEN,
                    FB_IE_F_REVERSIBLE, 0, 0, FB_STRING, NULL),
    FB_IE_INIT_FULL("dhcpOption", CERT_PEN, 297, 1, FB_IE_F_ENDIAN,
                    0, 0, FB_UINT_8, NULL),
    FB_IE_NULL
};

#endif

#endif
