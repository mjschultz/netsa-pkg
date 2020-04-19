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
# $SiLK: flowcap-daemon.py ef14e54179be 2020-04-14 21:57:45Z mthomas $
#######################################################################
from __future__ import print_function
import optparse
import subprocess
import signal
import re
import time
import traceback
import sys
import os
import os.path

from daemon_test import Dirobject, PduSender, TcpSender, TimedReadline, UdpSender, string_write

VERBOSE = False
logfile = None

if sys.version_info[0] >= 3:
    coding = {"encoding": "ascii"}
else:
    coding = {}

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
    send_list = [PduSender(int(count), int(port), address=addr, log=log)
                 for [count, addr, port] in split]
    # options.tcp is empty or a list of strings of the form
    # "<filename>,<address>,<port>"
    split = [x.split(',') for x in options.tcp]
    send_list += [TcpSender(open(fname, "rb"), int(port), address=addr, log=log)
                  for [fname, addr, port] in split]
    # options.udp is empty or a list of strings of the form
    # "<filename>,<address>,<port>"
    split = [x.split(',') for x in options.udp]
    send_list += [UdpSender(open(fname, "rb"), int(port), address=addr, log=log)
                  for [fname, addr, port] in split]
    for x in send_list:
        x.start()
    return send_list

def stop_network_data(send_list):
    for x in send_list:
        x.stop()

def main():
    global VERBOSE
    global logfile
    parser = optparse.OptionParser()
    parser.add_option("--pdu", action="append", type="string", dest="pdu",
                      default=[])
    parser.add_option("--tcp", action="append", type="string", dest="tcp",
                      default=[])
    # reads an IPFIX file and sends it over UDP
    parser.add_option("--udp", action="append", type="string", dest="udp",
                      default=[])
    parser.add_option("--basedir", action="store", type="string",
                      dest="basedir")
    parser.add_option("--daemon-timeout", action="store", type="int",
                      dest="daemon_timeout", default=60)
    parser.add_option("--limit", action="store", type="int", dest="limit")
    parser.add_option("--timeout", action="store", type="int",
                      dest="timeout", default=10)
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
    dirobj.dirs = ['destination', 'log']
    dirobj.create_dirs()

    # Make the log file
    logfile = open(dirobj.get_path('log', 'flowcap-daemon.log'), 'wb', 0)

    # Generate the subprocess arguments
    args += ['--timeout', str(options.timeout),
             '--log-dest', 'stderr',
             '--log-level', options.log_level,
             '--no-daemon']
    args += ['--destination-directory', dirobj.dirname['destination']]

    progname = os.environ.get("FLOWCAP", os.path.join('.', 'flowcap'))
    args = progname.split() + args

    # Set up state variables
    count = 0
    clean = False
    term = False
    send_list = None
    regexp = re.compile("Closing file .* seconds, (?P<recs>[0-9]+) records,")
    closing = re.compile("Stopped logging")
    started = re.compile("'.+': Reader thread started")

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
                num = int(match.group('recs'))
                if num > 0:
                    count += num
                    # Reset the timer if we are still receiving data
                    starttime = time.time()
                    if options.limit and count >= options.limit and not term:
                        shutdowntime = time.time()
                        log("Reached limit")
                        log("Sending SIGTERM")
                        term = True
                        try:
                            os.kill(proc.pid, signal.SIGTERM)
                        except OSError:
                            pass

            # check for starting up network data
            if started:
                match = started.search(line)
                if match and send_list is None:
                    send_list = send_network_data(options)
                    started = None

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
