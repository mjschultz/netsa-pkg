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

RCSIDENT("$SiLK: rwflowpack-pdusource.c efd886457770 2017-06-21 18:43:23Z mthomas $");

#include <poll.h>
#include <silk/redblack.h>
#include <silk/skdllist.h>
#include <silk/skpolldir.h>
#include "rwflowpack_priv.h"


#ifdef PDUSOURCE_TRACE_LEVEL
#  define TRACEMSG_LEVEL PDUSOURCE_TRACE_LEVEL
#endif
#define TRACEMSG(lvl, msg) TRACEMSG_TO_TRACEMSGLVL(lvl, msg)
#include <silk/sktracemsg.h>


/* LOCAL DEFINES AND TYPEDEFS */

/* Timeout to pass to the poll(2) system class, in milliseconds. */
#define POLL_TIMEOUT 500

/* Whether to compile in code to help debug accept-from-host */
#define DEBUG_ACCEPT_FROM 1
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
 *    sk_lua_nfv5_t is a single NetFlow record and the header of the
 *    message from which it came.
 */
struct sk_lua_nfv5_st {
    v5Header            header;
    v5Record            record;
};
/* typedef struct sk_lua_nfv5_st sk_lua_nfv5_t; */


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
 *    When the NetFlow v5 data is being read from a Berkeley socket,
 *    the following structure, a 'pdu_net_base_t', is the object that
 *    contains the file descriptors to the socket(s) bound to a single
 *    port from which data is being read.  (There will be multiple
 *    sockets when the hostname resolves to multiple addresses, but
 *    all will be bound to the same port number.)
 */
struct pdu_net_base_st {
    /* thread data */
    sk_coll_thread_t        t;

    /**    address to bind() to */
    const sk_sockaddr_array_t *listen_address;

    /**    name of address:port to bind to */
    const char             *name;

    /**   when a probe does not have an 'accept' clause, any peer may
     *    connect, and there is a one-to-one mapping between a source
     *    object and a base object.  The 'any' member points to the
     *    source, and the 'peer2probe' member must be NULL. */
    skpc_probe_t           *any;

    /**   if there is an 'accept' clause on the probe, the
     *    'peer2probe' red-black tree maps the address of the peer to
     *    a particular source object (via 'pdu_peer2probe_t'
     *    objects), and the 'any' member must be NULL. */
    struct rbtree          *peer2probe;

    /**   the probe from which this base was created.  the base is
     *    started when this probe is told to start its collector. */
    const skpc_probe_t     *start_from;

    /**    sockets to listen to */
    struct pollfd          *pfd;

    /**    number of valid entries in the 'pfd' array */
    nfds_t                  pfd_valid;

    /**    number of entries in the array when it was created */
    nfds_t                  pfd_len;

    /**    the number of 'sources' that use this 'base' */
    uint32_t                refcount;

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
 *    red-black tree (the 'peer2probe' member) that maps to all the
 *    'skPDUSource_t' that share that base.  The key in the red-black
 *    tree is the list of addresses (sk_sockaddr_t) expanded from the
 *    list of 'accept_from' addresses on the probe.
 *
 *    When there is an accept_from address and it is unique, there
 *    will still be one-to-one mapping between 'pdu_network_t' and
 *    'pdu_net_base_t', but the situation is handled as if multiple
 *    'pdu_network_t's shared a 'pdu_net_base_t'.
 */


/*
 *    pdu_peer2probe_t maps from an socket address to a probe.
 *
 *    The 'peer2probe' member of 'pdu_net_base_t' is a red-black
 *    tree whose data members are defined by the following structure,
 *    a 'pdu_peer2probe_t' object.
 *
 *    These objects are used when multiple sources listen on the same
 *    port and the sources are distinguished by the host they accept
 *    data from.  When a packet arrives, the 'pdu_net_base_t'
 *    searches the 'peer2probe' tree to find the appropriate source
 *    to give the packet to.
 *
 *    The 'peer2probe' tree uses the pdu_peer2probe_compare()
 *    comparison function.
 */
struct pdu_peer2probe_st {
    const sk_sockaddr_t *addr;
    skpc_probe_t        *probe;
};
typedef struct pdu_peer2probe_st pdu_peer2probe_t;


/*
 *    sk_conv_pdu_t maintains converter state information when
 *    converting NetFlow v5 packets to SiLK Flow records.
 */
struct sk_conv_pdu_st {
    pdu_statistics_t        statistics;
    pthread_mutex_t         stats_mutex;

    rwRec                   rec;

    /* Per-engine data.  Objects in this red-black tree are pointers
     * to pdu_engine_info_t */
    struct rbtree          *engine_info_tree;

    /* Per-engine data for most recent engine */
    pdu_engine_info_t      *engine_info;

    /* Number of consecutive bad PDUs we have seen---other than the
     * first */
    uint32_t                badpdu_consec;

    /* Why the last PDU packet was rejected; used to reduce number of
     * "bad packet" log messages */
    pdu_badpdu_status_t     badpdu_status;

    unsigned                stopped : 1;
};
typedef struct sk_conv_pdu_st sk_conv_pdu_t;



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


/* LOCAL FUNCTION PROTOTYPES */

static int
pdu_process_packet(
    skpc_probe_t       *probe,
    v5PDU              *pdu,
    ssize_t             pdu_len);


/* FUNCTION DEFINITIONS */

/**
 *    Comparison function for pdu_peer2probe_t objects used by the
 *    'peer2probe' red-black tree on the pdu_net_base_t object.
 *
 *     The tree stores pdu_peer2probe_t objects, keyed by
 *     sk_sockaddr_t address of the accepted peers.
 */
static int
pdu_peer2probe_compare(
    const void         *va,
    const void         *vb,
    const void  UNUSED(*ctx))
{
    const sk_sockaddr_t *a = ((const pdu_peer2probe_t*)va)->addr;
    const sk_sockaddr_t *b = ((const pdu_peer2probe_t*)vb)->addr;

    return skSockaddrCompare(a, b, SK_SOCKADDRCOMP_NOPORT);
}


/**
 *    Comparison function for pdu_engine_info_t objects used by the
 *    'engine_info_tree' red-black tree on the sk_conv_pdu_t object.
 */
static int
pdu_engine_compare(
    const void         *va,
    const void         *vb,
    const void  UNUSED(*ctx))
{
    const pdu_engine_info_t *a = (const pdu_engine_info_t*)va;
    const pdu_engine_info_t *b = (const pdu_engine_info_t*)vb;

    return ((a->id < b->id) ? -1 : (a->id > b->id));
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
pdu_net_base_list_search(
    pdu_net_base_t            **base_ret,
    const sk_sockaddr_array_t  *listen_address)
{
    pdu_net_base_t *base;
    sk_dll_iter_t iter;

    assert(base_ret);
    assert(listen_address);

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

static int
pdu_net_base_list_insert(
    const pdu_net_base_t   *base)
{
    pthread_mutex_lock(&pdu_net_base_list_mutex);
    if (NULL == pdu_net_base_list) {
        pdu_net_base_list = skDLListCreate(NULL);
        if (pdu_net_base_list == NULL) {
            skAppPrintOutOfMemory("global pdu_net_base_t list");
            pthread_mutex_unlock(&pdu_net_base_list_mutex);
            return -1;
        }
    }
    if (0 != skDLListPushTail(pdu_net_base_list, (void *)base)) {
        pthread_mutex_unlock(&pdu_net_base_list_mutex);
        return -1;
    }
    pthread_mutex_unlock(&pdu_net_base_list_mutex);
    return 0;
}

static void
pdu_net_base_list_remove(
    const pdu_net_base_t   *base)
{
    sk_dll_iter_t iter;
    void *b;

    pthread_mutex_lock(&pdu_net_base_list_mutex);
    if (pdu_net_base_list) {
        if (base) {
            skDLLAssignIter(&iter, pdu_net_base_list);
            while (skDLLIterForward(&iter, &b) == 0) {
                if (b == base) {
                    skDLLIterDel(&iter);
                    break;
                }
            }
        }
        if (skDLListIsEmpty(pdu_net_base_list)) {
            skDLListDestroy(pdu_net_base_list);
            pdu_net_base_list = NULL;
        }
    }
    pthread_mutex_unlock(&pdu_net_base_list_mutex);
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
    v5PDU nfv5_pkt;
    pdu_peer2probe_t target;
    socklen_t len;
    skpc_probe_t *probe = NULL;
    struct pollfd *pfd;
    const pdu_peer2probe_t *match_address;
    nfds_t i;
    ssize_t rv;

    assert(base != NULL);

    /* Communicate that the thread has started */
    pthread_mutex_lock(&base->t.mutex);
    if (STARTING != base->t.status) {
        goto END;
    }
    base->t.status = STARTED;
    pthread_cond_signal(&base->t.cond);
    pthread_mutex_unlock(&base->t.mutex);

    DEBUGMSG("NetFlowV5 listener started for %s", base->name);

    /* Main loop */
    for (;;) {
        /* to be pedantic, we should lock the mutex while checking the
         * value; however, that is probably not needed here since any
         * partially written value still indicates we want to exit the
         * loop */
        if (STARTED != base->t.status) {
            break;
        }

        /* Wait for data */
        rv = poll(base->pfd, base->pfd_len, POLL_TIMEOUT);
        if (rv < 1) {
            if (rv == -1) {
                if (errno == EINTR || errno == EAGAIN) {
                    /* Interrupted by a signal, or internal alloc
                     * failed, try again. */
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
            /* Unexpected negative value */
            continue;
        }

        /* Loop around file descriptors */
        for (i = 0, pfd = base->pfd; i < base->pfd_len; ++i, ++pfd) {
            if (pfd->revents & (POLLERR | POLLHUP | POLLNVAL)) {
                if (!(pfd->revents & POLLNVAL)) {
                    close(pfd->fd);
                }
                pfd->fd = -1;
                --base->pfd_valid;
                DEBUGMSG("Poll for %s encountered a (%s,%s,%s) condition",
                         base->name, (pfd->revents & POLLERR) ? "ERR": "",
                         (pfd->revents & POLLHUP) ? "HUP": "",
                         (pfd->revents & POLLNVAL) ? "NVAL": "");
                DEBUGMSG("Closing file handle, %d remaining",
                         (int)base->pfd_valid);
                if (0 == base->pfd_valid) {
                    goto BREAK_WHILE;
                }
                continue;
            }

            if (!(pfd->revents & POLLIN)) {
                continue;
            }

            /* Read the data */
            len = sizeof(addr);
            rv = recvfrom(pfd->fd, &nfv5_pkt, sizeof(nfv5_pkt), 0,
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
            if (base->any) {
                /* When there is no accept-from address on the probe,
                 * there is a one-to-one mapping between source and
                 * base, and all connections are permitted. */
                assert(NULL == base->peer2probe);
                probe = base->any;
            } else {
                /* Using the address of the incoming connection,
                 * search for the source object associated with this
                 * address. */
                assert(NULL != base->peer2probe);
                target.addr = &addr;
                match_address = ((const pdu_peer2probe_t*)
                                 rbfind(&target, base->peer2probe));
                if (match_address) {
                    /* we recognize the sender */
                    probe = match_address->probe;
                    base->unknown_host = 0;
#if  !DEBUG_ACCEPT_FROM
                } else if (!base->unknown_host) {
                    /* additional packets seen from one or more
                     * distinct unknown senders; ignore */
                    continue;
#endif
                } else {
                    /* first packet seen from unknown sender after
                     * receiving packet from valid sender; log */
                    char addr_buf[2 * SK_NUM2DOT_STRLEN];
                    base->unknown_host = 1;
                    skSockaddrString(addr_buf, sizeof(addr_buf), &addr);
                    INFOMSG("Ignoring packets from host %s", addr_buf);
                    continue;
                }
            }

            if (pdu_process_packet(probe, &nfv5_pkt, rv) == -1) {
                break;
            }
        } /* for (i = 0; i < base->nfds_t; i++) */
    }

  BREAK_WHILE:
    pthread_mutex_lock(&base->t.mutex);

  END:
    base->t.status = STOPPED;
    pthread_cond_broadcast(&base->t.cond);
    pthread_mutex_unlock(&base->t.mutex);

    DEBUGMSG("NetFlowV5 listener stopped for %s", base->name);

    decrement_thread_count(1);

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


static void
pdu_net_base_stop(
    pdu_net_base_t     *base)
{
    ASSERT_MUTEX_LOCKED(&base->t.mutex);

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
        /* FALLTHROUGH */
      case STOPPING:
        while (STOPPED != base->t.status) {
            pthread_cond_wait(&base->t.cond, &base->t.mutex);
        }
        break;
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

    if (NULL == base) {
        return;
    }

    pthread_mutex_lock(&base->t.mutex);
    assert(base->refcount == 0);

    pdu_net_base_stop(base);
    if (JOINED != base->t.status) {
        /* Reap thread */
        pthread_join(base->t.thread, NULL);
    }

    /* Close sockets */
    for (i = 0; i < base->pfd_len; ++i) {
        if (-1 != base->pfd[i].fd) {
            close(base->pfd[i].fd);
            base->pfd[i].fd = -1;
        }
    }
    free(base->pfd);
    base->pfd = NULL;

    /* Free peer2probe tree */
    if (base->peer2probe) {
        pdu_peer2probe_t *addr;
        RBLIST *iter;

        iter = rbopenlist(base->peer2probe);
        if (iter != NULL) {
            while ((addr = (pdu_peer2probe_t *)rbreadlist(iter)) != NULL) {
                free(addr);
            }
        }
        rbcloselist(iter);
        rbdestroy(base->peer2probe);
        base->peer2probe = NULL;
    }

    /* Remove from pdu_net_base_list list */
    pdu_net_base_list_remove(base);

    pthread_mutex_unlock(&base->t.mutex);
    pthread_mutex_destroy(&base->t.mutex);
    pthread_cond_destroy(&base->t.cond);

    free(base);
}


/**
 *    Create a base object, open and bind its sockets, but do not
 *    start its thread.
 */
static pdu_net_base_t *
pdu_net_base_create(
    const sk_sockaddr_array_t  *listen_address)
{
    const sk_sockaddr_t *addr;
    char addr_name[PATH_MAX];
    pdu_net_base_t *base;
    struct pollfd *pfd;
    uint32_t num_addrs;
    uint32_t i;

    assert(listen_address);

    /* number of addresses this base binds to */
    num_addrs = skSockaddrArrayGetSize(listen_address);
    if (0 == num_addrs) {
        return NULL;
    }

    /* create base structure */
    base = sk_alloc(pdu_net_base_t);

    pthread_mutex_init(&base->t.mutex, NULL);
    pthread_cond_init(&base->t.cond, NULL);
    base->t.thread = pthread_self();
    base->t.status = CREATED;

    base->name = skSockaddrArrayGetHostPortPair(listen_address);
    base->listen_address = listen_address;

    /* create array of poll structures on the base */
    base->pfd = sk_alloc_array(struct pollfd, num_addrs);

    /* open a socket and bind it */
    DEBUGMSG(("Attempting to bind %" PRIu32 " addresses for %s"),
             num_addrs, base->name);
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

        ++base->pfd_valid;
        ++base->pfd_len;
        ++pfd;
    }
    /* set remaining file descriptors to -1 */
    for (i = base->pfd_valid; i < num_addrs; ++i, ++pfd) {
        pfd->fd = -1;
    }

    if (0 == base->pfd_valid) {
        ERRMSG("Failed to bind any addresses for %s", base->name);
        goto ERROR;
    }
    DEBUGMSG(("Bound %" PRIu32 "/%" PRIu32 " addresses for %s"),
             (uint32_t)base->pfd_valid, num_addrs, base->name);

    /* add base onto the global list of bases */
    if (pdu_net_base_list_insert(base)) {
        goto ERROR;
    }

    /* adjust the socket buffer size */
    sockets_count += base->pfd_valid;
    adjust_socketbuffers();

    return base;

  ERROR:
    pdu_net_base_destroy(base);
    return NULL;
}


/**
 *    Start a base object and its associated thread.
 *
 *    Start a pdu_net_base_t, start its sockets, start the thread to
 *    process data, and add the base to the global list of bases.
 *
 *    On error, the function is expected to clean up all data
 *    structures it has startd (including the global list of bases)
 *    and close all sockets it has opened.
 */
static int
pdu_net_base_start(
    pdu_net_base_t             *base)
{
    int rv;

    assert(base);
    assert(base->pfd_valid);

    /* start the collection thread */
    pthread_mutex_lock(&base->t.mutex);
    base->t.status = STARTING;
    increment_thread_count();
    rv = skthread_create(
        base->name, &base->t.thread, &pdu_net_base_reader, (void*)base);
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
    return rv;
}


/**
 *    Stop a sk_coll_pdu_t that is listening on the network.
 *
 *    Mark the network-source object as stopped.  Decrement the number
 *    of active sources on the associated base.  If that value is
 *    zero, wait for the base to change its 'running' value to 0.
 */
void
sk_coll_pdu_stop(
    skpc_probe_t       *probe)
{
    pdu_net_base_t *base;

    assert(probe);
    assert(PROBE_ENUM_NETFLOW_V5 == skpcProbeGetType(probe));
    assert(NULL == skpcProbeGetPollDirectory(probe)
           && NULL == skpcProbeGetFileSource(probe));
    assert(0 == skpcProbeGetListenOnSockaddr(probe, NULL));

    base = (pdu_net_base_t *)probe->coll.network;
    if (base) {
        pthread_mutex_lock(&base->t.mutex);
        pdu_net_base_stop(base);
        pthread_mutex_unlock(&base->t.mutex);
    }
}


/**
 *    Destroy the 'net' member of the 'pdu_conv' that supports
 *    listening on the network.
 *
 *    Decrement the number of sources on the associated base.  If that
 *    value is zero, destroy the base object.
 *
 *    Free the sk_coll_pdu_t object.
 */
void
sk_coll_pdu_destroy(
    skpc_probe_t       *probe)
{
    pdu_net_base_t *base;

    assert(probe);
    assert(PROBE_ENUM_NETFLOW_V5 == skpcProbeGetType(probe));

    base = (pdu_net_base_t *)probe->coll.network;
    if (NULL == base) {
        return;
    }

    pthread_mutex_lock(&base->t.mutex);
    pdu_net_base_stop(base);

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
    pdu_net_base_destroy(base);
    probe->coll.network = NULL;
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
 *    This is a helper function for sk_coll_pdu_create().
 */
static int
sk_coll_pdu_create_helper(
    skpc_probe_t       *probe,
    pdu_net_base_t     *base)
{
    const sk_sockaddr_array_t **accept_from;
    const pdu_peer2probe_t *found;
    pdu_peer2probe_t *addr_src;
    size_t accept_from_count;
    size_t i;
    size_t j;

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

        return 0;
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
        base->peer2probe = rbinit(&pdu_peer2probe_compare, NULL);
        if (NULL == base->peer2probe) {
            skAppPrintOutOfMemory("Red black tree");
            return -1;
        }
        assert(0 == base->refcount);
        assert(NULL == base->start_from);
    }

    for (j = 0; j < accept_from_count; ++j) {
    for (i = 0; i < skSockaddrArrayGetSize(accept_from[j]); ++i) {
        /* create the mapping between this accept_from and the
         * probe */
        addr_src = sk_alloc(pdu_peer2probe_t);
        addr_src->probe = probe;
        addr_src->addr = skSockaddrArrayGet(accept_from[j], i);

        /* add the accept_from to the tree */
        found = ((const pdu_peer2probe_t*)
                 rbsearch(addr_src, base->peer2probe));
        if (found != addr_src) {
            if (found && (found->probe == addr_src->probe)) {
                /* duplicate address, same connection */
                free(addr_src);
                continue;
            }
            /* memory error adding to tree */
            free(addr_src);
            return -1;
        }
    }
    }

#if DEBUG_ACCEPT_FROM
    {
        char addr_buf[2 * SK_NUM2DOT_STRLEN];
        RBLIST *iter;

        iter = rbopenlist(base->peer2probe);
        while ((addr_src = (pdu_peer2probe_t *)rbreadlist(iter)) != NULL) {
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

    return 0;
}


/**
 *    Create a new network collector object and store that object on
 *    the probe.
 *
 *    This function either creates a pdu_net_base_t object or finds an
 *    existing one that is listening on the same port as 'probe'.
 *    Once the base object exists, call sk_coll_pdu_create_helper() to
 *    connect the base and the probe.
 */
int
sk_coll_pdu_create(
    skpc_probe_t       *probe)
{
    const sk_sockaddr_array_t *listen_address;
    pdu_net_base_t *base;
    int base_search;

    assert(probe);
    assert(PROBE_ENUM_NETFLOW_V5 == skpcProbeGetType(probe));
    assert(NULL == skpcProbeGetPollDirectory(probe)
           && NULL == skpcProbeGetFileSource(probe));
    assert(SKPROBE_COLL_NETWORK == probe->coll_type);
    assert(NULL == probe->coll.network);

    /* This must be a network-based probe */
    if (-1 == skpcProbeGetListenOnSockaddr(probe, &listen_address)) {
        CRITMSG("Cannot get listen address");
        skAbort();
    }

    /* search the existing bases to see if we have already created a
     * base that will listen on this port */
    base_search = pdu_net_base_list_search(&base, listen_address);
    if (-1 == base_search) {
        /* address mismatch */
        goto ERROR;
    }
    if (0 == base_search) {
        /* no existing base was found, so create one */
        base = pdu_net_base_create(listen_address);
        if (base == NULL) {
            goto ERROR;
        }
    }

    /* create a mapping between the base and the probe */
    if (sk_coll_pdu_create_helper(probe, base)) {
        goto ERROR;
    }

    /* successful */
    return 0;

  ERROR:
    /* on error, destroy the base if this function created it.  */
    if (0 == base_search && base) {
        base->t.status = JOINED;
        pdu_net_base_destroy(base);
    }
    return -1;
}


int
sk_coll_pdu_start(
    skpc_probe_t       *probe)
{
    pdu_net_base_t *base;

    assert(probe);
    assert(PROBE_ENUM_NETFLOW_V5 == skpcProbeGetType(probe));
    assert(NULL == skpcProbeGetPollDirectory(probe)
           && NULL == skpcProbeGetFileSource(probe));
    assert(0 == skpcProbeGetListenOnSockaddr(probe, NULL));

    base = (pdu_net_base_t *)probe->coll.network;
    assert(base);
    assert(base->start_from);

    if (base->start_from == probe) {
        return pdu_net_base_start(base);
    }
    return 0;
}


int
sk_conv_pdu_stream(
    skpc_probe_t       *probe,
    skstream_t         *stream)
{
    ssize_t rv;
    v5PDU cur_pkt;
    const ssize_t expected = sizeof(cur_pkt);
    int move_to_error_dir;

    move_to_error_dir = 1;
    while ((rv = skStreamRead(stream, &cur_pkt, expected)) == expected) {
        if (pdu_process_packet(probe, &cur_pkt, rv)) {
            move_to_error_dir = 1;
            rv = 0;
            break;
        }
        move_to_error_dir = 0;
    }

    /* end of file, error, or short read */
    if (rv == -1) {
        skStreamPrintLastErr(stream, rv, NOTICEMSG);
    } else if (rv > 0) {
        INFOMSG("'%s': Short read; read %" SK_PRIdZ" of %" SK_PRIdZ" expected",
                skpcProbeGetName(probe), rv, expected);
    }
    INFOMSG("'%s': Processed file '%s'",
            skpcProbeGetName(probe), skStreamGetPathname(stream));

    return move_to_error_dir;
}


/*
 *  ******************************************************************
 *
 *  Functions for processing a PDU packet and the records it contains
 *
 *  ******************************************************************
 */


#define PDU_LOG_ADDITIONAL_BAD(m_pdu_conv, m_probe_name)                \
    if (PDU_OK == (m_pdu_conv)->badpdu_status) { /* no-op */ } else {   \
        if ((m_pdu_conv)->badpdu_consec) {                              \
            NOTICEMSG(("'%s': Rejected %" PRIu32                        \
                       " additional PDU record%s %s"),                  \
                      (m_probe_name), (m_pdu_conv)->badpdu_consec,      \
                      (((m_pdu_conv)->badpdu_consec == 1) ? "" : "s"),  \
                      pdu_badpdu_msgs[(m_pdu_conv)->badpdu_status]);    \
        }                                                               \
        (m_pdu_conv)->badpdu_status = PDU_OK;                           \
    }

/**
 *  pdu_reject_packet(probe, reason);
 *
 *    Given that a PDU was rejected due to 'reason', update the
 *    statistics on 'probe' that keep track of rejected packets.
 *    Write log messages as needed.
 */
static void
pdu_reject_packet(
    skpc_probe_t           *probe,
    pdu_badpdu_status_t     reason)
{
    sk_conv_pdu_t *pdu_conv;

    assert(probe);
    pdu_conv = (sk_conv_pdu_t *)probe->converter;

    if (reason == pdu_conv->badpdu_status) {
        /* the status is same as before, increment counters */
        ++pdu_conv->badpdu_consec;
    } else {
        PDU_LOG_ADDITIONAL_BAD((sk_conv_pdu_t *)pdu_conv,
                               skpcProbeGetName(probe));

        INFOMSG("'%s': Rejected PDU record %s",
                skpcProbeGetName(probe), pdu_badpdu_msgs[reason]);

        /* Since we logged about this packet, no need to count it */
        pdu_conv->badpdu_consec = 0;
        pdu_conv->badpdu_status = reason;
    }
    pthread_mutex_lock(&pdu_conv->stats_mutex);
    ++pdu_conv->statistics.procPkts;
    ++pdu_conv->statistics.badPkts;
    pthread_mutex_unlock(&pdu_conv->stats_mutex);
}


/**
 *  status = pdu_get_packet(probe, pdu_packet);
 *
 *    Get the next PDU packet to process.
 *
 *    This function processes the packet's header, sets the timestamp
 *    for the flows in the packet, and checks the flow sequence
 *    numbers.
 */
static int
pdu_process_packet(
    skpc_probe_t       *probe,
    v5PDU              *pdu,
    ssize_t             data_len)
{
    /* For log messages that report out of sequence flow records,
     * these macros hold the start of the format and the start of the
     * argument list  */
#define PDU_OOS_FORMAT(diff_is_neg)                                     \
    "'%s': Out-of-sequence packet:"                                     \
        " expecting %" PRIu32 ", received %" PRIu32                     \
        ", difference " diff_is_neg "%" PRId64 ", elapsed %f sec"       \
        ", engine %u.%u;"
#define PDU_OOS_ARGS(diff_value)                                        \
    skpcProbeGetName(probe),                                            \
        engine->flow_sequence, pdu->hdr.flow_sequence, diff_value,      \
        ((float)(now - engine->last_timestamp) / 1000.0),               \
        engine->id >> 8, engine->id & 0xFF
#define COUNT_BAD_RECORD(cbr_pdu_conv)                           \
    {                                                           \
        pthread_mutex_lock(&((cbr_pdu_conv)->stats_mutex));      \
        ++(cbr_pdu_conv)->statistics.badRecs;                    \
        pthread_mutex_unlock(&((cbr_pdu_conv)->stats_mutex));    \
    }


    sk_conv_pdu_t      *pdu_conv;
    sk_lua_nfv5_t       incoming_rec;
    intmax_t            now;
    intmax_t            router_boot;
    int64_t             seq_differ;
    uint64_t            allrecs;
    pdu_engine_info_t  *engine;
    pdu_engine_info_t   target;
    size_t              i;
    v5Record           *v5rec;

    assert(probe);
    pdu_conv = (sk_conv_pdu_t *)probe->converter;

    assert(NULL != pdu_conv);

    if (pdu == NULL) {
        /* if we saw any bad PDUs, print message before returning */
        PDU_LOG_ADDITIONAL_BAD(pdu_conv, skpcProbeGetName(probe));
        return 1;
    }

    /* check the header; first check the length and version */
    if ((size_t)data_len < sizeof(v5Header)) {
        pdu_reject_packet(probe, PDU_TRUNCATED_HEADER);
        return 1;
    }
    if (pdu->hdr.version != htons(5)) {
        pdu_reject_packet(probe, PDU_BAD_VERSION);
        return 1;
    }

    /* byte swap the header */
    pdu->hdr.count = ntohs(pdu->hdr.count);
    pdu->hdr.SysUptime = ntohl(pdu->hdr.SysUptime);
    pdu->hdr.unix_secs = ntohl(pdu->hdr.unix_secs);
    pdu->hdr.unix_nsecs = ntohl(pdu->hdr.unix_nsecs);
    pdu->hdr.flow_sequence = ntohl(pdu->hdr.flow_sequence);
    pdu->hdr.sampling_interval = ntohs(pdu->hdr.sampling_interval);

    /* check that the number of records is sane */
    if (0 == pdu->hdr.count) {
        pdu_reject_packet(probe, PDU_ZERO_RECORDS);
        return 1;
    }
    if (pdu->hdr.count > V5PDU_MAX_RECS) {
        pdu_reject_packet(probe, PDU_OVERFLOW_RECORDS);
        return 1;
    }
    if ((size_t)data_len < pdu->hdr.count * sizeof(v5Record)) {
        pdu_reject_packet(probe, PDU_BAD_VERSION);
        return 1;
    }

    /* this packet looks good.  write a log message about previous bad
     * packets (if any) */
    PDU_LOG_ADDITIONAL_BAD(pdu_conv, skpcProbeGetName(probe));

    pthread_mutex_lock(&pdu_conv->stats_mutex);
    ++pdu_conv->statistics.procPkts;
    pthread_mutex_unlock(&pdu_conv->stats_mutex);

    /* use the PDU header to get the "current" time as
     * milliseconds since the UNIX epoch. */
    now = ((intmax_t)1000 * pdu->hdr.unix_secs
           + (pdu->hdr.unix_nsecs / 1000000));

    /* subtract sysUptime from current-time to get router boot time as
     * milliseconds since UNIX epoch */
    router_boot = now - (intmax_t)pdu->hdr.SysUptime;

    /* Determine the current engine */
    target.id = ((uint16_t)pdu->hdr.engine_type << 8) | pdu->hdr.engine_id;
    engine = pdu_conv->engine_info;
    if (engine == NULL || engine->id != target.id) {
        /* Current engine info must be updated */
        engine = ((pdu_engine_info_t *)
                  rbfind(&target, pdu_conv->engine_info_tree));
        if (engine == NULL) {
            /* There's no entry for this engine.  Add one */
            TRACEMSG(1, ("'%s': New engine %u.%u noticed",
                         skpcProbeGetName(probe),
                         target.id >> 8, target.id & 0xFF));
            engine = sk_alloc(pdu_engine_info_t);
            engine->id = target.id;
            engine->router_boot = router_boot;
            engine->sysUptime = pdu->hdr.SysUptime;
            engine->flow_sequence = pdu->hdr.flow_sequence;
            rbsearch(engine, pdu_conv->engine_info_tree);
        }
        pdu_conv->engine_info = engine;
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
        DEBUGMSG(("'%s': Router reboot for engine %u.%u."
                  " Last time %" PRIdMAX ", Current time %" PRIdMAX),
                 skpcProbeGetName(probe), engine->id >> 8, engine->id & 0xFF,
                 engine->router_boot, router_boot);
        engine->flow_sequence = pdu->hdr.flow_sequence;
    }
    engine->router_boot = router_boot;
    engine->sysUptime = pdu->hdr.SysUptime;

    /* handle sequence numbers */
    if (pdu->hdr.flow_sequence == engine->flow_sequence) {
        /* This packet is in sequence.  Update the next expected seq */
        engine->flow_sequence = pdu->hdr.flow_sequence + pdu->hdr.count;

    } else if (pdu->hdr.flow_sequence > engine->flow_sequence) {
        /* received is greater than expected */
        seq_differ = (pdu->hdr.flow_sequence - engine->flow_sequence);

        if (seq_differ < maximumSequenceDeviation) {
            /* assume dropped packets; increase the missing flow
             * record count, and update the expected sequence
             * number */
            pthread_mutex_lock(&pdu_conv->stats_mutex);
            pdu_conv->statistics.missingRecs += seq_differ;
            if (skpcProbeGetLogFlags(probe) & SOURCE_LOG_MISSING) {
                allrecs = (pdu_conv->statistics.goodRecs +
                           pdu_conv->statistics.badRecs +
                           pdu_conv->statistics.missingRecs);
                INFOMSG((PDU_OOS_FORMAT("")
                         " adding to missing records"
                         " %" PRId64 "/%" PRIu64 " == %7.4g%%"),
                        PDU_OOS_ARGS(seq_differ),
                        pdu_conv->statistics.missingRecs, allrecs,
                        ((float)pdu_conv->statistics.missingRecs
                         / (float)allrecs * 100.0));
            }
            pthread_mutex_unlock(&pdu_conv->stats_mutex);
            /* Update the next expected seq */
            engine->flow_sequence = pdu->hdr.flow_sequence + pdu->hdr.count;

        } else if (seq_differ > (ROLLOVER32 - maximumSequenceLateArrival)) {
            /* assume expected has rolled-over and we received a
             * packet that was generated before the roll-over and is
             * arriving late; subtract from the missing record
             * count and do NOT change expected value */
            pthread_mutex_lock(&pdu_conv->stats_mutex);
            pdu_conv->statistics.missingRecs -= pdu->hdr.count;
            if (pdu_conv->statistics.missingRecs < 0) {
                pdu_conv->statistics.missingRecs = 0;
            }
            pthread_mutex_unlock(&pdu_conv->stats_mutex);
            if (skpcProbeGetLogFlags(probe) & SOURCE_LOG_MISSING) {
                INFOMSG((PDU_OOS_FORMAT("")
                         " treating %u flows as arriving late after roll-over"
                         " (difference without roll-over %" PRId64 ")"),
                        PDU_OOS_ARGS(seq_differ), pdu->hdr.count,
                        seq_differ - ROLLOVER32);
            }

        } else {
            /* assume something caused the sequence numbers to change
             * radically; reset the expected sequence number and do
             * NOT add to missing record count */
            if (skpcProbeGetLogFlags(probe) & SOURCE_LOG_MISSING) {
                INFOMSG((PDU_OOS_FORMAT("")
                         " resetting sequence due to large difference;"
                         " next expected packet %" PRIu32),
                        PDU_OOS_ARGS(seq_differ),
                        pdu->hdr.flow_sequence + pdu->hdr.count);
            }
            /* Update the next expected seq */
            engine->flow_sequence = pdu->hdr.flow_sequence + pdu->hdr.count;
        }

    } else {
        /* expected is greater than received */
        seq_differ = (engine->flow_sequence - pdu->hdr.flow_sequence);

        if (seq_differ > (ROLLOVER32 - maximumSequenceDeviation)) {
            /* assume received has rolled over but expected has not
             * and there are dropped packets; increase the missing
             * flow record count and update the expected sequence
             * number */
            pthread_mutex_lock(&pdu_conv->stats_mutex);
            pdu_conv->statistics.missingRecs += ROLLOVER32 - seq_differ;
            if (skpcProbeGetLogFlags(probe) & SOURCE_LOG_MISSING) {
                allrecs = (pdu_conv->statistics.goodRecs +
                           pdu_conv->statistics.badRecs +
                           pdu_conv->statistics.missingRecs);
                INFOMSG((PDU_OOS_FORMAT("-")
                         " treating as missing packets during roll-over"
                         " (difference without roll-over %" PRId64 ");"
                         " adding to missing records"
                         " %" PRId64 "/%" PRIu64 " == %7.4g%%"),
                        PDU_OOS_ARGS(seq_differ), ROLLOVER32 - seq_differ,
                        pdu_conv->statistics.missingRecs, allrecs,
                        ((float)pdu_conv->statistics.missingRecs /
                         (float)allrecs * 100.0));
            }
            pthread_mutex_unlock(&pdu_conv->stats_mutex);

            /* Update the next expected seq */
            engine->flow_sequence = pdu->hdr.flow_sequence + pdu->hdr.count;

        } else if (seq_differ < maximumSequenceLateArrival) {
            /* assume we received a packet that is arriving late; log
             * the fact and subtract from the missing record count */
            pthread_mutex_lock(&pdu_conv->stats_mutex);
            pdu_conv->statistics.missingRecs -= pdu->hdr.count;
            if (pdu_conv->statistics.missingRecs < 0) {
                pdu_conv->statistics.missingRecs = 0;
            }
            pthread_mutex_unlock(&pdu_conv->stats_mutex);
            if (skpcProbeGetLogFlags(probe) & SOURCE_LOG_MISSING) {
                INFOMSG((PDU_OOS_FORMAT("-")
                         " treating %u flows as arriving late"),
                        PDU_OOS_ARGS(seq_differ), pdu->hdr.count);
            }

        } else {
            /* assume something caused the sequence numbers to change
             * radically; reset the expected sequence number and do
             * NOT add to missing record count */
            if (skpcProbeGetLogFlags(probe) & SOURCE_LOG_MISSING) {
                INFOMSG((PDU_OOS_FORMAT("-")
                         " resetting sequence due to large difference;"
                         " next expected packet %" PRIu32),
                        PDU_OOS_ARGS(seq_differ),
                        pdu->hdr.flow_sequence + pdu->hdr.count);
            }

            /* Update the next expected seq */
            engine->flow_sequence = pdu->hdr.flow_sequence + pdu->hdr.count;
        }
    }

    engine->last_timestamp = (sktime_t)now;

    incoming_rec.header = pdu->hdr;

    for (i = 0, v5rec = pdu->data; i < pdu->hdr.count; ++i, ++v5rec) {
        intmax_t  difference;
        rwRec *rwrec;
        intmax_t v5_first, v5_last;
        intmax_t sTime;
        uint32_t bytes;
        uint32_t pkts;

        /* ((sk_lua_nfv5_t *)probe->incoming_rec)->record = *v5rec;*/
        incoming_rec.record = *v5rec;
        probe->incoming_rec = &incoming_rec;

        /* Check for zero packets or bytes.  No need for byteswapping
         * when checking zero. */
        if (v5rec->dPkts == 0 || v5rec->dOctets == 0) {
            if (skpcProbeGetLogFlags(probe) & SOURCE_LOG_BAD) {
                NOTICEMSG("'%s': Netflow record has zero packets or bytes",
                          skpcProbeGetName(probe));
            }
            COUNT_BAD_RECORD(pdu_conv);
            continue;
        }

        pkts = ntohl(v5rec->dPkts);
        bytes = ntohl(v5rec->dOctets);
        /* Check to see if more packets them bytes. */
        if (pkts > bytes) {
            if (skpcProbeGetLogFlags(probe) & SOURCE_LOG_BAD) {
                NOTICEMSG("'%s': Netflow record has more packets then bytes",
                          skpcProbeGetName(probe));
            }
            COUNT_BAD_RECORD(pdu_conv);
            continue;
        }

        /* Check to see if the First and Last timestamps for the flow
         * record are reasonable, accounting for rollover.  If the
         * absolute value of the difference is greater than
         * maximumFlowTimeDeviation, we assume it has rolled over. */
        v5_first = (intmax_t)ntohl(v5rec->First);
        v5_last = (intmax_t)ntohl(v5rec->Last);
        difference = v5_last - v5_first;
        if ((difference > maximumFlowTimeDeviation)
            || ((difference < 0)
                && (difference > (-maximumFlowTimeDeviation))))
        {
            if (skpcProbeGetLogFlags(probe) & SOURCE_LOG_BAD) {
                NOTICEMSG(("'%s': Netflow record has earlier end time"
                           " than start time"), skpcProbeGetName(probe));
            }
            COUNT_BAD_RECORD(pdu_conv);
            continue;
        }

        /* Check for bogosities in how the ICMP type/code are set.  It
         * should be in dest port, but sometimes it is backwards in
         * src port. */
        if (v5rec->prot == 1 &&  /* ICMP */
            v5rec->dstport == 0) /* No byteswapping for check against 0 */
        {
            uint32_t *ports = (uint32_t*)&v5rec->srcport;
            *ports = BSWAP32(*ports); /* This will swap src into dest,
                                         while byteswapping. */
        }

        pthread_mutex_lock(&pdu_conv->stats_mutex);
        ++pdu_conv->statistics.goodRecs;
        pthread_mutex_unlock(&pdu_conv->stats_mutex);

        rwrec = &pdu_conv->rec;

        /* Setup start and duration */
        if (v5_first > v5_last) {
            /* End has rolled over, while start has not.  Adjust end
             * by 2^32 msecs in order to allow us to subtract start
             * from end and get a correct value for the duration. */
            v5_last += ROLLOVER32;
        }

        /* v5_first is milliseconds since the router booted.  To get
         * UNIX epoch milliseconds, add the router's boot time. */
        sTime = v5_first + pdu_conv->engine_info->router_boot;

        /* Check to see if the difference between the 32bit start time
         * and the sysUptime is overly large.  If it is, one of the
         * two has more than likely rolled over.  We need to adjust
         * based on this. */
        difference = pdu_conv->engine_info->sysUptime - v5_first;
        if (difference > maximumFlowTimeDeviation) {
            /* sTime rollover */
            sTime += ROLLOVER32;
        } else if (difference < (-maximumFlowTimeDeviation)) {
            /* sysUptime rollover */
            sTime -= ROLLOVER32;
        }

        rwRecReset(rwrec);

        /* Convert NetFlow v5 to SiLK */
        rwRecSetSIPv4(rwrec, ntohl(v5rec->srcaddr));
        rwRecSetDIPv4(rwrec, ntohl(v5rec->dstaddr));
        rwRecSetSPort(rwrec, ntohs(v5rec->srcport));
        rwRecSetDPort(rwrec, ntohs(v5rec->dstport));
        rwRecSetProto(rwrec, v5rec->prot);
        rwRecSetFlags(rwrec, v5rec->tcp_flags);
        rwRecSetInput(rwrec, ntohs(v5rec->input));
        rwRecSetOutput(rwrec, ntohs(v5rec->output));
        rwRecSetNhIPv4(rwrec, ntohl(v5rec->nexthop));
        rwRecSetStartTime(rwrec, (sktime_t)sTime);
        rwRecSetPkts(rwrec, pkts);
        rwRecSetBytes(rwrec, bytes);
        rwRecSetElapsed(rwrec, (uint32_t)(v5_last - v5_first));
        rwRecSetRestFlags(rwrec, 0);
        rwRecSetTcpState(rwrec, SK_TCPSTATE_NO_INFO);

        /* call the packer */
        if (skpcProbePackRecord(probe, rwrec, NULL)) {
            return -1;
        }
    }

    return 0;
}


int
sk_conv_pdu_create(
    skpc_probe_t       *probe)
{
    sk_conv_pdu_t *pdu_conv;

    assert(probe);
    assert(PROBE_ENUM_NETFLOW_V5 == skpcProbeGetType(probe));

    if (probe->converter) {
        return 0;
    }

    /* Create and initialize source */
    pdu_conv = sk_alloc(sk_conv_pdu_t);

    pdu_conv->engine_info_tree = rbinit(pdu_engine_compare, NULL);
    if (pdu_conv->engine_info_tree == NULL) {
        free(pdu_conv);
        return -1;
    }

#if 0
    {
        lua_State *L = (lua_State *)probe->pack.state;
        assert(L);

        /* we ought to create a single netflow record per probe and
         * refill it every time instead of creating and destroying a
         * new record on every iteration.  Perhaps incoming_rec should
         * be a reference into the lua registry that the packing
         * function can push onto the stack? */

        /* create a Lua version of a NFv5 record on the probe */
        probe->incoming_rec = (void *)sk_lua_push_nfv5(L);
    }
#endif  /* 0 */

    pthread_mutex_init(&pdu_conv->stats_mutex, NULL);

    probe->converter = pdu_conv;
    return 0;
}


void
sk_conv_pdu_destroy(
    skpc_probe_t       *probe)
{
    sk_conv_pdu_t *pdu_conv;
    pdu_engine_info_t *engine_info;
    RBLIST *iter;

    assert(probe);
    assert(PROBE_ENUM_NETFLOW_V5 == skpcProbeGetType(probe));

    pdu_conv = (sk_conv_pdu_t*)probe->converter;
    if (NULL == pdu_conv) {
        return;
    }
    probe->converter = NULL;

    pthread_mutex_destroy(&pdu_conv->stats_mutex);

    iter = rbopenlist(pdu_conv->engine_info_tree);
    if (iter != NULL) {
        while ((engine_info = (pdu_engine_info_t *)rbreadlist(iter))) {
            free(engine_info);
        }
        rbcloselist(iter);
    }
    rbdestroy(pdu_conv->engine_info_tree);

    free(pdu_conv);
}


/* Log statistics associated with a PDU source, and then clear the
 * statistics. */
void
sk_conv_pdu_log_stats(
    skpc_probe_t       *probe)
{
    sk_conv_pdu_t *pdu_conv;

    assert(probe);
    assert(PROBE_ENUM_NETFLOW_V5 == skpcProbeGetType(probe));

    if (!probe->converter) {
        return;
    }
    pdu_conv = (sk_conv_pdu_t*)probe->converter;

    pthread_mutex_lock(&pdu_conv->stats_mutex);
    INFOMSG(("'%s': Pkts %" PRIu64 "/%" PRIu64
             ", Recs %" PRIu64 ", MissRecs %" PRId64
             ", BadRecs %" PRIu64),
            skpcProbeGetName(probe),
            (pdu_conv->statistics.procPkts - pdu_conv->statistics.badPkts),
            pdu_conv->statistics.procPkts, pdu_conv->statistics.goodRecs,
            pdu_conv->statistics.missingRecs, pdu_conv->statistics.badRecs);
    memset(&pdu_conv->statistics, 0, sizeof(pdu_conv->statistics));
    pthread_mutex_unlock(&pdu_conv->stats_mutex);
}


/*
 *  ********************************************************************
 *  Lua Bindings for NetFlow v5
 *  ********************************************************************
 */

#define SK_LUA_NFV5     "silk.netflow_v5"

#define sk_lua_checknfv5(check_L, check_arg)                           \
    SKLUA_CHECK_TYPE(check_L, check_arg, SK_LUA_NFV5, sk_lua_nfv5_t *)

static const char *nfv5_field_list[] = {
    "sip",                 /*  0 */
    "dip",                 /*  1 */
    "nhip",                /*  2 */
    "input",               /*  3 */

    "output",              /*  4 */
    "packets",             /*  5 */
    "bytes",               /*  6 */
    "stime",               /*  7 */

    "etime",               /*  8 */
    "sport",               /*  9 */
    "dport",               /* 10 */
    "tcpflags",            /* 11 */

    "protocol",            /* 12 */
    "tos",                 /* 13 */
    "src_as",              /* 14 */
    "dst_as",              /* 15 */

    "src_mask",            /* 16 */
    "dst_mask",            /* 17 */
    "uptime",              /* 18 */
    "export_time",         /* 19 */

    "sequence",            /* 20 */
    "engine_type",         /* 21 */
    "engine_id",           /* 22 */
    "sampling_mode",       /* 23 */

    "sampling_interval",   /* 24 */
    NULL                   /* sentinel */
};


static int
sk_lua_nfv5_gc(
    lua_State          *L)
{
    /* I get an error when this is not commented out */
    (void)L;
    /* free(lua_touserdata(L, 1)); */
    return 0;
}


void
sk_lua_push_nfv5(
    lua_State          *L,
    sk_lua_nfv5_t      *nfv5)
{
    sk_lua_nfv5_t *lnfv5 = sk_lua_newuserdata(L, sk_lua_nfv5_t);
    luaL_setmetatable(L, SK_LUA_NFV5);
    *lnfv5 = *nfv5;
}


static int
sk_lua_nfv5_index(
    lua_State          *L)
{
    sk_lua_nfv5_t *nfv5;
    uint32_t u32;
    int field;
    skipaddr_t *ip;
    sktime_t *t;

    /* get the rwRec */
    nfv5 = sk_lua_checknfv5(L, 1);
    field = luaL_checkoption(L, 2, NULL, nfv5_field_list);
    switch (field) {
      case 0:                   /* sip */
        ip = sk_lua_push_ipaddr(L);
        u32 = ntohl(nfv5->record.srcaddr);
        skipaddrSetV4(ip, &u32);
        break;
      case 1:                   /* dip */
        ip = sk_lua_push_ipaddr(L);
        u32 = ntohl(nfv5->record.dstaddr);
        skipaddrSetV4(ip, &u32);
        break;
      case 2:                   /* nhip */
        ip = sk_lua_push_ipaddr(L);
        u32 = ntohl(nfv5->record.nexthop);
        skipaddrSetV4(ip, &u32);
        break;
      case 3:                   /* input */
        lua_pushinteger(L, ntohs(nfv5->record.input));
        break;
      case 4:                   /* output */
        lua_pushinteger(L, ntohs(nfv5->record.output));
        break;
      case 5:                   /* pkts */
        lua_pushinteger(L, ntohl(nfv5->record.dPkts));
        break;
      case 6:                   /* bytes */
        lua_pushinteger(L, ntohl(nfv5->record.dOctets));
        break;
      case 7:                   /* stime */
        lua_pushinteger(L, ntohl(nfv5->record.First));
        break;
      case 8:                   /* etime */
        lua_pushinteger(L, ntohl(nfv5->record.Last));
        break;
      case 9:                   /* sport */
        lua_pushinteger(L, ntohs(nfv5->record.srcport));
        break;
      case 10:                  /* dport */
        lua_pushinteger(L, ntohs(nfv5->record.dstport));
        break;
      case 11:                  /* tcpflags */
        lua_pushinteger(L, nfv5->record.tcp_flags);
        break;
      case 12:                  /* protocol */
        lua_pushinteger(L, nfv5->record.prot);
        break;
      case 13:                  /* tos */
        lua_pushinteger(L, nfv5->record.tos);
        break;
      case 14:                  /* src_as */
        lua_pushinteger(L, ntohs(nfv5->record.src_as));
        break;
      case 15:                  /* dst_as */
        lua_pushinteger(L, ntohs(nfv5->record.dst_as));
        break;
      case 16:                  /* src_mask */
        lua_pushinteger(L, nfv5->record.src_mask);
        break;
      case 17:                  /* dst_mask */
        lua_pushinteger(L, nfv5->record.dst_mask);
        break;
      case 18:                  /* uptime */
        lua_pushinteger(L, nfv5->header.SysUptime);
        break;
      case 19:                  /* export_time */
        t = sk_lua_push_datetime(L);
        *t = sktimeCreate(nfv5->header.unix_secs,
                          nfv5->header.unix_nsecs / 1000000);
        break;
      case 20:                  /* sequence */
        lua_pushinteger(L, nfv5->header.flow_sequence);
        break;
      case 21:                  /* engine_type */
        lua_pushinteger(L, nfv5->header.engine_type);
        break;
      case 22:                  /* engine_id */
        lua_pushinteger(L, nfv5->header.engine_id);
        break;
      case 23:                  /* sampling_mode */
        lua_pushinteger(L, nfv5->header.sampling_interval >> 14);
        break;
      case 24:                  /* sampling_interval */
        lua_pushinteger(L, nfv5->header.sampling_interval & 0x3fff);
        break;
      default:
        skAbortBadCase(field);
    }

    return 1;
}

static int
sk_lua_nfv5_newindex(
    lua_State          *L)
{
    return luaL_error(L, "object is readonly");
}


/*
 *    Function that is the __pairs iterator.  Use the integer upvalue
 *    to determine which key,value pair to return.
 */
static int
sk_lua_nfv5_pairs_iter(
    lua_State          *L)
{
    lua_Integer i;
    int isnum;

    i = lua_tointegerx(L, lua_upvalueindex(1), &isnum);
    if (!isnum
        || i >= (int)(sizeof(nfv5_field_list)/sizeof(nfv5_field_list[0]) - 1)
        || i < 0)
    {
        lua_pushnil(L);
        return 1;
    }
    /* increment i and store */
    lua_pushinteger(L, i + 1);
    lua_replace(L, lua_upvalueindex(1));
    /* push the key that this function will return */
    lua_pushstring(L, nfv5_field_list[i]);
    /* call nfv5[key] to get the value that is returned */
    lua_pushcfunction(L, sk_lua_nfv5_index);
    lua_pushvalue(L, 1);
    lua_pushstring(L, nfv5_field_list[i]);
    lua_call(L, 2, 1);

    return 2;
}


/*
 * =pod
 *
 * =item B<pairs(>I<nfv5>B<)>
 *
 * Return an iterator designed for the Lua B<for> statement that
 * iterates over (name, value) pairs of the nfv5 in position
 * order, where name is the string name of the field and value is
 * that field's value in I<nfv5>.  May be used as
 * B<for name, value in pairs(I<nfv5>) do...end>
 *
 * =cut
 *
 *    To implement the iterator, push an integer to use as the index,
 *    push a closure that uses the integer as the upvalue, push the
 *    record.
 */
static int
sk_lua_nfv5_pairs(
    lua_State          *L)
{
    /* const sk_lua_nfv5_t *nfv5; */

    sk_lua_checknfv5(L, 1);
    lua_pushinteger(L, 0);
    lua_pushcclosure(L, sk_lua_nfv5_pairs_iter, 1);
    lua_pushvalue(L, 1);
    return 2;
}


static int (*sk_lua_nfv5_create)(lua_State *L) = NULL;

static const luaL_Reg sk_lua_nfv5_metatable[] = {
    {"__gc",        sk_lua_nfv5_gc},
    {"__index",     sk_lua_nfv5_index},
    {"__newindex",  sk_lua_nfv5_newindex},
    {"__pairs",     sk_lua_nfv5_pairs},
    {NULL, NULL}    /* sentinel */
};

static const luaL_Reg *sk_lua_nfv5_methods = NULL;
#if 0
static const luaL_Reg sk_lua_nfv5_methods[] = {
    {NULL, NULL}    /* sentinel */
};
#endif

static const luaL_Reg *sk_lua_nfv5_static_methods = NULL;


int
sklua_open_pdusource(
    lua_State          *L)
{
    sk_lua_object_t objects[] = {
        SK_LUA_OBJECT_SENTINEL,
        SK_LUA_OBJECT_SENTINEL
    };

    objects[0].name = "nfv5";
    objects[0].ident = SK_LUA_NFV5;
    objects[0].constructor = sk_lua_nfv5_create;
    objects[0].metatable = sk_lua_nfv5_metatable;
    objects[0].methods = sk_lua_nfv5_methods;
    objects[0].static_methods = sk_lua_nfv5_static_methods;

    /* Check lua versions */
    luaL_checkversion(L);

    lua_getglobal(L, "silk");
    sk_lua_add_to_object_table(L, -1, objects);

    /* Return the silk module */
    return 1;
}


/*
** Local Variables:
** mode:c
** indent-tabs-mode:nil
** c-basic-offset:4
** End:
*/
