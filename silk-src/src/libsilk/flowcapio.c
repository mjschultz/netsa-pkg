/*
** Copyright (C) 2004-2017 by Carnegie Mellon University.
**
** @OPENSOURCE_LICENSE_START@
** See license information in ../../LICENSE.txt
** @OPENSOURCE_LICENSE_END@
*/

#include <silk/silk.h>

RCSIDENT("$SiLK: flowcapio.c efd886457770 2017-06-21 18:43:23Z mthomas $");

#include "skstream_priv.h"

/*
**  Converts FLOWCAP records to RWGENERIC records
**
*/


/* Version to use when SK_RECORD_VERSION_ANY is specified */
#define DEFAULT_RECORD_VERSION 5

/*
 *    Store 'ps_value' in the single byte referenced by 'ps_pos'.  If
 *    'ps_value' cannot be represented in a single byte, set the value
 *    referenced by 'ps_pos' to UINT8_MAX.
 */
#define flowcapPackSnmp8(ps_value, ps_pos)                              \
    {                                                                   \
        *(ps_pos) = (uint8_t)(((ps_value) <= 0xFF) ? (ps_value) : 0xFF); \
    }


/* Helper macros for the Packets & Protocol macros below */
#if SK_LITTLE_ENDIAN
#define flowcapPackPacketsProtoHelper(pph_value, pph_pos)       \
    memcpy(pph_pos, pph_value, 3)
#define flowcapUnpackPacketsProtoHelper(pph_value, pph_pos)     \
    memcpy(pph_value, pph_pos, 3)
#else
#define flowcapPackPacketsProtoHelper(pph_value, pph_pos)       \
    memcpy(pph_pos, (((uint8_t*)pph_value) + 1), 3)
#define flowcapUnpackPacketsProtoHelper(pph_value, pph_pos)     \
    memcpy((((uint8_t*)pph_value) + 1), pph_pos, 3)
#endif

/*
 *  flowcapPackPacketsProto(pp_rec, pp_pos);
 *
 *    Store the packets value and protocol from the SiLK flow record
 *    'pp_rec' in the 4-byte (32-bit) value referenced by 'pp_pos',
 *    using 3 bytes for the packets.  If the packets will not fit in 3
 *    bytes, store a value of 0xFFFFFF for the packets.
 */
#define flowcapPackPacketsProto(pp_rec, pp_pos)                 \
    do {                                                        \
        if (rwRecGetPkts(pp_rec) > 0xFFFFFF) {                  \
            memset((pp_pos), 0xFF, 3);                          \
        } else {                                                \
            uint32_t pp_tmp32 = (uint32_t)rwRecGetPkts(pp_rec); \
            flowcapPackPacketsProtoHelper(&pp_tmp32, (pp_pos)); \
        }                                                       \
        *((pp_pos) + 3) = rwRecGetProto(pp_rec);                \
    }while(0)

/*
 *  flowcapUnpackPacketsProto(pp_rec, pp_pos);
 *
 *    Retrieive the packets and protocol values from the 4-byte
 *    (32-bit) value referenced by 'pp_pos' and set the fields on the
 *    SiLK flow record 'pp_rec'.
 */
#define flowcapUnpackPacketsProto(pp_rec, pp_pos)               \
    do {                                                        \
        uint32_t pp_tmp32 = 0;                                  \
        flowcapUnpackPacketsProtoHelper(&pp_tmp32, (pp_pos));   \
        rwRecSetPkts((pp_rec), pp_tmp32);                       \
        rwRecSetProto((pp_rec), *((pp_pos) + 3));               \
    }while(0)


/* LOCAL FUNCTION PROTOTYPES */

static int
flowcapioRecordUnpack_V5(
    skstream_t         *stream,
    rwRec              *rwrec,
    uint8_t            *ar);
static int
flowcapioRecordUnpack_V3(
    skstream_t         *stream,
    rwRec              *rwrec,
    uint8_t            *ar);
static int
flowcapioRecordPack_V3(
    skstream_t         *stream,
    const rwRec        *rwrec,
    uint8_t            *ar);



/* ********************************************************************* */

/*
**  FLOWCAP VERSION 6
**
**    Flowcap version 6 is identical to V5, expect must clear the
**    application field when unpacking.  Packing functions for V5 and
**    V6 are identical.
*/

static int
flowcapioRecordUnpack_V6(
    skstream_t         *stream,
    rwRec              *rwrec,
    uint8_t            *ar)
{
    int rv;

    rv = flowcapioRecordUnpack_V5(stream, rwrec, ar);
    rwRecSetApplication(rwrec, 0);
    return rv;
}


/* ********************************************************************* */

/*
**  FLOWCAP VERSION 5
**
**  in the following: EXPANDED == ((tcp_state & SK_TCPSTATE_EXPANDED) ? 1 : 0)
**
**    uint32_t      sIP;             //  0- 3  Source IP
**    uint32_t      dIP;             //  4- 7  Destination IP
**
**    uint32_t      bytes;           //  8-11  Byte count
**
**    uint32_t      sTime;           // 12-15  Start time as UNIX epoch secs
**
**    uint16_t      elapsed;         // 16-17  Duration of flow in seconds
**    uint16_t      sPort;           // 18-19  Source port
**
**    uint16_t      dPort;           // 20-21  Destination port
**    uint16_t      service_port;    // 22-23  Port reported by flow collector
**
**    uint16_t      input;           // 24-25  SNMP Input
**    uint16_t      output;          // 26-27  SNMP Output
**
**    uint8_t       pkts[3]          // 28-30  Count of packets
**    uint8_t       proto            // 31     Protocol
**
**    uint8_t       flags            // 32     EXPANDED==0: All TCP Flags
**                                   //        EXPANDED==1: Flags !1st pkt
**    uint8_t       first_flags;     // 33     EXPANDED==0: 0
**                                   //        EXPANDED==1: TCP Flags 1st pkt
**    uint8_t       tcp_state;       // 34     TCP state machine info
**    uint8_t       time_frac[3];    // 35-37  sTime msec & elapsed msec
**
**
**  38 bytes on disk.
*/

#define RECLEN_FLOWCAP_V5 38


/*
 *    Byte swap the FLOWCAP v5 record 'ar' in place.
 */
#define flowcapioRecordSwap_V5(ar)                                      \
    {                                                                   \
        uint8_t swap_tmp;                                               \
                                                                        \
        SWAP_DATA32((ar) +  0);   /* sIP */                             \
        SWAP_DATA32((ar) +  4);   /* dIP */                             \
        SWAP_DATA32((ar) +  8);   /* bytes */                           \
        SWAP_DATA32((ar) + 12);   /* sTime */                           \
        SWAP_DATA16((ar) + 16);   /* dur */                             \
        SWAP_DATA16((ar) + 18);   /* sPort */                           \
        SWAP_DATA16((ar) + 20);   /* dPort */                           \
        SWAP_DATA16((ar) + 22);   /* service_port */                    \
        SWAP_DATA16((ar) + 24);   /* input */                           \
        SWAP_DATA16((ar) + 26);   /* output */                          \
                                                                        \
        swap_tmp = ar[28];        /* packets */                         \
        ar[28] = ar[30];                                                \
        ar[30] = swap_tmp;                                              \
                                                                        \
        /* four bytes: proto(31), flags(32), first_flags(33) tcp_state(34) */ \
        /* three bytes in hand-encoded time_frac[3] */                  \
    }


/*
 *  Unpack the array of bytes 'ar' into a record 'rwrec'
 */
static int
flowcapioRecordUnpack_V5(
    skstream_t         *stream,
    rwRec              *rwrec,
    uint8_t            *ar)
{
    uint32_t tmp32 = 0;
    uint16_t elapsed = 0;

    /* swap if required */
    if (stream->swap_flag) {
        flowcapioRecordSwap_V5(ar);
    }

    /* sIP, dIP */
    rwRecMemSetSIPv4(rwrec, &ar[0]);
    rwRecMemSetDIPv4(rwrec, &ar[4]);

    /* bytes */
    rwpackUnpackBytes32(rwrec, &ar[8]);

    /* sTime seconds, plus fractional seconds in bytes 35,36 */
    memcpy(&tmp32, &ar[12], sizeof(tmp32));
    rwRecSetStartTime(rwrec, sktimeCreate(tmp32,
                                          ((ar[35] << 2)
                                           |GET_MASKED_BITS(ar[36], 6, 2))));

    /* elapsed seconds, plus fractional seconds in bytes 36,37 */
    memcpy(&elapsed, &ar[16], sizeof(elapsed));
    rwRecSetElapsed(rwrec, (((uint32_t)1000 * elapsed)
                            + ((GET_MASKED_BITS(ar[36], 0, 6) << 4)
                               | GET_MASKED_BITS(ar[37], 4, 4))));

    /* sPort, dPort, application */
    rwRecMemSetSPort(rwrec, &ar[18]);
    rwRecMemSetDPort(rwrec, &ar[20]);
    rwRecMemSetApplication(rwrec, &ar[22]);

    /* input, output */
    rwpackUnpackInput16(rwrec, &ar[24]);
    rwpackUnpackOutput16(rwrec, &ar[26]);

    /* packets, protocol */
    flowcapUnpackPacketsProto(rwrec, &ar[28]);

    /* Flags, Initial flags, TCP State */
    rwRecSetTcpState(rwrec, ar[34]);
    if (ar[34] & SK_TCPSTATE_EXPANDED) {
        /* have separate initial and session flags */
        rwRecSetFlags(rwrec, (ar[32] | ar[33]));
        rwRecSetRestFlags(rwrec, ar[32]);
        rwRecSetInitFlags(rwrec, ar[33]);
    } else {
        /* have a single flags field */
        rwRecSetFlags(rwrec, ar[32]);
    }

    /* Fractional times in bytes 35-37 handled above */

    /* Get sensor from header */
    rwRecSetSensor(rwrec, stream->silkflow.hdr_sensor);

    return SKSTREAM_OK;
}


/*
 *  Pack the record 'rwrec' into an array of bytes 'ar'
 */
static int
flowcapioRecordPack_V5(
    skstream_t         *stream,
    const rwRec        *rwrec,
    uint8_t            *ar)
{
    uint16_t elapsed;
    int rv = 0;

    /* sIP, dIP */
    rwRecMemGetSIPv4(rwrec, &ar[0]);
    rwRecMemGetDIPv4(rwrec, &ar[4]);

    /* bytes */
    rwpackPackBytes32(rwrec, &ar[8], &rv);
    if (rv) {
        memset(&ar[8], 0xFF, 4);
    }

    /* sTime */
    rwRecMemGetStartSeconds(rwrec, &ar[12]);

    /* elapsed */
    if (rwRecGetElapsedSeconds(rwrec) > UINT16_MAX) {
        memset(&ar[16], 0xFF, sizeof(elapsed));
    } else {
        elapsed = (uint16_t)rwRecGetElapsedSeconds(rwrec);
        memcpy(&ar[16], &elapsed, sizeof(elapsed));
    }

    /* sPort, dPort, application */
    rwRecMemGetSPort(rwrec, &ar[18]);
    rwRecMemGetDPort(rwrec, &ar[20]);
    rwRecMemGetApplication(rwrec, &ar[22]);

    /* input, output */
    rwpackPackInput16(rwrec, &ar[24], &rv);
    if (rv) {
        memset(&ar[24], 0xFF, 2);
    }
    rwpackPackOutput16(rwrec, &ar[26], &rv);
    if (rv) {
        memset(&ar[26], 0xFF, 2);
    }

    /* packets, protocol */
    flowcapPackPacketsProto(rwrec, &ar[28]);

    /* Flags, Initial flags, TCP State */
    ar[34] = rwRecGetTcpState(rwrec);
    if (ar[34] & SK_TCPSTATE_EXPANDED) {
        /* have separate initial and rest flags */
        ar[32] = rwRecGetRestFlags(rwrec);
        ar[33] = rwRecGetInitFlags(rwrec);
    } else {
        /* have a single flags field */
        ar[32] = rwRecGetFlags(rwrec);
        ar[33] = 0;
    }

    /* Fractional time encoding: by hand, always big endian */
    ar[35] = 0xFF & (rwRecGetStartMSec(rwrec) >> 2);
    SET_MASKED_BITS(ar[36], rwRecGetStartMSec(rwrec), 6, 2);
    SET_MASKED_BITS(ar[36], rwRecGetElapsedMSec(rwrec) >> 4, 0, 6);
    ar[37] = 0xFF & (rwRecGetElapsedMSec(rwrec) << 4);

    /* swap if required */
    if (stream->swap_flag) {
        flowcapioRecordSwap_V5(ar);
    }

    return SKSTREAM_OK;
}


/* ********************************************************************* */

/*
**  FLOWCAP VERSION 4
**
**  in the following: EXPANDED == ((tcp_state & SK_TCPSTATE_EXPANDED) ? 1 : 0)
**
**    uint32_t      sIP;             //  0- 3  Source IP
**    uint32_t      dIP;             //  4- 7  Destination IP
**
**    uint32_t      bytes;           //  8-11  Byte count
**
**    uint32_t      sTime;           // 12-15  Start time as UNIX epoch secs
**
**    uint16_t      elapsed;         // 16-17  Duration of flow in seconds
**    uint16_t      sPort;           // 18-19  Source port
**
**    uint16_t      dPort;           // 20-21  Destination port
**    uint16_t      service_port;    // 22-23  Port reported by flow collector
**
**    uint8_t       input;           // 24     SNMP Input
**    uint8_t       output;          // 25     SNMP Output
**
**    uint8_t       pkts[3]          // 26-28  Count of packets
**    uint8_t       proto            // 29     Protocol
**
**    uint8_t       flags            // 30     EXPANDED==0: All TCP Flags
**                                   //        EXPANDED==1: Flags !1st pkt
**    uint8_t       first_flags;     // 31     EXPANDED==0: 0
**                                   //        EXPANDED==1: TCP Flags 1st pkt
**    uint8_t       tcp_state;       // 32     TCP state machine info
**    uint8_t       time_frac[3];    // 33-35  sTime msec & elapsed msec
**
**    uint32_t      payload_hash;    // 36-39  Hash of packet's payload
**
**
**  40 bytes on disk.
*/

#define RECLEN_FLOWCAP_V4 40


/*
 *  Unpack the array of bytes 'ar' into a record 'rwrec'
 */
static int
flowcapioRecordUnpack_V4(
    skstream_t         *stream,
    rwRec              *rwrec,
    uint8_t            *ar)
{
    int rv;

    /* The first 36 bytes of a V4 are identical to V3 */
    rv = flowcapioRecordUnpack_V3(stream, rwrec, ar);

    /* swap if required */
    if (stream->swap_flag) {
        /* only need to swap the payload hash */
        SWAP_DATA32((ar) + 36);
    }

    /* Put the payload hash into the nhIP */
    if (rv == SKSTREAM_OK) {
        rwRecMemSetNhIPv4(rwrec, &ar[36]);
    }

    return rv;
}


/*
 *  Pack the record 'rwrec' into an array of bytes 'ar'
 */
static int
flowcapioRecordPack_V4(
    skstream_t         *stream,
    const rwRec        *rwrec,
    uint8_t            *ar)
{
    int rv;

    /* The first 36 bytes of a V4 are identical to V3 */
    rv = flowcapioRecordPack_V3(stream, rwrec, ar);

    if (rv == SKSTREAM_OK) {
        rwRecMemGetNhIPv4(rwrec, &ar[36]);
    }

    /* swap if required */
    if (stream->swap_flag) {
        /* only need to swap the payload hash */
        SWAP_DATA32((ar) + 36);
    }

    return rv;
}


/* ********************************************************************* */

/*
**  FLOWCAP VERSION 3
**
**  in the following: EXPANDED == ((tcp_state & SK_TCPSTATE_EXPANDED) ? 1 : 0)
**
**    uint32_t      sIP;             //  0- 3  Source IP
**    uint32_t      dIP;             //  4- 7  Destination IP
**
**    uint32_t      bytes;           //  8-11  Byte count
**
**    uint32_t      sTime;           // 12-15  Start time as UNIX epoch secs
**
**    uint16_t      elapsed;         // 16-17  Duration of flow in seconds
**    uint16_t      sPort;           // 18-19  Source port
**
**    uint16_t      dPort;           // 20-21  Destination port
**    uint16_t      service_port;    // 22-23  Port reported by flow collector
**
**    uint8_t       input;           // 24     SNMP Input
**    uint8_t       output;          // 25     SNMP Output
**
**    uint8_t       pkts[3]          // 26-28  Count of packets
**    uint8_t       proto            // 29     Protocol
**
**    uint8_t       flags            // 30     EXPANDED==0: All TCP Flags
**                                   //        EXPANDED==1: Flags !1st pkt
**    uint8_t       first_flags;     // 31     EXPANDED==0: 0
**                                   //        EXPANDED==1: TCP Flags 1st pkt
**    uint8_t       tcp_state;       // 32     TCP state machine info
**    uint8_t       time_frac[3];    // 33-35  sTime msec & elapsed msec
**
**
**  36 bytes on disk.
*/

#define RECLEN_FLOWCAP_V3 36


/*
 *    Byte swap the FLOWCAP v3 record 'ar' in place.
 */
#define flowcapioRecordSwap_V3(ar)                                      \
    {                                                                   \
        uint8_t swap_tmp;                                               \
                                                                        \
        SWAP_DATA32((ar) +  0);   /* sIP */                             \
        SWAP_DATA32((ar) +  4);   /* dIP */                             \
        SWAP_DATA32((ar) +  8);   /* bytes */                           \
        SWAP_DATA32((ar) + 12);   /* sTime */                           \
        SWAP_DATA16((ar) + 16);   /* dur */                             \
        SWAP_DATA16((ar) + 18);   /* sPort */                           \
        SWAP_DATA16((ar) + 20);   /* dPort */                           \
        SWAP_DATA16((ar) + 22);   /* service_port */                    \
        /* Two single byte values: input(24), output(25) */             \
                                                                        \
        swap_tmp = ar[26];        /* packets */                         \
        ar[26] = ar[28];                                                \
        ar[28] = swap_tmp;                                              \
                                                                        \
        /* four bytes: proto(29), flags(30), first_flags(31) tcp_state(32) */ \
        /* three bytes in hand-encoded time_frac[3] */                  \
    }


/*
 *  Unpack the array of bytes 'ar' into a record 'rwrec'
 */
static int
flowcapioRecordUnpack_V3(
    skstream_t         *stream,
    rwRec              *rwrec,
    uint8_t            *ar)
{
    uint32_t tmp32 = 0;
    uint16_t elapsed = 0;

    /* swap if required */
    if (stream->swap_flag) {
        flowcapioRecordSwap_V3(ar);
    }

    /* sIP, dIP */
    rwRecMemSetSIPv4(rwrec, &ar[0]);
    rwRecMemSetDIPv4(rwrec, &ar[4]);

    /* bytes */
    rwpackUnpackBytes32(rwrec, &ar[8]);

    /* sTime seconds, plus fractional seconds in bytes 33,34 */
    memcpy(&tmp32, &ar[12], sizeof(tmp32));
    rwRecSetStartTime(rwrec, sktimeCreate(tmp32,
                                          ((ar[33] << 2)
                                           |GET_MASKED_BITS(ar[34], 6, 2))));

    /* elapsed seconds, plus fractional seconds in bytes 34,35 */
    memcpy(&elapsed, &ar[16], sizeof(elapsed));
    rwRecSetElapsed(rwrec, (((uint32_t)1000 * elapsed)
                            + ((GET_MASKED_BITS(ar[34], 0, 6) << 4)
                               | GET_MASKED_BITS(ar[35], 4, 4))));

    /* sPort, dPort, application */
    rwRecMemSetSPort(rwrec, &ar[18]);
    rwRecMemSetDPort(rwrec, &ar[20]);
    rwRecMemSetApplication(rwrec, &ar[22]);

    /* input, output are single byte values */
    rwRecSetInput(rwrec, ar[24]);
    rwRecSetOutput(rwrec, ar[25]);

    /* packets, protocol */
    flowcapUnpackPacketsProto(rwrec, &ar[26]);

    /* Flags, Initial flags, TCP State */
    rwRecSetTcpState(rwrec, ar[32]);
    if (ar[32] & SK_TCPSTATE_EXPANDED) {
        /* have separate initial and session flags */
        rwRecSetFlags(rwrec, (ar[30] | ar[31]));
        rwRecSetRestFlags(rwrec, ar[30]);
        rwRecSetInitFlags(rwrec, ar[31]);
    } else {
        /* have a single flags field */
        rwRecSetFlags(rwrec, ar[30]);
    }

    /* Fractional times in bytes 33-35 handled above */

    /* Get sensor from header */
    rwRecSetSensor(rwrec, stream->silkflow.hdr_sensor);

    return SKSTREAM_OK;
}


/*
 *  Pack the record 'rwrec' into an array of bytes 'ar'
 */
static int
flowcapioRecordPack_V3(
    skstream_t         *stream,
    const rwRec        *rwrec,
    uint8_t            *ar)
{
    uint16_t elapsed;
    int rv = 0;

    /* sIP, dIP */
    rwRecMemGetSIPv4(rwrec, &ar[0]);
    rwRecMemGetDIPv4(rwrec, &ar[4]);

    /* bytes */
    rwpackPackBytes32(rwrec, &ar[8], &rv);
    if (rv) {
        memset(&ar[8], 0xFF, 4);
    }

    /* sTime */
    rwRecMemGetStartSeconds(rwrec, &ar[12]);

    /* elapsed */
    if (rwRecGetElapsedSeconds(rwrec) > UINT16_MAX) {
        memset(&ar[16], 0xFF, sizeof(elapsed));
    } else {
        elapsed = (uint16_t)rwRecGetElapsedSeconds(rwrec);
        memcpy(&ar[16], &elapsed, sizeof(elapsed));
    }

    /* sPort, dPort, application */
    rwRecMemGetSPort(rwrec, &ar[18]);
    rwRecMemGetDPort(rwrec, &ar[20]);
    rwRecMemGetApplication(rwrec, &ar[22]);

    /* input, output are single byte values */
    flowcapPackSnmp8(rwRecGetInput(rwrec),  &ar[24]);
    flowcapPackSnmp8(rwRecGetOutput(rwrec), &ar[25]);

    /* packets, protocol */
    flowcapPackPacketsProto(rwrec, &ar[26]);

    /* Flags, Initial flags, TCP State */
    ar[32] = rwRecGetTcpState(rwrec);
    if (ar[32] & SK_TCPSTATE_EXPANDED) {
        /* have separate initial and rest flags */
        ar[30] = rwRecGetRestFlags(rwrec);
        ar[31] = rwRecGetInitFlags(rwrec);
    } else {
        /* have a single flags field */
        ar[30] = rwRecGetFlags(rwrec);
        ar[31] = 0;
    }

    /* Fractional time encoding: by hand, always big endian */
    ar[33] = 0xFF & (rwRecGetStartMSec(rwrec) >> 2);
    SET_MASKED_BITS(ar[34], rwRecGetStartMSec(rwrec), 6, 2);
    SET_MASKED_BITS(ar[34], rwRecGetElapsedMSec(rwrec) >> 4, 0, 6);
    ar[35] = 0xFF & (rwRecGetElapsedMSec(rwrec) << 4);

    /* swap if required */
    if (stream->swap_flag) {
        flowcapioRecordSwap_V3(ar);
    }

    return SKSTREAM_OK;
}



/* ********************************************************************* */

/*
**  FLOWCAP VERSION 2
**
**    uint32_t      sIP;             //  0- 3  Source IP
**    uint32_t      dIP;             //  4- 7  Destination IP
**
**    uint32_t      bytes;           //  8-11  Byte count
**
**    uint32_t      sTime;           // 12-15  Start time as UNIX epoch secs
**
**    uint16_t      elapsed;         // 16-17  Duration of flow in seconds
**    uint16_t      sPort;           // 18-19  Source port
**
**    uint16_t      dPort;           // 20-21  Destination port
**    uint8_t       input;           // 22     SNMP Input
**    uint8_t       output;          // 23     SNMP Output
**
**    uint8_t       pkts[3]          // 24-26  Count of packets
**    uint8_t       proto            // 27     Protocol
**
**    uint8_t       flags            // 28     OR of TCP Flags on all pkts
**    uint8_t       first_flags;     // 29     TOS (ignored)
**
**
**  30 bytes on disk.
*/

#define RECLEN_FLOWCAP_V2 30


/*
 *    Byte swap the FLOWCAP v2 record 'ar' in place.
 */
#define flowcapioRecordSwap_V2(ar)                              \
    {                                                           \
        uint8_t swap_tmp;                                       \
                                                                \
        SWAP_DATA32((ar) +  0);   /* sIP */                     \
        SWAP_DATA32((ar) +  4);   /* dIP */                     \
        SWAP_DATA32((ar) +  8);   /* bytes */                   \
        SWAP_DATA32((ar) + 12);   /* sTime */                   \
        SWAP_DATA16((ar) + 16);   /* dur */                     \
        SWAP_DATA16((ar) + 18);   /* sPort */                   \
        SWAP_DATA16((ar) + 20);   /* dPort */                   \
        /* Two single byte values: input(22), output(23) */     \
                                                                \
        swap_tmp = ar[24];        /* packets */                 \
        ar[24] = ar[26];                                        \
        ar[26] = swap_tmp;                                      \
                                                                \
        /* three bytes: proto(27), flags(28), TOS(29) */        \
    }


/*
 *  Unpack the array of bytes 'ar' into a record 'rwrec'
 */
static int
flowcapioRecordUnpack_V2(
    skstream_t         *stream,
    rwRec              *rwrec,
    uint8_t            *ar)
{
    uint32_t tmp32 = 0;
    uint16_t elapsed = 0;

    /* swap if required */
    if (stream->swap_flag) {
        flowcapioRecordSwap_V2(ar);
    }

    /* sIP, dIP */
    rwRecMemSetSIPv4(rwrec, &ar[0]);
    rwRecMemSetDIPv4(rwrec, &ar[4]);

    /* bytes */
    rwpackUnpackBytes32(rwrec, &ar[8]);

    /* sTime */
    memcpy(&tmp32, &ar[12], sizeof(tmp32));
    rwRecSetStartTime(rwrec, sktimeCreate(tmp32, 0));

    /* elapsed */
    memcpy(&elapsed, &ar[16], sizeof(elapsed));
    rwRecSetElapsed(rwrec, ((uint32_t)1000 * elapsed));

    /* sPort, dPort */
    rwRecMemSetSPort(rwrec, &ar[18]);
    rwRecMemSetDPort(rwrec, &ar[20]);

    /* input, output are single byte values */
    rwRecSetInput(rwrec, ar[22]);
    rwRecSetOutput(rwrec, ar[23]);

    /* packets, protocol */
    flowcapUnpackPacketsProto(rwrec, &ar[24]);

    /* Flags */
    rwRecSetFlags(rwrec, ar[28]);

    /* Get sensor from header */
    rwRecSetSensor(rwrec, stream->silkflow.hdr_sensor);

    return SKSTREAM_OK;
}


/*
 *  Pack the record 'rwrec' into an array of bytes 'ar'
 */
static int
flowcapioRecordPack_V2(
    skstream_t         *stream,
    const rwRec        *rwrec,
    uint8_t            *ar)
{
    uint16_t elapsed;
    int rv = 0;

    /* sIP, dIP */
    rwRecMemGetSIPv4(rwrec, &ar[0]);
    rwRecMemGetDIPv4(rwrec, &ar[4]);

    /* bytes */
    rwpackPackBytes32(rwrec, &ar[8], &rv);
    if (rv) {
        memset(&ar[8], 0xFF, 4);
    }

    /* sTime */
    rwRecMemGetStartSeconds(rwrec, &ar[12]);

    /* elapsed */
    if (rwRecGetElapsedSeconds(rwrec) > UINT16_MAX) {
        memset(&ar[16], 0xFF, sizeof(elapsed));
    } else {
        elapsed = (uint16_t)rwRecGetElapsedSeconds(rwrec);
        memcpy(&ar[16], &elapsed, sizeof(elapsed));
    }

    /* sPort, dPort */
    rwRecMemGetSPort(rwrec, &ar[18]);
    rwRecMemGetDPort(rwrec, &ar[20]);

    /* input, output are single byte values */
    flowcapPackSnmp8(rwRecGetInput(rwrec),  &ar[22]);
    flowcapPackSnmp8(rwRecGetOutput(rwrec), &ar[23]);

    /* packets, protocol */
    flowcapPackPacketsProto(rwrec, &ar[24]);

    /* Flags, TOS */
    ar[28] = rwRecGetFlags(rwrec);
    ar[29] = 0;

    /* swap if required */
    if (stream->swap_flag) {
        flowcapioRecordSwap_V2(ar);
    }

    return SKSTREAM_OK;
}


/* ********************************************************************* */

/*
 *  Return length of record of specified version, or 0 if no such
 *  version exists.  See skstream_priv.h for details.
 */
uint16_t
flowcapioGetRecLen(
    sk_file_version_t   vers)
{
    switch (vers) {
      case 2:
        return RECLEN_FLOWCAP_V2;
      case 3:
        return RECLEN_FLOWCAP_V3;
      case 4:
        return RECLEN_FLOWCAP_V4;
      case 5:
      case 6:
        return RECLEN_FLOWCAP_V5;
      default:
        return 0;
    }
}


/*
 *  status = flowcapioPrepare(&stream);
 *
 *    Sets the record version to the default if it is unspecified,
 *    checks that the record format supports the requested record
 *    version, sets the record length, and sets the pack and unpack
 *    functions for this record format and version.
 */
int
flowcapioPrepare(
    skstream_t         *stream)
{
#define FILE_FORMAT "FT_FLOWCAP"
    sk_file_header_t *hdr = stream->silk_hdr;
    int rv = SKSTREAM_OK; /* return value */

    assert(skHeaderGetFileFormat(hdr) == FT_FLOWCAP);

    /* Set version if none was selected by caller */
    if ((stream->io_mode == SK_IO_WRITE)
        && (skHeaderGetRecordVersion(hdr) == SK_RECORD_VERSION_ANY))
    {
        skHeaderSetRecordVersion(hdr, DEFAULT_RECORD_VERSION);
    }

    /* version check; set values based on version */
    switch (skHeaderGetRecordVersion(hdr)) {
      case 6:
        stream->silkflow.unpack = &flowcapioRecordUnpack_V6;
        stream->silkflow.pack   = &flowcapioRecordPack_V5;
        break;
      case 5:
        stream->silkflow.unpack = &flowcapioRecordUnpack_V5;
        stream->silkflow.pack   = &flowcapioRecordPack_V5;
        break;
      case 4:
        stream->silkflow.unpack = &flowcapioRecordUnpack_V4;
        stream->silkflow.pack   = &flowcapioRecordPack_V4;
        break;
      case 3:
        stream->silkflow.unpack = &flowcapioRecordUnpack_V3;
        stream->silkflow.pack   = &flowcapioRecordPack_V3;
        break;
      case 2:
        stream->silkflow.unpack = &flowcapioRecordUnpack_V2;
        stream->silkflow.pack   = &flowcapioRecordPack_V2;
        break;
      case 1:
        /* no longer supported */
      case 0:
      default:
        rv = SKSTREAM_ERR_UNSUPPORT_VERSION;
        goto END;
    }

    stream->rec_len = flowcapioGetRecLen(skHeaderGetRecordVersion(hdr));

    /* verify lengths */
    if (stream->rec_len == 0) {
        skAppPrintErr("Record length not set for %s version %u",
                      FILE_FORMAT, (unsigned)skHeaderGetRecordVersion(hdr));
        skAbort();
    }
    if (stream->rec_len != skHeaderGetRecordLength(hdr)) {
        if (0 == skHeaderGetRecordLength(hdr)) {
            skHeaderSetRecordLength(hdr, stream->rec_len);
        } else {
            skAppPrintErr(("Record length mismatch for %s version %u\n"
                           "\tcode = %" PRIu16 " bytes;  header = %lu bytes"),
                          FILE_FORMAT, (unsigned)skHeaderGetRecordVersion(hdr),
                          stream->rec_len,
                          (unsigned long)skHeaderGetRecordLength(hdr));
            skAbort();
        }
    }

  END:
    return rv;
}


/*
** Local Variables:
** mode:c
** indent-tabs-mode:nil
** c-basic-offset:4
** End:
*/
