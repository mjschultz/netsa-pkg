#######################################################################
# Copyright (C) 2009-2017 by Carnegie Mellon University.
#
# @OPENSOURCE_LICENSE_START@
# See license information in ../LICENSE.txt
# @OPENSOURCE_LICENSE_END@
#
#######################################################################

#######################################################################
# $SiLK: daemon_test.py 275df62a2e41 2017-01-05 17:30:40Z mthomas $
#######################################################################
from __future__ import print_function
import numbers
import os
import os.path
import select
import shutil
import signal
import socket
import subprocess
import sys
import tempfile
import threading

from config_vars import config_vars


#V5PDU_LEN  = 1464
TCPBUF     = 2048
LINEBUF    = 1024

if sys.version_info[0] >= 3:
    coding = {"encoding": "latin_1"}
else:
    coding = {}

def string_write(f, s):
    return f.write(bytes(s, **coding))

class TimedReadline(object):
    def __init__(self, fd):
        self.buf = ""
        if isinstance(fd, numbers.Integral):
            self.fd = fd
        else:
            self.fd = fd.fileno()

    def __call__(self, timeout):
        while True:
            x = self.buf.find('\n')
            if x >= 0:
                retval = self.buf[:x+1]
                self.buf = self.buf[x+1:]
                return retval
            (r, w, x) = select.select([self.fd], [], [], timeout)
            if r:
                more = os.read(self.fd, LINEBUF)
                if more:
                    self.buf += more.decode('latin_1')
                else:
                    return None
            else:
                return ""

class Dirobject(object):

    def __init__(self, overwrite=False, basedir=None):
        self.dirs = list()
        self.dirname = dict()
        self.basedir = basedir
        self.dirs_created = False
        self.overwrite = overwrite

    def create_basedir(self):
        if self.basedir:
            if not os.path.exists(self.basedir):
                os.mkdir(self.basedir)
        else:
            self.basedir = tempfile.mkdtemp()

    def remove_basedir(self):
        if self.basedir and self.dirs_created:
            shutil.rmtree(self.basedir)
            self.dirs_created = False

    def create_dirs(self):
        if not self.dirs_created:
            self.create_basedir()
            for name in self.dirs:
                self.dirname[name] = os.path.abspath(
                    os.path.join(self.basedir, name))
                if os.path.exists(self.dirname[name]):
                    if self.overwrite:
                        shutil.rmtree(self.dirname[name])
                        os.mkdir(self.dirname[name])
                else:
                    os.mkdir(self.dirname[name])
            self.dirs_created = True

    def get_path(self, name, path):
        return os.path.join(self.dirname[name], path)


class PduSender(object):
    def __init__(self, max_recs, port, log, address="localhost"):
        self._port = port
        self._max_recs = max_recs
        self._log = log
        self._address = ("[%s]" % address)
        self.process = None

    def start(self):
        prog = os.path.join(os.environ["top_srcdir"], "tests", "make-data.pl")
        args = [config_vars["PERL"], prog,
                "--pdu-network", self._address + ":" + str(self._port),
                "--max-records", str(self._max_recs)]
        self._log("Starting: %s" % args)
        self.process = subprocess.Popen(args)
        return self.process

    def stop(self):
        if self.process is None:
            return None
        self.process.poll()
        if self.process.returncode is None:
            try:
                os.kill(self.process.pid, signal.SIGTERM)
            except OSError:
                pass
        return self.process.returncode


class TcpSender(object):
    def __init__(self, file, port, log, address="localhost"):
        self._file = file
        self._port = port
        self._log = log
        self._address = address
        self._running = False

    def start(self):
        thread = threading.Thread(target = self.go)
        thread.daemon = True
        thread.start()

    def go(self):
        self._running = True
        sock = None
        # Try each address until we connect to one; no need to report
        # errors here
        for res in socket.getaddrinfo(self._address, self._port,
                                      socket.AF_UNSPEC, socket.SOCK_STREAM):
            af, socktype, proto, canonname, sa = res
            try:
                sock = socket.socket(af, socktype, proto)
            except socket.error:
                sock = None
                continue
            try:
                sock.connect(sa)
            except socket.error:
                sock.close()
                sock = None
                continue
            break
        if sock is None:
            self._log("Could not open connection to [%s]:%d" %
                      (self._address, self._port))
            sys.exit(1)
        self._log("Connected to [%s]:%d" % (self._address, self._port))
        sock.settimeout(1)
        # Send the data
        while self._running:
            pdu = self._file.read(TCPBUF)
            if not pdu:
                self._running = False
                continue
            count = len(pdu)
            while self._running and count:
                try:
                    num_sent = sock.send(pdu)
                    pdu = pdu[num_sent:]
                    count -= num_sent
                except socket.timeout:
                    pass
                except socket.error as msg:
                    if isinstance(msg, tuple):
                        errmsg = msg[1]
                    else:
                        errmsg = msg
                    self._log("Error sending to [%s]:%d: %s" %
                              (self._address, self._port, errmsg))
                    self._running = False
        # Done
        sock.close()

    def stop(self):
        self._running = False

def get_ephemeral_port():
    sock = socket.socket()
    sock.bind(("", 0))
    (addr, port) = sock.getsockname()
    sock.close()
    return port
