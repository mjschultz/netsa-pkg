/*
** Copyright (C) 2004-2020 by Carnegie Mellon University.
**
** @OPENSOURCE_LICENSE_START@
** See license information in ../../LICENSE.txt
** @OPENSOURCE_LICENSE_END@
*/
#ifndef _V5PDU_H
#define _V5PDU_H
#ifdef __cplusplus
extern "C" {
#endif

#include <silk/silk.h>

RCSIDENTVAR(rcsID_V5PDU_H, "$SiLK: v5pdu.h ef14e54179be 2020-04-14 21:57:45Z mthomas $");


/*
**  v5pdu.h
**
**  Structures defining Version 5 PDU NetFlow Records
**
*/

/*
** v5Header is 24 bytes, v5Record is 48 bytes.  Using the Ethernet MTU
** of 1500, we get get: ((1500 - 24)/48) => 30 v5Records per MTU, and
** the overall length of the PDU will be: (24 + (30*48)) => 1464 bytes
*/
#define V5PDU_LEN 1464
#define V5PDU_MAX_RECS 30

#define V5PDU_MAX_RECS_STR  "30"


typedef struct v5Header_st {
  uint16_t version;
  uint16_t count;
  uint32_t SysUptime;
  uint32_t unix_secs;
  uint32_t unix_nsecs;
  uint32_t flow_sequence;
  uint8_t  engine_type;
  uint8_t  engine_id;
  uint16_t sampling_interval;
} v5Header;

typedef struct v5Record_st {
  uint32_t  srcaddr;   /*  0- 3 */
  uint32_t  dstaddr;   /*  4- 7 */
  uint32_t  nexthop;   /*  8-11 */
  uint16_t  input;     /* 12-13 */
  uint16_t  output;    /* 14-15 */
  uint32_t  dPkts;     /* 16-19 */
  uint32_t  dOctets;   /* 20-23 */
  uint32_t  First;     /* 24-27 */
  uint32_t  Last;      /* 28-31 */
  uint16_t  srcport;   /* 32-33 */
  uint16_t  dstport;   /* 34-35 */
  uint8_t   pad1;      /* 36    */
  uint8_t   tcp_flags; /* 37    */
  uint8_t   prot;      /* 38    */
  uint8_t   tos;       /* 39    */
  uint16_t  src_as;    /* 40-41 */
  uint16_t  dst_as;    /* 42-43 */
  uint8_t   src_mask;  /* 44    */
  uint8_t   dst_mask;  /* 45    */
  uint16_t  pad2;      /* 46-47 */
} v5Record;

typedef struct v5PDU_st {
  v5Header hdr;
  v5Record data[V5PDU_MAX_RECS];
} v5PDU;

#ifdef __cplusplus
}
#endif
#endif /* _V5PDU_H */

/*
** Local Variables:
** mode:c
** indent-tabs-mode:nil
** c-basic-offset:4
** End:
*/
