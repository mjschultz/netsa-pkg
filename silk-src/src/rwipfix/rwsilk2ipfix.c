/*
** Copyright (C) 2007-2020 by Carnegie Mellon University.
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

RCSIDENT("$SiLK: rwsilk2ipfix.c ef14e54179be 2020-04-14 21:57:45Z mthomas $");

#include <silk/rwrec.h>
#include <silk/skipfix.h>
#include <silk/sklog.h>
#include <silk/sksite.h>
#include <silk/skstream.h>
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
#if SK_ENABLE_IPV6
#define TID6_NOPORTS    0x9ED0
#define TID6_ICMP       0x9ED1
#define TID6_UDP        0x9ED2
#define TID6_TCP        0x9ED3
#define TID6_TCP_EXP    0x9ED4
#endif  /* SK_ENABLE_IPV6 */

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

#if SK_ENABLE_IPV6
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
#endif  /* SK_ENABLE_IPV6 */

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

#if SK_ENABLE_IPV6
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
#endif  /* SK_ENABLE_IPV6 */

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

#if SK_ENABLE_IPV6
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
#endif  /* SK_ENABLE_IPV6 */

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

#if SK_ENABLE_IPV6
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
#endif  /* SK_ENABLE_IPV6 */

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

#if SK_ENABLE_IPV6
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
#endif  /* SK_ENABLE_IPV6 */


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
 *    Enterprise information elements to add to the information model.
 */
static fbInfoElement_t info_elements[] = {
    /* Extra fields produced by yaf for SiLK records */
    FB_IE_INIT("initialTCPFlags",              IPFIX_CERT_PEN, 14,  1,
               FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE),
    FB_IE_INIT("unionTCPFlags",                IPFIX_CERT_PEN, 15,  1,
               FB_IE_F_ENDIAN | FB_IE_F_REVERSIBLE),
    FB_IE_INIT("silkFlowType",                 IPFIX_CERT_PEN, 30,  1,
               FB_IE_F_ENDIAN),
    FB_IE_INIT("silkFlowSensor",               IPFIX_CERT_PEN, 31,  2,
               FB_IE_F_ENDIAN),
    FB_IE_INIT("silkTCPState",                 IPFIX_CERT_PEN, 32,  1,
               FB_IE_F_ENDIAN),
    FB_IE_INIT("silkAppLabel",                 IPFIX_CERT_PEN, 33,  2,
               FB_IE_F_ENDIAN),
    FB_IE_NULL
};


/* for looping over input */
static sk_options_ctx_t *optctx = NULL;

/* the IPFIX output file; use stdout if no name provided */
static sk_fileptr_t ipfix_output;

/* whether to print statistics */
static int print_statistics = 0;

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
    OPT_SINGLE_TEMPLATE
} appOptionsEnum;

static struct option appOptions[] = {
    {"ipfix-output",            REQUIRED_ARG, 0, OPT_IPFIX_OUTPUT},
    {"print-statistics",        NO_ARG,       0, OPT_PRINT_STATISTICS},
    {"single-template",         NO_ARG,       0, OPT_SINGLE_TEMPLATE},
    {0,0,0,0}                   /* sentinel entry */
};

static const char *appHelp[] = {
    ("Write IPFIX records to the specified path. Def. stdout"),
    ("Print the count of processed records. Def. No"),
    ("Use a single template for all IPFIX records. Def. No.\n"
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

    if (teardownFlag) {
        return;
    }
    teardownFlag = 1;

    if (ipfix_output.of_fp) {
        skFileptrClose(&ipfix_output, &skAppPrintErr);
    }

    if (fbuf) {
        fBufFree(fbuf);
        fbuf = NULL;
    }
    if (session) {
        fbSessionFree(session);
        session = NULL;
    }
    if (model) {
        fbInfoModelFree(model);
        model = NULL;
    }

    /* set level to "warning" to avoid the "Stopped logging" message */
    sklogSetLevel("warning");
    sklogTeardown();

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

    /* set up libflowsource */
    skIPFIXSourcesSetup();

    /* try to load site config file; if it fails, we will not be able
     * to resolve flowtype and sensor from input file names */
    sksiteConfigure(0);

    /* set level to "warning" to avoid the "Started logging" message */
    logmask = sklogGetMask();
    sklogSetLevel("warning");
    sklogOpen();
    sklogSetMask(logmask);

    /* open the provided output file or use stdout */
    if (NULL == ipfix_output.of_name) {
        ipfix_output.of_name = "-";
        ipfix_output.of_fp = stdout;
    } else {
        rv = skFileptrOpen(&ipfix_output, SK_IO_WRITE);
        if (rv) {
            skAppPrintErr("Could not open IPFIX output file '%s': %s",
                          ipfix_output.of_name, skFileptrStrerror(rv));
            exit(EXIT_FAILURE);
        }

        if (SK_FILEPTR_IS_PROCESS == ipfix_output.of_type) {
            skAppPrintErr("Writing to gzipped files is not supported");
            exit(EXIT_FAILURE);
        }
    }

    if (FILEIsATty(ipfix_output.of_fp)) {
        skAppPrintErr("Will not write binary data to the terminal");
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
    switch ((appOptionsEnum)opt_index) {
      case OPT_IPFIX_OUTPUT:
        if (ipfix_output.of_name) {
            skAppPrintErr("Invalid %s: Switch used multiple times",
                          appOptions[opt_index].name);
            return 1;
        }
        ipfix_output.of_name = opt_arg;
        break;

      case OPT_PRINT_STATISTICS:
        print_statistics = 1;
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
    skstream_t *stream = NULL;
    rwRec rwrec;
    uint64_t rec_count = 0;
    ssize_t rv;

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

    /* Create the output buffer with the session and an exporter
     * created from the file pointer */
    fbuf = fBufAllocForExport(session, fbExporterAllocFP(ipfix_output.of_fp));
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

    /* For each input, process each record */
    while ((rv = skOptionsCtxNextSilkFile(optctx, &stream, &skAppPrintErr))
           == 0)
    {
        while ((rv = skStreamReadRecord(stream, &rwrec)) == SKSTREAM_OK) {
            /* Convert times */
            fixrec.flowStartMilliseconds = (uint64_t)rwRecGetStartTime(&rwrec);
            fixrec.flowEndMilliseconds = (uint64_t)rwRecGetEndTime(&rwrec);

            /* Handle IP addresses */
#if SK_ENABLE_IPV6
            if (rwRecIsIPv6(&rwrec)) {
                rwRecMemGetSIPv6(&rwrec, fixrec.sourceIPv6Address);
                rwRecMemGetDIPv6(&rwrec, fixrec.destinationIPv6Address);
                rwRecMemGetNhIPv6(&rwrec, fixrec.ipNextHopIPv6Address);
                fixrec.sourceIPv4Address = 0;
                fixrec.destinationIPv4Address = 0;
                fixrec.ipNextHopIPv4Address = 0;
            } else
#endif
            {
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
        if (rv != SKSTREAM_ERR_EOF) {
            skStreamPrintLastErr(stream, rv, &skAppPrintErr);
        }
        skStreamDestroy(&stream);
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
                skAppName(), rec_count, ipfix_output.of_name);
    }

    return 0;
}


static int
toipfix_multiple_templates(
    void)
{
#define TMPL_COUNT 10
    const uint16_t tid[] = {
        TID4_NOPORTS, TID4_ICMP, TID4_UDP, TID4_TCP, TID4_TCP_EXP
#if SK_ENABLE_IPV6
        , TID6_NOPORTS, TID6_ICMP, TID6_UDP, TID6_TCP, TID6_TCP_EXP
#endif
    };
    const struct tid_to_position_st {
        unsigned int    p_TID4_NOPORTS;
        unsigned int    p_TID4_ICMP;
        unsigned int    p_TID4_UDP;
        unsigned int    p_TID4_TCP;
        unsigned int    p_TID4_TCP_EXP;
#if SK_ENABLE_IPV6
        unsigned int    p_TID6_NOPORTS;
        unsigned int    p_TID6_ICMP;
        unsigned int    p_TID6_UDP;
        unsigned int    p_TID6_TCP;
        unsigned int    p_TID6_TCP_EXP;
#endif  /* SK_ENABLE_IPV6 */
    } tid_to_position = {
        0, 1, 2, 3, 4
#if SK_ENABLE_IPV6
        , 5, 6, 7, 8, 9
#endif
    };
    const uint32_t spec_flag[] = {
        REC_V4 | REC_NO_PORTS,
        REC_V4 | REC_ICMP,
        REC_V4 | REC_UDP,
        REC_V4 | REC_TCP,
        REC_V4 | REC_TCP_EXP
#if SK_ENABLE_IPV6
        ,
        REC_V6 | REC_NO_PORTS,
        REC_V6 | REC_ICMP,
        REC_V6 | REC_UDP,
        REC_V6 | REC_TCP,
        REC_V6 | REC_TCP_EXP
#endif
    };
    const unsigned int count = sizeof(tid)/sizeof(tid[0]);
    fbTemplate_t *tmpl[TMPL_COUNT];

    union fixrec_un {
        rec_prelim_t     pre;
#if SK_ENABLE_IPV6
        rec_noports_v6_t rec6_noports;
        rec_icmp_v6_t    rec6_icmp;
        rec_udp_v6_t     rec6_udp;
        rec_tcp_v6_t     rec6_tcp;
        rec_tcp_exp_v6_t rec6_tcp_exp;
#endif  /* SK_ENABLE_IPV6 */
        rec_noports_v4_t rec4_noports;
        rec_icmp_v4_t    rec4_icmp;
        rec_udp_v4_t     rec4_udp;
        rec_tcp_v4_t     rec4_tcp;
        rec_tcp_exp_v4_t rec4_tcp_exp;
    } fixrec;

    GError *err = NULL;
    skstream_t *stream = NULL;
    rwRec rwrec;
    uint64_t rec_count = 0;
    unsigned int i;
    ssize_t rv;

    assert((sizeof(spec_flag)/sizeof(spec_flag[0])) == count);
    assert((sizeof(tid_to_position)/sizeof(tid_to_position.p_TID4_NOPORTS))
           == count);
    assert(count <= TMPL_COUNT);

    /* Create each template, add the spec to the template, and add the
     * template to the session */
    for (i = 0; i < count; ++i) {
        tmpl[i] = fbTemplateAlloc(model);
        if (!fbTemplateAppendSpecArray(
                tmpl[i], multiple_spec, spec_flag[i], &err))
        {
            skAppPrintErr("Could not create template: %s", err->message);
            g_clear_error(&err);
            fbTemplateFreeUnused(tmpl[i]);
            return EXIT_FAILURE;
        }

        /* Add the template to the session */
        if (!fbSessionAddTemplate(session, TRUE, tid[i], tmpl[i], &err)) {
            skAppPrintErr("Could not add template to session: %s",
                          err->message);
            g_clear_error(&err);
            fbTemplateFreeUnused(tmpl[i]);
            return EXIT_FAILURE;
        }
        if (!fbSessionAddTemplate(session, FALSE, tid[i], tmpl[i], &err)) {
            skAppPrintErr("Could not add template to session: %s",
                          err->message);
            g_clear_error(&err);
            return EXIT_FAILURE;
        }
    }

    /* Create the output buffer with the session and an exporter
     * created from the file pointer */
    fbuf = fBufAllocForExport(session, fbExporterAllocFP(ipfix_output.of_fp));
    /* The fbuf now owns the session */
    session = NULL;

    /* Write the templates */
    if (!fbSessionExportTemplates(fBufGetSession(fbuf), &err)) {
        skAppPrintErr("Could not add export templates: %s", err->message);
        g_clear_error(&err);
        return EXIT_FAILURE;
    }

    /* For each input, process each record */
    while ((rv = skOptionsCtxNextSilkFile(optctx, &stream, &skAppPrintErr))
           == 0)
    {
        while ((rv = skStreamReadRecord(stream, &rwrec)) == SKSTREAM_OK) {
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

#if SK_ENABLE_IPV6
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
            } else
#endif  /* SK_ENABLE_IPV6 */
            {
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
            if (!fBufSetInternalTemplate(fbuf, tid[i], &err)) {
                skAppPrintErr("Could not set internal template: %s",
                              err->message);
                g_clear_error(&err);
                return EXIT_FAILURE;
            }
            if (!fBufSetExportTemplate(fbuf, tid[i], &err)) {
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
        if (rv != SKSTREAM_ERR_EOF) {
            skStreamPrintLastErr(stream, rv, &skAppPrintErr);
        }
        skStreamDestroy(&stream);
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
                skAppName(), rec_count, ipfix_output.of_name);
    }

    return 0;
}



int main(int argc, char **argv)
{
    appSetup(argc, argv);                       /* never returns on error */

    /* Create the info model and add CERT elements */
    model = fbInfoModelAlloc();
    fbInfoModelAddElementArray(model, info_elements);

    /* Allocate a session.  The session will be owned by the fbuf, so
     * don't save it for later freeing. */
    session = fbSessionAlloc(model);

    /* Set an observation domain */
    fbSessionSetDomain(session, OBSERVATION_DOMAIN);

    if (single_template) {
        return toipfix_one_template();
    }
    return toipfix_multiple_templates();
}


/*
** Local Variables:
** mode:c
** indent-tabs-mode:nil
** c-basic-offset:4
** End:
*/
