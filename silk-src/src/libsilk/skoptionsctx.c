/*
** Copyright (C) 2011-2017 by Carnegie Mellon University.
**
** @OPENSOURCE_LICENSE_START@
** See license information in ../../LICENSE.txt
** @OPENSOURCE_LICENSE_END@
*/

/*
**  skoptionsctx.c
**
**    Support for --xargs, reading from stdin, and looping over
**    filenames on the command line.
**
**    Mark Thomas
**    May 2011
**
*/

#include <silk/silk.h>

RCSIDENT("$SiLK: skoptionsctx.c 64d7ed05a614 2017-06-26 16:36:15Z mthomas $");

#include <silk/skfglob.h>
#include <silk/skstream.h>
#include <silk/utils.h>


/* LOCAL DEFINES AND TYPEDEFS */

#define PATH_IS_STDIN(path)                                     \
    (0 == strcmp((path), "-") || 0 == strcmp((path), "stdin"))
#define PATH_IS_STDOUT(path)                                    \
    (0 == strcmp((path), "-") || 0 == strcmp((path), "stdout"))

/* typedef struct sk_options_ctx_st sk_options_ctx_t; */
struct sk_options_ctx_st {
    /**
     *    If non-NULL, the file handle to print filenames to.  Set by
     *    --print-filenames. Requires SK_OPTIONS_CTX_PRINT_FILENAMES
     */
    FILE               *print_filenames;
    /**
     *    If non-NULL, the stream to read filenames from.  Set by
     *    --xargs.  Requires SK_OPTIONS_CTX_XARGS
     */
    skstream_t         *xargs;
    /**
     *    If non-NULL, the stream to copy all SiLK Flow records
     *    to. Set by --copy-input.  Requires SK_OPTIONS_CTX_COPY_INPUT
     */
    skstream_t         *copy_input;
    /**
     *    If non-NULL, the file globbing context.  Requires
     *    SK_OPTIONS_CTX_FGLOB.
     */
    sk_fglob_t         *fglob;
    /**
     *    When certain fglob arguments are paired with other
     *    arguments, the fglob arguments become partitioning switches.
     *    This is the set of sensor IDs to use for partitioning.
     */
    sk_bitmap_t        *sensor_bmap;
    /**
     *    When certain fglob arguments are paired with other
     *    arguments, the fglob arguments become partitioning switches.
     *    This is the set of flowtype IDs to use for partitioning.
     */
    sk_bitmap_t        *flowtype_bmap;
    /**
     *    The command line arguments.  This is a pointer to the
     *    command line arguments passed into
     *    skOptionsCtxOptionsParse(), not a copy of them.
     */
    char              **argv;
    /**
     *    The number of command line arguments.
     */
    int                 argc;
    /**
     *    An index into the array of command line arguments.
     */
    int                 arg_index;
    /**
     *    Return code from calling skFGlobSetFilters().
     */
    int                 fglob_set_fltr;
    /**
     *    The flags that determine the behavior of the options ctx.
     */
    unsigned int        flags;
    /**
     *    True if any input stream or the --xargs stream reads from
     *    the standard input.
     */
    unsigned            stdin_used      :1;
    /**
     *    True when the --copy-input stream writes to the standard
     *    output.
     */
    unsigned            stdout_used     :1;
    /**
     *    True when file globbing is requested.
     */
    unsigned            fglob_valid     :1;
    /**
     *    True after a successful return from
     *    skOptionsCtxOptionsParse() and the options_ctx handles the
     *    input.
     */
    unsigned            parse_ok        :1;
    /**
     *    True after a successful return from
     *    skOptionsCtxOpenStreams()
     */
    unsigned            init_ok         :1;
    /**
     *    True after a unsuccessful return from
     *    skOptionsCtxOpenStreams()
     */
    unsigned            init_failed     :1;
    /**
     *    True if the options ctx has determined that it should read
     *    input from the standard input
     */
    unsigned            read_stdin      :1;
    /**
     *    True if all input streams have been returned to the caller
     */
    unsigned            no_more_inputs  :1;
    /**
     *    The IPv6 policy.  Set by --ipv6-policy.  Requires
     *    SK_OPTIONS_CTX_IPV6_POLICY
     */
    sk_ipv6policy_t     ipv6_policy;
};


/* LOCAL VARIABLE DEFINITIONS */

static const struct options_ctx_options_st {
    struct option   opt;
    const char     *help;
} options_ctx_options[] = {
    {{"print-filenames", NO_ARG,       0, SK_OPTIONS_CTX_PRINT_FILENAMES},
     ("Print input filenames while processing. Def. no")},
    {{"copy-input",      REQUIRED_ARG, 0, SK_OPTIONS_CTX_COPY_INPUT},
     ("Copy all input SiLK Flows to given pipe or file. Def. No")},
    {{"xargs",           OPTIONAL_ARG, 0, SK_OPTIONS_CTX_XARGS},
     ("Read the names of the files to process from named text file,\n"
      "\tone name per line, or from the standard input if no parameter."
      " Def. no")},
    {{0, 0, 0, 0}, 0}    /* sentinel */
};



/* FUNCTION DEFINITIONS */

static const char *
optionsCtxSwitchName(
    int                 opt_index)
{
    size_t i;

    for (i = 0; options_ctx_options[i].help; ++i) {
        if (options_ctx_options[i].opt.val == opt_index) {
            return options_ctx_options[i].opt.name;
        }
    }
    skAbortBadCase(opt_index);
}

static int
optionsCtxHandler(
    clientData          cData,
    int                 opt_index,
    char               *opt_arg)
{
    sk_options_ctx_t *arg_ctx = (sk_options_ctx_t*)cData;
    int rv;

    if (opt_arg && strlen(opt_arg) == strspn(opt_arg, "\t\n\v\f\r ")) {
        skAppPrintErr("Invalid %s: Argument contains only whitespace",
                      optionsCtxSwitchName(opt_index));
        return 1;
    }

    switch (opt_index) {
      case SK_OPTIONS_CTX_PRINT_FILENAMES:
        arg_ctx->print_filenames = stderr;
        break;

      case SK_OPTIONS_CTX_COPY_INPUT:
        if (arg_ctx->copy_input) {
            skAppPrintErr("Invalid %s: Switch used multiple times",
                          optionsCtxSwitchName(opt_index));
            return 1;
        }
        if (PATH_IS_STDOUT(opt_arg)) {
            if (arg_ctx->stdout_used) {
                skAppPrintErr("Multiple outputs attempt"
                              " to use standard output");
                return 1;
            }
            arg_ctx->stdout_used = 1;
        }
        if ((rv = skStreamCreate(&arg_ctx->copy_input, SK_IO_WRITE,
                                 SK_CONTENT_SILK_FLOW))
            || (rv = skStreamBind(arg_ctx->copy_input, opt_arg)))
        {
            skStreamPrintLastErr(arg_ctx->copy_input, rv, skAppPrintErr);
            skStreamDestroy(&arg_ctx->copy_input);
            return 1;
        }
        break;

      case SK_OPTIONS_CTX_XARGS:
        if (arg_ctx->xargs) {
            skAppPrintErr("Invalid %s: Switch used multiple times",
                          optionsCtxSwitchName(opt_index));
            return 1;
        }
        if (NULL == opt_arg || PATH_IS_STDIN(opt_arg)) {
            if (arg_ctx->stdin_used) {
                skAppPrintErr("Multiple inputs attempt to use standard input");
                return 1;
            }
            arg_ctx->stdin_used = 1;
        }
        if ((rv = skStreamCreate(&arg_ctx->xargs, SK_IO_READ, SK_CONTENT_TEXT))
            || (rv = skStreamBind(arg_ctx->xargs, (opt_arg ? opt_arg : "-"))))
        {
            skStreamPrintLastErr(arg_ctx->xargs, rv, &skAppPrintErr);
            skStreamDestroy(&arg_ctx->xargs);
            return 1;
        }
        break;

      default:
        skAbortBadCase(opt_index);
    }

    return 0;
}


/*
 *    If fglob is not active, return 0.  Otherwise, check whether
 *    fglob arguments were specified on the command line.  If not
 *    return 0.  If so and they could be used as partitioning switches
 *    to rwfilter, store the bitmaps that rwfilter would use for
 *    partitioning and return 0.  Otherwise, return -1.
 */
static int
optionsCtxParseCheckFGlob(
    sk_options_ctx_t   *arg_ctx,
    int                 xarg_or_argc)
{
    int fglob_valid;
    int rv;

    assert(arg_ctx);

    if (NULL == arg_ctx->fglob) {
        return 0;
    }
    rv = skFGlobSetFilters(arg_ctx->fglob, &arg_ctx->sensor_bmap,
                           &arg_ctx->flowtype_bmap);
    if (rv < 0) {
        return -1;
    }
    arg_ctx->fglob_set_fltr = rv;

    fglob_valid = skFGlobValid(arg_ctx->fglob);
    if (0 == fglob_valid) {
        return 0;
    }
    if (-1 == fglob_valid) {
        return -1;
    }
    if (0 == xarg_or_argc) {
        skAppPrintErr("May not use --%s and specify file selection switches",
                      optionsCtxSwitchName(SK_OPTIONS_CTX_XARGS));
    } else {
        skAppPrintErr("May not give files on the command line"
                      " and specify file selection switches");
    }
    return -1;
}


int
skOptionsCtxCopyStreamClose(
    sk_options_ctx_t   *arg_ctx,
    sk_msg_fn_t         err_fn)
{
    int rv;

    assert(arg_ctx);

    if (arg_ctx->copy_input && arg_ctx->init_ok) {
        rv = skStreamClose(arg_ctx->copy_input);
        if (rv && err_fn) {
            skStreamPrintLastErr(arg_ctx->copy_input, rv, err_fn);
        }
        return rv;
    }
    return 0;
}


int
skOptionsCtxCopyStreamIsActive(
    const sk_options_ctx_t *arg_ctx)
{
    assert(arg_ctx);
    return ((arg_ctx->copy_input) ? 1 : 0);
}


int
skOptionsCtxCopyStreamIsStdout(
    const sk_options_ctx_t *arg_ctx)
{
    assert(arg_ctx);
    if (arg_ctx->copy_input) {
        return PATH_IS_STDOUT(skStreamGetPathname(arg_ctx->copy_input));
    }
    return 0;
}


int
skOptionsCtxCountArgs(
    const sk_options_ctx_t *arg_ctx)
{
    assert(arg_ctx);
    if (!arg_ctx->parse_ok) {
        return -1;
    }
    return (arg_ctx->argc - arg_ctx->arg_index);
}


int
skOptionsCtxCreate(
    sk_options_ctx_t  **arg_ctx_parm,
    unsigned int        flags)
{
    sk_options_ctx_t *arg_ctx;

    /* this flags must be used by itself */
    if ((flags & SK_OPTIONS_CTX_SWITCHES_ONLY)
        && (flags != SK_OPTIONS_CTX_SWITCHES_ONLY))
    {
        return -1;
    }

    /* some flags imply others */
    if (flags & SK_OPTIONS_CTX_COPY_INPUT) {
        flags |= SK_OPTIONS_CTX_INPUT_SILK_FLOW;
    }
    if (flags & SK_OPTIONS_CTX_FGLOB) {
        flags |= SK_OPTIONS_CTX_INPUT_SILK_FLOW;
    }
    if (flags & SK_OPTIONS_CTX_INPUT_SILK_FLOW) {
        flags |= SK_OPTIONS_CTX_INPUT_BINARY;
    }

    arg_ctx = sk_alloc(sk_options_ctx_t);
    arg_ctx->ipv6_policy = SK_IPV6POLICY_MIX;
    arg_ctx->flags = flags;

    *arg_ctx_parm = arg_ctx;
    return 0;
}


int
skOptionsCtxDestroy(
    sk_options_ctx_t  **arg_ctx_parm)
{
    sk_options_ctx_t *arg_ctx;
    int rv = 0;

    if (NULL == arg_ctx_parm || NULL == *arg_ctx_parm) {
        return 0;
    }
    arg_ctx = *arg_ctx_parm;
    *arg_ctx_parm = NULL;

    skFGlobDestroy(&arg_ctx->fglob);

    skStreamDestroy(&arg_ctx->xargs);
    if (arg_ctx->copy_input) {
        if (arg_ctx->init_ok) {
            rv = skStreamClose(arg_ctx->copy_input);
        }
        skStreamDestroy(&arg_ctx->copy_input);
    }
    skBitmapDestroy(&arg_ctx->sensor_bmap);
    skBitmapDestroy(&arg_ctx->flowtype_bmap);
    free(arg_ctx);
    return rv;
}


skstream_t*
skOptionsCtxGetCopyStream(
    const sk_options_ctx_t *arg_ctx)
{
    assert(arg_ctx);
    return arg_ctx->copy_input;
}


int
skOptionsCtxGetFGlobFilters(
    sk_options_ctx_t   *arg_ctx,
    sk_bitmap_t       **sensor_bitmap,
    sk_bitmap_t       **flowtype_bitmap)
{
    int rv;

    assert(arg_ctx);

    rv = arg_ctx->fglob_set_fltr;
    *sensor_bitmap = arg_ctx->sensor_bmap;
    *flowtype_bitmap = arg_ctx->flowtype_bmap;

    arg_ctx->fglob_set_fltr = 0;
    arg_ctx->sensor_bmap = NULL;
    arg_ctx->flowtype_bmap = NULL;

    return rv;
}


sk_ipv6policy_t
skOptionsCtxGetIPv6Policy(
    const sk_options_ctx_t *arg_ctx)
{
    assert(arg_ctx);
    return arg_ctx->ipv6_policy;
}


FILE *
skOptionsCtxGetPrintFilenames(
    const sk_options_ctx_t *arg_ctx)
{
    assert(arg_ctx);
    return arg_ctx->print_filenames;
}


int
skOptionsCtxNextArgument(
    sk_options_ctx_t   *arg_ctx,
    char               *buf,
    size_t              bufsize)
{
    int rv;

    assert(arg_ctx);
    assert(buf);

    if (arg_ctx->no_more_inputs) {
        return 1;
    }
    if (!arg_ctx->parse_ok || arg_ctx->init_failed) {
        return -1;
    }
    if (!arg_ctx->init_ok) {
        rv = skOptionsCtxOpenStreams(arg_ctx, NULL);
        if (rv) {
            return rv;
        }
    }

    if (arg_ctx->fglob_valid) {
        if (skFGlobNext(arg_ctx->fglob, buf, bufsize)) {
            return 0;
        }
        arg_ctx->no_more_inputs = 1;
        return 1;
    }
    if (arg_ctx->xargs) {
        for (;;) {
            rv = skStreamGetLine(arg_ctx->xargs, buf, bufsize, NULL);
            if (SKSTREAM_OK == rv) {
                return 0;
            }
            if (SKSTREAM_ERR_LONG_LINE == rv) {
                continue;
            }
            arg_ctx->no_more_inputs = 1;
            if (SKSTREAM_ERR_EOF == rv) {
                return 1;
            }
            skStreamPrintLastErr(arg_ctx->xargs, rv, skAppPrintErr);
            return -1;
        }
    }
    if (arg_ctx->read_stdin) {
        arg_ctx->no_more_inputs = 1;
        strncpy(buf, "-", bufsize);
        return 0;
    }
    if (arg_ctx->arg_index < arg_ctx->argc) {
        strncpy(buf, arg_ctx->argv[arg_ctx->arg_index], bufsize);
        buf[bufsize - 1] = '\0';
        ++arg_ctx->arg_index;
        return 0;
    }
    arg_ctx->no_more_inputs = 1;
    return 1;
}


int
skOptionsCtxOpenStreams(
    sk_options_ctx_t   *arg_ctx,
    sk_msg_fn_t         err_fn)
{
    int rv;

    assert(arg_ctx);

    if (!arg_ctx->parse_ok) {
        return -1;
    }
    if (arg_ctx->init_ok) {
        return 0;
    }
    if (arg_ctx->init_failed) {
        return -1;
    }

    if (arg_ctx->xargs) {
        rv = skStreamOpen(arg_ctx->xargs);
        if (rv) {
            if (err_fn) {
                skStreamPrintLastErr(arg_ctx->xargs, rv, err_fn);
            }
            arg_ctx->init_failed = 1;
            return -1;
        }
    }
    if (arg_ctx->copy_input) {
        rv = skStreamOpen(arg_ctx->copy_input);
        if (rv) {
            if (err_fn) {
                skStreamPrintLastErr(arg_ctx->copy_input, rv, err_fn);
            }
            arg_ctx->init_failed = 1;
            return -1;
        }
    }

    arg_ctx->init_ok = 1;
    return 0;
}


/* FIXME: consider adding a separate flags parameter here */
int
skOptionsCtxOptionsParse(
    sk_options_ctx_t   *arg_ctx,
    int                 argc,
    char              **argv)
{
    int fglob_valid;

    if (NULL == arg_ctx) {
        return skOptionsParse(argc, argv);
    }

    arg_ctx->argc = argc;
    arg_ctx->argv = argv;
    arg_ctx->arg_index = skOptionsParse(argc, argv);
    if (arg_ctx->arg_index < 0) {
        return arg_ctx->arg_index;
    }

    /*
     * if (ignore_non_switch_args) {
     *     return arg_index;
     * }
     */

    /* handle case where none of the input capabilities of the
     * options-ctx are required and there should be no remaining
     * command-line arguments once all switches are processed */
    if (arg_ctx->flags & SK_OPTIONS_CTX_SWITCHES_ONLY) {
        if (arg_ctx->arg_index != argc) {
            skAppPrintErr("Too many arguments or unrecognized switch '%s'",
                          argv[arg_ctx->arg_index]);
            return -1;
        }
        return 0;
    }

    /* some sort of input is required */

    if (arg_ctx->xargs) {
        if (arg_ctx->arg_index != argc) {
            skAppPrintErr(
                "May not use --%s and give files on the command line",
                optionsCtxSwitchName(SK_OPTIONS_CTX_XARGS));
            return -1;
        }
        if (optionsCtxParseCheckFGlob(arg_ctx, 0)) {
            return -1;
        }
        arg_ctx->parse_ok = 1;
        return 0;
    }

    if (arg_ctx->arg_index < argc) {
        if (optionsCtxParseCheckFGlob(arg_ctx, 1)) {
            return -1;
        }
        arg_ctx->parse_ok = 1;
        return 0;
    }

    fglob_valid = skFGlobValid(arg_ctx->fglob);
    if (fglob_valid) {
        if (-1 == fglob_valid) {
            return -1;
        }
        arg_ctx->fglob_valid = 1;
        arg_ctx->parse_ok = 1;
        return 0;
    }

    if (!(arg_ctx->flags & SK_OPTIONS_CTX_ALLOW_STDIN)) {
        skAppPrintErr("No input files specified on the command line");
        return -1;
    }

    if (FILEIsATty(stdin)
        && (arg_ctx->flags & SK_OPTIONS_CTX_INPUT_BINARY))
    {
        skAppPrintErr("No input files specified on the command line"
                      " and standard input is a terminal");
        return -1;
    }
    if (arg_ctx->stdin_used) {
        skAppPrintErr("Multiple inputs attempt to use standard input");
        return 1;
    }
    arg_ctx->stdin_used = 1;
    arg_ctx->read_stdin = 1;

    arg_ctx->parse_ok = 1;
    return 0;
}


int
skOptionsCtxOptionsRegister(
    sk_options_ctx_t   *arg_ctx)
{
    size_t i;
    int rv = 0;

    assert(arg_ctx);

    for (i = 0; options_ctx_options[i].help && 0 == rv; ++i) {
        if (arg_ctx->flags & options_ctx_options[i].opt.val) {
            rv = skOptionsRegisterCount(&options_ctx_options[i].opt, 1,
                                        optionsCtxHandler,(clientData)arg_ctx);
        }
    }
    if (0 == rv && (arg_ctx->flags & SK_OPTIONS_CTX_IPV6_POLICY)) {
        rv = skIPv6PolicyOptionsRegister(&arg_ctx->ipv6_policy);
    }
    if (0 == rv && arg_ctx->flags & SK_OPTIONS_CTX_FGLOB) {
        rv = skFGlobCreate(&arg_ctx->fglob);
    }
    return rv;
}


void
skOptionsCtxOptionsUsage(
    const sk_options_ctx_t *arg_ctx,
    FILE                   *fh)
{
    size_t i;

    assert(arg_ctx);

    for (i = 0; options_ctx_options[i].help; ++i) {
        if (arg_ctx->flags & options_ctx_options[i].opt.val) {
            fprintf(fh, "--%s %s. %s\n", options_ctx_options[i].opt.name,
                    SK_OPTION_HAS_ARG(options_ctx_options[i].opt),
                    options_ctx_options[i].help);
        }
    }
    if (arg_ctx->flags & SK_OPTIONS_CTX_IPV6_POLICY) {
        skIPv6PolicyUsage(fh);
    }
    if (arg_ctx->fglob) {
        skFGlobUsage(arg_ctx->fglob, fh);
    }
}


/*
** Local Variables:
** mode:c
** indent-tabs-mode:nil
** c-basic-offset:4
** End:
*/
