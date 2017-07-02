/*
** Copyright (C) 2004-2017 by Carnegie Mellon University.
**
** @OPENSOURCE_LICENSE_START@
** See license information in ../../LICENSE.txt
** @OPENSOURCE_LICENSE_END@
*/

#include <silk/silk.h>

RCSIDENT("$SiLK: rwflowpack-probe.c efd886457770 2017-06-21 18:43:23Z mthomas $");

#include <silk/skipaddr.h>
#include <silk/sklog.h>
#include "rwflowpack_priv.h"


/*
 *  *****  Flow Type  **************************************************
 *
 *  The probe is used to determine the flow-type---as defined by in
 *  the silk.conf file---of a flow record (rwRec) read from that
 *  probe.
 *
 *  The skpcProbeDetermineFlowtype() function is defined in the
 *  probeconf-<$SILK_SITE>.c file.
 *
 */

/* LOCAL DEFINES AND TYPEDEFS */

/* Maximum valid value for a port 2^16 - 1 */
#define PORT_VALID_MAX 0xFFFF

/* Set ports to this invalid value initially */
#define PORT_NOT_SET   0xFFFFFFFF

/* Value to use for remaining IPs to say that it hasn't been set */
#define REMAINDER_NOT_SET  INT8_MAX

/*
 *  Specify the maximum size (in terms of RECORDS) of the buffer used
 *  to hold records that have been read from the flow-source but not
 *  yet processed.  This value is the number of records as read from
 *  the wire (e.g., PDUs for a NetFlow v5 probe) per PROBE.  The
 *  maximum memory per NetFlow v5 probe will be BUF_REC_COUNT * 1464.
 *  The maximum memory per IPFIX or NetFlow v9 probe will be
 *  BUF_REC_COUNT * 52 (or BUF_REC_COUNT * 88 for IPv6-enabled SiLK).
 *  If records are processed as quickly as they are read, the normal
 *  memory use per probe will be CIRCBUF_CHUNK_MAX_SIZE bytes.
 */
#define SKPC_DEFAULT_CIRCBUF_SIZE  (1 << 15)


/* a map between probe types and printable names */
static struct probe_type_name_map_st {
    const char         *name;
    skpc_probetype_t    value;
} probe_type_name_map[] = {
    {"ipfix",       PROBE_ENUM_IPFIX},
    {"netflow-v5",  PROBE_ENUM_NETFLOW_V5},
    {"netflow-v9",  PROBE_ENUM_NETFLOW_V9},
    {"sflow",       PROBE_ENUM_SFLOW},
    {"silk",        PROBE_ENUM_SILK},

    /* legacy name for netflow-v5 */
    {"netflow",     PROBE_ENUM_NETFLOW_V5},

    /* sentinel */
    {NULL,          PROBE_ENUM_INVALID}
};


/* a map between protocols and printable names */
static struct skpc_protocol_name_map_st {
    const char     *name;
    uint8_t         num;
    skpc_proto_t    value;
} skpc_protocol_name_map[] = {
    {"sctp", 132, SKPC_PROTO_SCTP},
    {"tcp",    6, SKPC_PROTO_TCP},
    {"udp",   17, SKPC_PROTO_UDP},

    /* sentinel */
    {NULL,     0, SKPC_PROTO_UNSET}
};



/* LOCAL VARIABLES */

/* The probes that have been created and verified */
static sk_vector_t *skpc_probes = NULL;


/* FUNCTION DEFINITIONS */


/*
 *  *****  Probe configuration  **************************************
 */

/* setup the probes */
int
skpcSetup(
    void)
{
    if (NULL == skpc_probes) {
        skpc_probes = skVectorNew(sizeof(skpc_probe_t*));
        if (NULL == skpc_probes) {
            goto ERROR;
        }
    }

    return 0;

  ERROR:
    if (skpc_probes) {
        skVectorDestroy(skpc_probes);
    }
    return -1;
}


/* destroy everything */
void
skpcTeardown(
    void)
{
    skpc_probe_t **probe;
    size_t i;

    /* Free all the probes */
    if (skpc_probes) {
        for (i = 0;
             (probe = (skpc_probe_t**)skVectorGetValuePointer(skpc_probes, i))
                 != NULL;
             ++i)
        {
            skpcProbeDestroy(probe);
        }
        /* destroy the vector itself */
        skVectorDestroy(skpc_probes);
        skpc_probes = NULL;
    }
}


/* return a count of verified probes */
size_t
skpcCountProbes(
    void)
{
    assert(skpc_probes);
    return skVectorGetCount(skpc_probes);
}


int
skpcProbeIteratorBind(
    skpc_probe_iter_t  *probe_iter)
{
    if (probe_iter == NULL || skpc_probes == NULL) {
        return -1;
    }
    probe_iter->cur = 0;
    return 0;
}


int
skpcProbeIteratorNext(
    skpc_probe_iter_t      *probe_iter,
    const skpc_probe_t    **probe)
{
    if (probe_iter == NULL || probe == NULL) {
        return -1;
    }

    if (0 != skVectorGetValue((void*)probe, skpc_probes, probe_iter->cur)) {
        return 0;
    }

    ++probe_iter->cur;
    return 1;
}


/* return a probe having the given probe-name. */
const skpc_probe_t *
skpcProbeLookupByName(
    const char         *probe_name)
{
    const skpc_probe_t **probe;
    size_t i;

    assert(skpc_probes);

    /* check input */
    if (probe_name == NULL) {
        return NULL;
    }

    /* loop over all probes until we find one with given name */
    for (i = 0;
         (probe=(const skpc_probe_t**)skVectorGetValuePointer(skpc_probes, i))
             != NULL;
         ++i)
    {
        if (0 == strcmp(probe_name, (*probe)->probe_name)) {
            return *probe;
        }
    }

    return NULL;
}


/*
 *  *****  Probes  *****************************************************
 */


/*
 *    Destroy the collector configuration data depending on the type
 *    of collector, and reset collector to SKPROBE_COLL_UNKNOWN.
 */
static void
probeDestoryCollectorConfig(
    skpc_probe_t       *probe)
{
    assert(probe);

    switch (probe->coll_type) {
      case SKPROBE_COLL_DIRECTORY:
        packconf_directory_destroy(
            (packconf_directory_t *)probe->coll_conf.directory);
        break;
      case SKPROBE_COLL_FILE:
        packconf_file_destroy(
            (packconf_file_t *)probe->coll_conf.file);
        break;
      case SKPROBE_COLL_NETWORK:
        packconf_network_destroy(
            (packconf_network_t *)probe->coll_conf.network);
        break;
      case SKPROBE_COLL_UNKNOWN:
        break;
    }
    probe->coll_type = SKPROBE_COLL_UNKNOWN;
    probe->coll_conf.network = NULL;
}


/* Create a probe */
int
skpcProbeCreate(
    skpc_probe_t      **probe_param)
{
    skpc_probe_t *probe;

    assert(probe_param);
    *probe_param = NULL;

    probe = sk_alloc(skpc_probe_t);
    probe->coll_type = SKPROBE_COLL_UNKNOWN;
    probe->probe_type = PROBE_ENUM_INVALID;
    probe->log_flags = SOURCE_LOG_ALL;

    *probe_param = probe;
    return 0;
}


/* Destroy a probe and free all memory associated with it */
void
skpcProbeDestroy(
    skpc_probe_t      **probe_param)
{
    skpc_probe_t *probe;

    if (!probe_param || !(*probe_param)) {
        return;
    }
    probe = *probe_param;
    *probe_param = NULL;

    probeDestoryCollectorConfig(probe);
    packer_fileinfo_destroy((packer_fileinfo_t *)probe->file_info);
    free((char *)probe->probe_name);
    free(probe);
}


#ifndef skpcProbeGetName
/* Get and set the name of probe */
const char *
skpcProbeGetName(
    const skpc_probe_t *probe)
{
    assert(probe);
    return probe->probe_name;
}
#endif  /* #ifndef skpcProbeGetName()  */


int
skpcProbeSetName(
    skpc_probe_t       *probe,
    char               *name)
{
    const char *cp;

    assert(probe);
    if (name == NULL || name[0] == '\0') {
        return -1;
    }

    /* check for illegal characters */
    cp = name;
    while (*cp) {
        if (*cp == '/' || isspace((int)*cp)) {
            return -1;
        }
        ++cp;
    }

    free((char *)probe->probe_name);
    probe->probe_name = name;
    return 0;
}


/* Get and set the probe type */
const char *
skpcProbeGetTypeAsString(
    const skpc_probe_t *probe)
{
    assert(probe);
    return skpcProbetypeEnumtoName(probe->probe_type);
}

#ifndef skpcProbeGetType
skpc_probetype_t
skpcProbeGetType(
    const skpc_probe_t *probe)
{
    assert(probe);
    return probe->probe_type;
}
#endif  /* #ifndef skpcProbeGetType */

int
skpcProbeSetType(
    skpc_probe_t       *probe,
    skpc_probetype_t    probe_type)
{
    assert(probe);

    if (NULL == skpcProbetypeEnumtoName(probe_type)) {
        return -1;
    }
    probe->probe_type = probe_type;
    return 0;
}


/* Get and set the probe's protocol */
skpc_proto_t
skpcProbeGetProtocol(
    const skpc_probe_t *probe)
{
    assert(probe);
    if (SKPROBE_COLL_NETWORK != probe->coll_type) {
        return SKPC_PROTO_UNSET;
    }
    return probe->coll_conf.network->n_protocol;
}

/* Get and set probe log-flags */
#ifndef skpcProbeGetLogFlags
uint8_t
skpcProbeGetLogFlags(
    const skpc_probe_t *probe)
{
    assert(probe);
    return probe->log_flags;
}
#endif  /* #ifndef skpcProbeGetLogFlags */

static int
probe_log_flag_search(
    const char         *log_flag_name,
    uint8_t            *log_flag_value)
{
    assert(log_flag_name);
    assert(log_flag_value);

    switch (log_flag_name[0]) {
      case 'a':
        if (0 == strcmp(log_flag_name, "all")) {
            *log_flag_value = SOURCE_LOG_ALL;
            return 0;
        }
        break;
      case 'b':
        if (0 == strcmp(log_flag_name, "bad")) {
            *log_flag_value = SOURCE_LOG_BAD;
            return 0;
        }
        break;
      case 'f':
        if (0 == strcmp(log_flag_name, "firewall-event")) {
            *log_flag_value = SOURCE_LOG_FIREWALL;
            return 0;
        }
        break;
      case 'm':
        if (0 == strcmp(log_flag_name, "missing")) {
            *log_flag_value = SOURCE_LOG_MISSING;
            return 0;
        }
        break;
      case 'n':
        if (0 == strcmp(log_flag_name, "none")) {
            *log_flag_value = SOURCE_LOG_NONE;
            return 0;
        }
        break;
      case 's':
        if (0 == strcmp(log_flag_name, "sampling")) {
            *log_flag_value = SOURCE_LOG_SAMPLING;
            return 0;
        }
        break;
    }
    return -1;
}

int
skpcProbeAddLogFlag(
    skpc_probe_t       *probe,
    const char         *log_flag)
{
    uint8_t value;

    assert(probe);

    if (probe_log_flag_search(log_flag, &value)) {
        /* unrecognized log_flag */
        return -1;
    }
    probe->log_flags |= value;
    return 0;
}

int
skpcProbeRemoveLogFlag(
    skpc_probe_t       *probe,
    const char         *log_flag)
{
    uint8_t value;

    assert(probe);

    if (probe_log_flag_search(log_flag, &value)) {
        /* unrecognized log_flag */
        return -1;
    }
    probe->log_flags &= 0xFF & ~value;
    return 0;
}

void
skpcProbeClearLogFlags(
    skpc_probe_t       *probe)
{
    assert(probe);
    probe->log_flags = 0;
}


/* Get and set host:port to listen on. */
int
skpcProbeGetListenOnSockaddr(
    const skpc_probe_t         *probe,
    const sk_sockaddr_array_t **addr)
{
    assert(probe);
    if (SKPROBE_COLL_NETWORK != probe->coll_type) {
        return -1;
    }

    if (addr) {
        *addr = probe->coll_conf.network->n_listen;
    }
    return 0;
}

const packconf_network_t *
skpcProbeGetNetworkSource(
    const skpc_probe_t *probe)
{
    assert(probe);
    if (SKPROBE_COLL_NETWORK != probe->coll_type) {
        return NULL;
    }
    return probe->coll_conf.network;
}

int
skpcProbeConfigureCollectorNetwork(
    skpc_probe_t               *probe,
    const packconf_network_t   *net)
{
    assert(probe);
    probeDestoryCollectorConfig(probe);
    probe->coll_type = SKPROBE_COLL_NETWORK;
    probe->coll_conf.network = net;
    return 0;
}


/* Get and set the file name to read data from */
const char *
skpcProbeGetFileSource(
    const skpc_probe_t *probe)
{
    assert(probe);
    if (SKPROBE_COLL_FILE != probe->coll_type) {
        return NULL;
    }
    return probe->coll_conf.file->f_file;
}

int
skpcProbeConfigureCollectorFile(
    skpc_probe_t           *probe,
    const packconf_file_t  *file)
{
    assert(probe);
    probeDestoryCollectorConfig(probe);
    probe->coll_type = SKPROBE_COLL_FILE;
    probe->coll_conf.file = file;
    return 0;
}


int
skpcProbeDisposeIncomingFile(
    const skpc_probe_t *probe,
    const char         *path,
    unsigned            has_error)
{
    assert(probe);
    if (SKPROBE_COLL_DIRECTORY != probe->coll_type) {
        return -1;
    }
    return dispose_incoming_file(path, probe->coll_conf.directory, has_error);
}


/* Get and set the attributes of the directory to poll for new files */
uint32_t
skpcProbeGetPollInterval(
    const skpc_probe_t *probe)
{
    assert(probe);
    if (SKPROBE_COLL_DIRECTORY != probe->coll_type) {
        return 0;
    }
    return probe->coll_conf.directory->d_poll_interval;
}

const char *
skpcProbeGetPollDirectory(
    const skpc_probe_t *probe)
{
    assert(probe);
    if (SKPROBE_COLL_DIRECTORY != probe->coll_type) {
        return NULL;
    }
    return probe->coll_conf.directory->d_poll_directory;
}

int
skpcProbeConfigureCollectorDirectory(
    skpc_probe_t               *probe,
    const packconf_directory_t *poll_dir)
{
    assert(probe);
    probeDestoryCollectorConfig(probe);
    probe->coll_type = SKPROBE_COLL_DIRECTORY;
    probe->coll_conf.directory = poll_dir;
    return 0;
}


/* Get and set host to accept connections from */
uint32_t
skpcProbeGetAcceptFromHost(
    const skpc_probe_t             *probe,
    const sk_sockaddr_array_t    ***addr)
{
    assert(probe);
    if (SKPROBE_COLL_NETWORK != probe->coll_type) {
        return -1;
    }
    if (addr) {
        /* const cast */
        *(sk_sockaddr_array_t***)addr = probe->coll_conf.network->n_accept;
    }
    return probe->coll_conf.network->n_accept_count;
}


/* Get and set the information for a file's header */
const packer_fileinfo_t *
skpcProbeGetFileInfo(
    const skpc_probe_t *probe)
{
    assert(probe);
    return probe->file_info;
}

int
skpcProbeSetFileInfo(
    skpc_probe_t               *probe,
    const packer_fileinfo_t    *file_info)
{
    assert(probe);
    packer_fileinfo_destroy((packer_fileinfo_t *)probe->file_info);
    probe->file_info = file_info;
    return 0;
}


/*
 *  *****  Packing  ****************************************************
 */

int
skpcProbeSetPackingFunction(
    skpc_probe_t               *probe,
    packlogic_init_packer_fn_t  packlogic_init)
{
    assert(probe);
    assert(packlogic_init);
    probe->pack.init_packer = packlogic_init;
    return 0;
}


int
skpcProbeInitializePacker(
    skpc_probe_t       *probe)
{
    assert(probe);
    assert(probe->pack.init_packer);
    return probe->pack.init_packer(probe);
}


void
skpcProbeTeardownPacker(
    skpc_probe_t       *probe)
{
    assert(probe);
    if (probe->pack.free_state) {
        probe->pack.free_state(probe);
    }
}


/*
 *  *****  Verification  ***********************************************
 */


/*
 *  is_valid = skpcProbeVerifyIPFIX(p);
 *
 *    Verify that probe has everything required to collect IPFIX data.
 */
static int
skpcProbeVerifyIPFIX(
    skpc_probe_t       *probe)
{
    /* skpcProbeVerify() should have verified that exactly one
     * collection mechanism is defined.  This function only needs to
     * ensure that this probe type supports that mechanism. */

    if (SKPROBE_COLL_NETWORK == probe->coll_type) {
        /* Our IPFIX  support only allows UDP and TCP and has no default */
        assert(probe->coll_conf.network);
        switch (probe->coll_conf.network->n_protocol) {
          case SKPC_PROTO_UDP:
            break;
          case SKPC_PROTO_TCP:
            break;
          case SKPC_PROTO_UNSET:
            skAppPrintErr(("Error verifying probe '%s':\n"
                           "\tType '%s' probes must set"
                           " the protocol to 'tcp' or 'udp'"),
                          probe->probe_name,
                          skpcProbetypeEnumtoName(probe->probe_type));
            return -1;
          default:
            skAppPrintErr(("Error verifying probe '%s':\n"
                           "\tType '%s' probes only support"
                           " the 'udp' or 'tcp' protocol"),
                          probe->probe_name,
                          skpcProbetypeEnumtoName(probe->probe_type));
            return -1;
        }
    }

    return 0;
}


/*
 *  is_valid = skpcProbeVerifyNetflowV5(p);
 *
 *    Verify that probe has everything required to collect NetFlow-V5
 *    data.
 */
static int
skpcProbeVerifyNetflowV5(
    skpc_probe_t       *probe)
{
    /* skpcProbeVerify() should have verified that exactly one
     * collection mechanism is defined.  This function only needs to
     * ensure that this probe type supports that mechanism. */

    /* NetFlow only supports the UDP protocol */
    if ((SKPROBE_COLL_NETWORK == probe->coll_type)
        && (probe->coll_conf.network->n_protocol != SKPC_PROTO_UDP))
    {
        skAppPrintErr(("Error verifying probe '%s':\n"
                       "\tType '%s' probes only support"
                       " the 'udp' protocol"),
                      probe->probe_name,
                      skpcProbetypeEnumtoName(probe->probe_type));
        return -1;
    }

    return 0;
}


/*
 *  is_valid = skpcProbeVerifyNetflowV9(p);
 *
 *    Verify that probe has everything required to collect NetFlow-V9
 *    data.
 */
static int
skpcProbeVerifyNetflowV9(
    skpc_probe_t       *probe)
{
    /* skpcProbeVerify() should have verified that exactly one
     * collection mechanism is defined.  This function only needs to
     * ensure that this probe type supports that mechanism. */

    /* NetFlow v9 does not support reading from files */
    if (SKPROBE_COLL_FILE == probe->coll_type) {
        skAppPrintErr(("Error verifying probe '%s':\n"
                       "\tType '%s' probes do not support"
                       " the read-from-file clause"),
                      probe->probe_name,
                      skpcProbetypeEnumtoName(probe->probe_type));
        return -1;
    }

    /* NetFlow v9 does not yet support directory polling */
    if (SKPROBE_COLL_DIRECTORY == probe->coll_type) {
        skAppPrintErr(("Error verifying probe '%s':\n"
                       "\tType '%s' probes do not support"
                       " the poll-directory clause"),
                      probe->probe_name,
                      skpcProbetypeEnumtoName(probe->probe_type));
        return -1;
    }

    /* NetFlow only supports the UDP protocol */
    if ((SKPROBE_COLL_NETWORK == probe->coll_type)
        && (probe->coll_conf.network->n_protocol != SKPC_PROTO_UDP))
    {
        skAppPrintErr(("Error verifying probe '%s':\n"
                       "\tType '%s' probes only support"
                       " the 'udp' protocol"),
                      probe->probe_name,
                      skpcProbetypeEnumtoName(probe->probe_type));
        return -1;
    }

    return 0;
}


/*
 *  is_valid = skpcProbeVerifySilk(p);
 *
 *    Verify that probe has everything required to re-pack SiLK flow
 *    files.
 */
static int
skpcProbeVerifySilk(
    skpc_probe_t       *probe)
{
    if (SKPROBE_COLL_NETWORK == probe->coll_type) {
        skAppPrintErr(("Error verifying probe '%s':\n"
                       "\tType '%s' probes do not support"
                       " listening on the network"),
                      probe->probe_name,
                      skpcProbetypeEnumtoName(probe->probe_type));
        return -1;
    }

    return 0;
}


/*
 *    Verify that the probes 'p1' and 'p2' both have a list of
 *    accept-from-host addresses and that none of the addresses
 *    overlap.
 *
 *    Return 0 if there is no overlap.  Return -1 if there is overlap
 *    or if either probe lacks an accept-from-host list.
 */
static int
skpcProbeVerifyCompareAcceptFrom(
    const skpc_probe_t *p1,
    const skpc_probe_t *p2)
{
    uint32_t i;
    uint32_t j;

    if (p1->coll_conf.network->n_accept == NULL
        || p2->coll_conf.network->n_accept == NULL)
    {
        return -1;
    }
    if (p1->coll_conf.network->n_accept_count == 0
        || p2->coll_conf.network->n_accept_count == 0)
    {
        return -1;
    }

    for (i = 0; i < p1->coll_conf.network->n_accept_count; ++i) {
        for (j = 0; j < p2->coll_conf.network->n_accept_count; ++j) {
            if (skSockaddrArrayMatches(p1->coll_conf.network->n_accept[i],
                                       p2->coll_conf.network->n_accept[j],
                                       SK_SOCKADDRCOMP_NOPORT))
            {
                return -1;
            }
        }
    }
    return 0;
}


/*
 *  is_valid = skpcProbeVerifyNetwork(p);
 *
 *    Verify that this network-based probe does not conflict with
 *    existing probes.
 */
static int
skpcProbeVerifyNetwork(
    const skpc_probe_t *probe)
{
    const skpc_probe_t **p;
    size_t i;

    /* this function should only be called for network-based probes */
    assert(SKPROBE_COLL_NETWORK == probe->coll_type);
    assert(probe->coll_conf.network);

    /* Loop over all existing probes */
    for (i = 0;
         (p = (const skpc_probe_t**)skVectorGetValuePointer(skpc_probes, i))
             != NULL;
         ++i)
    {
        if ((*p)->coll_conf.network
            && ((*p)->coll_conf.network->n_protocol
                == probe->coll_conf.network->n_protocol)
            && skSockaddrArrayMatches((*p)->coll_conf.network->n_listen,
                                      probe->coll_conf.network->n_listen, 0))
        {
            /* Listen addresses match.  */

            /* Must have the same probe type */
            if (probe->probe_type != (*p)->probe_type) {
                skAppPrintErr(("Error verifying probe '%s':\n"
                               "\tThe listening port and address are the same"
                               " as probe '%s'\n\tand the probe types do not"
                               " match"),
                              probe->probe_name, (*p)->probe_name);
                return -1;
            }

            /* Check their accept_from addresses. */
            if (skpcProbeVerifyCompareAcceptFrom(probe, *p)) {
                skAppPrintErr(("Error verifying probe '%s':\n"
                               "\tThe listening port and address are the same"
                               " as probe '%s';\n\tto distinguish each probe's"
                               " traffic, a unique value for the\n"
                               "\taccept-from-host clause is required on"
                               " each probe."),
                              probe->probe_name, (*p)->probe_name);
                return -1;
            }
        }
    }

    return 0;
}


/*
 *    Verify that 'p' is a valid probe.
 */
int
skpcProbeVerify(
    skpc_probe_t       *probe,
    int                 is_ephemeral)
{
    assert(probe);
    assert(skpc_probes);

    /* check name */
    if ('\0' == probe->probe_name[0]) {
        skAppPrintErr("Error verifying probe:\n\tProbe has no name.");
        return -1;
    }

    /* verify type is not invalid */
    if (probe->probe_type == PROBE_ENUM_INVALID) {
        skAppPrintErr(("Error verifying probe '%s':\n"
                       "\tProbe's type is INVALID."),
                      probe->probe_name);
        return -1;
    }

    /* make certain no other probe has this name */
    if (skpcProbeLookupByName(probe->probe_name)) {
        skAppPrintErr(("Error verifying probe '%s':\n"
                       "\tA probe with this name is already defined"),
                      probe->probe_name);
        return -1;
    }

    /* if is_ephemeral is specified, add it to the global list of
     * probes but don't mark it as verified */
    if (is_ephemeral) {
        return skVectorAppendValue(skpc_probes, &probe);
    }

    /* verification of an individual entry is handled in
     * rwflowpack-config.c */

    if (SKPROBE_COLL_UNKNOWN == probe->coll_type) {
        skAppPrintErr(("Error verifying probe '%s':\n"
                       "\tProbe needs a collection source; must give one"
                       " of listen-on-port,\n\tpoll-directory,"
                       " listen-on-unix-socket, or read-from-file."),
                      probe->probe_name);
    }

    /* when poll-directory is specified, no other probe can specify
     * that same directory */
    if (SKPROBE_COLL_DIRECTORY == probe->coll_type) {
        const skpc_probe_t **p;
        size_t i;

        /* loop over all probes checking the poll-directory */
        for (i = 0;
             (p = (const skpc_probe_t**)skVectorGetValuePointer(skpc_probes,i))
                 != NULL;
             ++i)
        {
            if ((*p)->coll_conf.directory
                && (0 == strcmp(probe->coll_conf.directory->d_poll_directory,
                                (*p)->coll_conf.directory->d_poll_directory)))
            {
                skAppPrintErr(("Error verifying probe '%s':\n"
                               "\tThe poll-directory must be unique, but"
                               " probe '%s' is\n\talso polling '%s'"),
                              probe->probe_name, (*p)->probe_name,
                              probe->coll_conf.directory->d_poll_directory);
                return -1;
            }
        }
    }

    /* when listening on a port, make sure we're not tromping over
     * other probes' ports */
    if (SKPROBE_COLL_NETWORK == probe->coll_type
        && skpcProbeVerifyNetwork(probe))
    {
        return -1;
    }

    /* verify the probe by its type */
    switch (probe->probe_type) {
      case PROBE_ENUM_NETFLOW_V5:
        if (0 != skpcProbeVerifyNetflowV5(probe)) {
            return -1;
        }
        break;

      case PROBE_ENUM_IPFIX:
        if (0 != skpcProbeVerifyIPFIX(probe)) {
            return -1;
        }
        break;

      case PROBE_ENUM_NETFLOW_V9:
        if (0 != skpcProbeVerifyNetflowV9(probe)) {
            return -1;
        }
        break;

      case PROBE_ENUM_SFLOW:
        /* sFlow probes have same requirements as NetFlow v9 */
        if (0 != skpcProbeVerifyNetflowV9(probe)) {
            return -1;
        }
        break;

      case PROBE_ENUM_SILK:
        if (0 != skpcProbeVerifySilk(probe)) {
            return -1;
        }
        break;

      case PROBE_ENUM_INVALID:
        /* should have caught this above */
        skAbortBadCase(probe->probe_type);
    }

    /* probe is valid; add it to the global vector of probes */
    if (skVectorAppendValue(skpc_probes, &probe)) {
        return -1;
    }

    return 0;
}


/*
 *  *****  Probe as Data Source  ****************************************
 */

void
skpcProbeLogSourceStats(
    skpc_probe_t       *probe)
{
    assert(probe);
    assert(probe->converter);

    switch (probe->probe_type) {
      case PROBE_ENUM_NETFLOW_V5:
        sk_conv_pdu_log_stats(probe);
        break;
      case PROBE_ENUM_IPFIX:
      case PROBE_ENUM_NETFLOW_V9:
      case PROBE_ENUM_SFLOW:
        sk_conv_ipfix_log_stats(probe);
        break;
      default:
        CRITMSG("'%s': Invalid probe type id '%d'",
                probe->probe_name, (int)probe->probe_type);
        skAbortBadCase(probe->probe_type);
    }
}


/*
 *  *****  Probes Types  *****************************************************
 */

/* return an enum value given a probe type name */
skpc_probetype_t
skpcProbetypeNameToEnum(
    const char         *name)
{
    struct probe_type_name_map_st *entry;

    if (name) {
        for (entry = probe_type_name_map; entry->name; ++entry) {
            if (0 == strcmp(name, entry->name)) {
                return entry->value;
            }
        }
    }
    return PROBE_ENUM_INVALID;
}


/* return the name given a probe type number */
const char *
skpcProbetypeEnumtoName(
    skpc_probetype_t    type)
{
    struct probe_type_name_map_st *entry;

    for (entry = probe_type_name_map; entry->name; ++entry) {
        if (type == entry->value) {
            return entry->name;
        }
    }
    return NULL;
}


/*
 *  *****  Probes Protocols  *************************************************
 */

/* return an protocol enum value given a probe protocol name */
skpc_proto_t
skpcProtocolNameToEnum(
    const char         *name)
{
    struct skpc_protocol_name_map_st *entry;
    uint32_t num;

    if (NULL != name) {
        for (entry = skpc_protocol_name_map; entry->name; ++entry) {
            if (0 == strcmp(name, entry->name)) {
                return entry->value;
            }
        }
        if (isdigit((int)*name)) {
            /* attempt to parse as a number */
            if (0 == skStringParseUint32(&num, name, 0, 255)) {
                for (entry = skpc_protocol_name_map; entry->name; ++entry) {
                    if (num == entry->num) {
                        return entry->value;
                    }
                }
            }
        }
    }

    return SKPC_PROTO_UNSET;
}


/* return a name given a probe protocol enum */
const char *
skpcProtocolEnumToName(
    skpc_proto_t        protocol)
{
    struct skpc_protocol_name_map_st *entry;

    for (entry = skpc_protocol_name_map; entry->name; ++entry) {
        if (protocol == entry->value) {
            return entry->name;
        }
    }
    return NULL;
}


/*
** Local Variables:
** mode:c
** indent-tabs-mode:nil
** c-basic-offset:4
** End:
*/
