#!/usr/bin/env python
#######################################################################
# Copyright (C) 2009-2017 by Carnegie Mellon University.
#
# @OPENSOURCE_LICENSE_START@
# See license information in ../../../LICENSE.txt
# @OPENSOURCE_LICENSE_END@
#
#######################################################################

#######################################################################
# $SiLK: rwflowpack-daemon.py efd886457770 2017-06-21 18:43:23Z mthomas $
#######################################################################
from __future__ import print_function
import optparse
import subprocess
import signal
import string
import tempfile
import re
import time
import traceback
import shutil
import sys
import os
import os.path

from daemon_test import Dirobject, PduSender, TcpSender, TimedReadline, string_write

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

def send_network_data(options):
    # options.pdu is empty or a list of strings of the form
    # "<num-recs>,<address>,<port>"
    split = [x.split(',') for x in options.pdu]
    send_list = [PduSender(int(count), int(port), address=addr, log=log, usleep=500)
                 for [count, addr, port] in split]
    # options.tcp is empty or a list of strings of the form
    # "<filename>,<address>,<port>"
    split = [x.split(',') for x in options.tcp]
    send_list += [TcpSender(open(fname, "rb"), int(port), address=addr, log=log)
                  for [fname, addr, port] in split]
    for x in send_list:
        x.start()
    return send_list

def stop_network_data(send_list):
    for x in send_list:
        x.stop()

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
    parser.add_option("--pdu", action="append", type="string", dest="pdu",
                      default=[])
    parser.add_option("--tcp", action="append", type="string", dest="tcp",
                      default=[])
    parser.add_option("--copy", action="append", type="string", dest="copy",
                      default=[])
    parser.add_option("--move", action="append", type="string", dest="move",
                      default=[])
    parser.add_option("--copy-after", action="append", type="string",
                      dest="copy_after", default=[])
    parser.add_option("--move-after", action="append", type="string",
                      dest="move_after", default=[])
    parser.add_option("--basedir", action="store", type="string",
                      dest="basedir")
    parser.add_option("--daemon-timeout", action="store", type="int",
                      dest="daemon_timeout", default=60)
    parser.add_option("--limit", action="store", type="int", dest="limit")
    parser.add_option("--overwrite-dirs", action="store_true", dest="overwrite",
                      default=False)
    (options, args) = parser.parse_args()
    VERBOSE = True

    # Create the dirs
    dirobj = Dirobject(overwrite=options.overwrite, basedir=options.basedir)
    dirobj.dirs = ['incoming', 'archive', 'error', 'processing',
                   'incremental', 'root', 'log', 'incoming2',
                   'incoming3', 'incoming4']
    dirobj.create_dirs()

    # Make the log file
    logfile = open(dirobj.get_path('log', 'rwflowpack-daemon.log'), 'wb', 0)

    progname = os.environ.get("RWFLOWPACK", os.path.join('.', 'rwflowpack'))
    args = progname.split() + args

    # Set up state variables
    count = 0
    clean = False
    send_list = None
    regexp = re.compile(": /[^:].*: (?P<recs>[0-9]+) recs")
    closing = re.compile("Stopped logging")
    started = re.compile("Starting flush timer")
    packing_logic_fail = "Unable to open packing-logic"
    packing_logic = re.compile("(Using packing logic|%s)" % packing_logic_fail)

    # Copy or move data
    copy_files(dirobj, options.copy)
    move_files(dirobj, options.move)

    # Note the time
    starttime = time.time()
    # The (possibly future) time when a sigterm should be sent
    future_sigtermtime = None
    # Time when SIGTERM is sent
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
            if time.time() - starttime > options.daemon_timeout and not future_sigtermtime:
                log("Timed out")
                future_sigtermtime = time.time()

            # Match record counts
            match = regexp.search(line)
            if match:
                num = int(match.group('recs'))
                if num > 0:
                    count += num
                    # Reset the timer if we are still receiving data
                    starttime = time.time()
                    if options.limit and count >= options.limit and not future_sigtermtime:
                        log("Reached limit")
                        future_sigtermtime = time.time() + 2

            # Check whether packing-logic was loaded
            if packing_logic:
                match = packing_logic.search(line)
                if match:
                    if match.group(0) == packing_logic_fail:
                        log("Skipping test: Unable to load packing logic")
                        sys.exit(77)
                    packing_logic = None

            # Check for starting up network data or after-data
            if started and started.search(line):
                if send_list is None:
                    send_list = send_network_data(options)
                copy_files(dirobj, options.copy_after)
                move_files(dirobj, options.move_after)
                started = None

            # Check for clean shutdown
            if closing.search(line):
                clean = True

            # Check for whether future-dated shutdowntime has arrived
            if future_sigtermtime and not shutdowntime and future_sigtermtime <= time.time():
                shutdowntime = time.time()
                log("Sending SIGTERM")
                term = True
                try:
                    os.kill(proc.pid, signal.SIGTERM)
                except OSError:
                    pass

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

    # Print record count to both stdout and to the log
    print("Record count:", count)
    base_log("Record count:", count)
    if options.limit and options.limit != count:
        print(("ERROR: expecting %s records, got %s records" %
               (options.limit, count)))
        base_log(("ERROR: expecting %s records, got %s records" %
                  (options.limit, count)))

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
