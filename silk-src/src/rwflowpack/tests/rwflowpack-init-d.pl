#! /usr/bin/perl -w
#
#
# RCSIDENT("$SiLK: rwflowpack-init-d.pl 23e62811e29c 2016-11-16 15:30:29Z mthomas $")

use strict;
use SiLKTests;

my $NAME = $0;
$NAME =~ s,.*/,,;

# create our tempdir
my $tmpdir = make_tempdir();

# the daemon being tested and the DAEMON.init.d and DAEMON.conf files
my $DAEMON = 'rwflowpack';

my $daemon_init = "$DAEMON.init.d";
unless (-x $daemon_init) {
    skip_test("Missing start-up script '$daemon_init'");
}
check_daemon_init_program_name($daemon_init, $DAEMON);

my $daemon_src = "$DAEMON.conf";
unless (-f $daemon_src) {
    skip_test("Missing template file '$daemon_src'");
}

my $daemon_conf = "$tmpdir/$DAEMON.conf";

# set environment variable to the directory holding $DAEMON.conf
$ENV{SCRIPT_CONFIG_LOCATION} = $tmpdir;


# directories
my $log_dir = "$tmpdir/log";
my $data_dir = "$tmpdir/data";

# pid file
my $pidfile = $log_dir ."/$DAEMON.pid";

# create the data directory and copy the silk.conf file
if (-f $ENV{SILK_CONFIG_FILE}) {
    mkdir $data_dir, 0700
        or die "$NAME: Unable to create directory '$data_dir': $!\n";
    system "cp", $ENV{SILK_CONFIG_FILE}, "$data_dir/silk.conf";
}

# create directories
for my $d (qw(error log netflow processing)) {
    my $dd = $tmpdir."/".$d;
    mkdir $dd, 0700
        or die "$NAME: Unable to create directory '$dd': $!\n";
}

# receive data to this port and host
my $host = '127.0.0.1';
my $port = get_ephemeral_port($host, 'udp');

# create the packer.lua configuration file
my $packer_lua = "$tmpdir/packer.lua";
open LUACONF_OUT, ">$packer_lua"
    or die "$NAME: Cannot open '$packer_lua': $!\n";
print LUACONF_OUT <<EOF;
if not silk.site.have_site_config() then
  if not silk.site.init_site(nil, nil, true) then
    error("The silk.conf file was not found")
  end
end

local file_info = {
  record_format = silk.file_format_id("FT_RWSPLIT"),
}

-- Given a probe definition and an rwrec, write the rwRec to
-- appropriate outputs.
local function pack_function (probe, rec)
  -- Set flowtype and sensor
  rec.classtype_id = probe.flowtype
  rec.sensor_id = probe.sensor

  -- Write record
  write_rwrec(rec, file_info)
end

local tmpdir = "${tmpdir}"
input = {
    mode = "stream",
    probes = {
        S0 = {
            name = "S0",
            type = "netflow-v5",
            source = {
                listen = "${host}:${port}",
                protocol = "udp",
            },
            packing_function = pack_function,
            vars = {
                flowtype = silk.site.flowtype_id("int2int"),
                sensor = silk.site.sensor_id("S0"),
            },
        },
        S1 = {
            name = "S1",
            type = "netflow-v5",
            source = {
                directory = tmpdir .. "/netflow",
                interval = 5,
                error_directory = tmpdir .. "/error",
            },
            packing_function = pack_function,
            vars = {
                flowtype = silk.site.flowtype_id("ext2ext"),
                sensor = silk.site.sensor_id("S1"),
            },
        },
    },
}
output = {
    mode = "local-storage",
    flush_interval = 10,
    processing = {
        directory = tmpdir .. "/processing",
        error_directory = tmpdir .. "/error",
    },
    root_directory = "${data_dir}",
}
log = {
    directory = "${log_dir}",
    level = "debug",
}
daemon = {
    pid_file = "${pidfile}",
}
EOF
close LUACONF_OUT
    or die "$NAME: Cannot close '$packer_lua': $!\n";

# open the template file for $DAEMON.conf
open SRC, $daemon_src
    or die "$NAME: Cannot open template file '$daemon_src': $!\n";

# create $DAEMON.conf
open CONF, ">$daemon_conf"
    or die "$NAME: Cannot create configuration file '$daemon_conf': $!\n";
while (<SRC>) {
    chomp;
    s/\#.*//;
    next unless /\S/;

    if (/^(BIN_DIR=).*/) {
        my $pwd = `pwd`;
        print CONF $1, $pwd;
        next;
    }
    if (/^(PACKER_LUA=).*/) {
        print CONF $1, $packer_lua, "\n";
        next;
    }
    if (/^(PID_DIR=).*/) {
        print CONF $1, $log_dir, "\n";
        next;
    }
    if (/^(USER=).*/) {
        print CONF $1, '`whoami`', "\n";
        next;
    }

    print CONF $_,"\n";
}
close CONF
    or die "$NAME: Cannot close '$daemon_conf': $!\n";
close SRC;


my $cmd;
my $expected_status;
my $good = 1;
my $pid;

# run "daemon.init.d status"; it should not be running
$expected_status = 'stopped';
$cmd = "/bin/sh -x ./$daemon_init status";
run_command($cmd, \&init_d);
exit 1
    unless $good;

# run "daemon.init.d start"
$expected_status = 'starting';
$cmd = "/bin/sh -x ./$daemon_init start";
run_command($cmd, \&init_d);

# get the PID from the pidfile
if (-f $pidfile) {
    open PID, $pidfile
        or die "$NAME: Cannot open '$pidfile': $!\n";
    $pid = "";
    while (my $x = <PID>) {
        $pid .= $x;
    }
    close PID;
    if ($pid && $pid =~ /^(\d+)/) {
        $pid = $1;
    }
    else {
        $pid = undef;
    }
}

# kill the process if the daemon.init.d script failed
unless ($good) {
    if ($pid) {
        kill_process($pid);
    }
    exit 1;
}

sleep 2;

# run "daemon.init.d stutus"; it should be running
$expected_status = 'running';
$cmd = "/bin/sh -x ./$daemon_init status";
run_command($cmd, \&init_d);

sleep 2;

$expected_status = 'stopping';
$cmd = "/bin/sh -x ./$daemon_init stop";
run_command($cmd, \&init_d);

sleep 2;

# run "daemon.init.d status"; it should not be running
$expected_status = 'stopped';
$cmd = "/bin/sh -x ./$daemon_init status";
run_command($cmd, \&init_d);

if ($pid) {
    if (kill 0, $pid) {
        print STDERR "$NAME: Process $pid is still running\n";
        $good = 0;
        kill_process($pid);
    }
}

check_log_stopped();

exit !$good;


sub init_d
{
    my ($io) = @_;

    while (my $line = <$io>) {
        if ($ENV{SK_TESTS_VERBOSE}) {
            print STDERR $line;
        }
        chomp $line;

        if ($expected_status eq 'running') {
            if ($line =~ /$DAEMON is running with pid (\d+)/) {
                if ($pid) {
                    if ($pid ne $1) {
                        print STDERR ("$NAME: Process ID mismatch:",
                                      " file '$pid' vs script '$1'\n");
                        $good = 0;
                    }
                }
                else {
                    $pid = $1;
                }
            }
            else {
                print STDERR
                    "$NAME: Unexpected $expected_status line '$line'\n";
                $good = 0;
            }
        }
        elsif ($expected_status eq 'starting') {
            unless ($line =~ /Starting $DAEMON:\t\[OK\]/) {
                print STDERR
                    "$NAME: Unexpected $expected_status line '$line'\n";
                $good = 0;
            }
        }
        elsif ($expected_status eq 'stopped') {
            unless ($line =~ /$DAEMON is stopped/) {
                print STDERR
                    "$NAME: Unexpected $expected_status line '$line'\n";
                $good = 0;
            }
        }
        elsif ($expected_status eq 'stopping') {
            unless ($line =~ /Stopping $DAEMON:\t\[OK\]/) {
                print STDERR
                    "$NAME: Unexpected $expected_status line '$line'\n";
                $good = 0;
            }
        }
        else {
            die "$NAME: Unknown \$expected_status '$expected_status'\n";
        }
    }

    close $io;
    if ($expected_status eq 'running') {
        unless ($? == 0) {
            print STDERR "$NAME: Unexpected $expected_status status $?\n";
            $good = 0;
        }
    }
    elsif ($expected_status eq 'starting') {
        unless ($? == 0) {
            print STDERR "$NAME: Unexpected $expected_status status $?\n";
            $good = 0;
        }
    }
    elsif ($expected_status eq 'stopped') {
        unless ($? == (1 << 8)) {
            print STDERR "$NAME: Unexpected $expected_status status $?\n";
            $good = 0;
        }
    }
    elsif ($expected_status eq 'stopping') {
        unless ($? == (1 << 8)) {
            print STDERR "$NAME: Unexpected $expected_status status $?\n";
            $good = 0;
        }
    }

    if ($ENV{SK_TESTS_VERBOSE}) {
        print STDERR "status = $?\n";
    }
}


sub kill_process
{
    my ($pid) = @_;

    if (!kill 0, $pid) {
        return;
    }
    if ($ENV{SK_TESTS_VERBOSE}) {
        print STDERR "Sending SIGTERM to process $pid\n";
    }
    kill 15, $pid;
    sleep 2;
    if (kill 0, $pid) {
        if ($ENV{SK_TESTS_VERBOSE}) {
            print STDERR "Sending SIGKILL to process $pid\n";
        }
        kill 9, $pid;
    }
}


sub check_log_stopped
{
    my @log_files = sort glob("$log_dir/$DAEMON-*.log");
    my $log = pop @log_files;
    open LOG, $log
        or die "$NAME: Cannot open log file '$log': $!\n";
    my $last_line;
    while (defined(my $line = <LOG>)) {
        $last_line = $line;
    }
    close LOG;
    unless (defined $last_line) {
        die "$NAME: Log file '$log' is empty\n";
    }
    if ($last_line !~ /Stopped logging/) {
        die "$NAME: Log file '$log' does not end correctly\n";
    }
}

