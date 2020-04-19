/*
** Copyright (C) 2006-2020 by Carnegie Mellon University.
**
** @OPENSOURCE_LICENSE_START@
** See license information in ../../LICENSE.txt
** @OPENSOURCE_LICENSE_END@
*/

/*
**  skdaemon.h
**
**    Setup logging, create a pid file, install a signal handler and
**    fork an application in order to run it as a daemon.
**
*/
#ifndef _SKDAEMON_H
#define _SKDAEMON_H
#ifdef __cplusplus
extern "C" {
#endif

#include <silk/silk.h>

RCSIDENTVAR(rcsID_SKDAEMON_H, "$SiLK: skdaemon.h ef14e54179be 2020-04-14 21:57:45Z mthomas $");

/**
 *  @file
 *
 *    Functions for daemonizing a SiLK application.
 *
 *    This file is part of libsilk.
 */


/**
 *    By default, skdaemonize() will cause the application to fork,
 *    though the user's --no-daemon option can override that behavior.
 *    When this function is called, the application will not fork,
 *    regardless of the user's --no-daemon option.
 */
void
skdaemonDontFork(
    void);


/**
 *    Write the usage strings for options that skdaemonSetup() added
 *    to the global list of options.
 */
void
skdaemonOptionsUsage(
    FILE               *fh);


/**
 *    Verify that all the required options were specified and that
 *    their values are valid.
 */
int
skdaemonOptionsVerify(
    void);


/**
 *    Register the options used when running as a daemon.  The
 *    'log_features' value will be passed to sklogSetup().
 *
 *    The 'argc' and 'argv' contain the commmand line used to start
 *    the program.  They will be written to the log.
 */
int
skdaemonSetup(
    int                 log_features,
    int                 argc,
    char   * const     *argv);


/**
 *    Stop logging and remove the PID file.
 */
void
skdaemonTeardown(
    void);


/**
 *    In the general case: start the logger, fork the application,
 *    register the specified 'exit_handler', create a pid file, and
 *    install a signal handler in order to run an application as a
 *    daemon.  When the signal handler is called, it will set
 *    'shutdown_flag' to a non-zero value.
 *
 *    The application will not fork if the user requested --no-daemon.
 *
 *    Returns 0 if the application forked and everything succeeds.
 *    Returns 1 if everything succeeds but the application did not
 *    fork.  Returns -1 to indicate an error.
 */
int
skdaemonize(
    volatile int       *shutdown_flag,
    void                (*exit_handler)(void));

#ifdef __cplusplus
}
#endif
#endif /* _SKDAEMON_H */

/*
** Local Variables:
** mode:c
** indent-tabs-mode:nil
** c-basic-offset:4
** End:
*/
