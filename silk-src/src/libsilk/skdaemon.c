/*
** Copyright (C) 2006-2017 by Carnegie Mellon University.
**
** @OPENSOURCE_LICENSE_START@
** See license information in ../../LICENSE.txt
** @OPENSOURCE_LICENSE_END@
*/

/*
**  Provide functions to daemonize an application
**
*/


#include <silk/silk.h>

RCSIDENT("$SiLK: skdaemon.c efd886457770 2017-06-21 18:43:23Z mthomas $");

#include <silk/skdaemon.h>
#include <silk/sklog.h>
#include <silk/utils.h>


/* LOCAL DEFINES AND TYPEDEFS */

/* the name of global variable in the Lua config file that holds the
 * table used to configure the daemon behavior */
#define SKDAEMON_CONFIG_FILE_VARNAME    "daemon"

/* skdaemon_ctx_t is the daemon context */
struct skdaemon_ctx_st {
    /* location of pid file */
    char           *pidfile;
    /* variable to set to '1' once the signal handler is called */
    volatile int   *shutdown_flag;
    /* whether to chdir to the root directory (0 = yes, 1 = no) */
    unsigned        no_chdir   :1;
    /* whether to run as a daemon (0 = yes, 1 = no) */
    unsigned        no_daemon  :1;
    /* whether the legacy logging was provided as an option */
    unsigned        legacy_log :1;
    /* whether the config-file is being used */
    unsigned        config_file:1;
};
typedef struct skdaemon_ctx_st skdaemon_ctx_t;


/* map a signal number to its name */
typedef struct sk_siglist_st {
    int         signal;
    const char *name;
} sk_siglist_t;


/* Use this macro to print an error message to the log stream and to
 * the standard error. Use double parens around arguments to this
 * macro: PRINT_AND_LOG(("%s is bad", "foo")); */
#ifdef TEST_PRINTF_FORMATS
#  define PRINT_AND_LOG(args) printf args
#else
#  define PRINT_AND_LOG(args) \
    do {                      \
        skAppPrintErr args;   \
        ERRMSG args;          \
    } while(0)
#endif


/* LOCAL VARIABLE DEFINITIONS */


/* there is a single context */
static skdaemon_ctx_t daemon_ctx;
static skdaemon_ctx_t *skdaemon = NULL;

/* Signals to ignore or to catch */
static sk_siglist_t ignored_signals[] = {
    /* {SIGCHLD, "CHLD"},  leave at default (which is ignore) */
    {SIGPIPE, "PIPE"},
    {0,NULL}  /* sentinel */
};

static sk_siglist_t caught_signals[] = {
    {SIGHUP,  "HUP"},
    {SIGINT,  "INT"},
#ifdef SIGPWR
    {SIGPWR,  "PWR"},
#endif
    {SIGQUIT, "QUIT"},
    {SIGTERM, "TERM"},
    {0,NULL}  /* sentinel */
};


/* OPTIONS SETUP */

/*
 *    Identifiers for each option.
 */
typedef enum {
    OPT_PIDFILE,
    OPT_NO_CHDIR,
    OPT_NO_DAEMON
} daemonOptionsEnum;

/*
 *    Array of options for command-line switches.  Must keep in sync
 *    with daemonOptionsEnum above.
 */
static struct option daemonOptions[] = {
    {"pidfile",               REQUIRED_ARG, 0, OPT_PIDFILE},
    {"no-chdir",              NO_ARG,       0, OPT_NO_CHDIR},
    {"no-daemon",             NO_ARG,       0, OPT_NO_DAEMON},
    {0,0,0,0}                 /* sentinel */
};

/*
 *    Array of names for configuration-file use.  Must keep in sync
 *    with daemonOptionsEnum above.
 */
static const char *config_file_keys[] = {
    "pid_file", "chdir", "fork", NULL
};


/* LOCAL FUNCTION PROTOTYPES */

static void daemonHandleSignal(int sigNum);
static int daemonInstallSignalHandler(void);
static int
daemonOptionsHandler(
    clientData          cData,
    int                 opt_index,
    char               *opt_arg);
static void daemon_unknown_key_callback(const char *key, void *v_config_file);
static int daemonWritePid(void);


/* FUNCTION DEFINITIONS */

/*
 *  daemonHandleSignal(sig_num);
 *
 *    Trap all signals and shutdown when told to.
 */
static void
daemonHandleSignal(
    int                 sig_num)
{
    /* determine name of our signal */
    sk_siglist_t *s;

    for (s = caught_signals; s->name && s->signal != sig_num; s++)
        ; /* empty */

    /* don't allow the writing of the log message to cause the entire
     * program to deadlock.  */
    if (s->name) {
        sklogNonBlock(LOG_NOTICE, "Shutting down due to SIG%s signal",s->name);
    } else {
        sklogNonBlock(LOG_NOTICE, "Shutting down due to unknown signal");
    }

    /* set the global shutdown variable */
    if (skdaemon && skdaemon->shutdown_flag) {
        *(skdaemon->shutdown_flag) = 1;
    }
}


/*
 *  ok = daemonInstallSignalHandler();
 *
 *    Trap all signals we can here with our own handler.
 *    Exception: SIGPIPE.  Set this to SIGIGN.
 *
 *    Returns 0 if OK. -1 else.
 */
static int
daemonInstallSignalHandler(
    void)
{
    sk_siglist_t *s;
    struct sigaction action;
    int rv;

    memset(&action, 0, sizeof(action));

    /* mask any further signals while we're inside the handler */
    sigfillset(&action.sa_mask);

    /* ignored signals */
    action.sa_handler = SIG_IGN;
    for (s = ignored_signals; s->name; ++s) {
        if (sigaction(s->signal, &action, NULL) == -1) {
            rv = errno;
            PRINT_AND_LOG(("Cannot ignore SIG%s: %s",
                           s->name, strerror(rv)));
            return -1;
        }
    }

    /* signals to catch */
    action.sa_handler = &daemonHandleSignal;
    for (s = caught_signals; s->name; ++s) {
        if (sigaction(s->signal, &action, NULL) == -1) {
            rv = errno;
            PRINT_AND_LOG(("Cannot handle SIG%s: %s",
                           s->name, strerror(rv)));
            return -1;
        }
    }

    return 0;
}


/*
 *  status = daemonOptionsHandler(cData, opt_index, opt_arg);
 *
 *    Handle the options that we registered in skdaemonSetup().
 */
static int
daemonOptionsHandler(
    clientData   UNUSED(cData),
    int                 opt_index,
    char               *opt_arg)
{
    switch ((daemonOptionsEnum)opt_index) {
      case OPT_PIDFILE:
        if (skdaemon->pidfile) {
            skAppPrintErr("Invalid %s: Switch used multiple times",
                          daemonOptions[opt_index].name);
            return -1;
        }
        if (opt_arg[0] != '/') {
            skAppPrintErr(("Invalid %s '%s': A complete path is required"
                           " and value does not begin with a slash)"),
                          daemonOptions[opt_index].name, opt_arg);
            return -1;
        }
        skdaemon->pidfile = strdup(opt_arg);
        if (NULL == skdaemon->pidfile) {
            skAppPrintOutOfMemory("string");
            return -1;
        }
        break;

      case OPT_NO_CHDIR:
        skdaemon->no_chdir = 1;
        break;

      case OPT_NO_DAEMON:
        skdaemon->no_daemon = 1;
        break;
    }

    return 0;
}


/*
 *  daemon_unknown_key_callback(key, v_config_file);
 *
 *    Print a warning about the unrecognized key 'key' in the 'daemon'
 *    table in the Lua configuration file.  The file name is specified
 *    in the 'v_config_file' parameter.
 */
static void
daemon_unknown_key_callback(
    const char         *key,
    void               *v_config_file)
{
    const char *config_file = (const char*)v_config_file;

    if (key) {
        skAppPrintErr("Warning for configuration '%s':"
                      " Unexpected key '%s' found in table '%s'",
                      config_file, key, SKDAEMON_CONFIG_FILE_VARNAME);
    } else {
        skAppPrintErr("Warning for configuration '%s':"
                      " Non-alphanumeric key found in table '%s'",
                      config_file, SKDAEMON_CONFIG_FILE_VARNAME);
    }
}


/*
 *  status = daemonWritePid();
 *
 *    Write the process ID (PID) to the pidfile the user specified.
 *    If no pidfile was specified but a log directory was specified,
 *    write it to that directory.  Otherwise, do not write the PID to
 *    disk.  Store the location of the pidfile on the global context.
 *
 *    Return 0 on success, or errno on failure.
 */
static int
daemonWritePid(
    void)
{
    char pidfile[PATH_MAX+1];
    char pidstr[24];
    int fd;
    ssize_t len;
    ssize_t len2;
    const char *log_directory;
    int saveerr;

    if (!skdaemon->pidfile) {
        /* No pidfile on command line. */
        log_directory = sklogGetDirectory(pidfile, sizeof(pidfile));
        if (!log_directory) {
            return 0;
        }
        /* We do have a log-directory; store the PID there using the
         * application name as the file's base name */
        len = strlen(pidfile);
        len2 = snprintf(&pidfile[len], sizeof(pidfile)-len, "/%s.pid",
                        skAppName());
        if ((size_t)(len + len2) >= sizeof(pidfile)) {
            /* make up an errno */
            return ENAMETOOLONG;
        }
        skdaemon->pidfile = strdup(pidfile);
        if (!skdaemon->pidfile) {
            return errno;
        }
    }

    /* Filesystem Hierarchy Standard says the pid file contains
     * the PID in ASCII-encoded decimal followed by a newline. */
    len = snprintf(pidstr, sizeof(pidstr), "%ld\n", (long)getpid());
    if ((size_t)len >= sizeof(pidstr)) {
        return ENAMETOOLONG;
    }
    fd = open(skdaemon->pidfile, O_CREAT | O_WRONLY | O_TRUNC, 0644);
    if (fd == -1) {
        return errno;
    }
    if (skwriten(fd, pidstr, len) == -1) {
        saveerr = errno;
        close(fd);
        unlink(skdaemon->pidfile);
        return saveerr;
    }
    if (close(fd) == -1) {
        saveerr = errno;
        unlink(skdaemon->pidfile);
        return saveerr;
    }

    return 0;
}


/* force the application not to fork */
void
skdaemonDontFork(
    void)
{
    if (skdaemon) {
        skdaemon->no_daemon = 1;
    }
}


/* print the usage of the options defined by this library */
void
skdaemonOptionsUsage(
    FILE               *fh)
{
    int i;

    if (skdaemon && skdaemon->config_file) {
        return;
    }

    sklogOptionsUsage(fh);
    for (i = 0; daemonOptions[i].name; ++i) {
        fprintf(fh, "--%s %s. ", daemonOptions[i].name,
                SK_OPTION_HAS_ARG(daemonOptions[i]));
        switch ((daemonOptionsEnum)i) {
          case OPT_PIDFILE:
            if (skdaemon && skdaemon->legacy_log) {
                fprintf(fh, ("Complete path to the process ID file."
                             "  Overrides the path\n"
                             "\tbased on the --log-directory argument."));
            } else {
                fprintf(fh, ("Complete path to the process ID file."
                             "  Def. None"));
            }
            break;
          case OPT_NO_CHDIR:
            fprintf(fh, ("Do not change directory to the root directory.\n"
                         "\tDef. Change directory unless --%s is specified"),
                    daemonOptions[OPT_NO_DAEMON].name);
            break;
          case OPT_NO_DAEMON:
            fprintf(fh, ("Do not fork off as a daemon (for debugging)."
                         " Def. Fork"));
            break;
        }
        fprintf(fh, "\n");
    }
}


/* verify that the options are valid and that all required options
 * were provided. */
int
skdaemonOptionsVerify(
    void)
{
    /* skdaemon doesn't have any options that it requires, but the
     * logging library does. */
    return sklogOptionsVerify();
}


/* Set daemon parameters from the configuration file */
int
skdaemonParseConfigFile(
    lua_State          *L,
    const char         *config_file)
{
    const char table[] = SKDAEMON_CONFIG_FILE_VARNAME;
    const char *key;
    const char *value;
    size_t error_count = 0;
    int true_false;
    int t;
    int retval = -1;

    /* table is in the stack at index 't' */
    lua_getglobal(L, table);
    t = lua_gettop(L);

    /* does it exist and is it a table? */
    if (!lua_istable(L, t)) {
        if (lua_isnil(L, t)) {
            /* the daemon settings are optional; we are done  */
            retval = 0;
        } else {
            skAppPrintErr("Error in configuration '%s':"
                          " Variable '%s' is a %s; %s expected",
                          config_file, table,
                          lua_typename(L, lua_type(L, t)),
                          lua_typename(L, LUA_TSTRING));
        }
        goto END;
    }

    /* check table for unrecognized keys */
    sk_lua_check_table_unknown_keys(
        L, t, -1, config_file_keys,
        daemon_unknown_key_callback, (void*)config_file);

    /* get daemon[pidfile] */
    key = config_file_keys[OPT_PIDFILE];
    lua_getfield(L, t, key);
    if (!lua_isstring(L, -1)) {
        if (!lua_isnil(L, -1)) {
            skAppPrintErr(("Error in configuration '%s':"
                           " %s['%s'] is a %s; %s expected"),
                          config_file, table, key,
                          lua_typename(L, lua_type(L, -1)),
                          lua_typename(L, LUA_TSTRING));
            ++error_count;
        }
        /* else value is not required, so nil is okay */
    } else {
        value = lua_tostring(L, -1);
        skdaemon->pidfile = strdup(value);
        if (NULL == skdaemon->pidfile) {
            skAppPrintOutOfMemory("string");
            lua_pop(L, 1);
            goto END;
        }
    }
    lua_pop(L, 1);

    /* get daemon[chdir] */
    key = config_file_keys[OPT_NO_CHDIR];
    lua_getfield(L, t, key);
    if (!lua_isboolean(L, -1)) {
        if (!lua_isnil(L, -1)) {
            skAppPrintErr(("Error in configuration '%s':"
                           " %s['%s'] is a %s; %s expected"),
                          config_file, table, key,
                          lua_typename(L, lua_type(L, -1)),
                          lua_typename(L, LUA_TBOOLEAN));
            ++error_count;
        }
        /* else value is not required, so nil is okay */
    } else {
        true_false = lua_toboolean(L, -1);
        skdaemon->no_chdir = !true_false;
    }
    lua_pop(L, 1);

    /* get daemon[chdir] */
    key = config_file_keys[OPT_NO_DAEMON];
    lua_getfield(L, t, key);
    if (!lua_isboolean(L, -1)) {
        if (!lua_isnil(L, -1)) {
            skAppPrintErr(("Error in configuration '%s':"
                           " %s['%s'] is a %s; %s expected"),
                          config_file, table, key,
                          lua_typename(L, lua_type(L, -1)),
                          lua_typename(L, LUA_TBOOLEAN));
            ++error_count;
        }
        /* else value is not required, so nil is okay */
    } else {
        true_false = lua_toboolean(L, -1);
        skdaemon->no_daemon = !true_false;
    }
    lua_pop(L, 1);

    /* done */
    if (0 == error_count) {
        retval = 0;
    }

  END:
    /* table should be only thing on the stack at this point */
    assert(lua_gettop(L) == t);
    /* pop the table */
    lua_pop(L, 1);
    return retval;
}


/* register our options and the options for logging */
int
skdaemonSetup(
    int                 log_features,
    int                 argc,
    char * const       *argv)
{
    if (skdaemon) {
        /* called mulitple times */
        return -1;
    }

    skdaemon = &daemon_ctx;
    memset(skdaemon, 0, sizeof(skdaemon_ctx_t));

    /* setup the log. have it write the invocation when we open it */
    if (sklogSetup(log_features)) {
        return -1;
    }
    sklogCommandLine(argc, argv);

    /* note whether legacy logging was requested so we know how to
     * print the help for the --pidfile switch */
    if (log_features & SKLOG_FEATURE_LEGACY) {
        skdaemon->legacy_log = 1;
    }

    /* use the same configuration-file setting as the log */
    if (log_features & SKLOG_FEATURE_CONFIG_FILE) {
        skdaemon->config_file = 1;
        return 0;
    }

    return skOptionsRegister(daemonOptions, &daemonOptionsHandler, NULL);
}


/* remove the PID file and shutdown the logger */
void
skdaemonTeardown(
    void)
{
    if (skdaemon == NULL) {
        return;
    }

    sklogTeardown();

    if (skdaemon->pidfile != NULL) {
        (void)unlink(skdaemon->pidfile);
        free(skdaemon->pidfile);
        skdaemon->pidfile = NULL;
    }

    skdaemon = NULL;
}


/* start logging, install the signal handler, fork off the daemon, and
 * write the PID file */
int
skdaemonize(
    volatile int       *shutdown_flag,
    void              (*exit_handler)(void))
{
    char errbuf[512];
    pid_t pid;
    int rv = -1;
    int fd_devnull = -1;

    /* Must call setup before daemonize; make certain we have a
     * shutdown variable */
    assert(skdaemon);
    assert(shutdown_flag);

    /* Store the shutdown flag */
    skdaemon->shutdown_flag = shutdown_flag;

    /* Start the logger */
    if (sklogOpen()) {
        return -1;
    }

    /* Install the signal handler */
    rv = daemonInstallSignalHandler();
    if (rv) {
        goto ERROR;
    }

    /* Fork a child and exit the parent. */
    if ( !skdaemon->no_daemon) {
        if ( !skdaemon->no_chdir) {
            if (chdir("/") == -1) {
                rv = errno;
                PRINT_AND_LOG(("Cannot change directory: %s", strerror(rv)));
                goto ERROR;
            }
        }
        if ((pid = fork()) == -1) {
            rv = errno;
            PRINT_AND_LOG(("Cannot fork for daemon: %s", strerror(rv)));
            goto ERROR;
        } else if (pid != 0) {
            NOTICEMSG("Forked child %ld.  Parent exiting", (long)pid);
            _exit(EXIT_SUCCESS);
        }

        setsid();
    }

    /* Set umask */
    umask(0022);

    /* Install the exit handler; do this after the fork() so the
     * parent does not execute it. */
    if (exit_handler) {
        rv = atexit(exit_handler);
        if (rv == -1) {
            PRINT_AND_LOG(("Unable to register function with atexit(): %s",
                           strerror(rv)));
            goto ERROR;
        }
    }

    /* Write the pidfile when running as a daemon */
    if ( !skdaemon->no_daemon) {
        rv = daemonWritePid();
        if (rv) {
            if (skdaemon->pidfile) {
                PRINT_AND_LOG(("Error creating pid file '%s': %s",
                               skdaemon->pidfile, strerror(rv)));
            } else {
                PRINT_AND_LOG(("Unable to create pid file path: %s",
                               strerror(rv)));
            }
            goto ERROR;
        }

        /* redirect stdin to /dev/null */
        fd_devnull = open("/dev/null", O_RDWR);
        if (fd_devnull == -1) {
            rv = errno;
            PRINT_AND_LOG(("Error opening /dev/null: %s", strerror(rv)));
            goto ERROR;
        }
        if (dup2(fd_devnull, STDIN_FILENO) == -1) {
            rv = errno;
            PRINT_AND_LOG(("Cannot dup(stdin): %s", strerror(rv)));
            goto ERROR;
        }
        close(fd_devnull);

        /* handle redirection of stdout and stderr to the log */
        if (sklogRedirectStandardStreams(errbuf, sizeof(errbuf))) {
            PRINT_AND_LOG(("%s", errbuf));
            goto ERROR;
        }
    }

    /* Send all error messages to the log */
    skAppSetFuncPrintErr(&WARNINGMSG_v);
    skAppSetFuncPrintSyserror(&WARNINGMSG_v);
    skAppSetFuncPrintFatalErr(&CRITMSG);

    /* Success! */
    if (skdaemon->no_daemon) {
        return 1;
    } else {
        return 0;
    }

  ERROR:
    skdaemonTeardown();
    return -1;
}


/*
** Local Variables:
** mode:c
** indent-tabs-mode:nil
** c-basic-offset:4
** End:
*/
