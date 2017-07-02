/*
** Copyright (C) 2004-2017 by Carnegie Mellon University.
**
** @OPENSOURCE_LICENSE_START@
** See license information in ../../LICENSE.txt
** @OPENSOURCE_LICENSE_END@
*/
#ifndef _RWFLOWPACK_PRIV_H
#define _RWFLOWPACK_PRIV_H
#ifdef __cplusplus
extern "C" {
#endif

#include <silk/silk.h>

RCSIDENTVAR(rcsID_RWFLOWPACK_PRIV_H, "$SiLK: rwflowpack_priv.h efd886457770 2017-06-21 18:43:23Z mthomas $");

/*
**  rwflowpack_priv.h
**
**    Private definitions and prototypes for rwflowpack.
*/

#include "rwflowpack.h"
#include <silk/skdeque.h>
#include <silk/sklog.h>
#include <silk/sksite.h>
#include <silk/skstream.h>
#include <silk/skthread.h>
#include <silk/skvector.h>
#include <silk/utils.h>


/* Helper macro for messages */
#define CHECK_PLURAL(cp_x) ((1 == (cp_x)) ? "" : "s")

/*
 *    rwflowpack-pdusource.c
 */

int
sk_conv_pdu_create(
    skpc_probe_t       *probe);

void
sk_conv_pdu_destroy(
    skpc_probe_t       *probe);

int
sk_conv_pdu_stream(
    skpc_probe_t       *probe,
    skstream_t         *stream);


/**
 *    Creates a PDU source based on a skpc_probe_t.
 */
int
sk_coll_pdu_create(
    skpc_probe_t       *probe);

/**
 *
 *    If the source is a network-based probe, this function also
 *    starts the collection process.
 *
 *    When creating a source from a network-based probe, the 'params'
 *    union should have the 'max_pkts' member specify the maximum
 *    number of packets to buffer in memory for this source.
 *
 *    When creating a source from a probe that specifies either a file
 *    or a directory that is polled for files, the 'params' union must
 *    have the 'path_name' specify the full path of the file to
 *    process.
 *
 *    Return the new source, or NULL on error.
 */
int
sk_coll_pdu_start(
    skpc_probe_t       *probe);

/**
 *    Stops processing of packets.  This will cause a call to any
 *    sk_coll_pdu_GetGeneric() function to stop blocking.  Meant to be
 *    used as a prelude to sk_coll_pdu_Destroy() in threaded code.
 */
void
sk_coll_pdu_stop(
    skpc_probe_t       *probe);

/**
 *    Destroys the PDU source.  This will also cause a call to any
 *    sk_coll_pdu_GetGeneric() function to stop blocking.  In threaded
 *    programs, it is best to call sk_coll_pdu_Stop() first, wait for
 *    any consumers of the packets to notice that the source has
 *    stopped, then call sk_coll_pdu_Destroy().
 */
void
sk_coll_pdu_destroy(
    skpc_probe_t       *probe);

/**
 *    Logs statistics associated with a PDU source, and then clears
 *    the stats.
 */
void
sk_conv_pdu_log_stats(
    skpc_probe_t       *probe);


int
sklua_open_pdusource(
    lua_State          *L);

typedef struct sk_lua_nfv5_st sk_lua_nfv5_t;

void
sk_lua_push_nfv5(
    lua_State          *L,
    sk_lua_nfv5_t      *nfv5);



/*
 *    rwflowpack-ipfixsource.c
 */

/**
 *    Set 'probe' to convert IPFIX records to SiLK Flow records'
 */
int
sk_conv_ipfix_create(
    skpc_probe_t       *probe);

/**
 *    Destroy any state used by the IPFIX converter.
 */
void
sk_conv_ipfix_destroy(
    skpc_probe_t       *probe);

void
sk_conv_ipfix_log_stats(
    skpc_probe_t       *probe);

/**
 *    Read IPFIX records from 'stream', convert them to SiLK Flow
 *    records, and invoke the packer to store them.
 */
int
sk_conv_ipfix_stream(
    skpc_probe_t       *probe,
    skstream_t         *stream);

/**
 *    Set 'probe' to collect IPFIX from the network.  Open the port
 *    specified on the probe.
 */
int
sk_coll_ipfix_create(
    skpc_probe_t       *probe);

/**
 *    Start the collection thread to collect IPFIX from the network
 *    and pack those records as SiLK Flow records.
 */
int
sk_coll_ipfix_start(
    skpc_probe_t       *probe);

/**
 *    Stop the collection thread that is collecting IPFIX from the
 *    network.
 */
void
sk_coll_ipfix_stop(
    skpc_probe_t       *probe);

/**
 *    Destory any state that was used while collecting IPFIX from the
 *    network.
 */
void
sk_coll_ipfix_destroy(
    skpc_probe_t       *probe);



/**
 *    packconf_directory_t manages information about a directory to poll
 *    periodically.  Created while parsing the configuration file,
 *    destroyed by calling packconf_directory_destroy();
 */
struct packconf_directory_st {
    const char         *d_poll_directory;
    const char         *d_error_directory;
    const char         *d_archive_directory;
    const char         *d_post_archive_command;
    uint32_t            d_poll_interval;
    unsigned            d_flat_archive  :1;
};
/* packconf_directory_t */


/**
 *    packconf_file_t manages information regarding the path to an
 *    incoming file to process and how to dispose of that file once it
 *    has been processed.  Created while parsing the configuration
 *    file, destroyed by calling packconf_file_destroy();
 */
struct packconf_file_st {
    const char         *f_file;
    const char         *f_error_directory;
    const char         *f_archive_directory;
    const char         *f_post_archive_command;
};
/* packconf_file_t */

/**
 *    packconf_network_t manages information regarding reading packets
 *    from the network.  Created while parsing the configuration file,
 *    destroyed by calling packconf_network_destroy();
 */
struct packconf_network_st {
    /* address and port to bind() to */
    sk_sockaddr_array_t    *n_listen;
    /* array of hosts that may connect; length is given by n_accept_count */
    sk_sockaddr_array_t   **n_accept;
    /* string specified to create the 'n_listen' address */
    const char             *n_listen_str;
    /* number of entries in the 'n_accept' array */
    uint32_t                n_accept_count;
    skpc_proto_t            n_protocol;
};
/* packconf_network_t */


/**
 *    packer_fileinfo_t holds information regarding output files
 *    created by rwflowpack.  The structure holds the file format,
 *    that format's version, and the sidecar description to be written
 *    to the file's header.
 *
 *    The structure is specified in the packing function when
 *    rwflowpack is categorizing records (output-mode is
 *    OUTPUT_LOCAL_STORAGE or OUTPUT_INCREMENTAL_FILES).  In flowcap
 *    output mode (OUTPUT_FLOWCAP), the structure is specified on each
 *    individual probe.  In one-destination output mode
 *    (OUTPUT_ONE_DESTINATION), the structure is specified in the
 *    output block, and the destination file's location is also
 *    specified in that block.
 *
 *    May be freed by calling packer_fileinfo_destroy();
 */
struct packer_fileinfo_st {
    sk_file_format_t    record_format;
    sk_file_version_t   record_version;
    silk_endian_t       byte_order;
    sk_compmethod_t     comp_method;
    const sk_sidecar_t *sidecar;
};
/* packer_fileinfo_t */


/**
 *    Destroy the directory structure 'dir'.  This is a no-op when
 *    'dir' is NULL.
 */
void
packconf_directory_destroy(
    packconf_directory_t   *dir);

/**
 *    Destroy the file structure 'file'.  This is a no-op when 'file'
 *    is NULL.
 */
void
packconf_file_destroy(
    packconf_file_t    *file);

/**
 *    Destroy the network structure 'net'.  This is a no-op when 'net'
 *    is NULL.
 */
void
packconf_network_destroy(
    packconf_network_t *net);

/**
 *    Destory the file-info structure 'finfo'.  This is a no-ip when
 *    'finfo' is NULL.
 */
void
packer_fileinfo_destroy(
    packer_fileinfo_t  *finfo);


/*
 *    Each input_mode_type in rwflowpack is implemented in a separate
 *    file.  Each of these files must define a core set of functions,
 *    which are specified in this structure.
 *
 *    rwflowpack has an array of initialization functions, one for
 *    each input_mode type.  Once the input-mode is known, rwflowpack
 *    invokes the initialization function for that input_mode_type
 *    with this strucure as a parameter; the initialization function
 *    must set function pointers for that input_mode_type.
 */
typedef struct input_mode_type_st input_mode_type_t;
struct input_mode_type_st {
    /*
     * The setup_fn() function is called once for the active
     * input_mode_type.
     *
     * This function is the last time the input_mode_type has to do
     * any processing or setup prior to rwflowpack daemonizing itself.
     *
     * The function should return 0 on successful set-up, or non-zero
     * if there was an error.
     */
    int       (*setup_fn)(void);

    /*
     * The start_fn() is the function rwflowpack calls to tell the
     * active input_mode type to start processing records.  The
     * start_fn() should start its flow collector(s) and spawn
     * thread(s) as needed to manage those collectors.
     *
     * The function should returns 0 on success, or non-zero on
     * failure.
     */
    int       (*start_fn)(void);

    /*
     * When rwflowpack periodically flushes files, it calls the
     * print_stats_fn().  This function should log information about
     * the number of records processed.
     */
    void      (*print_stats_fn)(void);

    /*
     * When rwflowpack has been signaled to terminate, it will call
     * the stop_fn() to stop the input_mode.
     */
    void      (*stop_fn)(void);

    /*
     * rwflowpack calls the teardown_fn() once for each input_mode
     * type.  This function can perform any final cleanup.  This is
     * the final function called for each input_mode type.
     */
    void      (*teardown_fn)(void);
};
/* input_mode_type_t */



/* How to run: Input and Output Modes. Must be kept in sync with the
 * available_modes[] array */
typedef enum {
    INPUT_STREAM,
    INPUT_SINGLEFILE,
    INPUT_FCFILES,
    INPUT_APPEND,
    OUTPUT_LOCAL_STORAGE,
    OUTPUT_INCREMENTAL_FILES,
    OUTPUT_FLOWCAP,
    OUTPUT_ONE_DESTINATION
} io_mode_t;

/* The number of modes */
#define NUM_MODES 8

/* structure of function pointers for the input_mode being used */
extern input_mode_type_t input_mode_type;

/* non-zero when file locking is disabled */
extern int no_file_locking;

/* Size of the cache for writing output files.  Can be modified by
 * --file-cache-size switch. */
extern uint32_t stream_cache_size;

/* Number of seconds between cache flushes */
extern uint32_t flush_timeout;

/* In stream input mode, this holds the default settings for disposal
 * of files from directory-based probes.  In addition, this determines
 * the default polling interval for those probes. */
extern packconf_directory_t *stream_directory_defaults;

/* Incoming directory used by the input-modes append-incremental and
 * fcfiles. */
extern packconf_directory_t *incoming_directory;

extern const char *processing_directory;
extern const char *incremental_directory;
extern const char *destination_directory;

/* Command to run on newly created hourly files */
extern const char *hour_file_command;

/* deque that contains incremental files that need to be processed */
extern sk_deque_t *output_deque;

/* oldest file (in hours) that is considered acceptable.  incremental
 * files older than this will be put into the error directory. */
extern int64_t reject_hours_past;

/* how far into the future are incremental files accepted. */
extern int64_t reject_hours_future;

/* whether reject_hours_past and/or reject_hours_future differ from
 * their default values--meaning we need to run the tests */
extern int check_time_window;

/* To ensure records are sent along in a timely manner, the files are
 * closed when a timer fires or once they get to a certain size.
 * These variables define those values. */
extern uint64_t max_file_size;

/* Timer base (0 if none) from which we calculate timeouts */
extern sktime_t clock_time;

/* Amount of disk space to allow for a new file when determining
 * whether there is disk space available for flowcap stuff.  This will
 * be max_file_size plus some overhead should the compressed data be
 * larger than the raw data. */
extern uint64_t alloc_file_size;

/* leave at least this much free space on the disk; specified by
 * --freespace-minimum.  Gets set to DEFAULT_FREESPACE_MINIMUM */
extern int64_t freespace_minimum_bytes;

/* take no more that this amount of the disk; as a percentage.
 * specified by --space-maximum-percent */
extern double usedspace_maximum_percent;

/* default input and output modes */
extern io_mode_t input_mode;
extern io_mode_t output_mode;

/* number of appender threads to run */
extern uint32_t appender_count;

/* Set to true once the input thread(s) have started.  This is used by
 * the output thread to know whether to check the thread count. */
extern int input_thread_started;

/* for one-destination output mode, where to write the data */
extern const char *one_destination_path;

/* for one-destination output mode, the information about the format
 * and sidecar for that file */
extern const packer_fileinfo_t *one_destination_fileinfo;

/* A read-only cache of IPset files shared across all threads */
extern sk_ipset_cache_t *ipset_cache;

/* The path to the config file. */
extern const char *packer_config_file;


/*
 *    The initialization functions for each input_mode_type_t.  These
 *    are defined in the *readers.c files.  These could be specified
 *    in a header file for each input_mode_type, but it seems like
 *    overkill to have a header for a single function declaration.
 *
 *    These functions are passed the input_mode_type structure; the
 *    functions should set the function pointers.
 */
int
append_initialize(
    input_mode_type_t  *input_mode_type);

int
fcfiles_initialize(
    input_mode_type_t  *input_mode_type);

int
singlefile_initialize(
    input_mode_type_t  *input_mode_type);

int
stream_initialize(
    input_mode_type_t  *input_mode_type);


/*
 *    Increase by one the count of the processing threads.
 */
void
increment_thread_count(
    void);

/*
 *    Decrease by one the count of the processing threads.  If the
 *    argument is non-zero and the number of threads is 0, send a
 *    signal to the main thread.
 */
void
decrement_thread_count(
    int                 send_signal_to_main);

/*
 *    Return the number of running threads.
 */
size_t
get_thread_count(
    void);


/*
 *  status = move_to_directory(in_path,out_dir,out_basename,result,result_sz);
 *
 *    Move the file whose full path is 'in_path' into the directory
 *    named by 'out_dir' and attempt to name the file 'out_basename'.
 *    Return 0 on success, or -1 on failure.
 *
 *    The filename in 'out_basename' is expected to have a mkstemp()
 *    type extension on the end.  This function attempts to move the
 *    file into the 'out_dir' without changing the extension.
 *    However, if a file with that name already exists, an additional
 *    mkstemp()-type extension is added.
 *
 *    If 'result' is non-NULL, it must be a character array whose size
 *    is specified by 'result_sz', and 'result' will be filled with
 *    the complete path of resulting file.
 */
int
move_to_directory(
    const char         *in_path,
    const char         *out_dir,
    const char         *out_basename,
    char               *result,
    size_t              result_sz);

/*
 *    Dispose of the incoming file 'filepath' that was found by
 *    polling the directory specified by 'src_dir'.
 *
 *    Move the file to the error directory when 'has_error' is
 *    non-zero.
 *
 *    When 'has_error' is non-zero, either remove the file is no
 *    archive_directory is set or move the file to the
 *    archive_directory.  If a post-archive command is set, run the
 *    command after archiving the file.
 */
int
dispose_incoming_file(
    const char                 *filepath,
    const packconf_directory_t *src_dir,
    int                         has_error);

void
appSetup(
    int                 argc,
    char              **argv);
void
appTeardown(
    void);


/*
 *    Functions in rwflowpack.c to limit the number of incoming files
 *    the directory polling threads can have open.
 */
int
flowpackAcquireFileHandle(
    void);

void
flowpackReleaseFileHandle(
    void);

int
flowpackSetMaximumFileHandles(
    int                 new_max_fh);


/*
 *    Create the state necessary to append incremental files to the
 *    hourly files in the repository.  Return 0 on success, or -1 on
 *    failure.
 */
int
appender_setup(
    void);


/*
 *    Start appending incremental files to the repository.
 */
int
appender_start(
    void);

/*
 *    Stop appending incremental files to the repository and wait for
 *    all threads to complete.
 */
void
appender_stop(
    void);

/*
 *    Destroy the state created by appender_setup().
 */
void
appender_teardown(
    void);


int
byteOrderParse(
    const char         *endian_name,
    silk_endian_t      *endian_value);


int
repo_write_rwrec_lua(
    lua_State          *L);

int
flowcap_write_rwrec_lua(
    lua_State          *L);

int
flowcap_initialize_packer(
    skpc_probe_t       *probe,
    lua_State          *L);


int
onedest_write_rwrec_lua(
    lua_State          *L);

int
onedest_initialize_packer(
    skpc_probe_t       *probe,
    lua_State          *L);


#ifdef __cplusplus
}
#endif
#endif /* _RWFLOWPACK_PRIV_H */

/*
** Local Variables:
** mode:c
** indent-tabs-mode:nil
** c-basic-offset:4
** End:
*/
