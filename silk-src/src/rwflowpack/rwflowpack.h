/*
** Copyright (C) 2004-2017 by Carnegie Mellon University.
**
** @OPENSOURCE_LICENSE_START@
** See license information in ../../LICENSE.txt
** @OPENSOURCE_LICENSE_END@
*/

/*
**  rwflowpack.h
**
**    This header defines the structure of function pointers used by a
**    packing-logic plug-in for rwflowpack.
**
*/
#ifndef _RWFLOWPACK_H
#define _RWFLOWPACK_H
#ifdef __cplusplus
extern "C" {
#endif

#include <silk/silk.h>

RCSIDENTVAR(rcsID_RWFLOWPACK_H, "$SiLK: rwflowpack.h efd886457770 2017-06-21 18:43:23Z mthomas $");

#include <silk/rwrec.h>
#include <silk/silk_types.h>
#include <silk/sklua.h>
#include <silk/skpolldir.h>


/**
 *  @file
 *
 *    Interface between rwflowpack and the packing logic plug-in that
 *    is used to decide how to pack each flow record.
 *
 *    This file is part of libflowsource.
 */


/*
 *  **********************************************************************
 *  **********************************************************************
 *
 *  libflowsource.h
 *
 *    libflowsource is used by flowcap and rwflowpack to import
 *    NetFlowV5, IPFIX, or NetFlowV9 flow records.
 *
 *    This file is part of libflowsource.
 */

/*
 *    Forward declaration of the probe type, from rwflowpack-probe.h.
 */
typedef struct skpc_probe_st skpc_probe_t;

/**
 *    Value for skpcProbeSetLogFlags() that suppresses all log
 *    messages.
 */
#define SOURCE_LOG_NONE         0

/**
 *    Flag for skpcProbeSetLogFlags() that enables log messages about
 *    out of sequence NetFlow v5 packets.
 */
#define SOURCE_LOG_MISSING      (1 << 0)

/**
 *    Flag for skpcProbeSetLogFlags() that enables log messages about
 *    invalid NetFlow v5 packets.
 */
#define SOURCE_LOG_BAD          (1 << 1)

/**
 *    Flag for skpcProbeSetLogFlags() that enables log messages about
 *    the sampling interval used in NetFlow v9/IPFIX.
 */
#define SOURCE_LOG_SAMPLING     (1 << 2)

/**
 *    Flag for skpcProbeSetLogFlags() that enables log messages about
 *    flow records being ignored due to an NetFlow v9/IPFIX firewall
 *    event setting.
 */
#define SOURCE_LOG_FIREWALL     (1 << 3)

/**
 *    Value for skpcProbeSetLogFlags() that enables all log messages.
 */
#define SOURCE_LOG_ALL          0xff



/**
 *    Number of bytes we want to split between socket buffers
 */
#define SOCKETBUFFER_NOMINAL_TOTAL 0x800000 /* 8M */

/**
 *    Environment variable to modify SOCKETBUFFER_NOMINAL_TOTAL
 */
#define SOCKETBUFFER_NOMINAL_TOTAL_ENV "SK_SOCKETBUFFER_TOTAL"

/**
 *    Minimum number of bytes to attempt to allocate to a socket buffer
 */
#define SOCKETBUFFER_MINIMUM       0x20000 /* 128K */

/**
 *    Environment variable to modify SOCKETBUFFER_MINIMUM
 */
#define SOCKETBUFFER_MINIMUM_ENV "SK_SOCKETBUFFER_MINIMUM"


/**
 *    packconf_directory_t manages information about a directory to
 *    poll periodically.
 */
typedef struct packconf_directory_st packconf_directory_t;

/**
 *    packconf_file_t manages information regarding the path to an
 *    incoming file to process and how to dispose of that file once it
 *    has been processed.  Created while parsing the configuration
 *    file,
 */
typedef struct packconf_file_st packconf_file_t;

/**
 *    packconf_network_t manages information regarding reading packets
 *    from the network.
 */
typedef struct packconf_network_st packconf_network_t;

/**
 *    packer_fileinfo_t holds the format of the output files created
 *    by rwflowpack, and the sidecar description to write into the
 *    files' header.
 */
typedef struct packer_fileinfo_st packer_fileinfo_t;



/***  IPFIX SOURCES  ******************************************************/


/**
 *    Values that represent constants used by the IPFIX standard
 *    and/or CISCO devices to represent firewall events:
 *
 *      firewallEvent is an official IPFIX information element, IE 233
 *
 *      NF_F_FW_EVENT is Cisco IE 40005
 *
 *      NF_F_FW_EXT_EVENT is Cisco IE 33002.
 *
 *    The NF_F_FW_EXT_EVENT provides a subtype for the NF_F_FW_EVENT
 *    type.  See the lengthy comment in skipfix.c.
 */
#define SKIPFIX_FW_EVENT_CREATED            1
#define SKIPFIX_FW_EVENT_DELETED            2
#define SKIPFIX_FW_EVENT_DENIED             3
/* denied due to ingress acl */
#define SKIPFIX_FW_EVENT_DENIED_INGRESS       1001
/* denied due to egress acl */
#define SKIPFIX_FW_EVENT_DENIED_EGRESS        1002
/* denied due to attempting to contact ASA's service port */
#define SKIPFIX_FW_EVENT_DENIED_SERV_PORT     1003
/* denied due to first packet not syn */
#define SKIPFIX_FW_EVENT_DENIED_NOT_SYN       1004
#define SKIPFIX_FW_EVENT_ALERT              4
#define SKIPFIX_FW_EVENT_UPDATED            5

/**
 *    Return true if value in 'sfedcv_val' is recognized as a
 *    NF_F_FW_EXT_EVENT sub-value for "Denied" firewall events.
 */
#define SKIPFIX_FW_EVENT_DENIED_CHECK_VALID(sfedcv_val)         \
    (SKIPFIX_FW_EVENT_DENIED_INGRESS <= (sfedcv_val)            \
     && SKIPFIX_FW_EVENT_DENIED_NOT_SYN >= (sfedcv_val))


/*
 *  **********************************************************************
 *  **********************************************************************
 *
 *  probeconf.h
 *
 *    Functions to parse a probe configuration file and use the
 *    results.
 *
 *    This file is part of libflowsource.
 *
 *
 *    Lifecycle:
 *
 *    The application calls skpcSetup() to initialize the
 *    skpc data structures and memory.
 *
 *    The application should call skpcParse() to parse the
 *    application's configuration file.  skpcParse() will create
 *    sensors (if any) and probes.  The probes are created and checked
 *    for validity--this means they have all the data they require.
 *    If valid they are added to the list maintained by the skpc.
 *    If not valid, they are destroyed.
 *
 *    Once the probes have been created, the application can use
 *    skpcProbeIteratorBind() and skpcProbeIteratorNext() to process
 *    each probe.
 *
 *    Finally, the application calls skpcTeardown() to destroy
 *    the probes, sensors, and to fee all memory.
 *
 *    Note that skpc allows one to create a "temporary" sensor;
 *    i.e., a sensor that will only exist as long as the application
 *    is running; this is useful for testing a new sensor without
 *    requiring a complete recompile of SiLK.  However, "temporary"
 *    sensors will NOT be available to the analysis applications.  For
 *    the analysis applications to know about a sensor, it MUST be
 *    listed in the sensorInfo[] array.
 */


typedef enum {
    SKPROBE_COLL_UNKNOWN = 0,
    SKPROBE_COLL_NETWORK,
    SKPROBE_COLL_DIRECTORY,
    SKPROBE_COLL_FILE
} skprobe_coll_type_t;


/**
 *    Values for the type of a probe.
 */
typedef enum {
    PROBE_ENUM_INVALID = 0,
    PROBE_ENUM_IPFIX = 10,
    PROBE_ENUM_NETFLOW_V5 = 5,
    PROBE_ENUM_NETFLOW_V9 = 9,
    PROBE_ENUM_SFLOW = 16,
    PROBE_ENUM_SILK = 15
} skpc_probetype_t;


/**
 *    Possible protocols
 */
typedef enum {
    SKPC_PROTO_UNSET = 0,
    SKPC_PROTO_TCP = 1,
    SKPC_PROTO_UDP = 2,
#if 0
    /* not sure if these should be here; we'll decide when we add SSL
     * support */
    SKPC_PROTO_DTLS_SCTP,
    SKPC_PROTO_TLS_TCP,
    SKPC_PROTO_DTLS_UDP,
#endif
    SKPC_PROTO_SCTP = 3
} skpc_proto_t;


/**
 *    The signature of a function to initialize the packing function
 *    on a probe.
 *
 *    This callback is set by calling skpcProbeSetPackingFunction().
 *
 *    This callback is invoked by calling skpcProbeInitializePacker().
 */
typedef int
(*packlogic_init_packer_fn_t)(
    skpc_probe_t       *probe);

/**
 *    The signature of a function to interrupt the packer and to tell
 *    it to stop packing.
 *
 *    This callback should be set as part of setting the packing
 *    function or initialzing the packing function.
 *
 *    This callback is invoked by calling skpcProbeStopPacker().
 */
typedef void
(*packlogic_stop_packer_fn_t)(
    skpc_probe_t       *probe);

/**
 *    The signature of a function to destroy the packing state that is
 *    on probe.
 *
 *    This callback should be set as part of setting the packing
 *    function or initialzing the packing function.
 *
 *    This callback is invoked when the probe is destroyed.
 */
typedef void
(*packlogic_free_state_fn_t)(
    skpc_probe_t       *probe);

/**
 *    Definition of a type of the packing logic plug-in to maintain
 *    state.
 */
typedef void* packlogic_state_t;

/**
 *    Definition of the signature for a function that determines the
 *    flow type(s) and sensorID(s) of a "forward" flow record
 *    'fwd_rwrec' and perhaps a "reverse" flow record 'rev_rwrec' that
 *    were collected from 'probe'.
 *
 *    A function with this signature is set on the probe by calling
 *    the set_packing_function_fn() member of the packing logic
 *    plug-in.
 *
 *    A function with the packlogic_pack_record_fn_t() signature is
 *    expected to call write_record() to output the record(s) to disk.
 *
 *    The packlogic_pack_record_fn_t() must return 0 for success and
 *    non-zero for failure.  Typically, the return value of
 *    packlogic_pack_record_fn_t() will be the result of
 *    write_record().
 *
 *    This function is designed to replace the 'determine_flowtype_fn'
 *    and 'determine_fileformat_fn' function pointers that exist on
 *    the packing-logic plug-ins in SiLK 3.
 */
typedef int
(*packlogic_pack_record_fn_t)(
    skpc_probe_t         *probe,
    const rwRec          *fwd_rwrec,
    const rwRec          *rev_rwrec);


/**
 *    sk_coll_status_t gives possible states for the collector.
 */
enum sk_coll_status_en {
    UNKNONWN = 0,
    CREATED,
    STARTING,
    STARTED,
    STOPPING,
    STOPPED,
    JOINED
};
typedef enum sk_coll_status_en sk_coll_status_t;


/**
 *    sk_coll_thread_t holds the thread-related variables for a
 *    collector.  Each collector should start with this structure.
 */
struct sk_coll_thread_st {
    /* The thread that reads records from the probe */
    pthread_t           thread;
    /* Thread mutex */
    pthread_mutex_t     mutex;
    /* Thread condition variable */
    pthread_cond_t      cond;
    /* Current status of the thread */
    sk_coll_status_t    status;
};
typedef struct sk_coll_thread_st sk_coll_thread_t;

struct sk_coll_file_st {
    sk_coll_thread_t    t;
    /* For a file-based probe, the stream */
    skstream_t         *stream;
    skcontent_t         content_type;
};
typedef struct sk_coll_file_st sk_coll_file_t;

struct sk_coll_directory_st {
    sk_coll_thread_t    t;
    /* For a directory-based probe, the directory poller */
    skPollDir_t        *polldir;
    skcontent_t         content_type;
};
typedef struct sk_coll_directory_st sk_coll_directory_t;

/**
 *    skpc_probe_t is the probe definition.
 *
 *    A probe tells how to collect data and the type of data.  For
 *    example, IPFIX data from machine 10.10.10.10 as TCP to port
 *    9999.  A probe is associated with one or more sensors.
 */
/* typedef struct skpc_probe_st skpc_probe_t;  // from above */
struct skpc_probe_st {
    /**
     *    Functions and data used for packing records, for
     *    initializing the packing function and data, and freeing the
     *    packing data.
     */
    struct pack_st {
        /** The function to initialize the packing function. */
        packlogic_init_packer_fn_t  init_packer;

        /** The function to stop the packer */
        packlogic_stop_packer_fn_t  stop_packer;

        /** The function to free the packing state. */
        packlogic_free_state_fn_t   free_state;

        /** The packing logic function to use for records read from this
         * probe. */
        packlogic_pack_record_fn_t  pack_record;

        /** The state for the pack_record function. */
        packlogic_state_t           state;

        /** The Lua state that the packer may use. */
        lua_State                  *lua_state;
    }                   pack;

    /**
     *    The configuration information necesary to collect the
     *    incoming data.  Which of these is used is determined by the
     *    'coll_type' member.
     */
    union coll_conf_un {
        /** A directory path name to poll in order to find files from which
         * to read flow data */
        const packconf_directory_t *directory;

        /** A file name from which to read flow data */
        const packconf_file_t      *file;

        /** The necessary information to read packets from the network */
        const packconf_network_t   *network;
    }                   coll_conf;

    /**
     *    The variables that hold the active collector.
     */
    union coll_en {
        struct any_st {
            sk_coll_thread_t        t;
        }                      *any;
        sk_coll_directory_t    *directory;
        sk_coll_file_t         *file;
        void                   *network;
    }                   coll;

    /** the name of the probe */
    const char         *probe_name;

    /**
     *    The file format, file format version, and sidecar header to
     *    use when writing records collected by this probe. */
    const packer_fileinfo_t    *file_info;

    /* the converter of this probe */
    void               *converter;

    /** The object that is being packed. For example, a NetFlow v5
     * record or an sk_fixrec_t representation of an IPFIX record */
    void               *incoming_rec;

    /** The rwRec form of the record */
    rwRec               rwrec;

    /** The type of data collected by the probe (e.g. IPFIX) */
    skpc_probetype_t    probe_type;

    /** How the probe collects data (e.g., network socket) */
    skprobe_coll_type_t coll_type;

    /* The status of the collector (starting, started, ...) */
    sk_coll_status_t    coll_status;

    /** Probe logging flags */
    uint8_t             log_flags;
};


/**
 *  Iterator over probes
 */
typedef struct skpc_probe_iter_st {
    size_t cur;
} skpc_probe_iter_t;



/*
 *  *****  Probe configuration  **************************************
 */



/* Indices of values in the Lua probe table */
enum {
    IDX_PROBE_FUNCTION = 1,
    IDX_PROBE_VARS,

    PROBE_TABLE_NEXT_IDX
};


/**
 *    Initialize the probe configuration data structures.
 */
int
skpcSetup(
    void);


/**
 *    Destroy all probes and sensors and free all memory used by the
 *    probe configuration.
 */
void
skpcTeardown(
    void);


/**
 *    Return the count of created and verified probes.
 */
size_t
skpcCountProbes(
    void);


/**
 *    Bind 'probe_iter' to loop over all the probes that have been
 *    defined.  Returns 0 on success, or -1 on error.
 */
int
skpcProbeIteratorBind(
    skpc_probe_iter_t  *probe_iter);


/**
 *    If the probe iterator 'probe_iter' has exhausted all probes,
 *    leave 'probe' untouched and return 0; otherwise, fill 'probe'
 *    with a pointer to the next verified probe and return 1.  Returns
 *    -1 on error (such as NULL input).  The caller should not modify
 *    or free the probe.
 */
int
skpcProbeIteratorNext(
    skpc_probe_iter_t      *probe_iter,
    const skpc_probe_t    **probe);


/**
 *    Returns the probe named 'probe_name'.  Returns NULL if not
 *    found.  The caller should not modify nor free the return value.
 */
const skpc_probe_t *
skpcProbeLookupByName(
    const char         *probe_name);


/**
 *    Given a printable representation of a probe, return the probe
 *    type.
 *
 *    Return PROBE_ENUM_INVALID when given an unrecognized name.
 */
skpc_probetype_t
skpcProbetypeNameToEnum(
    const char         *name);

/**
 *    Return the printable respresentation of the probe type.
 *
 *    Return NULL when given an illegal value.
 */
const char *
skpcProbetypeEnumtoName(
    skpc_probetype_t    type);


/**
 *    Given a printable representation of a protocol, return the
 *    protocol.
 *
 *    Return SKPC_PROTO_UNSET when given an unrecognized name.
 */
skpc_proto_t
skpcProtocolNameToEnum(
    const char         *name);

/**
 *    Return the printable respresentation of the protocol.
 *
 *    Return NULL when given an illegal value.
 */
const char *
skpcProtocolEnumToName(
    skpc_proto_t        proto);


/*
 *  *****  Probes  *****************************************************
 *
 *
 *  Flows are stored by SENSOR; a SENSOR is a logical construct made
 *  up of one or more physical PROBES.
 *
 *  A probe collects flows in one of three ways:
 *
 *  1.  The probe can listen to network traffic.  For this case,
 *  skpcProbeGetListenOnSockaddr() will return the port on which to
 *  listen for traffic and the IP address that the probe should bind()
 *  to.  In addition, the skpcProbeGetAcceptFromHost() method will
 *  give the IP address from which the probe should accept
 *  connections.
 *
 *  2.  The probe can listen on a UNIX domain socket.  The
 *  skpcProbeGetListenOnUnixDomainSocket() method returns the pathname
 *  to the socket.
 *
 *  3.  The probe can read from a file.  The skpcProbeGetFileSource()
 *  method returns the name of the file.
 *
 *  A probe is not valid it has been set to use one and only one of
 *  these collection methods.
 *
 *  Once the probe has collected a flow, it needs to determine whether
 *  the flow represents incoming traffic, outgoing traffic, ACL
 *  traffic, etc.  The packLogicDetermineFlowtype() will take an
 *  'rwrec' and the probe where the record was collected and use the
 *  external, internal, and null interface values and the list of ISP
 *  IPs to update the 'flow_type' field on the rwrec.  The rwrec's
 *  sensor id ('sID') field is also updated.
 *
 */


/**
 *    Create a new probe and fill in 'probe' with the address of
 *    the newly allocated probe.
 */
int
skpcProbeCreate(
    skpc_probe_t      **probe);


/**
 *    Destroy the probe at '**probe' and free all memory.  Sets *probe
 *    to NULL.
 */
void
skpcProbeDestroy(
    skpc_probe_t      **probe);


/**
 *    Return the name of a probe.  The caller should not modify the
 *    name, and does not need to free() it.
 */
#define skpcProbeGetName(m_probe)  ((m_probe)->probe_name)
#ifndef skpcProbeGetName
const char *
skpcProbeGetName(
    const skpc_probe_t *probe);
#endif  /* #ifndef skpcProbeGetName */


/**
 *    Set the name of a probe.  The probe name must
 *    meet all the requirements of a sensor name.  Each probe that is
 *    a collection point for a single sensor must have a unique name.
 *
 *    The function takes ownership of 'name'.
 */
int
skpcProbeSetName(
    skpc_probe_t       *probe,
    char               *name);


/**
 *    Return a string that represents the type of the probe.  The
 *    caller should not modify the name, and does not need to free()
 *    it.  Return NULL if probe's type is PROBE_ENUM_INVALID.
 */
const char *
skpcProbeGetTypeAsString(
    const skpc_probe_t *probe);

/**
 *    Return the type of the probe.  Before it is set by the user, the
 *    probe's type is PROBE_ENUM_INVALID.
 */
#define skpcProbeGetType(m_probe)  ((m_probe)->probe_type)
#ifndef skpcProbeGetType
skpc_probetype_t
skpcProbeGetType(
    const skpc_probe_t *probe);
#endif  /* #ifndef skpcProbeGetType */


/**
 *    Set the probe's type.
 */
int
skpcProbeSetType(
    skpc_probe_t       *probe,
    skpc_probetype_t    probe_type);


/**
 *    Get the probe's protocol.  Before it is set by the user, the
 *    probe's protocol is SKPC_PROTO_UNSET.
 */
skpc_proto_t
skpcProbeGetProtocol(
    const skpc_probe_t *probe);


/**
 *    Get the probe's logging-flags.  These logging flags refer to the
 *    log messages regarding missing and bad packet counts that some
 *    of the flow sources support.
 *
 *    The value of log_flags can be SOURCE_LOG_NONE to log nothing;
 *    SOURCE_LOG_ALL to log everything (the default); or a bitwise OR
 *    of the SOURCE_LOG_* values defined in libflowsource.h.
 */
#define skpcProbeGetLogFlags(m_probe)  ((m_probe)->log_flags)
#ifndef skpcProbeGetLogFlags
uint8_t
skpcProbeGetLogFlags(
    const skpc_probe_t *probe);
#endif  /* #ifndef skpcProbeGetLogFlags */

/**
 *    Add the log-flag named 'log_flag' to the log-flag settings
 *    on 'probe'.
 */
int
skpcProbeAddLogFlag(
    skpc_probe_t       *probe,
    const char         *log_flag);

/**
 *    Remove the log-flag named 'log_flag' from the log-flag settings
 *    on 'probe'.
 */
int
skpcProbeRemoveLogFlag(
    skpc_probe_t       *probe,
    const char         *log_flag);

/**
 *    Clear all the log-flag settings on 'probe'.
 */
void
skpcProbeClearLogFlags(
    skpc_probe_t       *probe);


/**
 *    Get the port on which the probe listens for
 *    connections, and, for multi-homed hosts, the IP address that the
 *    probe should consider to be its IP.  The IP address is in host
 *    byte order.
 *
 *    When getting the information, the caller may pass in locations
 *    to be filled with the address and port; either parameter may be
 *    NULL to ignore that value.
 *
 *    The probe simply stores the address; it does not manipulate
 *    it in any way.
 *
 *    If the IP address to listen as is not set, the function
 *    returns INADDR_ANY in the 'out_addr'.  If the port has not been
 *    set, the 'get' function returns -1 and neither 'out' parameter
 *    is modified.
 */
int
skpcProbeGetListenOnSockaddr(
    const skpc_probe_t         *probe,
    const sk_sockaddr_array_t **addr);

/**
 *    Get the attributes to read data from the network.
 *
 *    The caller should neither modify nor free the value returned by
 *    the 'get' method.  The 'get' method returns NULL if the 'set'
 *    method has not yet been called.
 */
const packconf_network_t *
skpcProbeGetNetworkSource(
    const skpc_probe_t *probe);

/**
 *    Set the attributes to read data from the network.
 *
 *    The 'set' function takes ownership of 'network'.
 *
 *    If a network-source already exists on 'probe', the previous
 *    value is destroyed.
 */
int
skpcProbeConfigureCollectorNetwork(
    skpc_probe_t               *probe,
    const packconf_network_t   *network);

/**
 *    Get the name of the file from which to read data.
 *
 *    The caller should neither modify nor free the value returned by
 *    the 'get' method.  The 'get' method returns NULL if the 'set'
 *    method has not yet been called.
 */
const char *
skpcProbeGetFileSource(
    const skpc_probe_t *probe);

/**
 *    Set the attributes of the file name from which to read data.
 *
 *    The 'set' function takes ownership of 'file'.
 *
 *    If a file-source already exists on 'probe', the previous
 *    value is destroyed.
 */
int
skpcProbeConfigureCollectorFile(
    skpc_probe_t           *probe,
    const packconf_file_t  *file);


int
skpcProbeDisposeIncomingFile(
    const skpc_probe_t *probe,
    const char         *path,
    unsigned            has_error);



/**
 *    Get the polling interval when polling a directory for files
 *    containing flow records.
 *
 *    Return 0 if the skpcProbeConfigureCollectorDirectory() method
 *    has not been called.
 */
uint32_t
skpcProbeGetPollInterval(
    const skpc_probe_t *probe);

/**
 *    Get the name of the directory to poll for files containing flow
 *    records.
 *
 *    The caller should neither modify nor free the value returned by
 *    the 'get' method.  The 'get' method returns NULL if the 'set'
 *    method has not yet been called.
 */
const char *
skpcProbeGetPollDirectory(
    const skpc_probe_t *probe);

/**
 *    Set the attributes of the directory to poll for
 *    files containing flow records.
 *
 *    This function takes ownership of the 'poll_directory'.
 *
 *    If a poll-directory already exists on 'probe', the previous
 *    value is destroyed.
 */
int
skpcProbeConfigureCollectorDirectory(
    skpc_probe_t               *probe,
    const packconf_directory_t *poll_directory);


/**
 *    Get the hosts that are allowed to connect to 'probe'.
 *
 *    The function stores the address of an array of
 *    sk_sockaddr_array_t* objects in the location referenced by
 *    'addr_array'.  The return value is the length of that array.
 *
 *    The caller must not modify the values in the 'addr_array'.
 *
 *    If the get function is called before the set function, the
 *    location referenced by 'addr_array' is set to NULL and 0 is
 *    returned.
 *
 *    If 'addr_array' is NULL, the number of addresses is returned.
 */
uint32_t
skpcProbeGetAcceptFromHost(
    const skpc_probe_t             *probe,
    const sk_sockaddr_array_t    ***addr);


int
skpcProbeSetPackingFunction(
    skpc_probe_t               *probe,
    packlogic_init_packer_fn_t  packlogic_init);

int
skpcProbeInitializePacker(
    skpc_probe_t       *probe);

void
skpcProbeTeardownPacker(
    skpc_probe_t       *probe);

#define skpcProbePackRecord(m_probe, m_fwd_rec, m_rev_rec)              \
    ((m_probe)->pack.pack_record((m_probe), (m_fwd_rec), (m_rev_rec)))


/**
 *    Verify the 'probe' is valid.  For example, that it's name is
 *    unique among all probes, and that if it is an IPFIX probe,
 *    verify that a listen-on-port has been specified.
 *
 *    When 'is_ephemeral' is specified, the function only verifies
 *    that is name is unique.  If the name is unique, the probe will
 *    be added to the global list of probes, but skpcProbeIsVerified()
 *    on the probe will return 0.
 *
 *    If valid, add the probe to the list of probes and return 0.
 *    Otherwise return non-zero.
 */
int
skpcProbeVerify(
    skpc_probe_t       *probe,
    int                 is_ephemeral);

/**
 *    Log statistics about the number of records the probe has
 *    received since the previous call to this function, and then
 *    clear the statistics.
 */
void
skpcProbeLogSourceStats(
    skpc_probe_t       *probe);


const packer_fileinfo_t *
skpcProbeGetFileInfo(
    const skpc_probe_t *probe);

int
skpcProbeSetFileInfo(
    skpc_probe_t               *probe,
    const packer_fileinfo_t    *file_info);


/*
 *  **********************************************************************
 *  **********************************************************************
 *
 *    rwflowpack-stream.c
 *
 */

int
sk_coll_create(
    skpc_probe_t       *probe);

int
sk_coll_start(
    skpc_probe_t       *probe);

void
sk_coll_stop(
    skpc_probe_t       *probe);

void
sk_coll_destroy(
    skpc_probe_t       *probe);


int
sk_conv_silk_create(
    skpc_probe_t       *probe);
int
sk_conv_silk_destroy(
    skpc_probe_t       *probe);



/*
 *  **********************************************************************
 *  **********************************************************************
 *
 *    rwflowpack.h
 *
 */

/**
 *    The maximum number of flowtype/sensors that a single flow can be
 *    packed to at one time.  Used to set array sizes.
 */
#define MAX_SPLIT_FLOWTYPES 16


/**
 *    Default record version to write.
 */
#ifndef RWFLOWPACK_DEFAULT_VERSION
#  define RWFLOWPACK_DEFAULT_VERSION SK_RECORD_VERSION_ANY
#endif



#ifdef __cplusplus
}
#endif
#endif /* _RWFLOWPACK_H */

/*
** Local Variables:
** mode:c
** indent-tabs-mode:nil
** c-basic-offset:4
** End:
*/
