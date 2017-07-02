/*
** Copyright (C) 2007-2017 by Carnegie Mellon University.
**
** @OPENSOURCE_LICENSE_START@
** See license information in ../../LICENSE.txt
** @OPENSOURCE_LICENSE_END@
*/

/*
**  rwsilk2ipfix.c
**
**    SiLK to IPFIX translation application
**
**    Brian Trammell
*/


#include <silk/silk.h>

RCSIDENT("$SiLK: rwsilk2ipfix.c efd886457770 2017-06-21 18:43:23Z mthomas $");

#include <silk/rwrec.h>
#include <silk/skfixstream.h>
#include <silk/skflowiter.h>
#include <silk/skipfix.h>
#include <silk/skipfixcert.h>
#include <silk/sklog.h>
#include <silk/skschema.h>
#include <silk/sksite.h>
#include <silk/skstream.h>
#include <silk/skvector.h>
#include <silk/utils.h>


/* LOCAL DEFINES AND TYPEDEFS */

/* where to write --help output */
#define USAGE_FH stdout

/* where to write --print-stat output */
#define STATS_FH stderr

/* destination for log messages; go ahead and use stderr since
 * normally there are no messages when converting SiLK to IPFIX. */
#define LOG_DESTINATION_DEFAULT  "stderr"

/* The IPFIX Private Enterprise Number for CERT */
#define IPFIX_CERT_PEN  6871

/* The observation domain to use in the output */
#define OBSERVATION_DOMAIN  0

/* The sk_field_ident_t representing the IE for padding */
#define PADDING_IE      SK_FIELD_IDENT_CREATE(0, 210)

/* The sk_field_ident_t representing the IE for a basicList */
#define BASICLIST_IE    SK_FIELD_IDENT_CREATE(0, 291)


/*
 *    These flags are used to select particular fields from the
 *    fbInfoElementSpec_t 'multiple_spec' array below.
 */
/* IP version */
#define REC_V6            (1 <<  0)
#define REC_V4            (1 <<  1)
/* for protocols with no ports */
#define REC_NO_PORTS      (1 <<  2)
/* for ICMP records */
#define REC_ICMP          (1 <<  3)
/* for non-TCP records with ports (UDP, SCTP) */
#define REC_UDP           (1 <<  4)
/* for TCP records with a single flag */
#define REC_TCP           (1 <<  5)
/* for TCP records with a expanded flags */
#define REC_TCP_EXP       (1 <<  6)
/* additional flags could be added based on the type of SiLK flow
 * file; for example: whether the record has NextHopIP + SNMP ports,
 * or whether it has an app-label.  Each additional test doubles the
 * number templates to manage. */

/*
 *    External Template ID traditionally used for SiLK Flow
 *    records written to an IPFIX stream.
 */
#define SKI_RWREC_TID        0xAFEA

/*
 *    Template IDs used for each template
 */
#define TID4_NOPORTS    0x9DD0
#define TID4_ICMP       0x9DD1
#define TID4_UDP        0x9DD2
#define TID4_TCP        0x9DD3
#define TID4_TCP_EXP    0x9DD4
#define TID6_NOPORTS    0x9ED0
#define TID6_ICMP       0x9ED1
#define TID6_UDP        0x9ED2
#define TID6_TCP        0x9ED3
#define TID6_TCP_EXP    0x9ED4

/*
 *    The number of template IDs defined immediately above.
 */
#define TMPL_COUNT 10

/*
 *    Structures to map an rwRec into prior to transcoding with the
 *    template.
 */
struct rec_prelim_st {
    uint64_t            stime;
    uint64_t            etime;
    uint32_t            packets;
    uint32_t            bytes;
    uint16_t            ingress;
    uint16_t            egress;
    uint16_t            application;
    uint16_t            sensor;
};
typedef struct rec_prelim_st rec_prelim_t;

struct rec_noports_v4_st {
    rec_prelim_t        pre;
    uint8_t             flowtype;
    uint8_t             attributes;
    uint8_t             protocol;
    uint8_t             padding1;
    uint32_t            sip;
    uint32_t            dip;
    uint32_t            nhip;
};
typedef struct rec_noports_v4_st rec_noports_v4_t;

struct rec_noports_v6_st {
    rec_prelim_t        pre;
    uint8_t             flowtype;
    uint8_t             attributes;
    uint8_t             protocol;
    uint8_t             padding1;
    uint32_t            padding3;
    uint8_t             sip[16];
    uint8_t             dip[16];
    uint8_t             nhip[16];
};
typedef struct rec_noports_v6_st rec_noports_v6_t;

struct rec_icmp_v4_st {
    rec_prelim_t        pre;
    uint8_t             flowtype;
    uint8_t             attributes;
    uint8_t             protocol;
    uint8_t             padding1;
    uint16_t            padding2;
    uint16_t            icmptypecode;
    uint32_t            padding3;
    uint32_t            sip;
    uint32_t            dip;
    uint32_t            nhip;
};
typedef struct rec_icmp_v4_st rec_icmp_v4_t;

struct rec_icmp_v6_st {
    rec_prelim_t        pre;
    uint8_t             flowtype;
    uint8_t             attributes;
    uint8_t             protocol;
    uint8_t             padding1;
    uint16_t            padding2;
    uint16_t            icmptypecode;
    uint8_t             sip[16];
    uint8_t             dip[16];
    uint8_t             nhip[16];
};
typedef struct rec_icmp_v6_st rec_icmp_v6_t;

struct rec_udp_v4_st {
    rec_prelim_t        pre;
    uint8_t             flowtype;
    uint8_t             attributes;
    uint8_t             protocol;
    uint8_t             padding1;
    uint16_t            sport;
    uint16_t            dport;
    uint32_t            padding3;
    uint32_t            sip;
    uint32_t            dip;
    uint32_t            nhip;
};
typedef struct rec_udp_v4_st rec_udp_v4_t;

struct rec_udp_v6_st {
    rec_prelim_t        pre;
    uint8_t             flowtype;
    uint8_t             attributes;
    uint8_t             protocol;
    uint8_t             padding1;
    uint16_t            sport;
    uint16_t            dport;
    uint8_t             sip[16];
    uint8_t             dip[16];
    uint8_t             nhip[16];
};
typedef struct rec_udp_v6_st rec_udp_v6_t;

struct rec_tcp_v4_st {
    rec_prelim_t        pre;
    uint8_t             flowtype;
    uint8_t             attributes;
    uint8_t             protocol;
    uint8_t             flags_all;
    uint16_t            sport;
    uint16_t            dport;
    uint32_t            padding3;
    uint32_t            sip;
    uint32_t            dip;
    uint32_t            nhip;
};
typedef struct rec_tcp_v4_st rec_tcp_v4_t;

struct rec_tcp_v6_st {
    rec_prelim_t        pre;
    uint8_t             flowtype;
    uint8_t             attributes;
    uint8_t             protocol;
    uint8_t             flags_all;
    uint16_t            sport;
    uint16_t            dport;
    uint8_t             sip[16];
    uint8_t             dip[16];
    uint8_t             nhip[16];
};
typedef struct rec_tcp_v6_st rec_tcp_v6_t;

struct rec_tcp_exp_v4_st {
    rec_prelim_t        pre;
    uint8_t             flowtype;
    uint8_t             attributes;
    uint8_t             protocol;
    uint8_t             padding1;
    uint16_t            sport;
    uint16_t            dport;
    uint8_t             padding4;
    uint8_t             flags_all;
    uint8_t             flags_init;
    uint8_t             flags_rest;
    uint32_t            sip;
    uint32_t            dip;
    uint32_t            nhip;
};
typedef struct rec_tcp_exp_v4_st rec_tcp_exp_v4_t;

struct rec_tcp_exp_v6_st {
    rec_prelim_t        pre;
    uint8_t             flowtype;
    uint8_t             attributes;
    uint8_t             protocol;
    uint8_t             padding1;
    uint16_t            sport;
    uint16_t            dport;
    uint32_t            padding3;
    uint8_t             padding4;
    uint8_t             flags_all;
    uint8_t             flags_init;
    uint8_t             flags_rest;
    uint8_t             sip[16];
    uint8_t             dip[16];
    uint8_t             nhip[16];
};
typedef struct rec_tcp_exp_v6_st rec_tcp_exp_v6_t;


/* LOCAL VARIABLE DEFINITIONS */

/*
 *    Defines the fields contained by the various templates.
 */
static fbInfoElementSpec_t multiple_spec[] = {
    /* sTime */
    {(char *)"flowStartMilliseconds",    8,  0},
    /* eTime */
    {(char *)"flowEndMilliseconds",      8,  0},
    /* pkts */
    {(char *)"packetDeltaCount",         4,  0},
    /* bytes */
    {(char *)"octetDeltaCount",          4,  0},
    /* input, output */
    {(char *)"ingressInterface",         2,  0},
    {(char *)"egressInterface",          2,  0},
    /* application */
    {(char *)"silkAppLabel",             2,  0},
    /* sID */
    {(char *)"silkFlowSensor",           2,  0},
    /* flow_type */
    {(char *)"silkFlowType",             1,  0},
    /* attributes */
    {(char *)"silkTCPState",             1,  0},
    /* proto */
    {(char *)"protocolIdentifier",       1,  0},

    /* either flags_all or padding1 */
    {(char *)"tcpControlBits",           1,  REC_TCP},
    {(char *)"paddingOctets",            1,  REC_TCP_EXP},
    {(char *)"paddingOctets",            1,  REC_NO_PORTS},
    {(char *)"paddingOctets",            1,  REC_ICMP},
    {(char *)"paddingOctets",            1,  REC_UDP},

    /* nothing if no_ports, padding2 if ICMP, or sPort */
    {(char *)"paddingOctets",            2,  REC_ICMP},
    {(char *)"sourceTransportPort",      2,  REC_UDP},
    {(char *)"sourceTransportPort",      2,  REC_TCP},
    {(char *)"sourceTransportPort",      2,  REC_TCP_EXP},

    /* nothing if no_ports, icmpTypeCode if ICMP, or dPort */
    {(char *)"icmpTypeCodeIPv4",         2,  REC_ICMP | REC_V4},
    {(char *)"icmpTypeCodeIPv6",         2,  REC_ICMP | REC_V6},
    {(char *)"destinationTransportPort", 2,  REC_UDP},
    {(char *)"destinationTransportPort", 2,  REC_TCP},
    {(char *)"destinationTransportPort", 2,  REC_TCP_EXP},

    /* nothing if no_ports and IPv4; padding3 if (1)IPv6 and no_ports,
     * (2)IPv6 and expanded TCP, (3)IPv4 and not expanded TCP */
    {(char *)"paddingOctets",            4,  REC_NO_PORTS | REC_V6},
    {(char *)"paddingOctets",            4,  REC_TCP_EXP | REC_V6},
    {(char *)"paddingOctets",            4,  REC_ICMP | REC_V4},
    {(char *)"paddingOctets",            4,  REC_UDP  | REC_V4},
    {(char *)"paddingOctets",            4,  REC_TCP  | REC_V4},

    /* nothing unless expanded TCP */
    {(char *)"paddingOctets",            1,  REC_TCP_EXP},
    {(char *)"tcpControlBits",           1,  REC_TCP_EXP},
    {(char *)"initialTCPFlags",          1,  REC_TCP_EXP},
    {(char *)"unionTCPFlags",            1,  REC_TCP_EXP},

    /* sIP -- one of these is used */
    {(char *)"sourceIPv6Address",        16, REC_V6},
    {(char *)"sourceIPv4Address",        4,  REC_V4},
    /* dIP -- one of these is used */
    {(char *)"destinationIPv6Address",   16, REC_V6},
    {(char *)"destinationIPv4Address",   4,  REC_V4},
    /* nhIP -- one of these is used */
    {(char *)"ipNextHopIPv6Address",     16, REC_V6},
    {(char *)"ipNextHopIPv4Address",     4,  REC_V4},

    /* done */
    FB_IESPEC_NULL
};

/*
 *    Flags to select elements from the multiple_spec[] above.
 */
static const uint32_t multiple_spec_flag[] = {
    REC_V4 | REC_NO_PORTS,
    REC_V4 | REC_ICMP,
    REC_V4 | REC_UDP,
    REC_V4 | REC_TCP,
    REC_V4 | REC_TCP_EXP,
    REC_V6 | REC_NO_PORTS,
    REC_V6 | REC_ICMP,
    REC_V6 | REC_UDP,
    REC_V6 | REC_TCP,
    REC_V6 | REC_TCP_EXP
};

/*
 *    The Template IDs to assign to the templates
 */
static const uint16_t multiple_tid[] = {
    TID4_NOPORTS, TID4_ICMP, TID4_UDP, TID4_TCP, TID4_TCP_EXP,
    TID6_NOPORTS, TID6_ICMP, TID6_UDP, TID6_TCP, TID6_TCP_EXP
};

/*
 *    tid_to_position is a variable that allows mapping from a
 *    Template ID to its position in various arrays.
 */
static const struct tid_to_position_st {
    unsigned int    p_TID4_NOPORTS;
    unsigned int    p_TID4_ICMP;
    unsigned int    p_TID4_UDP;
    unsigned int    p_TID4_TCP;
    unsigned int    p_TID4_TCP_EXP;
    unsigned int    p_TID6_NOPORTS;
    unsigned int    p_TID6_ICMP;
    unsigned int    p_TID6_UDP;
    unsigned int    p_TID6_TCP;
    unsigned int    p_TID6_TCP_EXP;
} tid_to_position = {
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9
};


/* for looping over input */
static sk_options_ctx_t *optctx = NULL;
static sk_flow_iter_t *flowiter = NULL;

/* the IPFIX output file; use stdout if no name provided */
static skstream_t *ipfix_output = NULL;

/* a FILE* created from the ipfix_output stream, used when fixbuf is
 * writing the output */
static FILE *ipfix_output_fp = NULL;

/* whether to print statistics */
static int print_statistics = 0;

/* whether to exclude sidecar data */
static int no_sidecar = 0;

/* whether to use a single template or many templates */
static int single_template = 0;

/* the IPFIX infomation model */
static fbInfoModel_t *model = NULL;

/* the fixbuf session */
static fbSession_t *session = NULL;

/* the fixbuf output buffer */
static fBuf_t *fbuf = NULL;


/* OPTIONS SETUP */

typedef enum {
    OPT_IPFIX_OUTPUT,
    OPT_PRINT_STATISTICS,
    OPT_NO_SIDECAR,
    OPT_SINGLE_TEMPLATE
} appOptionsEnum;

static struct option appOptions[] = {
    {"ipfix-output",            REQUIRED_ARG, 0, OPT_IPFIX_OUTPUT},
    {"print-statistics",        NO_ARG,       0, OPT_PRINT_STATISTICS},
    {"no-sidecar",              NO_ARG,       0, OPT_NO_SIDECAR},
    {"single-template",         NO_ARG,       0, OPT_SINGLE_TEMPLATE},
    {0,0,0,0}                   /* sentinel entry */
};

static const char *appHelp[] = {
    ("Write IPFIX records to the specified path. Def. stdout"),
    ("Print the count of processed records. Def. No"),
    ("Do not include sidecar data. Def. Include sidecar"),
    ("Use a single template for all IPFIX records and do\n"
     "\tnot include sidecar data. Def. Multiple templates with sidecar.\n"
     "\tThis switch creates output identical to that produced by SiLK 3.11.0\n"
     "\tand earlier."),
    (char *)NULL
};


/* LOCAL FUNCTION PROTOTYPES */

static int  appOptionsHandler(clientData cData, int opt_index, char *opt_arg);
static size_t logprefix(char *buffer, size_t bufsize);


/* FUNCTION DEFINITIONS */

/*
 *  appUsageLong();
 *
 *    Print complete usage information to USAGE_FH.  Pass this
 *    function to skOptionsSetUsageCallback(); skOptionsParse() will
 *    call this funciton and then exit the program when the --help
 *    option is given.
 */
static void
appUsageLong(
    void)
{
#define USAGE_MSG                                                             \
    ("[SWITCHES] [SILK_FILES]\n"                                              \
     "\tReads SiLK Flow records from files named on the command line or\n"    \
     "\tfrom the standard input, converts them to an IPFIX format, and\n"     \
     "\twrites the IPFIX records to the named file or the standard output.\n")

    FILE *fh = USAGE_FH;

    skAppStandardUsage(fh, USAGE_MSG, appOptions, appHelp);
    skOptionsCtxOptionsUsage(optctx, fh);
    sksiteOptionsUsage(fh);
}


/*
 *  appTeardown()
 *
 *    Teardown all modules, close all files, and tidy up all
 *    application state.
 *
 *    This function is idempotent.
 */
static void
appTeardown(
    void)
{
    static int teardownFlag = 0;
    ssize_t rv;

    if (teardownFlag) {
        return;
    }
    teardownFlag = 1;

    if (ipfix_output_fp) {
        fflush(ipfix_output_fp);
    }
    rv = skStreamClose(ipfix_output);
    if (rv && SKSTREAM_ERR_NOT_OPEN != rv) {
        skStreamPrintLastErr(ipfix_output, rv, &skAppPrintErr);
    }
    skStreamDestroy(&ipfix_output);

    if (fbuf) {
        fBufFree(fbuf);
        fbuf = NULL;
    }
    if (session) {
        fbSessionFree(session);
        session = NULL;
    }
    skipfix_information_model_destroy(model);
    model = NULL;

    /* set level to "warning" to avoid the "Stopped logging" message */
    sklogSetLevel("warning");
    sklogTeardown();

    sk_flow_iter_destroy(&flowiter);
    skOptionsCtxDestroy(&optctx);
    skAppUnregister();
}


/*
 *  appSetup(argc, argv);
 *
 *    Perform all the setup for this application include setting up
 *    required modules, parsing options, etc.  This function should be
 *    passed the same arguments that were passed into main().
 *
 *    Returns to the caller if all setup succeeds.  If anything fails,
 *    this function will cause the application to exit with a FAILURE
 *    exit status.
 */
static void
appSetup(
    int                 argc,
    char              **argv)
{
    SILK_FEATURES_DEFINE_STRUCT(features);
    unsigned int optctx_flags;
    int logmask;
    int rv;

    /* verify same number of options and help strings */
    assert((sizeof(appHelp)/sizeof(char *)) ==
           (sizeof(appOptions)/sizeof(struct option)));

    assert(sizeof(multiple_tid)/sizeof(multiple_tid[0]) == TMPL_COUNT);
    assert((sizeof(multiple_spec_flag)/sizeof(multiple_spec_flag[0]))
           == TMPL_COUNT);
    assert((sizeof(tid_to_position)/sizeof(tid_to_position.p_TID4_NOPORTS))
           == TMPL_COUNT);

    /* register the application */
    skAppRegister(argv[0]);
    skAppVerifyFeatures(&features, NULL);
    skOptionsSetUsageCallback(&appUsageLong);

    optctx_flags = (SK_OPTIONS_CTX_INPUT_SILK_FLOW | SK_OPTIONS_CTX_ALLOW_STDIN
                    | SK_OPTIONS_CTX_XARGS);

    /* register the options */
    if (skOptionsCtxCreate(&optctx, optctx_flags)
        || skOptionsCtxOptionsRegister(optctx)
        || skOptionsRegister(appOptions, &appOptionsHandler, NULL)
        || sksiteOptionsRegister(SK_SITE_FLAG_CONFIG_FILE))
    {
        skAppPrintErr("Unable to register options");
        exit(EXIT_FAILURE);
    }

    /* enable the logger */
    sklogSetup(0);
    sklogSetStampFunction(&logprefix);
    sklogSetDestination(LOG_DESTINATION_DEFAULT);

    /* create the output stream */
    if (skStreamCreate(&ipfix_output, SK_IO_WRITE, SK_CONTENT_OTHERBINARY)) {
        skAppPrintErr("Unable to create output stream");
        exit(EXIT_FAILURE);
    }

    /* register the teardown handler */
    if (atexit(appTeardown) < 0) {
        skAppPrintErr("Unable to register appTeardown() with atexit()");
        appTeardown();
        exit(EXIT_FAILURE);
    }

    /* parse the options */
    rv = skOptionsCtxOptionsParse(optctx, argc, argv);
    if (rv < 0) {
        skAppUsage();           /* never returns */
    }

    /* create flow iterator to read the records from the stream */
    flowiter = skOptionsCtxCreateFlowIterator(optctx);

    /* initialize ipfix */
    skipfix_initialize(0);

    /* try to load site config file; if it fails, we will not be able
     * to resolve flowtype and sensor from input file names */
    sksiteConfigure(0);

    /* set level to "warning" to avoid the "Started logging" message */
    logmask = sklogGetMask();
    sklogSetLevel("warning");
    sklogOpen();
    sklogSetMask(logmask);

    /* open the provided output file or use stdout */
    if (NULL == skStreamGetPathname(ipfix_output)) {
        rv = skStreamBind(ipfix_output, "-");
        if (rv) {
            skStreamPrintLastErr(ipfix_output, rv, &skAppPrintErr);
            exit(EXIT_FAILURE);
        }
    }
    rv = skStreamOpen(ipfix_output);
    if (rv) {
        skStreamPrintLastErr(ipfix_output, rv, &skAppPrintErr);
        exit(EXIT_FAILURE);
    }

    return;  /* OK */
}


/*
 *  status = appOptionsHandler(cData, opt_index, opt_arg);
 *
 *    This function is passed to skOptionsRegister(); it will be called
 *    by skOptionsParse() for each user-specified switch that the
 *    application has registered; it should handle the switch as
 *    required---typically by setting global variables---and return 1
 *    if the switch processing failed or 0 if it succeeded.  Returning
 *    a non-zero from from the handler causes skOptionsParse() to return
 *    a negative value.
 *
 *    The clientData in 'cData' is typically ignored; 'opt_index' is
 *    the index number that was specified as the last value for each
 *    struct option in appOptions[]; 'opt_arg' is the user's argument
 *    to the switch for options that have a REQUIRED_ARG or an
 *    OPTIONAL_ARG.
 */
static int
appOptionsHandler(
    clientData   UNUSED(cData),
    int                 opt_index,
    char               *opt_arg)
{
    char errbuf[2 * PATH_MAX];
    ssize_t rv;

    switch ((appOptionsEnum)opt_index) {
      case OPT_IPFIX_OUTPUT:
        if (skStreamGetPathname(ipfix_output)) {
            skAppPrintErr("Invalid %s: Switch used multiple times",
                          appOptions[opt_index].name);
            return 1;
        }
        rv = skStreamBind(ipfix_output, opt_arg);
        if (rv) {
            skStreamLastErrMessage(ipfix_output, rv, errbuf, sizeof(errbuf));
            skAppPrintErr("Invalid %s '%s': %s",
                          appOptions[opt_index].name, optarg, errbuf);
            return 1;
        }
        break;

      case OPT_PRINT_STATISTICS:
        print_statistics = 1;
        break;

      case OPT_NO_SIDECAR:
        no_sidecar = 1;
        break;

      case OPT_SINGLE_TEMPLATE:
        single_template = 1;
        break;
    }

    return 0;  /* OK */
}


/*
 *    Prefix any log messages from libflowsource with the program name
 *    instead of the standard logging tag.
 */
static size_t
logprefix(
    char               *buffer,
    size_t              bufsize)
{
    return (size_t)snprintf(buffer, bufsize, "%s: ", skAppName());
}


/*
 *    Set the global variable 'ipfix_output_fp' to a FILE*
 *    representation of the global skstream 'ipfix_output'.  Exit the
 *    application on error.
 */
static void
make_fileptr_from_stream(
    void)
{
    int fd;

    fd = skStreamGetDescriptor(ipfix_output);
    assert(-1 != fd);

    ipfix_output_fp = fdopen(fd, "wb");
    if (NULL == ipfix_output_fp) {
        skAppPrintSyserror("Unable to open stream for use by fixbuf");
        exit(EXIT_FAILURE);
    }
}


/*
 *    Read SiLK flow records and write IPFIX records using a single
 *    IPFIX template for all records and do not include sidecar data.
 *
 *    Creates output compatible with SiLK 3.11.0 and older.
 */
static int
toipfix_one_template(
    void)
{
    /* Map each rwRec into this structure, which matches the template
     * below. Ensure it is padded to 64bits */
    struct fixrec_st {
        uint64_t            flowStartMilliseconds;      /*   0-  7 */
        uint64_t            flowEndMilliseconds;        /*   8- 15 */

        uint8_t             sourceIPv6Address[16];      /*  16- 31 */
        uint8_t             destinationIPv6Address[16]; /*  32- 47 */

        uint32_t            sourceIPv4Address;          /*  48- 51 */
        uint32_t            destinationIPv4Address;     /*  52- 55 */

        uint16_t            sourceTransportPort;        /*  56- 57 */
        uint16_t            destinationTransportPort;   /*  58- 59 */

        uint32_t            ipNextHopIPv4Address;       /*  60- 63 */
        uint8_t             ipNextHopIPv6Address[16];   /*  64- 79 */
        uint32_t            ingressInterface;           /*  80- 83 */
        uint32_t            egressInterface;            /*  84- 87 */

        uint64_t            packetDeltaCount;           /*  88- 95 */
        uint64_t            octetDeltaCount;            /*  96-103 */

        uint8_t             protocolIdentifier;         /* 104     */
        uint8_t             silkFlowType;               /* 105     */
        uint16_t            silkFlowSensor;             /* 106-107 */

        uint8_t             tcpControlBits;             /* 108     */
        uint8_t             initialTCPFlags;            /* 109     */
        uint8_t             unionTCPFlags;              /* 110     */
        uint8_t             silkTCPState;               /* 111     */
        uint16_t            silkAppLabel;               /* 112-113 */
        uint8_t             pad[6];                     /* 114-119 */
    } fixrec;

    /* The elements of the template to write. This must be in sync
     * with the structure above. */
    fbInfoElementSpec_t fixrec_spec[] = {
        /* Millisecond start and end (epoch) (native time) */
        { (char*)"flowStartMilliseconds",              8, 0 },
        { (char*)"flowEndMilliseconds",                8, 0 },
        /* 4-tuple */
        { (char*)"sourceIPv6Address",                 16, 0 },
        { (char*)"destinationIPv6Address",            16, 0 },
        { (char*)"sourceIPv4Address",                  4, 0 },
        { (char*)"destinationIPv4Address",             4, 0 },
        { (char*)"sourceTransportPort",                2, 0 },
        { (char*)"destinationTransportPort",           2, 0 },
        /* Router interface information */
        { (char*)"ipNextHopIPv4Address",               4, 0 },
        { (char*)"ipNextHopIPv6Address",              16, 0 },
        { (char*)"ingressInterface",                   4, 0 },
        { (char*)"egressInterface",                    4, 0 },
        /* Counters (reduced length encoding for SiLK) */
        { (char*)"packetDeltaCount",                   8, 0 },
        { (char*)"octetDeltaCount",                    8, 0 },
        /* Protocol; sensor information */
        { (char*)"protocolIdentifier",                 1, 0 },
        { (char*)"silkFlowType",                       1, 0 },
        { (char*)"silkFlowSensor",                     2, 0 },
        /* Flags */
        { (char*)"tcpControlBits",                     1, 0 },
        { (char*)"initialTCPFlags",                    1, 0 },
        { (char*)"unionTCPFlags",                      1, 0 },
        { (char*)"silkTCPState",                       1, 0 },
        { (char*)"silkAppLabel",                       2, 0 },
        /* pad record to 64-bit boundary */
        { (char*)"paddingOctets",                      6, 0 },
        FB_IESPEC_NULL
    };

    const uint16_t tid = SKI_RWREC_TID;
    fbTemplate_t *tmpl = NULL;
    GError *err = NULL;
    rwRec rwrec;
    uint64_t rec_count;

    memset(&fixrec, 0, sizeof(fixrec));

    /* Create the template and add the spec */
    tmpl = fbTemplateAlloc(model);
    if (!fbTemplateAppendSpecArray(tmpl, fixrec_spec, 0, &err)) {
        skAppPrintErr("Could not create template: %s", err->message);
        g_clear_error(&err);
        fbTemplateFreeUnused(tmpl);
        return EXIT_FAILURE;
    }

    /* Add the template to the session */
    if (!fbSessionAddTemplate(session, TRUE, tid, tmpl, &err)) {
        skAppPrintErr("Could not add template to session: %s", err->message);
        g_clear_error(&err);
        fbTemplateFreeUnused(tmpl);
        return EXIT_FAILURE;
    }
    if (!fbSessionAddTemplate(session, FALSE, tid, tmpl, &err)) {
        skAppPrintErr("Could not add template to session: %s", err->message);
        g_clear_error(&err);
        return EXIT_FAILURE;
    }

    /* Get a FILE* from the stream */
    make_fileptr_from_stream();

    /* Create the output buffer with the session and an exporter
     * created from the file pointer */
    fbuf = fBufAllocForExport(session, fbExporterAllocFP(ipfix_output_fp));
    /* The fbuf now owns the session */
    session = NULL;

    /* Write the template */
    if (!fbSessionExportTemplates(fBufGetSession(fbuf), &err)) {
        skAppPrintErr("Could not add export templates: %s", err->message);
        g_clear_error(&err);
        return EXIT_FAILURE;
    }

    /* Set default template for the buffer */
    if (!fBufSetInternalTemplate(fbuf, tid, &err)) {
        skAppPrintErr("Could not set internal template: %s", err->message);
        g_clear_error(&err);
        return EXIT_FAILURE;
    }
    if (!fBufSetExportTemplate(fbuf, tid, &err)) {
        skAppPrintErr("Could not set external template: %s", err->message);
        g_clear_error(&err);
        return EXIT_FAILURE;
    }

    rwRecInitialize(&rwrec, NULL);
    rec_count = 0;

    /* For each input, process each record */
    while (sk_flow_iter_get_next_rec(flowiter, &rwrec) == 0) {
        /* Convert times */
        fixrec.flowStartMilliseconds = (uint64_t)rwRecGetStartTime(&rwrec);
        fixrec.flowEndMilliseconds = (uint64_t)rwRecGetEndTime(&rwrec);

        /* Handle IP addresses */
        if (rwRecIsIPv6(&rwrec)) {
            rwRecMemGetSIPv6(&rwrec, fixrec.sourceIPv6Address);
            rwRecMemGetDIPv6(&rwrec, fixrec.destinationIPv6Address);
            rwRecMemGetNhIPv6(&rwrec, fixrec.ipNextHopIPv6Address);
            fixrec.sourceIPv4Address = 0;
            fixrec.destinationIPv4Address = 0;
            fixrec.ipNextHopIPv4Address = 0;
        } else {
            memset(fixrec.sourceIPv6Address, 0,
                   sizeof(fixrec.sourceIPv6Address));
            memset(fixrec.destinationIPv6Address, 0,
                   sizeof(fixrec.destinationIPv6Address));
            memset(fixrec.ipNextHopIPv6Address, 0,
                   sizeof(fixrec.ipNextHopIPv6Address));
            fixrec.sourceIPv4Address = rwRecGetSIPv4(&rwrec);
            fixrec.destinationIPv4Address = rwRecGetDIPv4(&rwrec);
            fixrec.ipNextHopIPv4Address = rwRecGetNhIPv4(&rwrec);
        }

        /* Copy rest of record */
        fixrec.sourceTransportPort = rwRecGetSPort(&rwrec);
        fixrec.destinationTransportPort = rwRecGetDPort(&rwrec);
        fixrec.ingressInterface = rwRecGetInput(&rwrec);
        fixrec.egressInterface = rwRecGetOutput(&rwrec);
        fixrec.packetDeltaCount = rwRecGetPkts(&rwrec);
        fixrec.octetDeltaCount = rwRecGetBytes(&rwrec);
        fixrec.protocolIdentifier = rwRecGetProto(&rwrec);
        fixrec.silkFlowType = rwRecGetFlowType(&rwrec);
        fixrec.silkFlowSensor = rwRecGetSensor(&rwrec);
        fixrec.tcpControlBits = rwRecGetFlags(&rwrec);
        fixrec.initialTCPFlags = rwRecGetInitFlags(&rwrec);
        fixrec.unionTCPFlags = rwRecGetRestFlags(&rwrec);
        fixrec.silkTCPState = rwRecGetTcpState(&rwrec);
        fixrec.silkAppLabel = rwRecGetApplication(&rwrec);

        /* Append the record to the buffer */
        if (fBufAppend(fbuf, (uint8_t *)&fixrec, sizeof(fixrec), &err)) {
            /* processed record */
            ++rec_count;
        } else {
            skAppPrintErr("Could not write IPFIX record: %s",
                          err->message);
            g_clear_error(&err);
        }
    }

    /* finalize the output */
    if (!fBufEmit(fbuf, &err)) {
        skAppPrintErr("Could not write final IPFIX message: %s",
                      err->message);
        g_clear_error(&err);
        fbExporterClose(fBufGetExporter(fbuf));
        return EXIT_FAILURE;
    }
    fbExporterClose(fBufGetExporter(fbuf));

    fBufFree(fbuf);
    fbuf = NULL;

    /* print record count */
    if (print_statistics) {
        fprintf(STATS_FH, ("%s: Wrote %" PRIu64 " IPFIX records to '%s'\n"),
                skAppName(), rec_count, skStreamGetPathname(ipfix_output));
    }

    return 0;
}


/*
 *    Read SiLK flow records and write IPFIX records using multiple
 *    IPFIX templates depending on what each SiLK flow record
 *    contains, but do not include sidecar data.
 *
 *    Creates output compatible with SiLK 3.12.0 and newer.
 */
static int
toipfix_multiple_templates(
    void)
{
    /* a union of the structures defined at the top of this file */
    union fixrec_un {
        rec_prelim_t     pre;
        rec_noports_v6_t rec6_noports;
        rec_icmp_v6_t    rec6_icmp;
        rec_udp_v6_t     rec6_udp;
        rec_tcp_v6_t     rec6_tcp;
        rec_tcp_exp_v6_t rec6_tcp_exp;
        rec_noports_v4_t rec4_noports;
        rec_icmp_v4_t    rec4_icmp;
        rec_udp_v4_t     rec4_udp;
        rec_tcp_v4_t     rec4_tcp;
        rec_tcp_exp_v4_t rec4_tcp_exp;
    } fixrec;

    fbTemplate_t *tmpl[TMPL_COUNT];
    GError *err = NULL;
    rwRec rwrec;
    uint64_t rec_count;
    unsigned int i;

    /* Create each template, add the spec to the template, and add the
     * template to the session */
    for (i = 0; i < TMPL_COUNT; ++i) {
        tmpl[i] = fbTemplateAlloc(model);
        if (!fbTemplateAppendSpecArray(
                tmpl[i], multiple_spec, multiple_spec_flag[i], &err))
        {
            skAppPrintErr("Could not create template: %s", err->message);
            g_clear_error(&err);
            fbTemplateFreeUnused(tmpl[i]);
            return EXIT_FAILURE;
        }

        /* Add the template to the session */
        if (!fbSessionAddTemplate(
                session, TRUE, multiple_tid[i], tmpl[i], &err))
        {
            skAppPrintErr("Could not add template to session: %s",
                          err->message);
            g_clear_error(&err);
            fbTemplateFreeUnused(tmpl[i]);
            return EXIT_FAILURE;
        }
        if (!fbSessionAddTemplate(
                session, FALSE, multiple_tid[i], tmpl[i], &err))
        {
            skAppPrintErr("Could not add template to session: %s",
                          err->message);
            g_clear_error(&err);
            return EXIT_FAILURE;
        }
    }

    /* Get a FILE* from the stream */
    make_fileptr_from_stream();

    /* Create the output buffer with the session and an exporter
     * created from the file pointer */
    fbuf = fBufAllocForExport(session, fbExporterAllocFP(ipfix_output_fp));
    /* The fbuf now owns the session */
    session = NULL;

    /* Write the templates */
    if (!fbSessionExportTemplates(fBufGetSession(fbuf), &err)) {
        skAppPrintErr("Could not add export templates: %s", err->message);
        g_clear_error(&err);
        return EXIT_FAILURE;
    }

    rwRecInitialize(&rwrec, NULL);
    rec_count = 0;

    /* For each input, process each record */
    while (sk_flow_iter_get_next_rec(flowiter, &rwrec) == 0) {
        /* process record */
        memset(&fixrec, 0, sizeof(fixrec));
        /* handle fields that are the same for all */
        fixrec.pre.stime = (uint64_t)rwRecGetStartTime(&rwrec);
        fixrec.pre.etime = (uint64_t)rwRecGetEndTime(&rwrec);
        fixrec.pre.packets = rwRecGetPkts(&rwrec);
        fixrec.pre.bytes = rwRecGetBytes(&rwrec);
        fixrec.pre.ingress = rwRecGetInput(&rwrec);
        fixrec.pre.egress = rwRecGetOutput(&rwrec);
        fixrec.pre.application = rwRecGetApplication(&rwrec);
        fixrec.pre.sensor = rwRecGetSensor(&rwrec);

        if (rwRecIsIPv6(&rwrec)) {
            switch (rwRecGetProto(&rwrec)) {
              case IPPROTO_ICMP:
              case IPPROTO_ICMPV6:
                i = tid_to_position.p_TID6_ICMP;
                fixrec.rec6_icmp.flowtype = rwRecGetFlowType(&rwrec);
                fixrec.rec6_icmp.attributes = rwRecGetTcpState(&rwrec);
                fixrec.rec6_icmp.protocol = rwRecGetProto(&rwrec);
                fixrec.rec6_icmp.icmptypecode = rwRecGetDPort(&rwrec);
                rwRecMemGetSIPv6(&rwrec, fixrec.rec6_icmp.sip);
                rwRecMemGetDIPv6(&rwrec, fixrec.rec6_icmp.dip);
                rwRecMemGetNhIPv6(&rwrec, fixrec.rec6_icmp.nhip);
                break;

              case IPPROTO_UDP:
              case IPPROTO_SCTP:
                i = tid_to_position.p_TID6_UDP;
                fixrec.rec6_udp.flowtype = rwRecGetFlowType(&rwrec);
                fixrec.rec6_udp.attributes = rwRecGetTcpState(&rwrec);
                fixrec.rec6_udp.protocol = rwRecGetProto(&rwrec);
                fixrec.rec6_udp.sport = rwRecGetSPort(&rwrec);
                fixrec.rec6_udp.dport = rwRecGetDPort(&rwrec);
                rwRecMemGetSIPv6(&rwrec, fixrec.rec6_udp.sip);
                rwRecMemGetDIPv6(&rwrec, fixrec.rec6_udp.dip);
                rwRecMemGetNhIPv6(&rwrec, fixrec.rec6_udp.nhip);
                break;

              case IPPROTO_TCP:
                if (rwRecGetTcpState(&rwrec) & SK_TCPSTATE_EXPANDED) {
                    i = tid_to_position.p_TID6_TCP_EXP;
                    fixrec.rec6_tcp_exp.flowtype = rwRecGetFlowType(&rwrec);
                    fixrec.rec6_tcp_exp.attributes=rwRecGetTcpState(&rwrec);
                    fixrec.rec6_tcp_exp.protocol = rwRecGetProto(&rwrec);
                    fixrec.rec6_tcp_exp.sport = rwRecGetSPort(&rwrec);
                    fixrec.rec6_tcp_exp.dport = rwRecGetDPort(&rwrec);
                    fixrec.rec6_tcp_exp.flags_all = rwRecGetFlags(&rwrec);
                    fixrec.rec6_tcp_exp.flags_init
                        = rwRecGetInitFlags(&rwrec);
                    fixrec.rec6_tcp_exp.flags_rest
                        = rwRecGetRestFlags(&rwrec);
                    rwRecMemGetSIPv6(&rwrec, fixrec.rec6_tcp_exp.sip);
                    rwRecMemGetDIPv6(&rwrec, fixrec.rec6_tcp_exp.dip);
                    rwRecMemGetNhIPv6(&rwrec, fixrec.rec6_tcp_exp.nhip);
                } else {
                    i = tid_to_position.p_TID6_TCP;
                    fixrec.rec6_tcp.flowtype = rwRecGetFlowType(&rwrec);
                    fixrec.rec6_tcp.attributes = rwRecGetTcpState(&rwrec);
                    fixrec.rec6_tcp.protocol = rwRecGetProto(&rwrec);
                    fixrec.rec6_tcp.flags_all = rwRecGetFlags(&rwrec);
                    fixrec.rec6_tcp.sport = rwRecGetSPort(&rwrec);
                    fixrec.rec6_tcp.dport = rwRecGetDPort(&rwrec);
                    rwRecMemGetSIPv6(&rwrec, fixrec.rec6_tcp.sip);
                    rwRecMemGetDIPv6(&rwrec, fixrec.rec6_tcp.dip);
                    rwRecMemGetNhIPv6(&rwrec, fixrec.rec6_tcp.nhip);
                }
                break;

              default:
                i = tid_to_position.p_TID6_NOPORTS;
                fixrec.rec6_noports.flowtype = rwRecGetFlowType(&rwrec);
                fixrec.rec6_noports.attributes = rwRecGetTcpState(&rwrec);
                fixrec.rec6_noports.protocol = rwRecGetProto(&rwrec);
                rwRecMemGetSIPv6(&rwrec, fixrec.rec6_noports.sip);
                rwRecMemGetDIPv6(&rwrec, fixrec.rec6_noports.dip);
                rwRecMemGetNhIPv6(&rwrec, fixrec.rec6_noports.nhip);
                break;
            }
        } else {
            switch (rwRecGetProto(&rwrec)) {
              case IPPROTO_ICMP:
              case IPPROTO_ICMPV6:
                i = tid_to_position.p_TID4_ICMP;
                fixrec.rec4_icmp.flowtype = rwRecGetFlowType(&rwrec);
                fixrec.rec4_icmp.attributes = rwRecGetTcpState(&rwrec);
                fixrec.rec4_icmp.protocol = rwRecGetProto(&rwrec);
                fixrec.rec4_icmp.icmptypecode = rwRecGetDPort(&rwrec);
                rwRecMemGetSIPv4(&rwrec, &fixrec.rec4_icmp.sip);
                rwRecMemGetDIPv4(&rwrec, &fixrec.rec4_icmp.dip);
                rwRecMemGetNhIPv4(&rwrec, &fixrec.rec4_icmp.nhip);
                break;

              case IPPROTO_UDP:
              case IPPROTO_SCTP:
                i = tid_to_position.p_TID4_UDP;
                fixrec.rec4_udp.flowtype = rwRecGetFlowType(&rwrec);
                fixrec.rec4_udp.attributes = rwRecGetTcpState(&rwrec);
                fixrec.rec4_udp.protocol = rwRecGetProto(&rwrec);
                fixrec.rec4_udp.sport = rwRecGetSPort(&rwrec);
                fixrec.rec4_udp.dport = rwRecGetDPort(&rwrec);
                rwRecMemGetSIPv4(&rwrec, &fixrec.rec4_udp.sip);
                rwRecMemGetDIPv4(&rwrec, &fixrec.rec4_udp.dip);
                rwRecMemGetNhIPv4(&rwrec, &fixrec.rec4_udp.nhip);
                break;

              case IPPROTO_TCP:
                if (rwRecGetTcpState(&rwrec) & SK_TCPSTATE_EXPANDED) {
                    i = tid_to_position.p_TID4_TCP_EXP;
                    fixrec.rec4_tcp_exp.flowtype = rwRecGetFlowType(&rwrec);
                    fixrec.rec4_tcp_exp.attributes=rwRecGetTcpState(&rwrec);
                    fixrec.rec4_tcp_exp.protocol = rwRecGetProto(&rwrec);
                    fixrec.rec4_tcp_exp.sport = rwRecGetSPort(&rwrec);
                    fixrec.rec4_tcp_exp.dport = rwRecGetDPort(&rwrec);
                    fixrec.rec4_tcp_exp.flags_all = rwRecGetFlags(&rwrec);
                    fixrec.rec4_tcp_exp.flags_init
                        = rwRecGetInitFlags(&rwrec);
                    fixrec.rec4_tcp_exp.flags_rest
                        = rwRecGetRestFlags(&rwrec);
                    rwRecMemGetSIPv4(&rwrec, &fixrec.rec4_tcp_exp.sip);
                    rwRecMemGetDIPv4(&rwrec, &fixrec.rec4_tcp_exp.dip);
                    rwRecMemGetNhIPv4(&rwrec, &fixrec.rec4_tcp_exp.nhip);
                } else {
                    i = tid_to_position.p_TID4_TCP;
                    fixrec.rec4_tcp.flowtype = rwRecGetFlowType(&rwrec);
                    fixrec.rec4_tcp.attributes = rwRecGetTcpState(&rwrec);
                    fixrec.rec4_tcp.protocol = rwRecGetProto(&rwrec);
                    fixrec.rec4_tcp.flags_all = rwRecGetFlags(&rwrec);
                    fixrec.rec4_tcp.sport = rwRecGetSPort(&rwrec);
                    fixrec.rec4_tcp.dport = rwRecGetDPort(&rwrec);
                    rwRecMemGetSIPv4(&rwrec, &fixrec.rec4_tcp.sip);
                    rwRecMemGetDIPv4(&rwrec, &fixrec.rec4_tcp.dip);
                    rwRecMemGetNhIPv4(&rwrec, &fixrec.rec4_tcp.nhip);
                }
                break;

              default:
                i = tid_to_position.p_TID4_NOPORTS;
                fixrec.rec4_noports.flowtype = rwRecGetFlowType(&rwrec);
                fixrec.rec4_noports.attributes = rwRecGetTcpState(&rwrec);
                fixrec.rec4_noports.protocol = rwRecGetProto(&rwrec);
                rwRecMemGetSIPv4(&rwrec, &fixrec.rec4_noports.sip);
                rwRecMemGetDIPv4(&rwrec, &fixrec.rec4_noports.dip);
                rwRecMemGetNhIPv4(&rwrec, &fixrec.rec4_noports.nhip);
                break;
            }
        }

        /* Set the template */
        if (!fBufSetInternalTemplate(fbuf, multiple_tid[i], &err)) {
            skAppPrintErr("Could not set internal template: %s",
                          err->message);
            g_clear_error(&err);
            return EXIT_FAILURE;
        }
        if (!fBufSetExportTemplate(fbuf, multiple_tid[i], &err)) {
            skAppPrintErr("Could not set external template: %s",
                          err->message);
            g_clear_error(&err);
            return EXIT_FAILURE;
        }

        /* Append the record to the buffer */
        if (fBufAppend(fbuf, (uint8_t *)&fixrec, sizeof(fixrec), &err)) {
            /* processed record */
            ++rec_count;
        } else {
            skAppPrintErr("Could not write IPFIX record: %s",
                          err->message);
            g_clear_error(&err);
        }
    }

    /* finalize the output */
    if (!fBufEmit(fbuf, &err)) {
        skAppPrintErr("Could not write final IPFIX message: %s",
                      err->message);
        g_clear_error(&err);
        fbExporterClose(fBufGetExporter(fbuf));
        return EXIT_FAILURE;
    }
    fbExporterClose(fBufGetExporter(fbuf));

    fBufFree(fbuf);
    fbuf = NULL;

    /* print record count */
    if (print_statistics) {
        fprintf(STATS_FH, ("%s: Wrote %" PRIu64 " IPFIX records to '%s'\n"),
                skAppName(), rec_count, skStreamGetPathname(ipfix_output));
    }

    return 0;
}


/*
 *    Read SiLK flow records and write IPFIX records using multiple
 *    IPFIX templates depending on what each SiLK flow record
 *    contains and include the sidecar data with each record.
 */
static int
toipfix_with_sidecar(
    void)
{
    sk_vector_t *field_list[TMPL_COUNT];
    sk_vector_t *v;
    sk_fixrec_t fixrec[TMPL_COUNT];
    sk_fixrec_t *r;
    const sk_sidecar_elem_t *sc_elem;
    sk_fixstream_t *fixstream;
    sk_sidecar_t *sidecar;
    sk_sidecar_iter_t sc_iter;
    const sk_field_t *f;
    sk_schema_err_t err;
    sk_fixlist_t *list;
    sk_schema_t *schema;
    sk_field_t *field;
    sk_field_ident_t id;
    sk_fixrec_t record;
    lua_Integer items;
    lua_Integer k;
    uint64_t rec_count;
    unsigned int i;
    skipaddr_t ipaddr;
    char buf[PATH_MAX];
    size_t buflen;
    int64_t sc_idx;
    int have_sidecar;
    lua_State *L;
    rwRec rwrec;
    ssize_t rv;
    int retval;
    size_t j;

    retval = EXIT_FAILURE;

    sk_sidecar_create(&sidecar);

    /* Get the sidecar data descriptions from all input files */
    if (sk_flow_iter_fill_sidecar(flowiter, sidecar)) {
        skAppPrintErr("Error reading file header");
        return retval;
    }

    /* When there is no sidecar data in any input files, use the
     * simpler multiple-template code */
    if (0 == sk_sidecar_count_elements(sidecar)) {
        sk_sidecar_destroy(&sidecar);
        return toipfix_multiple_templates();
    }

    /*
     *    Currently all sidecar fields are appended to all IPFIX
     *    records that this application writes.  Thus, if the input
     *    contains some DNS records and some SSL records, each output
     *    record will have both DNS and SSL fields attached.
     *
     *    A better solution would be to create many output schemas
     *    where each schema only contains the IEs that were present on
     *    the each individual record.
     *
     *    We could also go the YAF route and have a single
     *    subTemplateMultiList on each record with whatever data was
     *    found for that record in the STML.
     *
     *    Finally, there ought to be a way for the user to write Lua
     *    code that affects how the sidecar fields are mapped to IPFIX
     *    records.
     */

    memset(field_list, 0, sizeof(field_list));
    memset(fixrec, 0, sizeof(fixrec));
    fixstream = NULL;
    L = NULL;

    /* Create each schema: use the spec array to add elements to the
     * schema template, and add sidecar fields to each schema */
    for (i = 0; i < TMPL_COUNT; ++i) {
        /* a vector maintains pointers to each field on each record */
        v = field_list[i] = sk_vector_create(sizeof(sk_field_t *));

        schema = NULL;
        err = sk_schema_create(&schema, model, multiple_spec,
                               multiple_spec_flag[i]);
        if (err) {
            skAppPrintErr("Unable to create schema: %s",
                          sk_schema_strerror(err));
            goto END;
        }
        for (j = 0; (f = sk_schema_get_field(schema, j)) != NULL; ++j) {
            if (sk_field_get_ident(f) != PADDING_IE) {
                sk_vector_append_value(v, &f);
            }
        }

        sk_sidecar_iter_bind(sidecar, &sc_iter);
        while (sk_sidecar_iter_next(&sc_iter, &sc_elem) == SK_ITERATOR_OK) {
            /* FIXME: Who is responsible for handling alignment of
             * these items in the IPFIX record? */

            id = sk_sidecar_elem_get_ipfix_ident(sc_elem);
            buflen = sizeof(buf);
            sk_sidecar_elem_get_name(sc_elem, buf, &buflen);

            if (sk_sidecar_elem_get_data_type(sc_elem) != SK_SIDECAR_LIST) {
                if (((0 == id)
                     || ((err = (sk_schema_insert_field_by_ident(
                                     &field, schema, id, NULL, NULL))) != 0))
                    && ((err = (sk_schema_insert_field_by_name(
                                    &field, schema, buf, NULL, NULL))) != 0))
                {
                    field = NULL;
                    if (0 == i) {
                        /* only report unsupported fields the first time */
                        skAppPrintErr(
                            "Unable to add sidecar element %s to schema: %s",
                            buf, sk_schema_strerror(err));
                    }
                }
            } else {
                /* verify that ident or name map to a known IE */
                if (((0 == id)
                     || (NULL == (fbInfoModelGetElementByID(
                                      model, SK_FIELD_IDENT_GET_ID(id),
                                      SK_FIELD_IDENT_GET_PEN(id)))))
                    && (NULL == (fbInfoModelGetElementByName(model, buf))))
                {
                    /* not a known ident or name */
                    field = NULL;
                } else {
                    err = (sk_schema_insert_field_by_ident(
                               &field, schema, BASICLIST_IE, NULL, NULL));
                    if (err) {
                        field = NULL;
                        if (0 == i) {
                            skAppPrintErr("Unable to add basicList"
                                          " element to schema: %s",
                                          sk_schema_strerror(err));
                        }
                    }
                }
            }
            sk_vector_append_value(v, &field);
        }

        err = sk_schema_freeze(schema);
        if (err) {
            skAppPrintErr("Unable to freeze schema: %s",
                          sk_schema_strerror(err));
            goto END;
        }
        err = sk_fixrec_init(&fixrec[i], schema);
        if (err) {
            skAppPrintErr("Unable to initialize record with schema: %s",
                          sk_schema_strerror(err));
            goto END;
        }
        /* destroy schema; fixrec owns it now */
        sk_schema_destroy(schema);
    }

    /* create the IPFIX output fixstream by wrapping the stream */
    if ((rv = sk_fixstream_create(&fixstream))
        || (rv = sk_fixstream_set_info_model(fixstream, model))
        || (rv = sk_fixstream_set_stream(fixstream, ipfix_output))
        || (rv = sk_fixstream_open(fixstream)))
    {
        skAppPrintErr("Unable to create IPFIX output stream: %s",
                      sk_fixstream_strerror(fixstream));
        goto END;
    }

    L = sk_lua_newstate();
    rwRecInitialize(&rwrec, L);
    rec_count = 0;

    /* For each input, process each record */
    while (sk_flow_iter_get_next_rec(flowiter, &rwrec) == 0) {
        /* determine the index into the fixrec[] and field_list[]
         * arrays */
        if (rwRecIsIPv6(&rwrec)) {
            switch (rwRecGetProto(&rwrec)) {
              case IPPROTO_ICMP:
              case IPPROTO_ICMPV6:
                i = tid_to_position.p_TID6_ICMP;
                break;
              case IPPROTO_UDP:
              case IPPROTO_SCTP:
                i = tid_to_position.p_TID6_UDP;
                break;
              case IPPROTO_TCP:
                if (rwRecGetTcpState(&rwrec) & SK_TCPSTATE_EXPANDED) {
                    i = tid_to_position.p_TID6_TCP_EXP;
                } else {
                    i = tid_to_position.p_TID6_TCP;
                }
                break;
              default:
                i = tid_to_position.p_TID6_NOPORTS;
                break;
            }
        } else {
            switch (rwRecGetProto(&rwrec)) {
              case IPPROTO_ICMP:
              case IPPROTO_ICMPV6:
                i = tid_to_position.p_TID4_ICMP;
                break;
              case IPPROTO_UDP:
              case IPPROTO_SCTP:
                i = tid_to_position.p_TID4_UDP;
                break;
              case IPPROTO_TCP:
                if (rwRecGetTcpState(&rwrec) & SK_TCPSTATE_EXPANDED) {
                    i = tid_to_position.p_TID4_TCP_EXP;
                } else {
                    i = tid_to_position.p_TID4_TCP;
                }
                break;
              default:
                i = tid_to_position.p_TID4_NOPORTS;
                break;
            }
        }
        v = field_list[i];
        r = &fixrec[i];
        sk_fixrec_clear(r);
        j = 0;

        /* handle fields that are the same for all */
        sk_vector_get_value(v, j++, &f);  /* flowStartMilliseconds */
        sk_fixrec_set_datetime(r, f, rwRecGetStartTime(&rwrec));
        sk_vector_get_value(v, j++, &f);  /* flowEndMilliseconds */
        sk_fixrec_set_datetime(r, f, rwRecGetEndTime(&rwrec));
        sk_vector_get_value(v, j++, &f);  /* packetDeltaCount */
        sk_fixrec_set_unsigned(r, f, rwRecGetPkts(&rwrec));
        sk_vector_get_value(v, j++, &f);  /* octetDeltaCount */
        sk_fixrec_set_unsigned(r, f, rwRecGetBytes(&rwrec));
        sk_vector_get_value(v, j++, &f);  /* ingressInterface */
        sk_fixrec_set_unsigned(r, f, rwRecGetInput(&rwrec));
        sk_vector_get_value(v, j++, &f);  /* egressInterface */
        sk_fixrec_set_unsigned(r, f, rwRecGetOutput(&rwrec));
        sk_vector_get_value(v, j++, &f);  /* silkAppLabel */
        sk_fixrec_set_unsigned(r, f, rwRecGetApplication(&rwrec));
        sk_vector_get_value(v, j++, &f);  /* silkFlowSensor */
        sk_fixrec_set_unsigned(r, f, rwRecGetSensor(&rwrec));
        sk_vector_get_value(v, j++, &f);  /* silkFlowType */
        sk_fixrec_set_unsigned(r, f, rwRecGetFlowType(&rwrec));
        sk_vector_get_value(v, j++, &f);  /* silkTCPState */
        sk_fixrec_set_unsigned(r, f, rwRecGetTcpState(&rwrec));
        sk_vector_get_value(v, j++, &f);  /* protocolIdentifier */
        sk_fixrec_set_unsigned(r, f, rwRecGetProto(&rwrec));

        /* handle protocol-specific fields */
        switch (rwRecGetProto(&rwrec)) {
          case IPPROTO_ICMP:
          case IPPROTO_ICMPV6:
            sk_vector_get_value(v, j++, &f);  /* icmpTypeCodeIPv6 or IPv4 */
            sk_fixrec_set_unsigned(r, f, rwRecGetDPort(&rwrec));
            break;
          case IPPROTO_UDP:
          case IPPROTO_SCTP:
            sk_vector_get_value(v, j++, &f);  /* sourceTransportPort */
            sk_fixrec_set_unsigned(r, f, rwRecGetSPort(&rwrec));
            sk_vector_get_value(v, j++, &f);  /* destinationTransportPort */
            sk_fixrec_set_unsigned(r, f, rwRecGetDPort(&rwrec));
            break;
          case IPPROTO_TCP:
            if (rwRecGetTcpState(&rwrec) & SK_TCPSTATE_EXPANDED) {
                sk_vector_get_value(v, j++, &f);  /* sourceTransportPort */
                sk_fixrec_set_unsigned(r, f, rwRecGetSPort(&rwrec));
                sk_vector_get_value(v, j++, &f);  /* destinationTransportPort */
                sk_fixrec_set_unsigned(r, f, rwRecGetDPort(&rwrec));
                sk_vector_get_value(v, j++, &f);  /* tcpControlBits */
                sk_fixrec_set_unsigned(r, f, rwRecGetFlags(&rwrec));
                sk_vector_get_value(v, j++, &f);  /* initialTCPFlags */
                sk_fixrec_set_unsigned(r, f, rwRecGetInitFlags(&rwrec));
                sk_vector_get_value(v, j++, &f);  /* unionTCPFlags */
                sk_fixrec_set_unsigned(r, f, rwRecGetRestFlags(&rwrec));
            } else {
                sk_vector_get_value(v, j++, &f);  /* tcpControlBits */
                sk_fixrec_set_unsigned(r, f, rwRecGetFlags(&rwrec));
                sk_vector_get_value(v, j++, &f);  /* sourceTransportPort */
                sk_fixrec_set_unsigned(r, f, rwRecGetSPort(&rwrec));
                sk_vector_get_value(v, j++, &f);  /* destinationTransportPort */
                sk_fixrec_set_unsigned(r, f, rwRecGetDPort(&rwrec));
            }
            break;
        }

        /* handle IP addresses; this works for both IPv4 and IPv6
         * addresses */
        sk_vector_get_value(v, j++, &f);  /* sourceIPv6Address */
        rwRecMemGetSIP(&rwrec, &ipaddr);
        sk_fixrec_set_ip_address(r, f, &ipaddr);
        sk_vector_get_value(v, j++, &f);  /* destinationIPv6Address */
        rwRecMemGetDIP(&rwrec, &ipaddr);
        sk_fixrec_set_ip_address(r, f, &ipaddr);
        sk_vector_get_value(v, j++, &f);  /* ipNextHopIPv6Address */
        rwRecMemGetNhIP(&rwrec, &ipaddr);
        sk_fixrec_set_ip_address(r, f, &ipaddr);

        /* handle sidecar data */
        if ((sc_idx = rwRecGetSidecar(&rwrec)) == LUA_NOREF) {
            /* no sidecar data on this record */
            have_sidecar = 0;
        } else if (lua_rawgeti(L, LUA_REGISTRYINDEX, sc_idx) != LUA_TTABLE) {
            /* whatever is here is not a table; ignore it */
            have_sidecar = 0;
            lua_pop(L, 1);
        } else {
            have_sidecar = 1;
        }

        sk_sidecar_iter_bind(sidecar, &sc_iter);
        while (sk_sidecar_iter_next(&sc_iter, &sc_elem) == SK_ITERATOR_OK) {
            sk_vector_get_value(v, j++, &f);
            if (NULL == f) {
                continue;
            }

            if (sk_sidecar_elem_get_data_type(sc_elem) != SK_SIDECAR_LIST) {
                if (!have_sidecar) {
                    continue;
                }

                buflen = sizeof(buf);
                sk_sidecar_elem_get_name(sc_elem, buf, &buflen);
                if (lua_getfield(L, -1, buf) != LUA_TNIL) {
                    switch (sk_sidecar_elem_get_data_type(sc_elem)) {
                      case SK_SIDECAR_UINT8:
                      case SK_SIDECAR_UINT16:
                      case SK_SIDECAR_UINT32:
                      case SK_SIDECAR_UINT64:
                        {
                            uint64_t n = lua_tointeger(L, -1);
                            sk_fixrec_set_unsigned(r, f, n);
                            break;
                        }
                      case SK_SIDECAR_DOUBLE:
                        {
                            double d = lua_tonumber(L, -1);
                            sk_fixrec_set_float(r, f, d);
                            break;
                        }
                      case SK_SIDECAR_ADDR_IP4:
                      case SK_SIDECAR_ADDR_IP6:
                        {
                            const skipaddr_t *ip = sk_lua_toipaddr(L, -1);
                            sk_fixrec_set_ip_address(r, f, ip);
                            break;
                        }
                      case SK_SIDECAR_DATETIME:
                        {
                            sktime_t *t = sk_lua_todatetime(L, -1);
                            sk_fixrec_set_datetime(r, f, *t);
                            break;
                        }
                      case SK_SIDECAR_BOOLEAN:
                        {
                            sk_fixrec_set_boolean(r, f, lua_toboolean(L, -1));
                            break;
                        }
                      case SK_SIDECAR_EMPTY:
                        {
                            break;
                        }
                      case SK_SIDECAR_STRING:
                        {
                            const char *str = lua_tostring(L, -1);
                            sk_fixrec_set_string(r, f, str);
                            break;
                        }
                      case SK_SIDECAR_BINARY:
                        {
                            const char *str;
                            size_t len;
                            str = lua_tolstring(L, -1, &len);
                            sk_fixrec_set_octet_array(
                                r, f, (const uint8_t *)str, len);
                            break;
                        }
                      case SK_SIDECAR_UNKNOWN:
                        break;
                      case SK_SIDECAR_LIST:
                      case SK_SIDECAR_TABLE:
                        skAbortBadCase(sk_sidecar_elem_get_data_type(sc_elem));
                    }
                }
                lua_pop(L, 1);
                continue;
            }

            /* when the element is a list, we must add a list to the
             * record even if there is no sidecar data */
            assert(sk_sidecar_elem_get_data_type(sc_elem) == SK_SIDECAR_LIST);

            id = sk_sidecar_elem_get_ipfix_ident(sc_elem);
            buflen = sizeof(buf);
            sk_sidecar_elem_get_name(sc_elem, buf, &buflen);

            /* Create the basicList */
            list = NULL;
            if (((0 == id)
                 || ((err = (sk_fixlist_create_basiclist_from_ident(
                                &list, model, id))) != 0))
                && ((err = (sk_fixlist_create_basiclist_from_name(
                                &list, model, buf))) != 0))
            {
                skAppPrintErr("Unable to create basicList of %s: %s",
                              buf, sk_schema_strerror(err));
                exit(EXIT_FAILURE);
            }

            if (!have_sidecar) {
                goto ADD_LIST;
            }
            if (lua_getfield(L, -1, buf) != LUA_TTABLE) {
                /* field not present on record or is not a table */
                lua_pop(L, 1);
                goto ADD_LIST;
            }

            /* We must create a fixrec to hold the item prior to
             * adding to the list, and to create a fixrec we first
             * must create a schema. */
            err = sk_schema_create(&schema, model, NULL, 0);
            if (err) {
                lua_pop(L, 1);
                goto ADD_LIST;
            }
            if (((0 == id)
                 || ((err = (sk_schema_insert_field_by_ident(
                                 &field, schema, id, NULL,NULL))) != 0))
                && ((err = (sk_schema_insert_field_by_name(
                                &field, schema, buf, NULL, NULL))) != 0))
            {
                skAppPrintErr("Unable to add IE %s to schema: %s",
                              buf, sk_schema_strerror(err));
                sk_schema_destroy(schema);
                lua_pop(L, 1);
                goto ADD_LIST;
            }
            sk_schema_freeze(schema);
            if (err) {
                skAppPrintErr("Unable to freeze schema: %s",
                              sk_schema_strerror(err));
                sk_schema_destroy(schema);
                lua_pop(L, 1);
                goto ADD_LIST;
            }
            err = sk_fixrec_init(&record, schema);
            if (err) {
                skAppPrintErr("Unable to initialize record with schema: %s",
                              sk_schema_strerror(err));
                sk_schema_destroy(schema);
                lua_pop(L, 1);
                goto ADD_LIST;
            }
            sk_schema_destroy(schema);

            /* loop over the items in the Lua list (table) and add to
             * the sk_fixrec_t */
            items = (lua_Integer)lua_rawlen(L, -1);
            for (k = 1; k <= items; ++k) {
                lua_rawgeti(L, -1, k);
                switch (sk_sidecar_elem_get_list_elem_type(sc_elem)) {
                  case SK_SIDECAR_UINT8:
                  case SK_SIDECAR_UINT16:
                  case SK_SIDECAR_UINT32:
                  case SK_SIDECAR_UINT64:
                    {
                        uint64_t n = lua_tointeger(L, -1);
                        err = sk_fixrec_set_unsigned(&record, field, n);
                        break;
                    }
                  case SK_SIDECAR_DOUBLE:
                    {
                        double d = lua_tonumber(L, -1);
                        err = sk_fixrec_set_float(&record, field, d);
                        break;
                    }
                  case SK_SIDECAR_ADDR_IP4:
                  case SK_SIDECAR_ADDR_IP6:
                    {
                        const skipaddr_t *ip = sk_lua_toipaddr(L, -1);
                        err = sk_fixrec_set_ip_address(&record, field, ip);
                        break;
                    }
                  case SK_SIDECAR_DATETIME:
                    {
                        sktime_t *t = sk_lua_todatetime(L, -1);
                        err = sk_fixrec_set_datetime(&record, field, *t);
                        break;
                    }
                  case SK_SIDECAR_BOOLEAN:
                    {
                        err = sk_fixrec_set_boolean(&record, field,
                                                    lua_toboolean(L, -1));
                        break;
                    }
                  case SK_SIDECAR_EMPTY:
                    {
                        err = SK_SCHEMA_ERR_SUCCESS;
                        break;
                    }
                  case SK_SIDECAR_STRING:
                    {
                        const char *str = lua_tostring(L, -1);
                        err = sk_fixrec_set_string(&record, field, str);
                        break;
                    }
                  case SK_SIDECAR_BINARY:
                    {
                        const char *str;
                        size_t len;
                        str = lua_tolstring(L, -1, &len);
                        err = (sk_fixrec_set_octet_array(
                                   &record, field, (const uint8_t *)str, len));
                        break;
                    }
                  case SK_SIDECAR_UNKNOWN:
                    err = SK_SCHEMA_ERR_SUCCESS;
                    break;
                  case SK_SIDECAR_LIST:
                  case SK_SIDECAR_TABLE:
                    skAbortBadCase(sk_sidecar_elem_get_list_elem_type(sc_elem));
                }
                lua_pop(L, 1);
                if (err) {
                    skAppPrintErr("Unable to set value on record: %s",
                                  sk_schema_strerror(err));
                    continue;
                }
                err = sk_fixlist_append_fixrec(list, &record);
                if (err) {
                    skAppPrintErr("Unable to append record to basicList: %s",
                                  sk_schema_strerror(err));
                    continue;
                }
            }
            lua_pop(L, 1);
            sk_fixrec_destroy(&record);

          ADD_LIST:
            err = sk_fixrec_set_list(r, f, list);
            if (err) {
                skAppPrintErr("Unable to set list on record: %s",
                              sk_schema_strerror(err));
            }
            sk_fixlist_destroy(list);
        }

        if (have_sidecar) {
            /* pop the table of sidecar data */
            lua_pop(L, 1);
        }
        rwRecReset(&rwrec);

        rv = sk_fixstream_write_record(fixstream, r, NULL);
        if (rv) {
            skAppPrintErr("Unable to write record: %s",
                          sk_fixstream_strerror(fixstream));
            break;
        }
        ++rec_count;
    }

    retval = 0;

  END:
    if (fixstream) {
        skstream_t *stream_handle = NULL;
        rv = sk_fixstream_remove_stream(fixstream, &stream_handle);
        if (rv) {
            skAppPrintErr("Unable to flush stream: %s",
                          sk_fixstream_strerror(fixstream));
        }
        sk_fixstream_destroy(&fixstream);
        assert(NULL == stream_handle || stream_handle == ipfix_output);
    }

    for (i = 0; i < TMPL_COUNT; ++i) {
        sk_fixrec_destroy(&fixrec[i]);
        sk_vector_destroy(field_list[i]);
    }

    sk_sidecar_destroy(&sidecar);
    sk_lua_closestate(L);

    /* print record count */
    if (0 == retval && print_statistics) {
        fprintf(STATS_FH, ("%s: Wrote %" PRIu64 " IPFIX records to '%s'\n"),
                skAppName(), rec_count, skStreamGetPathname(ipfix_output));
    }

    return retval;
}



int main(int argc, char **argv)
{
    appSetup(argc, argv);                       /* never returns on error */

    /* Create the info model with CERT elements */
    model = skipfix_information_model_create(0);

    /* Allocate a session.  The session will be owned by the fbuf, so
     * don't save it for later freeing. */
    session = fbSessionAlloc(model);

    /* Set an observation domain */
    fbSessionSetDomain(session, OBSERVATION_DOMAIN);

    if (single_template) {
        return toipfix_one_template();
    }
    if (no_sidecar) {
        return toipfix_multiple_templates();
    }
    return toipfix_with_sidecar();
}


/*
** Local Variables:
** mode:c
** indent-tabs-mode:nil
** c-basic-offset:4
** End:
*/
