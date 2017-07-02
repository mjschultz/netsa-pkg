/*
** Copyright (C) 2004-2017 by Carnegie Mellon University.
**
** @OPENSOURCE_LICENSE_START@
** See license information in ../../LICENSE.txt
** @OPENSOURCE_LICENSE_END@
*/

/*
**  Interface to pull a single flow from a NetFlow v5 PDU
**
*/


#include <silk/silk.h>

RCSIDENT("$SiLK: pdusource.c efd886457770 2017-06-21 18:43:23Z mthomas $");

#if SK_ENABLE_ZLIB
#include <zlib.h>
#endif
#include <poll.h>
#include <silk/libflowsource.h>
#include <silk/redblack.h>
#include <silk/rwrec.h>
#include <silk/skcircbuf.h>
#include <silk/skdllist.h>
#include <silk/sklog.h>
#include <silk/skstream.h>
#include <silk/skthread.h>
#include <silk/utils.h>

#ifdef PDUSOURCE_TRACE_LEVEL
#  define TRACEMSG_LEVEL PDUSOURCE_TRACE_LEVEL
#endif
#define TRACEMSG(lvl, msg) TRACEMSG_TO_TRACEMSGLVL(lvl, msg)
#include <silk/sktracemsg.h>


/* LOCAL DEFINES AND TYPEDEFS */

/* Timeout to pass to the poll(2) system class, in milliseconds. */
#define POLL_TIMEOUT 500

/* Whether to compile in code to help debug accept-from-host */
#ifndef DEBUG_ACCEPT_FROM
#define DEBUG_ACCEPT_FROM 0
#endif

/* One more than UINT32_MAX. */
#define ROLLOVER32  INT64_C(0x100000000)

/* Number of milliseconds the calculated router boot time for a PDU
 * packet must differ from boot time calculated using the previous
 * packet in order to consider the router as having rebooted. */
#define ROUTER_BOOT_FUZZ 1000




/*
 *    The NetFlow v5 header is 24 bytes.
 *
 *    A single NetFlow v5 record is 48 bytes.
 *
 *    Using the Ethernet MTU of 1500, we get get the maximum number of
 *    records per packet as 30, and the maximum packet size of 1464
 *    bytes.
 *
 *    ((1500 - 24)/48) => 30.75
 *    (24 + (30*48)) => 1464
 */
#define V5PDU_MAX_LEN      1464
#define V5PDU_MAX_RECS       30

#define V5PDU_MAX_RECS_STR  "30"


/**
 *    v5Header represents the 24-byte header of a NetFlow V5 packet.
 */
struct v5Header_st {
    /** NetFlow export format version number (5) */
    uint16_t    version;
    /** Number of flows exported in this packet (1-30) */
    uint16_t    count;
    /** Current time in milliseconds since the export device booted */
    uint32_t    SysUptime;
    /** Current count of seconds since 0000 UTC 1970 */
    uint32_t    unix_secs;
    /** Residual nanoseconds since 0000 UTC 1970 */
    uint32_t    unix_nsecs;
    /** Sequence counter of total flows seen */
    uint32_t    flow_sequence;
    /** Type of flow-switching engine */
    uint8_t     engine_type;
    /** Slot number of the flow-switching engine */
    uint8_t     engine_id;
    /** First two bits hold the sampling mode; remaining 14 bits hold
     * value of sampling interval */
    uint16_t    sampling_interval;
};
typedef struct v5Header_st v5Header;

/**
 *    v5Record represents an individual 48-byte NetFlow V5 record.
 */
struct v5Record_st {
    /** Source IP address */
    uint32_t    srcaddr;   /*  0- 3 */
    /** Destination IP address */
    uint32_t    dstaddr;   /*  4- 7 */
    /** IP address of next hop router */
    uint32_t    nexthop;   /*  8-11 */
    /** SNMP index of input interface */
    uint16_t    input;     /* 12-13 */
    /** SNMP index of output interface */
    uint16_t    output;    /* 14-15 */
    /** Packets in the flow */
    uint32_t    dPkts;     /* 16-19 */
    /** Total number of Layer 3 bytes in the packets of the flow */
    uint32_t    dOctets;   /* 20-23 */
    /** SysUptime at start of flow */
    uint32_t    First;     /* 24-27 */
    /** SysUptime at the time the last packet of the flow was received */
    uint32_t    Last;      /* 28-31 */
    /** TCP/UDP source port number or equivalent */
    uint16_t    srcport;   /* 32-33 */
    /** TCP/UDP destination port number or equivalent */
    uint16_t    dstport;   /* 34-35 */
    /** Unused (zero) bytes */
    uint8_t     pad1;      /* 36    */
    /** Cumulative OR of TCP flags */
    uint8_t     tcp_flags; /* 37    */
    /** IP protocol type (for example, TCP = 6; UDP = 17) */
    uint8_t     prot;      /* 38    */
    /** IP type of service (ToS) */
    uint8_t     tos;       /* 39    */
    /** Autonomous system number of the source, either origin or
     * peer */
    uint16_t    src_as;    /* 40-41 */
    /** Autonomous system number of the destination, either origin
     * or peer */
    uint16_t    dst_as;    /* 42-43 */
    /** Source address prefix mask bits */
    uint8_t     src_mask;  /* 44    */
    /** Destination address prefix mask bits */
    uint8_t     dst_mask;  /* 45    */
    /** Unused (zero) bytes */
    uint16_t    pad2;      /* 46-47 */
};
typedef struct v5Record_st v5Record;

/**
 *    v5PDU represents a completely-filled NetFlowV5 packet.
 */
struct v5PDU_st {
    v5Header    hdr;
    v5Record    data[V5PDU_MAX_RECS];
};
typedef struct v5PDU_st v5PDU;

/**
 *    pdu_engine_info_t contains per-engine data structures for a
 *    Netflow v5 stream
 */
struct pdu_engine_info_st {
    /**   holds (engine_type << 8) | engine_id.  Used to distinguish
     *    multiple PDU streams arriving on a single port. */
    uint16_t    id;
    /**   flow sequence number we expect to see on the next packet. */
    uint32_t    flow_sequence;
    /**   router boot time as milliseconds since the UNIX epoch */
    intmax_t    router_boot;
    /**   packet export time given as milliseconds since the router
     *    booted */
    intmax_t    sysUptime;
    /**   timestamp of last PDU */
    sktime_t    last_timestamp;

};
typedef struct pdu_engine_info_st pdu_engine_info_t;

/**
 *    pdu_badpdu_status_t lists the types of bad PDUs we may
 *    encounter.  Keep this list in sync with pdu_badpdu_msgs[] below.
 */
enum pdu_badpdu_status_en {
    PDU_OK = 0,
    PDU_BAD_VERSION,
    PDU_ZERO_RECORDS,
    PDU_OVERFLOW_RECORDS,
    PDU_TRUNCATED_HEADER,
    PDU_TRUNCATED_DATA
};
typedef enum pdu_badpdu_status_en pdu_badpdu_status_t;

/**
 *    error messages for invalid PDUs.  Keep in sync with
 *    pdu_badpdu_status_t
 */
static const char *pdu_badpdu_msgs[] = {
    "No Error",
    "not marked as version 5",
    "reporting zero records",
    "reporting more than " V5PDU_MAX_RECS_STR " records",
    "due to truncated header",
    "due to truncated data section"
};


/**
 *    pdu_statistics_t is used to report the statistics of packets
 *    processed by a flow source.
 */
struct pdu_statistics_st {
    /** Number of processed packets */
    uint64_t    procPkts;
    /** Number of completely bad packets */
    uint64_t    badPkts;
    /** Number of good records processed */
    uint64_t    goodRecs;
    /** Number of records with bad data */
    uint64_t    badRecs;
    /** Number of missing records; NOTE: signed int to allow for out of
     * seq pkts */
    int64_t     missingRecs;
};
typedef struct pdu_statistics_st pdu_statistics_t;


/**
 *    pdu_file_t is a helper structure for skPDUSource_t that reads
 *    NetFlow v5 PDUs from a stream.
 *
 *    This object wraps a SiLK stream that reads the data from a file.
 */
struct pdu_file_st {
    /**
     *    Source of the NetFlowV5 data.
     */
    skstream_t             *stream;

    /* Thread data */
    pthread_t               thread;
    pthread_mutex_t         mutex;
    pthread_cond_t          cond;

    /**
     *    NetFlowV5 record that was read from the stream and is
     *    currently being processed.
     */
    v5PDU                   file_buffer;
};
typedef struct pdu_file_st pdu_file_t;


/**
 *    When the NetFlow v5 data is being read from a Berkeley socket,
 *    the following structure, a 'pdu_net_base_t', is the object that
 *    contains the file descriptors to the socket(s) bound to a single
 *    port from which data is being read.  (There will be multiple
 *    sockets when the hostname resolves to multiple addresses, but
 *    all will be bound to the same port number.)
 *
 *    Another structure, the 'pdu_network_t' sits between the
 *    skPDUSource_t and the 'pdu_net_base_t'.
 */
struct pdu_net_base_st {
    /**    name of address:port to bind to */
    const char             *name;

    /**    address to bind() to */
    const sk_sockaddr_array_t *listen_address;

    /**   when a probe does not have an 'accept' clause, any peer may
     *    connect, and there is a one-to-one mapping between a source
     *    object and a base object.  The 'any' member points to the
     *    source, and the 'addr2source' member will be NULL. */
    skPDUSource_t          *any;

    /**   if there is an 'accept' clause on the probe, the
     *    'addr2source' red-black tree maps the address of the peer to
     *    a particular source object (via 'pdu_addr2source_t'
     *    objects), and the 'any' member must be NULL. */
    struct rbtree          *addr2source;

    /**    sockets to listen to */
    struct pollfd          *pfd;

    /**    number of valid entries in the 'pfd' array */
    nfds_t                  pfd_valid;

    /**    number of entries in the array when it was created */
    nfds_t                  pfd_len;

    /* Thread data */
    pthread_t               thread;
    pthread_mutex_t         mutex;
    pthread_cond_t          cond;

    /**    the number of 'sources' that use this 'base' */
    uint32_t                refcount;

    /**    number of 'sources' that are running */
    uint32_t                active_sources;

    /* Is the udp_reader thread running? */
    unsigned                running    : 1;

    /* Set to 1 to signal the udp_reader thread to stop running */
    unsigned                stop       : 1;

    /* Was the previous packet from an unknown host? */
    unsigned                unknown_host:1;
};
typedef struct pdu_net_base_st pdu_net_base_t;


/**
 *    There is one of the following objects, a 'pdu_network_t' for
 *    every skPDUSource_t that accepts data on a UDP port.  The
 *    'pdu_network_t' contains data collected for that particular
 *    probe until the data is requested by the application.
 *
 *    For each UDP probe, the pair (listen_address, accept_from) must
 *    be unique.  That is, either the source is only thing listening
 *    on this address/port, or the sources are distinguished by the
 *    address that is sending the packets (i.e., the peer address).
 *
 *    The 'pdu_network_t' points to a 'pdu_net_base_t' object, which
 *    handles the collection of data from the network.
 *
 *    When there is no accept_from address, there is a one-to-one
 *    mapping between the 'pdu_network_t' and the 'pdu_net_base_t'.
 *
 *    When multiple 'pdu_network_t's listen on the same address, they
 *    share the same 'pdu_net_base_t'.  The 'pdu_net_base_t' has an
 *    red-black tree (the 'addr2source' member) that maps to all the
 *    'skPDUSource_t' that share that base.  The key in the red-black
 *    tree is the list of addresses (sk_sockaddr_t) expanded from the
 *    list of 'accept_from' addresses on the probe.
 *
 *    When there is an accept_from address and it is unique, there
 *    will still be one-to-one mapping between 'pdu_network_t' and
 *    'pdu_net_base_t', but the situation is handled as if multiple
 *    'pdu_network_t's shared a 'pdu_net_base_t'.
 */
struct pdu_network_st {
    /**   the network collector */
    pdu_net_base_t         *base;

    /**   structure to holds packets collected for this probe but not
     *    yet requested. */
    sk_circbuf_t           *circbuf;

    /**   the current location in the 'data_buffer' */
    v5PDU                  *circbuf_pos;

    /**   has the source been told to stop? */
    unsigned                stopped : 1;
};
typedef struct pdu_network_st pdu_network_t;


/*
 *    The 'addr2source' member of 'pdu_net_base_t' is a red-black
 *    tree whose data members are defined by the following structure,
 *    a 'pdu_addr2source_t' objects.
 *
 *    These objects are used when multiple sources listen on the same
 *    port and the sources are distinguished by the host they accept
 *    data from.  When a packet arrives, the 'pdu_net_base_t'
 *    searches the 'addr2source' tree to find the appropriate source
 *    to give the packet to.
 */
struct pdu_addr2source_st {
    const sk_sockaddr_t *addr;
    skPDUSource_t       *pdusrc;
};
typedef struct pdu_addr2source_st pdu_addr2source_t;


/* Definition of the skPDUSource_t structure. */
struct skPDUSource_st {
    pdu_statistics_t        statistics;
    pthread_mutex_t         stats_mutex;

    const skpc_probe_t     *probe;
    const char             *name;
    pdu_network_t          *net;

    pdu_file_t             *file;

    /* Current pdu */
    const v5PDU            *pdu;

    /* Per-engine data */
    struct rbtree          *engine_info_tree;

    /* Per-engine data for most recent engine */
    pdu_engine_info_t      *engine_info;

    /* Number of consecutive bad PDUs we have seen---other than the
     * first */
    uint32_t                badpdu_consec;

    /* Number of recs left in current PDU */
    uint8_t                 count;

    /* What to log regarding bad or missing PDUs, as set by the
     * log-flags statement in sensor.conf */
    uint8_t                 logopt;

    /* Why the last PDU packet was rejected; used to reduce number of
     * "bad packet" log messages */
    pdu_badpdu_status_t  badpdu_status;

    unsigned                stopped : 1;
};
/* typedef struct skPDUSource_st skPDUSource_t;   // libflowsource.h */



/* LOCAL VARIABLE DEFINITIONS */

/*
 *  TIME VALUES IN THE NETFLOW V5 PDU
 *
 *  The naive ordering of events with respect to time in the router
 *  would be to collect the flows and generate the PDU.  Thus, one
 *  would expect:
 *
 *      flow.Start  <  flow.End  <  hdr.sysUptime
 *
 *  where all values are given as milliseconds since the router's
 *  interface was booted, and hdr.sysUptime is advertised as the
 *  "current" time.
 *
 *  However, since values are given as 32bit numbers, the values will
 *  roll-over after about 49.7 days.  If the values roll-over in the
 *  middle of writing the PDU, we will see one of these two
 *  conditions:
 *
 *      hdr.sysUptime  <<  flow.Start  <  flow.End
 *
 *      flow.End  <  hdr.sysUptime  <<  flow.Start
 *
 *  Thus, if flow.End less than flow.Start, we need to account for the
 *  roll-over when computing the flow's duration.
 *
 *  In practice, the PDU's header gets filled in before flows are
 *  added, making the hdr.sysUptime not have any true time ordering
 *  with respect to the flow.Start and flow.End, and we have seen
 *  cases in real NetFlow data where hdr.sysUptime is slightly less
 *  than flow.End:
 *
 *      flow.Start  <  hdr.sysUptime  <  flow.End
 *
 *  Moreover, some naive NetFlow PDU generators simply pin the
 *  hdr.sysUptime to zero, and don't account for rollover at all.
 *  This can make hdr.sysUptime much less than flow.Start.
 *
 *  In order to make the determination whether the flow.Start or
 *  hdr.sysUptime values have overflown their values and rolled-over,
 *  we look at the difference between them.  If the absolute value of
 *  the difference is greater than some very large maximum defined in
 *  maximumFlowTimeDeviation (currently 45 days), we assume that one
 *  of the two has rolled over, and adjust based on that assumption.
 */
static const intmax_t maximumFlowTimeDeviation =
    (intmax_t)45 * 24 * 3600 * 1000; /* 45 days */

/*
 *  SEQUENCE NUMBERS IN NETFLOW V5 PDU
 *
 *  When the sequence number we receive is greater than the value we
 *  were expecting but within the maximumSequenceDeviation window,
 *  assume that we have lost flow records:
 *
 *  (received - expected) < maximumSequenceDeviation ==> LOST PACKETS
 *
 *
 *  If the value we receive is less than the expected value but within
 *  the maximumSequenceLateArrival window, assume the received packet
 *  is arriving late.
 *
 *  (expected - received) < maximumSequenceLateArrival ==> LATE PACKET
 *
 *
 *  If the values vary wildly, first check whether either of the above
 *  relationships hold if we take sequence number roll-over into
 *  account.
 *
 *  Otherwise, assume something caused the sequence numbers to reset.
 *
 *  maximumSequenceDeviation is set assuming we receive 1k flows/sec
 *  and we lost 1 hour (3600 seconds) of flows
 *
 *  maximumSequenceLateArrival is set assuming we receive 1k flows/sec
 *  and the packet is 1 minute (60 seconds) late
 *
 *  (1k flows/sec is 33 pkts/sec if all packets hold 30 flows)
 */

static const int64_t maximumSequenceDeviation = INT64_C(1000) * INT64_C(3600);

static const int64_t maximumSequenceLateArrival = INT64_C(1000) * INT64_C(60);


/*    The 'pdu_net_base_list' contains pointers to all existing
 *    pdu_net_base_t objects.  When creating a new pdu_network_t, the
 *    list is checked for existing sources listening on the same
 *    port. */
static sk_dllist_t *pdu_net_base_list = NULL;

/*    A mutex the protect access to pdu_net_base_list. */
static pthread_mutex_t pdu_net_base_list_mutex = PTHREAD_MUTEX_INITIALIZER;

/*    The 'sockets_count' variable maintains the number of open
 *    sockets; used when setting the socket buffer size. */
static uint32_t sockets_count = 0;


/* FUNCTION DEFINITIONS */

/**
 *    Comparison function for pdu_addr2source_t objects used by the
 *    'addr2source' red-black tree on the pdu_net_base_t object.
 */
static int
pdu_addr2source_compare(
    const void         *va,
    const void         *vb,
    const void  UNUSED(*ctx))
{
    const sk_sockaddr_t *a = ((const pdu_addr2source_t*)va)->addr;
    const sk_sockaddr_t *b = ((const pdu_addr2source_t*)vb)->addr;

    return skSockaddrCompare(a, b, SK_SOCKADDRCOMP_NOPORT);
}


/**
 *    Comparison function for pdu_engine_info_t objects used by the
 *    'engine_info_tree' red-black tree on the skPDUSource_t object.
 */
static int
pdu_engine_compare(
    const void         *va,
    const void         *vb,
    const void  UNUSED(*ctx))
{
    const pdu_engine_info_t *a = (const pdu_engine_info_t*)va;
    const pdu_engine_info_t *b = (const pdu_engine_info_t*)vb;

    if (a->id < b->id) {
        return -1;
    }
    return (a->id > b->id);
}


/**
 *  reject_if_true = pdu_reject_packet(pdu_src, data, sz);
 *
 *    Return TRUE if the bytes in 'data' do not represent a valid PDU
 *    packet.  'sz' is the length of the packet.  'pdu_network' is
 *    PDU source object.
 *
 *    Callback function passed to the pdu_network_t collector.
 */
static int
pdu_reject_packet(
    skPDUSource_t      *pdu_src,
    const v5PDU        *pdu,
    ssize_t             data_len)
{
    pdu_badpdu_status_t pdu_status = PDU_OK;
    uint16_t count;

    if ((size_t)data_len < sizeof(v5Header)) {
        /* length cannot even hold a PDU header */
        pdu_status = PDU_TRUNCATED_HEADER;
    } else if (ntohs(pdu->hdr.version) != 5) {
        /* reject packet */
        pdu_status = PDU_BAD_VERSION;
    } else if (0 == (count = ntohs(pdu->hdr.count))) {
        pdu_status = PDU_ZERO_RECORDS;
    } else if (count > V5PDU_MAX_RECS) {
        pdu_status = PDU_OVERFLOW_RECORDS;
    } else if ((size_t)data_len < count * sizeof(v5Record)) {
        pdu_status = PDU_TRUNCATED_DATA;
    } else {
        /* current status is PDU_OK */
        if (PDU_OK == pdu_src->badpdu_status) {
            /* previous status was also PDU_OK; return */
            pthread_mutex_lock(&pdu_src->stats_mutex);
            ++pdu_src->statistics.procPkts;
            pthread_mutex_unlock(&pdu_src->stats_mutex);
            return 0;
        }
        pdu_status = PDU_OK;
    }

    /* when here, one or both of the current status and the previous
     * status are not PDU_OKAY */

    /* if status is same as before, increment counters and return */
    if (pdu_status == pdu_src->badpdu_status) {
        ++pdu_src->badpdu_consec;
        pthread_mutex_lock(&pdu_src->stats_mutex);
        ++pdu_src->statistics.procPkts;
        ++pdu_src->statistics.badPkts;
        pthread_mutex_unlock(&pdu_src->stats_mutex);
        return 1;
    }

    /* status has changed; we need to write a log message about the
     * previous status unless it was PDU_OK */
    if (PDU_OK != pdu_src->badpdu_status) {
        /* note, we have already logged about 1 bad packet */
        if (pdu_src->badpdu_consec) {
            NOTICEMSG(("'%s': Rejected %" PRIu32 " additional PDU record%s %s"),
                      pdu_src->name, pdu_src->badpdu_consec,
                      ((pdu_src->badpdu_consec == 1) ? "" : "s"),
                      pdu_badpdu_msgs[pdu_src->badpdu_status]);
        }

        if (PDU_OK == pdu_status) {
            pdu_src->badpdu_status = PDU_OK;
            pthread_mutex_lock(&pdu_src->stats_mutex);
            ++pdu_src->statistics.procPkts;
            pthread_mutex_unlock(&pdu_src->stats_mutex);
            return 0;
        }
    }

    INFOMSG("'%s': Rejected PDU record %s",
            pdu_src->name, pdu_badpdu_msgs[pdu_status]);

    /* Since we logged about this packet, no need to count it */
    pdu_src->badpdu_consec = 0;
    pdu_src->badpdu_status = pdu_status;
    pthread_mutex_lock(&pdu_src->stats_mutex);
    ++pdu_src->statistics.procPkts;
    ++pdu_src->statistics.badPkts;
    pthread_mutex_unlock(&pdu_src->stats_mutex);
    return 1;
}


/**
 *    THREAD ENTRY POINT
 *
 *    The pdu_net_base_reader() function is the thread for listening
 *    to data on a single UDP port.  The pdu_net_base_t object
 *    containing information about the port is passed into this
 *    function.
 *
 *    This thread is started from the pdu_net_base_create() function,
 *    and its location is stored in the 'thread' member of the
 *    pdu_net_base_t structure.
 */
static void*
pdu_net_base_reader(
    void               *vbase)
{
    pdu_net_base_t *base = (pdu_net_base_t*)vbase;
    sk_sockaddr_t addr;
    v5PDU data;
    pdu_addr2source_t target;
    socklen_t len;
    skPDUSource_t *pdu_src = NULL;
    const pdu_addr2source_t *match_address;
    nfds_t i;
    ssize_t rv;

    assert(base != NULL);

    /* Lock for initialization */
    pthread_mutex_lock(&base->mutex);

    DEBUGMSG("UDP listener started for %s", base->name);

    /* Note run state */
    base->running = 1;

    /* Signal completion of initialization */
    pthread_cond_broadcast(&base->cond);

    /* Wait for initial source to be connected to this base*/
    while (!base->stop && !base->active_sources) {
        pthread_cond_wait(&base->cond, &base->mutex);
    }
    pthread_mutex_unlock(&base->mutex);

    /* Main loop */
    while (!base->stop && base->active_sources && base->pfd_valid) {
        /* Wait for data */
        rv = poll(base->pfd, base->pfd_len, POLL_TIMEOUT);
        if (rv == -1) {
            if (errno == EINTR || errno == EAGAIN) {
                /* Interrupted by a signal, or internal alloc failed,
                 * try again. */
                continue;
            }
            /* Error */
            ERRMSG("Poll error for %s (%d) [%s]",
                   base->name, errno, strerror(errno));
            break;
        }

        /* See if we timed out.  We time out every now and then in
         * order to see if we need to shut down. */
        if (rv == 0) {
            continue;
        }

        /* Loop around file descriptors */
        for (i = 0; i < base->pfd_len; i++) {
            struct pollfd *pfd = &base->pfd[i];

            if (pfd->revents & (POLLERR | POLLHUP | POLLNVAL)) {
                if (!(pfd->revents & POLLNVAL)) {
                    close(pfd->fd);
                }
                pfd->fd = -1;
                base->pfd_valid--;
                DEBUGMSG("Poll for %s encountered a (%s,%s,%s) condition",
                         base->name, (pfd->revents & POLLERR) ? "ERR": "",
                         (pfd->revents & POLLHUP) ? "HUP": "",
                         (pfd->revents & POLLNVAL) ? "NVAL": "");
                DEBUGMSG("Closing file handle, %d remaining",
                         (int)base->pfd_valid);
                continue;
            }

            if (!(pfd->revents & POLLIN)) {
                continue;
            }

            /* Read the data */
            len = sizeof(addr);
            rv = recvfrom(pfd->fd, &data, sizeof(data), 0,
                          (struct sockaddr*)&addr, &len);

            /* Check for error or recv from wrong address */
            if (rv == -1) {
                switch (errno) {
                  case EINTR:
                    /* Interrupted by a signal: ignore now, try again
                     * later. */
                    continue;
                  case EAGAIN:
                    /* We should not be getting this, but have seen them
                     * in the field nonetheless.  Note and ignore them. */
                    NOTICEMSG(("Ignoring spurious EAGAIN from recvfrom() "
                               "call on %s"), base->name);
                    continue;
                  default:
                    ERRMSG("recvfrom error from %s (%d) [%s]",
                           base->name, errno, strerror(errno));
                    goto BREAK_WHILE;
                }
            }

            /* Match the address on the packet against the list of
             * accept_from addresses for each source that uses this
             * base. */
            pthread_mutex_lock(&base->mutex);
            if (base->any) {
                /* When there is no accept-from address on the probe,
                 * there is a one-to-one mapping between source and
                 * base, and all connections are permitted. */
                assert(NULL == base->addr2source);
                pdu_src = base->any;
            } else {
                /* Using the address of the incoming connection,
                 * search for the source object associated with this
                 * address. */
                assert(NULL != base->addr2source);
                target.addr = &addr;
                match_address = ((const pdu_addr2source_t*)
                                 rbfind(&target, base->addr2source));
                if (match_address) {
                    /* we recognize the sender */
                    pdu_src = match_address->pdusrc;
                    base->unknown_host = 0;
#if  !DEBUG_ACCEPT_FROM
                } else if (!base->unknown_host) {
                    /* additional packets seen from one or more
                     * distinct unknown senders; ignore */
                    pthread_mutex_unlock(&base->mutex);
                    continue;
#endif
                } else {
                    /* first packet seen from unknown sender after
                     * receiving packet from valid sender; log */
                    char addr_buf[2 * SK_NUM2DOT_STRLEN];
                    base->unknown_host = 1;
                    pthread_mutex_unlock(&base->mutex);
                    skSockaddrString(addr_buf, sizeof(addr_buf), &addr);
                    INFOMSG("Ignoring packets from host %s", addr_buf);
                    continue;
                }
            }

            if (pdu_src->net->stopped) {
                pthread_mutex_unlock(&base->mutex);
                continue;
            }

            /* Copy the data */
            memcpy(pdu_src->net->circbuf_pos, &data, rv);
            pthread_mutex_unlock(&base->mutex);

            if (pdu_reject_packet(pdu_src, pdu_src->net->circbuf_pos, rv)) {
                /* reject the packet; do not advance to next location */
                continue;
            }

            /* Acquire the next location */
            pdu_src->net->circbuf_pos
                = (v5PDU*)sk_circbuf_get_write_pos(pdu_src->net->circbuf);
            if (NULL == pdu_src->net->circbuf_pos) {
                NOTICEMSG("Non-existent data buffer for %s", base->name);
                break;
            }
        } /* for (i = 0; i < base->nfds_t; i++) */
    } /* while (!base->stop && base->pfd_valid) */

  BREAK_WHILE:

    /* Set running to zero, and notify waiters of our exit */
    pthread_mutex_lock(&base->mutex);
    base->running = 0;
    pthread_cond_broadcast(&base->cond);
    pthread_mutex_unlock(&base->mutex);

    DEBUGMSG("UDP listener stopped for %s", base->name);

    return NULL;
}


/* Adjust socket buffer sizes */
static void
adjust_socketbuffers(
    void)
{
    static int sbufmin = SOCKETBUFFER_MINIMUM;
    static int sbufnominaltotal = SOCKETBUFFER_NOMINAL_TOTAL;
    static int env_calculated = 0;
    int sbufsize;
    const pdu_net_base_t *base;
    sk_dll_iter_t iter;

    ASSERT_MUTEX_LOCKED(&pdu_net_base_list_mutex);

    if (!env_calculated) {
        const char *env;
        char *end;

        env = getenv(SOCKETBUFFER_NOMINAL_TOTAL_ENV);
        if (env) {
            long int val = strtol(env, &end, 0);
            if (end != env && *end == '\0') {
                if (val > INT_MAX) {
                    val = INT_MAX;
                }
                sbufnominaltotal = val;
            }
        }
        env = getenv(SOCKETBUFFER_MINIMUM_ENV);
        if (env) {
            long int val = strtol(env, &end, 0);
            if (end != env && *end == '\0') {
                if (val > INT_MAX) {
                    val = INT_MAX;
                }
                sbufmin = val;
            }
        }
        env_calculated = 1;
    }

    if (sockets_count) {
        assert(pdu_net_base_list);
        sbufsize = sbufnominaltotal / sockets_count;
        if (sbufsize < sbufmin) {
            sbufsize = sbufmin;
        }

        skDLLAssignIter(&iter, pdu_net_base_list);
        while (skDLLIterForward(&iter, (void **)&base) == 0) {
            nfds_t i;
            for (i = 0; i < base->pfd_len; ++i) {
                if (base->pfd[i].fd >= 0) {
                    skGrowSocketBuffer(base->pfd[i].fd, SO_RCVBUF, sbufsize);
                }
            }
        }
    }
}


/**
 *    Destroy a base object.
 *
 *    Join with the base's thread, close all the sockets, remove the
 *    base from the global list of bases, and free the base object.
 */
static void
pdu_net_base_destroy(
    pdu_net_base_t     *base)
{
    nfds_t i;

    assert(base);

    pthread_mutex_lock(&base->mutex);

    assert(base->refcount == 0);

    /* If running, notify thread to stop, and then wait for exit */
    if (base->running) {
        base->stop = 1;
        while (base->running) {
            pthread_cond_wait(&base->cond, &base->mutex);
        }
    }
    /* Reap thread */
    pthread_join(base->thread, NULL);

    /* Close sockets */
    for (i = 0; i < base->pfd_len; ++i) {
        if (base->pfd[i].fd >= 0) {
            pthread_mutex_lock(&pdu_net_base_list_mutex);
            close(base->pfd[i].fd);
            base->pfd[i].fd = -1;
            --base->pfd_valid;
            --sockets_count;
            pthread_mutex_unlock(&pdu_net_base_list_mutex);
        }
    }
    free(base->pfd);
    base->pfd = NULL;

    /* Free addr2source tree */
    if (base->addr2source) {
        assert(rblookup(RB_LUFIRST, NULL, base->addr2source) == NULL);
        rbdestroy(base->addr2source);
    }

    /* Remove from pdu_net_base_list list */
    if (base->listen_address) {
        sk_dll_iter_t iter;
        pdu_net_base_t *b;

        pthread_mutex_lock(&pdu_net_base_list_mutex);
        assert(pdu_net_base_list);
        skDLLAssignIter(&iter, pdu_net_base_list);
        while (skDLLIterForward(&iter, (void**)&b) == 0) {
            if (b == base) {
                skDLLIterDel(&iter);
                if (skDLListIsEmpty(pdu_net_base_list)) {
                    skDLListDestroy(pdu_net_base_list);
                    pdu_net_base_list = NULL;
                } else {
                    adjust_socketbuffers();
                }
                break;
            }
        }
        pthread_mutex_unlock(&pdu_net_base_list_mutex);
    }

    free((void*)base->name);

    pthread_mutex_unlock(&base->mutex);
    pthread_mutex_destroy(&base->mutex);

    pthread_cond_destroy(&base->cond);

    free(base);
}


/**
 *    Create a base object and its associated thread.
 *
 *    Create a pdu_net_base_t, create its sockets, start the thread to
 *    process data, and add the base to the global list of bases.
 *
 *    On error, the function is expected to clean up all data
 *    structures it has created (including the global list of bases)
 *    and close all sockets it has opened.
 */
static pdu_net_base_t*
pdu_net_base_create(
    const sk_sockaddr_array_t  *listen_address)
{
    const sk_sockaddr_t *addr;
    pdu_net_base_t *base;
    char addr_name[PATH_MAX];
    struct pollfd *pfd;
    uint32_t pfd_valid;
    uint32_t num_addrs;
    uint32_t i;
    int rv;
    uint16_t port;

    assert(listen_address);
    ASSERT_MUTEX_LOCKED(&pdu_net_base_list_mutex);

    /* the port of the listen_address array (0 == undecided) */
    port = 0;

    /* number of sockets we successfully bind to */
    pfd_valid = 0;

    /* number of addresses to check */
    num_addrs = skSockaddrArrayGetSize(listen_address);
    if (0 == num_addrs) {
        return NULL;
    }

    /* create base structure */
    base = (pdu_net_base_t*)calloc(1, sizeof(pdu_net_base_t));
    if (NULL == base) {
        skAppPrintOutOfMemory("pdu_net_base_t");
        return NULL;
    }
    pthread_mutex_init(&base->mutex, NULL);
    pthread_cond_init(&base->cond, NULL);

    /* create array of poll structures */
    base->pfd = (struct pollfd*)calloc(num_addrs, sizeof(struct pollfd));
    if (NULL == base->pfd) {
        skAppPrintOutOfMemory("network polling structure");
        goto ERROR;
    }

    DEBUGMSG(("Attempting to bind %" PRIu32 " addresses for %s"),
             num_addrs, skSockaddrArrayGetHostPortPair(listen_address));
    pfd = base->pfd;
    for (i = 0; i < num_addrs; ++i) {
        addr = skSockaddrArrayGet(listen_address, i);
        skSockaddrString(addr_name, sizeof(addr_name), addr);

        /* get a socket */
        pfd->fd = socket(addr->sa.sa_family, SOCK_DGRAM, 0);
        if (pfd->fd == -1) {
            DEBUGMSG("Skipping %s: Unable to create dgram socket: %s",
                     addr_name, strerror(errno));
            continue;
        }
        /* bind socket to address/port */
        if (bind(pfd->fd, &addr->sa, skSockaddrGetLen(addr)) == -1) {
            DEBUGMSG("Skipping %s: Unable to bind: %s",
                     addr_name, strerror(errno));
            close(pfd->fd);
            pfd->fd = -1;
            continue;
        }
        DEBUGMSG("Bound %s for listening", addr_name);
        pfd->events = POLLIN;

        /* which port are we using? */
        if (0 == port) {
            port = skSockaddrGetPort(addr);
        } else if (port != skSockaddrGetPort(addr)) {
            /* all ports in the listen_address array must be the same */
            CRITMSG("Different ports found in %s: %u vs %u",
                    skSockaddrArrayGetHostname(listen_address),
                    port, skSockaddrGetPort(addr));
            skAbort();
        }

        ++pfd_valid;
        ++pfd;
    }

    if (0 == pfd_valid) {
        ERRMSG("Failed to bind any addresses for %s",
               skSockaddrArrayGetHostPortPair(listen_address));
        goto ERROR;
    }

    DEBUGMSG(("Bound %" PRIu32 "/%" PRIu32 " addresses for %s"),
             pfd_valid, skSockaddrArrayGetSize(listen_address),
             skSockaddrArrayGetHostPortPair(listen_address));

    /* set any remaining file descriptors to -1 */
    for (i = pfd_valid; i < num_addrs; ++i) {
        base->pfd[i].fd = -1;
    }

    assert(port != 0);

    /* fill the 'base' data structure */
    base->name = strdup(skSockaddrArrayGetHostPortPair(listen_address));
    if (NULL == base->name) {
        goto ERROR;
    }
    base->listen_address = listen_address;
    base->pfd_valid = pfd_valid;
    base->pfd_len = pfd_valid;

    /* push base onto the global list of bases */
    if (NULL == pdu_net_base_list) {
        pdu_net_base_list = skDLListCreate(NULL);
        if (pdu_net_base_list == NULL) {
            skAppPrintOutOfMemory("global pdu_net_base_t list");
            goto ERROR;
        }
    }
    if (0 != skDLListPushTail(pdu_net_base_list, base)) {
        goto ERROR;
    }

    /* start the collection thread */
    pthread_mutex_lock(&base->mutex);
    rv = skthread_create(
        base->name, &base->thread, &pdu_net_base_reader, (void*)base);
    if (rv != 0) {
        pthread_mutex_unlock(&base->mutex);
        WARNINGMSG("Unable to spawn new collection thread for '%s': %s",
                   base->name, strerror(rv));
        skDLListPopTail(pdu_net_base_list, NULL);
        goto ERROR;
    }

    /* wait for the thread to finish initializing before returning. */
    while (!base->running) {
        pthread_cond_wait(&base->cond, &base->mutex);
    }
    pthread_mutex_unlock(&base->mutex);

    /* update globals */
    sockets_count += pfd_valid;
    adjust_socketbuffers();

    return base;

  ERROR:
    if (pdu_net_base_list && skDLListIsEmpty(pdu_net_base_list)) {
        skDLListDestroy(pdu_net_base_list);
        pdu_net_base_list = NULL;
    }
    if (base) {
        /* close sockets */
        for (i = 0, pfd = base->pfd; i < pfd_valid; ++i, ++pfd) {
            close(pfd->fd);
        }
        pthread_mutex_destroy(&base->mutex);
        pthread_cond_destroy(&base->cond);
        free(base->pfd);
        free((void*)base->name);
        free(base);
    }
    return NULL;
}


/**
 *    Search for an existing base object listening on
 *    'listen_address'.  If one is found, store its location in
 *    'base_ret' and return 1.
 *
 *    If no existing base object is found, return 0.
 *
 *    If an existing base object is found but its addresses do not
 *    match exactly, return -1.
 */
static int
pdu_net_base_search(
    pdu_net_base_t            **base_ret,
    const sk_sockaddr_array_t  *listen_address)
{
    pdu_net_base_t *base;
    sk_dll_iter_t iter;

    assert(base_ret);
    assert(listen_address);

    ASSERT_MUTEX_LOCKED(&pdu_net_base_list_mutex);

    *base_ret = NULL;
    if (NULL == pdu_net_base_list) {
        return 0;
    }
    skDLLAssignIter(&iter, pdu_net_base_list);
    while (skDLLIterForward(&iter, (void **)&base) == 0) {
        if (skSockaddrArrayEqual(listen_address, base->listen_address,
                                 SK_SOCKADDRCOMP_NOT_V4_AS_V6))
        {
            if (!skSockaddrArrayEqual(listen_address, base->listen_address,
                                      SK_SOCKADDRCOMP_NOT_V4_AS_V6))
            {
                /* error: sources that listen to the same address must
                 * listen to *all* the same addresses. */
                return -1;
            }
            /* found it */
            *base_ret = base;
            return 1;
        }
        if (skSockaddrArrayMatches(listen_address, base->listen_address,
                                   SK_SOCKADDRCOMP_NOT_V4_AS_V6))
        {
            /* if two arrays match imperfectly, bail out */
            return -1;
        }
    }
    /* not found */
    return 0;
}


/**
 *    Stop a skPDUSource_t that is listening on the network.
 *
 *    Mark the network-source object as stopped.  Decrement the number
 *    of active sources on the associated base.  If that value is
 *    zero, wait for the base to change its 'running' value to 0.
 *
 *    Unblock the circular buffer.
 */
static void
pdu_network_stop(
    skPDUSource_t      *pdu_src)
{
    pdu_network_t *net_src;
    pdu_net_base_t *base;

    assert(pdu_src);
    assert(pdu_src->net);

    net_src = pdu_src->net;
    if (!net_src->stopped) {
        base = net_src->base;

        /* Mark the source as stopped */
        net_src->stopped = 1;

        /* Notify the base that the source has stopped */
        if (base) {
            pthread_mutex_lock(&base->mutex);

            /* Decrement the base's active source count */
            assert(base->active_sources);
            --base->active_sources;

            /* If the count has reached zero, wait for the base thread
             * to stop running. */
            if (base->active_sources == 0) {
                while (base->running) {
                    pthread_cond_wait(&base->cond, &base->mutex);
                }
            }
            pthread_mutex_unlock(&base->mutex);
        }

        /* Unblock the data buffer */
        if (net_src->circbuf) {
            sk_circbuf_stop(net_src->circbuf);
        }
    }
}


/**
 *    Destroy the 'net' member of the 'pdu_src' that supports
 *    listening on the network.
 *
 *    Decrement the number of sources on the associated base.  If that
 *    value is zero, destroy the base object.
 *
 *    Destroy the circular buffer and free the pdu_network_t object.
 */
static void
pdu_network_destory(
    skPDUSource_t      *pdu_src)
{
    const sk_sockaddr_array_t **accept_from;
    const pdu_addr2source_t *addr_src;
    pdu_addr2source_t target;
    pdu_net_base_t *base;
    uint32_t accept_from_count;
    uint32_t i;
    uint32_t j;

    if (NULL == pdu_src || NULL == pdu_src->net) {
        return;
    }

    /* Stop the source */
    if (!pdu_src->net->stopped) {
        pdu_network_stop(pdu_src);
    }

    base = pdu_src->net->base;

    if (NULL == base) {
        if (pdu_src->net->circbuf) {
            sk_circbuf_destroy(pdu_src->net->circbuf);
        }
        free(pdu_src->net);
        pdu_src->net = NULL;
        return;
    }

        pthread_mutex_lock(&base->mutex);

        if (base->any) {
            /* there should be a one-to-one mapping between the base
             * and the source */
            assert(skpcProbeGetAcceptFromHost(pdu_src->probe, NULL) == 0);
            assert(base->any == pdu_src);
            base->any = NULL;

            if (base->refcount != 1) {
                ERRMSG("Unexpected reference count %" PRIu32, base->refcount);
            }
            base->refcount = 0;
        } else {
            accept_from_count
                = skpcProbeGetAcceptFromHost(pdu_src->probe, &accept_from);

            if (accept_from && base->addr2source) {
                /* remove the source's accept_from from
                 * base->addr2source */
                for (j = 0; j < accept_from_count; ++j) {
                    for (i = 0; i < skSockaddrArrayGetSize(accept_from[j]);++i)
                    {
                        target.addr = skSockaddrArrayGet(accept_from[j], i);
                        addr_src = ((const pdu_addr2source_t*)
                                    rbdelete(&target, base->addr2source));
                        assert(addr_src->pdusrc == pdu_src);
                        free((void*)addr_src);
                    }
                }
            }

            /* decref and possibly delete the base */
            assert(base->refcount);
            --base->refcount;
        }

        if (base->refcount) {
            pthread_mutex_unlock(&base->mutex);
        } else {
            pthread_mutex_unlock(&base->mutex);
            pdu_net_base_destroy(base);
        }

    if (pdu_src->net->circbuf) {
        sk_circbuf_destroy(pdu_src->net->circbuf);
    }
    free(pdu_src->net);
    pdu_src->net = NULL;
}


/**
 *    Create a new network-source object that wraps 'base' and store
 *    that object on the 'net' member of 'pdu_src'.  This is a helper
 *    function for pdu_network_create().
 *
 *    Create any data strucures required for the network-source object
 *    and the 'base' object to cooperate.
 */
static int
pdu_network_create_helper(
    skPDUSource_t      *pdu_src,
    pdu_net_base_t     *base)
{
    const sk_sockaddr_array_t **accept_from;
    const pdu_addr2source_t *found;
    pdu_addr2source_t *addr_src;
    uint32_t max_pkts;
    pdu_network_t *net_src;
    size_t accept_from_count;
    size_t i;
    size_t j;
    int modified_rbtree;

    /* whether this function modified the red/black tree on base */
    modified_rbtree = 0;
    i = 0;

    /* get data we need from the probe */
    accept_from_count = skpcProbeGetAcceptFromHost(pdu_src->probe,
                                                   &accept_from);
    max_pkts = (uint32_t)skpcProbeGetMaximumBuffer(pdu_src->probe);

    /* create and initialize a new network-based source object */
    net_src = (pdu_network_t*)calloc(1, sizeof(pdu_network_t));
    if (NULL == net_src) {
        skAppPrintOutOfMemory("pdu_network_t");
        return -1;
    }

    /* create the circular buffer */
    if (sk_circbuf_create_const_itemsize(
            &net_src->circbuf, sizeof(v5PDU), max_pkts))
    {
        goto ERROR;
    }
    net_src->circbuf_pos = (v5PDU*)sk_circbuf_get_write_pos(net_src->circbuf);
    if (NULL == net_src->circbuf_pos) {
        ERRMSG("Write position of newly created circular buffer is NULL");
        goto ERROR;
    }

    /* lock the base */
    pthread_mutex_lock(&base->mutex);

    if (NULL == accept_from) {
        /* source accepts packets from any address.  By definition
         * there is a one-to-one mapping between source and base; this
         * must be a newly created base. */
        if (base->any || base->addr2source
            || base->refcount || base->active_sources)
        {
            CRITMSG("Expected unused base object for promiscuous source");
            skAbort();
        }

        /* update the pointers: pdu_src to net_src, net_src to base,
         * and base to pdu_src */
        pdu_src->net = net_src;
        net_src->base = base;
        base->any = pdu_src;

        ++base->refcount;
        ++base->active_sources;

        pthread_cond_broadcast(&base->cond);
        pthread_mutex_unlock(&base->mutex);

        return 0;
    }

    /* otherwise, we need to update the base so that it knows packets
     * coming from the 'accept_from' should be processed by this
     * source */
    if (base->any) {
        CRITMSG("Base object is promiscuous and source is not");
        skAbort();
    }
    if (NULL == base->addr2source) {
        base->addr2source = rbinit(&pdu_addr2source_compare, NULL);
        if (NULL == base->addr2source) {
            skAppPrintOutOfMemory("Red black tree");
            goto ERROR;
        }
    }

    for (j = 0; j < accept_from_count; ++j) {
    for (i = 0; i < skSockaddrArrayGetSize(accept_from[j]); ++i) {
        /* create the mapping between this accept_from and the
         * pdu_src */
        addr_src = (pdu_addr2source_t*)calloc(1, sizeof(pdu_addr2source_t));
        if (addr_src == NULL) {
            goto ERROR;
        }
        addr_src->pdusrc = pdu_src;
        addr_src->addr = skSockaddrArrayGet(accept_from[j], i);

        /* add the accept_from to the tree */
        found = ((const pdu_addr2source_t*)
                 rbsearch(addr_src, base->addr2source));
        if (found != addr_src) {
            if (found && (found->pdusrc == addr_src->pdusrc)) {
                /* duplicate address, same connection */
                free(addr_src);
                continue;
            }
            /* memory error adding to tree */
            free(addr_src);
            goto ERROR;
        }
        modified_rbtree = 1;
    }
    }

#if DEBUG_ACCEPT_FROM
    {
        char addr_buf[2 * SK_NUM2DOT_STRLEN];
        RBLIST *iter;

        iter = rbopenlist(base->addr2source);
        while ((addr_src = (pdu_addr2source_t*)rbreadlist(iter)) != NULL) {
            skSockaddrString(addr_buf, sizeof(addr_buf), addr_src->addr);
            DEBUGMSG("Base '%s' accepts packets from '%s'",
                     base->name, addr_buf);
        }
        rbcloselist(iter);
    }
#endif  /* DEBUG_ACCEPT_FROM */

    /* update the pointers: pdu_src to net_src, and net_src to base */
    pdu_src->net = net_src;
    net_src->base = base;

    ++base->refcount;
    ++base->active_sources;

    pthread_cond_broadcast(&base->cond);
    pthread_mutex_unlock(&base->mutex);

    return 0;

  ERROR:
    if (modified_rbtree) {
        /* Ugh! Memory error somewhere in the red-black tree setup.
         * Attempt to roll-back.... */
        pdu_addr2source_t peer_target;
        const size_t outer_added = j;
        const size_t added = i;
        size_t max_inner;

        for (j = 0; j <= outer_added; ++j) {
            max_inner = ((j < outer_added)
                         ? skSockaddrArrayGetSize(accept_from[j])
                         : added);
            for (i = 0; i < max_inner; ++i) {
                peer_target.addr = skSockaddrArrayGet(accept_from[j], i);
                found = ((const pdu_addr2source_t*)
                         rbdelete(&peer_target, base->addr2source));
                if (found) {
                    assert(found->pdusrc == pdu_src);
                    free((void*)found);
                }
            }
        }
    }
    if (net_src->circbuf) {
        sk_circbuf_destroy(net_src->circbuf);
    }
    free(net_src);

    pthread_cond_broadcast(&base->cond);
    pthread_mutex_unlock(&base->mutex);
    return -1;
}


/**
 *    Create a new network-source object and store that object on the
 *    'net' member of 'pdu_src'.
 *
 *    This function either creates a pdu_net_base_t object or finds an
 *    existing one that is listening on the same port as 'pdu_src'.
 *    Once the base object exists, call pdu_network_create_helper() to
 *    actually create the pdu_network_t.
 */
static int
pdu_network_create(
    skPDUSource_t      *pdu_src)
{
    const sk_sockaddr_array_t *listen_address;
    pdu_net_base_t *base;
    int base_search;

    assert(pdu_src);

    /* This must be a network-based probe */
    if (-1 == skpcProbeGetListenOnSockaddr(pdu_src->probe, &listen_address)) {
        return -1;
    }

    /* look over the existing bases to see if we have already created
     * a listener on this port */
    pthread_mutex_lock(&pdu_net_base_list_mutex);
    base_search = pdu_net_base_search(&base, listen_address);
    if (-1 == base_search) {
        goto ERROR;
    }
    if (0 == base_search) {
        /* no existing base was found, so create one */
        base = pdu_net_base_create(listen_address);
        if (base == NULL) {
            goto ERROR;
        }
    }

    /* create the network-based source (pdu_src->net) as a wrapper
     * over 'base' */
    if (pdu_network_create_helper(pdu_src, base)) {
        goto ERROR;
    }

    /* successful */
    pthread_mutex_unlock(&pdu_net_base_list_mutex);
    return 0;

  ERROR:
    pthread_mutex_unlock(&pdu_net_base_list_mutex);

    /* on error, destory the base if this function created it.  */
    if (0 == base_search && base) {
        /* this function may lock pdu_net_base_list_mutex, so be sure
         * to unlock it before calling it. */
        pdu_net_base_destroy(base);
    }
    return -1;
}


/**
 *    Return the next PDU packet received from the network, or return
 *    NULL if the network-source has been stopped.  If no packet is
 *    available, block until a packet becomes available or the source
 *    is stopped.
 */
static v5PDU*
pdu_network_get_packet(
    skPDUSource_t      *pdu_src)
{
    int stopped;

    assert(pdu_src);
    assert(pdu_src->net);
    assert(pdu_src->net->base);

    pthread_mutex_lock(&pdu_src->net->base->mutex);
    stopped = pdu_src->net->base->stop;
    pthread_mutex_unlock(&pdu_src->net->base->mutex);

    if (stopped) {
        return NULL;
    }

    /* network based UDP source. sk_circbuf_get_read_pos() will block
     * until data is ready */
    if (pdu_src->net->circbuf) {
        return (v5PDU*)sk_circbuf_get_read_pos(pdu_src->net->circbuf);
    }
    return NULL;
}


/**
 *    Destroy the 'file' member of the 'pdu_src' struct that supports
 *    reading from a file.
 *
 *    Destroy the associated stream object.
 */
static void
pdu_file_destroy(
    skPDUSource_t      *pdu_src)
{
    int rv;

    if (NULL == pdu_src || NULL == pdu_src->file) {
        return;
    }

    pthread_mutex_lock(&pdu_src->file->mutex);
    rv = skStreamDestroy(&pdu_src->file->stream);
    if (rv) {
        skStreamPrintLastErr(pdu_src->file->stream, rv, ERRMSG);
    }
    pthread_mutex_unlock(&pdu_src->file->mutex);
    pthread_mutex_destroy(&pdu_src->file->mutex);
    free(pdu_src->file);
    pdu_src->file = NULL;
}


/**
 *    Create a new file-based source object and store that object on
 *    the 'file' member of 'pdu_src'.  'path_name' is the file to
 *    process.
 */
static int
pdu_file_create(
    skPDUSource_t      *pdu_src,
    const char         *path_name)
{
    pdu_file_t *file_src = NULL;
    int rv;

    assert(pdu_src);
    assert(path_name);

    /* Create and initialize file structure */
    file_src = (pdu_file_t*)calloc(1, sizeof(pdu_file_t));
    if (NULL == file_src) {
        skAppPrintOutOfMemory("pdu_file_t");
        return -1;
    }

    /* Create a stream to read the file as raw bytes */
    if ((rv = skStreamCreate(&file_src->stream, SK_IO_READ,
                             SK_CONTENT_OTHERBINARY))
        || (rv = skStreamBind(file_src->stream, path_name))
        || (rv = skStreamOpen(file_src->stream)))
    {
        skStreamPrintLastErr(file_src->stream, rv, &ERRMSG);
        skStreamDestroy(&file_src->stream);
        free(file_src);
        return -1;
    }

    pthread_mutex_init(&file_src->mutex, NULL);
    pdu_src->file = file_src;

    return 0;
}


/**
 *    Return the next PDU packet read from the file, or return NULL if
 *    the file is empty or encounters an error.
 */
static v5PDU*
pdu_file_get_packet(
    skPDUSource_t      *pdu_src)
{
    pdu_file_t *file_src = pdu_src->file;
    const ssize_t expected = sizeof(file_src->file_buffer);
    ssize_t size;

    pthread_mutex_lock(&file_src->mutex);
    while ((size = skStreamRead(file_src->stream, &file_src->file_buffer,
                                expected))
           == expected)
    {
        if (pdu_reject_packet(pdu_src, &file_src->file_buffer, size)) {
            /* reject the packet */
            continue;
        }
        pthread_mutex_unlock(&file_src->mutex);
        return &file_src->file_buffer;
    }

    /* error, end of file, or short read */
    if (size == -1) {
        skStreamPrintLastErr(file_src->stream, size, NOTICEMSG);
    } else if (size > 0) {
        INFOMSG(("Short read; read %" SK_PRIdZ " of %" SK_PRIdZ
                 " expected"),
                size, expected);
    }
    pthread_mutex_unlock(&file_src->mutex);
    return NULL;
}


/**
 *  pdu_packet = pdu_get_packet(pdu_src);
 *
 *    Get the next PDU packet to process.
 *
 *    This function processes the packet's header, sets the timestamp
 *    for the flows in the packet, and checks the flow sequence
 *    numbers.
 */
static const v5PDU*
pdu_get_packet(
    skPDUSource_t      *pdu_src)
{
    /* For log messages that report out of sequence flow records,
     * these macros hold the start of the format and the start of the
     * argument list  */
#define PDU_OOS_FORMAT(diff_is_neg)                                     \
    "'%s': Out-of-sequence packet:"                                     \
        " expecting %" PRIu32 ", received %" PRIu32                     \
        ", difference " diff_is_neg "%" PRId64 ", elapsed %f sec"       \
        ", engine %u.%u;"
#define PDU_OOS_ARGS(diff_value)                                \
    pdu_src->name,                                              \
        engine->flow_sequence, flow_sequence, diff_value,       \
        ((float)(now - engine->last_timestamp) / 1000.0),       \
        engine->id >> 8, engine->id & 0xFF

    const v5PDU       *pdu;
    intmax_t           now;
    uint16_t           count;
    uint32_t           flow_sequence;
    intmax_t           router_boot;
    intmax_t           sysUptime;
    int64_t            seq_differ;
    uint64_t           allrecs;
    pdu_engine_info_t *engine;
    pdu_engine_info_t  target;

    assert(pdu_src != NULL);

    if (pdu_src->file) {
        pdu = pdu_file_get_packet(pdu_src);
    } else {
        pdu = pdu_network_get_packet(pdu_src);
    }
    if (pdu == NULL) {
        /* if we saw any bad PDUs, print message before returning */
        if (pdu_src->badpdu_status != PDU_OK
            && pdu_src->badpdu_consec)
        {
            NOTICEMSG(("'%s': Rejected %" PRIu32 " additional PDU record%s %s"),
                      pdu_src->name, pdu_src->badpdu_consec,
                      ((pdu_src->badpdu_consec == 1) ? "" : "s"),
                      pdu_badpdu_msgs[pdu_src->badpdu_status]);
            pdu_src->badpdu_status = PDU_OK;
        }
        return NULL;
    }

    /* number of flow records in this packet */
    count = ntohs(pdu->hdr.count);

    /* get the sequence number */
    flow_sequence = ntohl(pdu->hdr.flow_sequence);

    /* use the PDU header to get the "current" time as
     * milliseconds since the UNIX epoch. */
    now = ((intmax_t)1000 * ntohl(pdu->hdr.unix_secs)
           + (ntohl(pdu->hdr.unix_nsecs) / 1000000));

    /* get sysUptime, which is the "current" time in milliseconds
     * since the export device booted */
    sysUptime = ntohl(pdu->hdr.SysUptime);

    /* subtract sysUptime from current-time to get router boot time as
     * milliseconds since UNIX epoch */
    router_boot = now - sysUptime;

    /* Determine the current engine */
    target.id = ((uint16_t)pdu->hdr.engine_type << 8) | pdu->hdr.engine_id;
    engine = pdu_src->engine_info;
    if (engine == NULL || engine->id != target.id) {
        /* Current engine info must be updated */
        engine = (pdu_engine_info_t*)rbfind(&target, pdu_src->engine_info_tree);
        if (engine == NULL) {
            /* There's no entry for this engine.  Add one */
            TRACEMSG(1, ("'%s': New engine %u.%u noticed",
                         pdu_src->name, target.id >> 8, target.id & 0xFF));
            engine = (pdu_engine_info_t*)calloc(1, sizeof(pdu_engine_info_t));
            if (engine == NULL) {
                ERRMSG(("'%s': Memory allocation error allocating"
                        " PDU engine %u.%u.  Aborting."),
                       pdu_src->name, target.id >> 8, target.id & 0xFF);
                exit(EXIT_FAILURE);
            }
            engine->id = target.id;
            engine->router_boot = router_boot;
            engine->sysUptime = sysUptime;
            engine->flow_sequence = flow_sequence;
            rbsearch(engine, pdu_src->engine_info_tree);
        }
        pdu_src->engine_info = engine;
    }

    /* check for router reboot.  Determine whether the absolute
     * value of
     *   (router_boot - engine->router_boot)
     * is greater than ROUTER_BOOT_FUZZ.  If so, assume router
     * rebooted and reset the engine values. */
    if (((router_boot > engine->router_boot)
         && ((router_boot - engine->router_boot) > ROUTER_BOOT_FUZZ))
        || ((router_boot - engine->router_boot) < -ROUTER_BOOT_FUZZ))
    {
        if (pdu_src->logopt & SOURCE_LOG_TIMESTAMPS) {
            INFOMSG(("'%s': Router reboot for engine %u.%u."
                     " Last time %" PRIdMAX ", Current time %" PRIdMAX),
                    pdu_src->name, engine->id >> 8, engine->id & 0xFF,
                    engine->router_boot, router_boot);
        } else {
            DEBUGMSG(("'%s': Router reboot for engine %u.%u."
                      " Last time %" PRIdMAX ", Current time %" PRIdMAX),
                     pdu_src->name, engine->id >> 8, engine->id & 0xFF,
                     engine->router_boot, router_boot);
        }
        engine->flow_sequence = flow_sequence;
    }
    engine->router_boot = router_boot;
    engine->sysUptime = sysUptime;

    /* handle sequence numbers */
    if (flow_sequence == engine->flow_sequence) {
        /* This packet is in sequence.  Update the next expected seq */
        engine->flow_sequence = flow_sequence + count;

    } else if (flow_sequence > engine->flow_sequence) {
        /* received is greater than expected */
        seq_differ = (flow_sequence - engine->flow_sequence);

        if (seq_differ < maximumSequenceDeviation) {
            /* assume dropped packets; increase the missing flow
             * record count, and update the expected sequence
             * number */
            pthread_mutex_lock(&pdu_src->stats_mutex);
            pdu_src->statistics.missingRecs += seq_differ;
            if (pdu_src->logopt & SOURCE_LOG_MISSING) {
                allrecs = (pdu_src->statistics.goodRecs +
                           pdu_src->statistics.badRecs +
                           pdu_src->statistics.missingRecs);
                INFOMSG((PDU_OOS_FORMAT("")
                         " adding to missing records"
                         " %" PRId64 "/%" PRIu64 " == %7.4g%%"),
                        PDU_OOS_ARGS(seq_differ),
                        pdu_src->statistics.missingRecs, allrecs,
                        ((float)pdu_src->statistics.missingRecs
                         / (float)allrecs * 100.0));
            }
            pthread_mutex_unlock(&pdu_src->stats_mutex);

            /* Update the next expected seq */
            engine->flow_sequence = flow_sequence + count;

        } else if (seq_differ > (ROLLOVER32 - maximumSequenceLateArrival)) {
            /* assume expected has rolled-over and we received a
             * packet that was generated before the roll-over and is
             * arriving late; subtract from the missing record
             * count and do NOT change expected value */
            pthread_mutex_lock(&pdu_src->stats_mutex);
            pdu_src->statistics.missingRecs -= count;
            if (pdu_src->statistics.missingRecs < 0) {
                pdu_src->statistics.missingRecs = 0;
            }
            pthread_mutex_unlock(&pdu_src->stats_mutex);
            if (pdu_src->logopt & SOURCE_LOG_MISSING) {
                INFOMSG((PDU_OOS_FORMAT("")
                         " treating %u flows as arriving late after roll-over"
                         " (difference without roll-over %" PRId64 ")"),
                        PDU_OOS_ARGS(seq_differ), count,
                        seq_differ - ROLLOVER32);
            }

        } else {
            /* assume something caused the sequence numbers to change
             * radically; reset the expected sequence number and do
             * NOT add to missing record count */
            if (pdu_src->logopt & SOURCE_LOG_MISSING) {
                INFOMSG((PDU_OOS_FORMAT("")
                         " resetting sequence due to large difference;"
                         " next expected packet %" PRIu32),
                        PDU_OOS_ARGS(seq_differ), flow_sequence + count);
            }

            /* Update the next expected seq */
            engine->flow_sequence = flow_sequence + count;
        }

    } else {
        /* expected is greater than received */
        seq_differ = (engine->flow_sequence - flow_sequence);

        if (seq_differ > (ROLLOVER32 - maximumSequenceDeviation)) {
            /* assume received has rolled over but expected has not
             * and there are dropped packets; increase the missing
             * flow record count and update the expected sequence
             * number */
            pthread_mutex_lock(&pdu_src->stats_mutex);
            pdu_src->statistics.missingRecs += ROLLOVER32 - seq_differ;
            if (pdu_src->logopt & SOURCE_LOG_MISSING) {
                allrecs = (pdu_src->statistics.goodRecs +
                           pdu_src->statistics.badRecs +
                           pdu_src->statistics.missingRecs);
                INFOMSG((PDU_OOS_FORMAT("-")
                         " treating as missing packets during roll-over"
                         " (difference without roll-over %" PRId64 ");"
                         " adding to missing records"
                         " %" PRId64 "/%" PRIu64 " == %7.4g%%"),
                        PDU_OOS_ARGS(seq_differ), ROLLOVER32 - seq_differ,
                        pdu_src->statistics.missingRecs, allrecs,
                        ((float)pdu_src->statistics.missingRecs /
                         (float)allrecs * 100.0));
            }
            pthread_mutex_unlock(&pdu_src->stats_mutex);

            /* Update the next expected seq */
            engine->flow_sequence = flow_sequence + count;

        } else if (seq_differ < maximumSequenceLateArrival) {
            /* assume we received a packet that is arriving late; log
             * the fact and subtract from the missing record count */
            pthread_mutex_lock(&pdu_src->stats_mutex);
            pdu_src->statistics.missingRecs -= count;
            if (pdu_src->statistics.missingRecs < 0) {
                pdu_src->statistics.missingRecs = 0;
            }
            pthread_mutex_unlock(&pdu_src->stats_mutex);
            if (pdu_src->logopt & SOURCE_LOG_MISSING) {
                INFOMSG((PDU_OOS_FORMAT("-")
                         " treating %u flows as arriving late"),
                        PDU_OOS_ARGS(seq_differ), count);
            }

        } else {
            /* assume something caused the sequence numbers to change
             * radically; reset the expected sequence number and do
             * NOT add to missing record count */
            if (pdu_src->logopt & SOURCE_LOG_MISSING) {
                INFOMSG((PDU_OOS_FORMAT("-")
                         " resetting sequence due to large difference;"
                         " next expected packet %" PRIu32),
                        PDU_OOS_ARGS(seq_differ), flow_sequence + count);
            }

            /* Update the next expected seq */
            engine->flow_sequence = flow_sequence + count;
        }
    }

    engine->last_timestamp = (sktime_t)now;
    return pdu;
}


/**
 *  nf5_record = pdu_get_record(pdu_src);
 *
 *    Get the next NetFlow V5 record to process.
 */
static const v5Record*
pdu_get_record(
    skPDUSource_t      *pdu_src)
{
#define COUNT_BAD_RECORD(cbr_pdu_src)                           \
    {                                                           \
        pthread_mutex_lock(&((cbr_pdu_src)->stats_mutex));      \
        ++(cbr_pdu_src)->statistics.badRecs;                    \
        pthread_mutex_unlock(&((cbr_pdu_src)->stats_mutex));    \
    }

    assert(pdu_src != NULL);

    /* Infloop; exit by return only */
    for (;;) {
        const v5Record *v5RPtr;
        intmax_t  difference;

        if (pdu_src->stopped) {
            return NULL;
        }

        /* If we need a PDU, get a new one, otherwise we are not
         * finished with the last. */
        if (pdu_src->count == 0) {
            pdu_src->pdu = pdu_get_packet(pdu_src);
            if (pdu_src->pdu == NULL) {
                return NULL;
            }
            pdu_src->count = ntohs(pdu_src->pdu->hdr.count);
        }

        /* Get next record, and decrement counter*/
        v5RPtr = &pdu_src->pdu->data[ntohs(pdu_src->pdu->hdr.count)
                                     - pdu_src->count--];

        /* Check for zero packets or bytes.  No need for byteswapping
         * when checking zero. */
        if (v5RPtr->dPkts == 0 || v5RPtr->dOctets == 0) {
            if (pdu_src->logopt & SOURCE_LOG_BAD) {
                NOTICEMSG("'%s': Netflow record has zero packets or bytes",
                          pdu_src->name);
            }
            COUNT_BAD_RECORD(pdu_src);
            continue;
        }

        /* Check to see if more packets them bytes. */
        if (ntohl(v5RPtr->dPkts) > ntohl(v5RPtr->dOctets)) {
            if (pdu_src->logopt & SOURCE_LOG_BAD) {
                NOTICEMSG("'%s': Netflow record has more packets them bytes",
                          pdu_src->name);
            }
            COUNT_BAD_RECORD(pdu_src);
            continue;
        }

        /* Check to see if the First and Last timestamps for the flow
         * record are reasonable, accounting for rollover.  If the
         * absolute value of the difference is greater than
         * maximumFlowTimeDeviation, we assume it has rolled over. */
        difference = (intmax_t)ntohl(v5RPtr->Last) - ntohl(v5RPtr->First);
        if ((difference > maximumFlowTimeDeviation)
            || ((difference < 0)
                && (difference > (-maximumFlowTimeDeviation))))
        {
            if (pdu_src->logopt & SOURCE_LOG_BAD) {
                NOTICEMSG(("'%s': Netflow record has earlier end time"
                           " than start time"), pdu_src->name);
            }
            COUNT_BAD_RECORD(pdu_src);
            continue;
        }

        /* Check for bogosities in how the ICMP type/code are set.  It
         * should be in dest port, but sometimes it is backwards in
         * src port. */
        if (v5RPtr->prot == 1 &&  /* ICMP */
            v5RPtr->dstport == 0) /* No byteswapping for check against 0 */
        {
            uint32_t *ports = (uint32_t*)&v5RPtr->srcport;
            *ports = BSWAP32(*ports); /* This will swap src into dest,
                                         while byteswapping. */
        }

        pthread_mutex_lock(&pdu_src->stats_mutex);
        ++pdu_src->statistics.goodRecs;
        pthread_mutex_unlock(&pdu_src->stats_mutex);

        return v5RPtr;
    }
}


skPDUSource_t*
skPDUSourceCreate(
    const skpc_probe_t         *probe,
    const skFlowSourceParams_t *params)
{
    skPDUSource_t *pdu_src;
    int rv;

    assert(probe);

    /* Create and initialize source */
    pdu_src = (skPDUSource_t*)calloc(1, sizeof(*pdu_src));
    if (pdu_src == NULL) {
        goto ERROR;
    }
    pdu_src->probe = probe;
    pdu_src->name = skpcProbeGetName(probe);
    pdu_src->logopt = skpcProbeGetLogFlags(probe);

    pdu_src->engine_info_tree = rbinit(pdu_engine_compare, NULL);
    if (pdu_src->engine_info_tree == NULL) {
        goto ERROR;
    }

    /* Check whether this is a file-based probe---either handles a
     * single file or files pulled from a directory poll */
    if (NULL != skpcProbeGetPollDirectory(probe)
        || NULL != skpcProbeGetFileSource(probe))
    {
        if (NULL == params->path_name) {
            goto ERROR;
        }
        rv = pdu_file_create(pdu_src, params->path_name);
    } else {
        rv = pdu_network_create(pdu_src);
    }
    if (rv != 0) {
        goto ERROR;
    }

    pthread_mutex_init(&pdu_src->stats_mutex, NULL);

    return pdu_src;

  ERROR:
    if (pdu_src) {
        if (pdu_src->engine_info_tree) {
            rbdestroy(pdu_src->engine_info_tree);
        }
        free(pdu_src);
    }
    return NULL;
}


void
skPDUSourceStop(
    skPDUSource_t      *pdu_src)
{
    pdu_src->stopped = 1;
    if (pdu_src->net) {
        pdu_network_stop(pdu_src);
    }
}


void
skPDUSourceDestroy(
    skPDUSource_t      *pdu_src)
{
    RBLIST *iter;
    pdu_engine_info_t *engine_info;

    if (!pdu_src) {
        return;
    }
    if (!pdu_src->stopped) {
        skPDUSourceStop(pdu_src);
    }
    if (pdu_src->net) {
        pdu_network_destory(pdu_src);
    } else {
        pdu_file_destroy(pdu_src);
    }
    pthread_mutex_destroy(&pdu_src->stats_mutex);
    iter = rbopenlist(pdu_src->engine_info_tree);
    if (iter != NULL) {
        while ((engine_info = (pdu_engine_info_t*)rbreadlist(iter))) {
            free(engine_info);
        }
        rbcloselist(iter);
    }
    rbdestroy(pdu_src->engine_info_tree);
    free(pdu_src);
}


int
skPDUSourceGetGeneric(
    skPDUSource_t      *pdu_src,
    rwRec              *rwrec)
{
    const char *rollover_first;
    const char *rollover_last = "";
    const v5Record *v5RPtr;
    intmax_t v5_first, v5_last;
    intmax_t sTime;
    intmax_t difference;
    lua_State *L = rwrec->lua_state;

    v5RPtr = pdu_get_record(pdu_src);
    if (v5RPtr == NULL) {
        return -1;
    }

    /* v5_first and v5_last are milliseconds since the router booted.
     * To get UNIX epoch milliseconds, add the router's boot time. */
    v5_first = ntohl(v5RPtr->First);
    v5_last = ntohl(v5RPtr->Last);

    if (v5_first > v5_last) {
        /* End has rolled over, while start has not.  Adjust end by
         * 2^32 msecs in order to allow us to subtract start from end
         * and get a correct value for the duration. */
        v5_last += ROLLOVER32;
        rollover_last = ", assume Last rollover";
    }

    /* Check to see if the difference between the 32bit start time and
     * the sysUptime is overly large.  If it is, one of the two has
     * more than likely rolled over.  We need to adjust based on
     * this. */
    difference = pdu_src->engine_info->sysUptime - v5_first;
    if (difference > maximumFlowTimeDeviation) {
        /* sTime rollover */
        sTime = pdu_src->engine_info->router_boot + v5_first + ROLLOVER32;
        rollover_first = ", assume First rollover";
    } else if (difference < (-maximumFlowTimeDeviation)) {
        /* sysUptime rollover */
        sTime = pdu_src->engine_info->router_boot + v5_first - ROLLOVER32;
        rollover_first = ", assume Uptime rollover";
    } else {
        sTime = v5_first + pdu_src->engine_info->router_boot;
        rollover_first = "";
    }

    if (pdu_src->logopt & SOURCE_LOG_TIMESTAMPS) {
        INFOMSG(("'%s': Router boot (ms)=%" PRIdMAX ", Uptime=%" PRIdMAX
                 ", First=%" PRIuMAX ", Last=%" PRIu32 "%s%s"),
                pdu_src->name, pdu_src->engine_info->router_boot,
                pdu_src->engine_info->sysUptime, v5_first, ntohl(v5RPtr->Last),
                rollover_first, rollover_last);
    }

    RWREC_CLEAR(rwrec);
    rwrec->lua_state = L;

    /* Convert NetFlow v5 to SiLK */
    rwRecSetSIPv4(rwrec, ntohl(v5RPtr->srcaddr));
    rwRecSetDIPv4(rwrec, ntohl(v5RPtr->dstaddr));
    rwRecSetSPort(rwrec, ntohs(v5RPtr->srcport));
    rwRecSetDPort(rwrec, ntohs(v5RPtr->dstport));
    rwRecSetProto(rwrec, v5RPtr->prot);
    rwRecSetFlags(rwrec, v5RPtr->tcp_flags);
    rwRecSetInput(rwrec, ntohs(v5RPtr->input));
    rwRecSetOutput(rwrec, ntohs(v5RPtr->output));
    rwRecSetNhIPv4(rwrec, ntohl(v5RPtr->nexthop));
    rwRecSetStartTime(rwrec, (sktime_t)sTime);
    rwRecSetPkts(rwrec, ntohl(v5RPtr->dPkts));
    rwRecSetBytes(rwrec, ntohl(v5RPtr->dOctets));
    rwRecSetElapsed(rwrec, (uint32_t)(v5_last - v5_first));
    rwRecSetRestFlags(rwrec, 0);
    rwRecSetTcpState(rwrec, SK_TCPSTATE_NO_INFO);

    if ((L = rwrec->lua_state) != NULL) {
        /* create sidecar data for the additional fields */
        lua_createtable(L, 0, 5);
        lua_pushliteral(L, "ipClassOfService");
        lua_pushinteger(L, v5RPtr->tos);
        lua_rawset(L, -3);
        lua_pushliteral(L, "bgpSourceAsNumber");
        lua_pushinteger(L, ntohs(v5RPtr->src_as));
        lua_rawset(L, -3);
        lua_pushliteral(L, "bgpDestinationAsNumber");
        lua_pushinteger(L, ntohs(v5RPtr->dst_as));
        lua_rawset(L, -3);
        lua_pushliteral(L, "sourceIPv4PrefixLength");
        lua_pushinteger(L, v5RPtr->src_mask);
        lua_rawset(L, -3);
        lua_pushliteral(L, "destinationIPv4PrefixLength");
        lua_pushinteger(L, v5RPtr->dst_mask);
        lua_rawset(L, -3);

        rwRecSetSidecar(rwrec, luaL_ref(L, LUA_REGISTRYINDEX));
    }

    return 0;
}


/* Log statistics associated with a PDU source, and then clear the
 * statistics. */
void
skPDUSourceLogStatsAndClear(
    skPDUSource_t      *pdu_src)
{
    pthread_mutex_lock(&pdu_src->stats_mutex);
    INFOMSG(("'%s': Pkts %" PRIu64 "/%" PRIu64
             ", Recs %" PRIu64 ", MissRecs %" PRId64
             ", BadRecs %" PRIu64),
            pdu_src->name,
            (pdu_src->statistics.procPkts - pdu_src->statistics.badPkts),
            pdu_src->statistics.procPkts, pdu_src->statistics.goodRecs,
            pdu_src->statistics.missingRecs, pdu_src->statistics.badRecs);
    memset(&pdu_src->statistics, 0, sizeof(pdu_src->statistics));
    pthread_mutex_unlock(&pdu_src->stats_mutex);
}


/*
** Local Variables:
** mode:c
** indent-tabs-mode:nil
** c-basic-offset:4
** End:
*/
