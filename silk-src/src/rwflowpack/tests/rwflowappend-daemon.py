#!/usr/bin/env python
#######################################################################
# Copyright (C) 2009-2020 by Carnegie Mellon University.
#
# @OPENSOURCE_LICENSE_START@
# See license information in ../../../LICENSE.txt
# @OPENSOURCE_LICENSE_END@
#
#######################################################################

#######################################################################
# $SiLK: rwflowappend-daemon.py ef14e54179be 2020-04-14 21:57:45Z mthomas $
#######################################################################
from __future__ import print_function
import optparse
import subprocess
import signal
import re
import time
import traceback
import shutil
import sys
import os
import os.path

from daemon_test import Dirobject, TimedReadline, string_write

VERBOSE = False
logfile = None

def base_log(*args):
    global VERBOSE
    global logfile

    out = ' '.join(str(x) for x in args)
    if out[-1] != '\n':
        out += '\n'
    string_write(logfile, out)
    if VERBOSE:
        sys.stderr.write(out)

def log(*args):
    out = "%s: %s:" % (time.asctime(), os.path.basename(sys.argv[0]))
    base_log(out, *args)

def _copy_files(dirobj, spec, fn):
    split = [x.split(':') for x in spec]
    for f, d in split:
        if d not in dirobj.dirs:
            raise RuntimeError("Unknown destination directory")
        fn(f, dirobj.dirname[d])

def copy_files(dirobj, spec):
    return _copy_files(dirobj, spec, shutil.copy)

def move_files(dirobj, spec):
    return _copy_files(dirobj, spec, shutil.move)

def main():
    global VERBOSE
    global logfile
    parser = optparse.OptionParser()
    parser.add_option("--copy", action="append", type="string", dest="copy",
                      default=[])
    parser.add_option("--move", action="append", type="string", dest="move",
                      default=[])
    parser.add_option("--file-limit", action="store", type="int",
                      dest="file_limit", default=0)
    parser.add_option("--no-archive", action="store_true", dest="no_archvie",
                      default=False)
    parser.add_option("--basedir", action="store", type="string",
                      dest="basedir")
    parser.add_option("--daemon-timeout", action="store", type="int",
                      dest="daemon_timeout", default=60)
    parser.add_option("--verbose", action="store_true", dest="verbose",
                      default=False)
    parser.add_option("--overwrite-dirs", action="store_true", dest="overwrite",
                      default=False)
    parser.add_option("--log-level", action="store", type="string",
                      dest="log_level", default="info")
    (options, args) = parser.parse_args()
    VERBOSE = options.verbose

    # Create the dirs
    dirobj = Dirobject(overwrite=options.overwrite, basedir=options.basedir)
    dirobj.dirs = ['incoming', 'archive', 'error', 'root', 'log']
    dirobj.create_dirs()

    # Make the log file
    logfile = open(dirobj.get_path('log', 'rwflowappend-daemon.log'), 'wb', 0)

    # Generate the subprocess arguments
    args += ['--log-dest', 'stderr',
             '--log-level', options.log_level,
             '--no-daemon',
             '--root-directory', dirobj.dirname['root'],
             '--incoming-directory', dirobj.dirname['incoming'],
             '--error-directory', dirobj.dirname['error']]

    if not options.no_archvie:
        args += ['--archive-directory', dirobj.dirname['archive']]


    progname = os.environ.get("RWFLOWAPPEND",
                              os.path.join('.', 'rwflowappend'))
    args = progname.split() + args

    # Set up state variables
    count = 0
    clean = False
    term = False
    send_list = None
    limit = len(options.copy) + len(options.move) + options.file_limit
    regexp = re.compile("APPEND OK")
    closing = re.compile("Stopped logging")

    if 0 == limit:
        print("ERROR: File limit is zero")
        base_log("ERROR: File limit is zero")
        sys.exit(1)

    # Copy or move data
    copy_files(dirobj, options.copy)
    move_files(dirobj, options.move)

    # Note the time
    starttime = time.time()
    shutdowntime = None

    # Start the process
    log("Running", "'%s'" % "' '".join(args))
    proc = subprocess.Popen(args, stderr=subprocess.PIPE)
    line_reader = TimedReadline(proc.stderr.fileno())

    try:
        # Read the output data
        line = line_reader(1)
        while line is not None:
            if line:
                base_log(line)

            # Check for timeout
            if time.time() - starttime > options.daemon_timeout and not term:
                shutdowntime = time.time()
                log("Timed out")
                log("Sending SIGTERM")
                term = True
                try:
                    os.kill(proc.pid, signal.SIGTERM)
                except OSError:
                    pass

            # Match record counts
            match = regexp.search(line)
            if match:
                count += 1
                # Reset the timer if we are still receiving data
                starttime = time.time()
                if count >= limit and not term:
                    shutdowntime = time.time()
                    log("Reached limit")
                    log("Sending SIGTERM")
                    term = True
                    try:
                        os.kill(proc.pid, signal.SIGTERM)
                    except OSError:
                        pass

            # Check for clean shutdown
            match = closing.search(line)
            if match:
                clean = True

            # Check for timeout on SIGTERM shutdown
            if shutdowntime and time.time() - shutdowntime > 15:
                log("Timeout on SIGTERM.  Sending SIGKILL")
                shutdowntime = None
                try:
                    os.kill(proc.pid, signal.SIGKILL)
                except OSError:
                    pass

            # Get next line
            line = line_reader(1)

    finally:
        # Stop sending network data
        if send_list:
            stop_network_data(send_list)

        # Sleep briefly before polling for exit.
        time.sleep(1)
        proc.poll()
        if proc.returncode is None:
            log("Daemon has not exited.  Sending SIGKILL")
            try:
                os.kill(proc.pid, signal.SIGKILL)
            except OSError:
                pass

    # Print record count
    print("File count:", count)
    base_log("File count:", count)
    if limit != count:
        print(("ERROR: expecting %s files, got %s files" %
               (limit, count)))
        base_log(("ERROR: expecting %s files, got %s files" %
                  (limit, count)))

    # Check for clean exit
    if not clean:
        print("ERROR: shutdown was not clean")
        base_log("ERROR: shutdown was not clean")

    # Exit with the process's error code
    if proc.returncode is None:
        log("Exiting: Deamon did not appear to terminate normally")
        sys.exit(1)
    else:
        log("Exiting: Daemon returned", proc.returncode)
        sys.exit(proc.returncode)


if __name__ == "__main__":
    try:
        main()
    except SystemExit:
        raise
    except:
        traceback.print_exc(file=sys.stdout)
        if logfile:
            traceback.print_exc(file=logfile)
