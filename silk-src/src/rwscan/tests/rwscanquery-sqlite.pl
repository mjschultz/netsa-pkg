#! /usr/bin/perl -w
#
#
# RCSIDENT("$SiLK: rwscanquery-sqlite.pl 23e62811e29c 2016-11-16 15:30:29Z mthomas $")

use strict;
use SiLKTests;
use File::Find;

my $NAME = $0;
$NAME =~ s,.*/,,;

my $rwscanquery = check_silk_app('rwscanquery');

# find the apps we need.  this will exit 77 if they're not available
my $rwcut = check_silk_app('rwcut');
my $rwfilter = check_silk_app('rwfilter');
my $rwscan = check_silk_app('rwscan');
my $rwset = check_silk_app('rwset');
my $rwsetbuild = check_silk_app('rwsetbuild');
my $rwsetcat = check_silk_app('rwsetcat');
my $rwsort = check_silk_app('rwsort');

# find the data files we use as sources, or exit 77
my %file;
$file{data} = get_data_or_exit77('data');
$file{scandata} = get_data_or_exit77('scandata');

# the SQLite command
my $sqlite = 'sqlite3';

# skip this test if SQLite is not on the user's path
if (system "$sqlite -version >/dev/null 2>&1") {
    skip_test("$sqlite not available");
}

# skip this test if the DBD::SQLite is not available
unless (eval "require DBD::SQLite;") {
    skip_test("DBD::SQLite module not available");
}

# update path so rwscanquery can find the tools it executes
$ENV{RWFILTER}   = $rwfilter;
$ENV{RWSET}      = $rwset;
$ENV{RWSETBUILD} = $rwsetbuild;
$ENV{RWSETCAT}   = $rwsetcat;

# include those envars in the log
push @SiLKTests::DUMP_ENVVARS, qw(RWSCANRC RWFILTER RWSET RWSETBUILD RWSETCAT);

# create our tempdir
my $tmpdir = make_tempdir();

# result of running MD5 on /dev/null
my $empty_md5 = 'd41d8cd98f00b204e9800998ecf8427e';

# command used to get consistent flow output
my @clean_flows = ($rwcut, '--ipv6-policy=ignore', '--fields=1-6,8-15');

my ($cmd, $md5);

#### CREATE IPSETS

# IPs of scanners we look for; these get used in the --saddress or
# --sipset options to rwscanquery
my $scanner_set = "$tmpdir/scanner_set.set";

my $scanner_ips1 = '10.128.0.0/11';
my $scanner_ips2 = join ",", ('10.144.0.0',
                              '10.128.0.0/12',
                              '10.144.0.1-10.159.255.255',
    );
$cmd = "echo $scanner_ips1 | $rwsetbuild - $scanner_set";
check_exit_status($cmd)
    or die "$NAME Error while running '$cmd'\n";


# IPs of destinations we want to see whether the scanners hit
my $got_scanned_set = "$tmpdir/got_scanned_set.set";

my $got_scanned_ips1 = '192.168.196.0/22';
my $got_scanned_ips2 = '192.168.196-199.x';

$cmd = "echo $got_scanned_ips1 | $rwsetbuild - $got_scanned_set";
check_exit_status($cmd)
    or die "$NAME Error while running '$cmd'\n";


#### CREATE AND POPULATE THE SCAN DATABASE

my $valid_incoming_flows = "$tmpdir/incoming.rwf";
my $valid_destination_ips = "$tmpdir/trw.set";

my $rwscan_result = "$tmpdir/scans.txt";

my $db = "$tmpdir/scans.db";

# these next two commands are the ones used by tests/rwscan-hybrid.pl
$cmd = join " ", ($rwfilter,
                  '--daddress=192.168.0.0/16',
                  "--pass=$valid_incoming_flows",
                  "--pass=-",
                  $file{data},
                  "|",
                  $rwset,
                  "--dip=$valid_destination_ips",
    );
check_exit_status($cmd)
    or die "$NAME Error while running '$cmd'\n";

$cmd = join " ", ($rwsort,
                  '--fields=sip,proto,dip',
                  $valid_incoming_flows,
                  $file{scandata},
                  "|",
                  $rwscan,
                  "--trw-sip-set=$valid_destination_ips",
                  "--scandb",
                  "--output-path=$rwscan_result",
    );
check_md5_output($empty_md5, $cmd);

$md5 = '65555955c145c255f7dc1fed90973505';
check_md5_file($md5, $rwscan_result);

open SCANS, $rwscan_result
    or die "$NAME: Failed to open '$rwscan_result': $!\n";
open SQLITE, "| $sqlite $db"
    or die "$NAME: Failed to run '$sqlite $db': $!\n";

print SQLITE <<'EOF;';
CREATE TABLE scans (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    sip         INTEGER             NOT NULL,
    proto       SMALLINT            NOT NULL,
    stime       TIMESTAMP           NOT NULL,
    etime       TIMESTAMP           NOT NULL,
    flows       INTEGER             NOT NULL,
    packets     INTEGER             NOT NULL,
    bytes       INTEGER             NOT NULL,
    scan_model  INTEGER             NOT NULL,
    scan_prob   FLOAT               NOT NULL
    );
CREATE INDEX scans_stime_idx ON scans (stime);
CREATE INDEX scans_etime_idx ON scans (etime);
EOF;

while (<SCANS>) {
    chomp;
    $_ = join ',', ('NULL', map {/\s/ ? qq("$_") : $_} split /\|/);
    print SQLITE 'INSERT INTO scans VALUES (', $_, ");\n";
}

close SCANS;
close SQLITE
    or die "$NAME: Failed to run '$sqlite $db': $!\n";


#### CREATE THE CONFIGURATION FILE

$ENV{RWSCANRC} = "$tmpdir/rwscanrc";
open RWSCANRC, ">$ENV{RWSCANRC}"
    or die "$NAME: Cannot open $ENV{RWSCANRC}: $!\n";
print RWSCANRC <<"EOF;";
db_driver=sqlite
db_userid=
db_password=
db_instance=$db
rw_out_class=all
rw_in_type=in,inweb,inicmp
rw_out_type=out,outweb,outicmp
EOF;
close RWSCANRC
    or die "$NAME: Cannot save $ENV{RWSCANRC}: $!\n";


#### TEST THE EXPORT REPORT

$cmd = join " ", ($rwscanquery,
                  '--report=export',
                  '--start-date=2009/02/11',
                  '--end-date=2009/02/15',
    );
$md5 = 'b9e9fc973a34d3745673c4b378f92d03';
scanquery_md5($md5, $cmd);

$md5 = 'b9e9fc973a34d3745673c4b378f92d03';
scanquery_md5($md5, $cmd);

# The rows printed by export include the 'id', so we cannot do a
# direct comparison with the output from rwscan.  To compare, the MD5s
# of the following should be the same:
#
#  sort $tmpdir/scans.txt | perl -lpwe 's/0+$//; s/\.$//;' | md5sum
#
#  rwscanquery --report=export | perl -lpwe 's/^\d+\|//;' | sort | md5sum
#


#### TEST THE VOLUME REPORT

$cmd = join " ", ($rwscanquery,
                  '--show-header',
                  '--columnar',
                  '--report=volume',
                  '--start-date=2009/02/11',
                  '--end-date=2009/02/15',
    );
$md5 = 'a64a5b16c025d07d185e677861063479';
scanquery_md5($md5, $cmd);


#### TEST THE STANDARD REPORT

$cmd = join " ", ($rwscanquery,
                  '--start-date=2009/02/11',
                  '--end-date=2009/02/15',
                  '|',
                  'sort',
    );
$md5 = 'd8b2ffecf4ac62a7fcaee09831ba6839';
scanquery_md5($md5, $cmd);

$cmd = join " ", ($rwscanquery,
                  "--saddress=$scanner_ips1",
                  '--report=standard',
                  '--start-date=2009/02/11',
                  '--end-date=2009/02/15',
                  '|',
                  'sort',
    );
$md5 = '48af6109c3b292cb4953d3dcc264848c';
scanquery_md5($md5, $cmd);

$cmd = join " ", ($rwscanquery,
                  "--saddress=$scanner_ips2",
                  '--start-date=2009/02/11',
                  '--end-date=2009/02/15',
                  '|',
                  'sort',
    );
$md5 = '48af6109c3b292cb4953d3dcc264848c';
scanquery_md5($md5, $cmd);

$cmd = join " ", ($rwscanquery,
                  "--sipset=$scanner_set",
                  '--start-date=2009/02/11',
                  '--end-date=2009/02/15',
                  '|',
                  'sort',
    );
$md5 = '48af6109c3b292cb4953d3dcc264848c';
scanquery_md5($md5, $cmd);

$cmd = join " ", ($rwscanquery,
                  '--start-date=2009/02/12',
                  '|',
                  'sort',
    );
$md5 = '7317d165cb5022e900ff27c3dab070b1';
scanquery_md5($md5, $cmd);

$cmd = join " ", ($rwscanquery,
                  '--start-date=2009/02/13:02',
                  '--end-date=2009/02/13:12',
                  '|',
                  'sort',
    );
$md5 = '5f37f643ad8b739fdeb48a131cf580ff';
scanquery_md5($md5, $cmd);

$cmd = join " ", ($rwscanquery,
                  '--start-date=2009/02/14:23',
                  '|',
                  'sort',
    );
$md5 = 'abecfd1ebaf36bc489b63afffb53ad27';
scanquery_md5($md5, $cmd);


#### TEST A BASIC SCANSET REPORT

$cmd = join " ", ($rwscanquery,
                  '--report=scanset',
                  '--start-date=2009/02/11',
                  '--end-date=2009/02/15',
                  '|',
                  $rwsetcat,
    );
$md5 = 'f10b8782232256402c47e57e3e4a0a56';
scanquery_md5($md5, $cmd);

my $scanset_out = "$tmpdir/scanset_out.set";
$cmd = join " ", ($rwscanquery,
                  '--report=scanset',
                  '--start-date=2009/02/11',
                  '--end-date=2009/02/15',
                  "--output-path='$scanset_out'",
                  '&&',
                  "$rwsetcat '$scanset_out'",
    );
$md5 = 'f10b8782232256402c47e57e3e4a0a56';
scanquery_md5($md5, $cmd);

# Remaining reports require a flow repository
create_data_repo();

$cmd = join " ", ($rwscanquery,
                  "--daddress=$got_scanned_ips1",
                  '--report=scanset',
                  '--start-date=2009/02/11',
                  '--end-date=2009/02/15',
                  '|',
                  $rwsetcat,
    );
$md5 = '5db0241b0abec5e2bb41e709e110f96c';
scanquery_md5($md5, $cmd);

$cmd = join " ", ($rwscanquery,
                  "--daddress=$got_scanned_ips2",
                  '--report=scanset',
                  '--start-date=2009/02/11',
                  '--end-date=2009/02/15',
                  '|',
                  $rwsetcat,
    );
$md5 = '5db0241b0abec5e2bb41e709e110f96c';
scanquery_md5($md5, $cmd);

my $scanset_dip_out = "$tmpdir/scanscan_dip_out.set";
$cmd = join " ", ($rwscanquery,
                  "--dipset=$got_scanned_set",
                  '--report=scanset',
                  '--start-date=2009/02/11',
                  '--end-date=2009/02/15',
                  "--output-path='$scanset_dip_out'",
                  '&&',
                  "$rwsetcat '$scanset_dip_out'",
    );
$md5 = '5db0241b0abec5e2bb41e709e110f96c';
scanquery_md5($md5, $cmd);


# TEST A SCANFLOWS REPORT

$cmd = join " ", ($rwscanquery,
                  '--report=scanflows',
                  '--start-date=2009/02/11',
                  '--end-date=2009/02/15',
                  '|',
                  @clean_flows,
    );
$md5 = '24be386188897452d6f3ff84a5303dc6';
scanquery_md5($md5, $cmd);

$cmd = join " ", ($rwscanquery,
                  "--saddress=$scanner_ips2",
                  "--daddress=$got_scanned_ips1",
                  '--report=scanflows',
                  '--start-date=2009/02/11',
                  '--end-date=2009/02/15',
                  '|',
                  @clean_flows,
    );
$md5 = 'c74589ec06492d5d9a9f656981b9c3d6';
scanquery_md5($md5, $cmd);

$cmd = join " ", ($rwscanquery,
                  "--sipset=$scanner_set",
                  "--daddress=$got_scanned_ips2",
                  '--report=scanflows',
                  '--start-date=2009/02/11',
                  '--end-date=2009/02/15',
                  '|',
                  @clean_flows,
    );
$md5 = 'c74589ec06492d5d9a9f656981b9c3d6';
scanquery_md5($md5, $cmd);

my $scanflows_out = "$tmpdir/scanflows.rwf";
$cmd = join " ", ($rwscanquery,
                  "--saddress=$scanner_ips1",
                  "--dipset=$got_scanned_set",
                  '--report=scanflows',
                  '--start-date=2009/02/11',
                  '--end-date=2009/02/15',
                  "--output-path='$scanflows_out'",
                  '&&',
                  @clean_flows, "'$scanflows_out'",
    );
$md5 = 'c74589ec06492d5d9a9f656981b9c3d6';
scanquery_md5($md5, $cmd);


#### TEST A RESPFLOWS QUERY

$cmd = join " ", ($rwscanquery,
                  '--report=respflows',
                  '--start-date=2009/02/11',
                  '--end-date=2009/02/15',
                  '|',
                  @clean_flows,
    );
$md5 = '1dd314f812d5b32cd6e02bdeb60ad374';
scanquery_md5($md5, $cmd);

$cmd = join " ", ($rwscanquery,
                  "--sipset=$scanner_set",
                  "--daddress=$got_scanned_ips1",
                  '--report=respflows',
                  '--start-date=2009/02/11',
                  '--end-date=2009/02/15',
                  '|',
                  @clean_flows,
    );
$md5 = 'c81571a5bd4a60551f3874cef4be16f8';
scanquery_md5($md5, $cmd);

$cmd = join " ", ($rwscanquery,
                  "--saddress=$scanner_ips2",
                  "--daddress=$got_scanned_ips2",
                  '--report=respflows',
                  '--start-date=2009/02/11',
                  '--end-date=2009/02/15',
                  '|',
                  @clean_flows,
    );
$md5 = 'c81571a5bd4a60551f3874cef4be16f8';
scanquery_md5($md5, $cmd);

my $respflows_out = "$tmpdir/respflows.rwf";
$cmd = join " ", ($rwscanquery,
                  "--saddress=$scanner_ips1",
                  "--dipset=$got_scanned_set",
                  '--report=respflows',
                  '--start-date=2009/02/11',
                  '--end-date=2009/02/15',
                  "--output-path='$respflows_out'",
                  '&&',
                  @clean_flows, "'$respflows_out'",
    );
$md5 = 'c81571a5bd4a60551f3874cef4be16f8';
scanquery_md5($md5, $cmd);

exit 0;


##########################################################################

sub scanquery_md5
{
    my ($md5, $cmd) = @_;

    if ($ENV{SK_TESTS_CHECK_MAKEFILE} || $ENV{SK_TESTS_MAKEFILE}) {
        my $new_md5;
        compute_md5(\$new_md5, $cmd);
        if ($md5 ne $new_md5) {
            print STDERR "OLD $md5 != NEW $new_md5 <== $cmd\n";
        }
    }
    else {
        check_md5_output($md5, $cmd);
    }
}


sub create_data_repo
{
    my $root_ipfix = "$tmpdir/root-ipfix";
    my $root_dir   = "$tmpdir/root";

    my $rwfileinfo = check_silk_app('rwfileinfo');
    my $rwipfix2silk = check_silk_app('rwipfix2silk');

    create_ipfix_repo($root_ipfix);

    mkdir $root_dir
        or die "$NAME: Cannot create directory '$root_dir': $!\n";

    my @files;
    my @dirs;

    # convert every IPFIX file in ${root_ipfix} to a SiLK Flow File
    File::Find::find(sub {if (-f $_) { push @files, $File::Find::name; }
                          elsif (-d _) { push @dirs, $File::Find::name; }},
                     $root_ipfix);
    for my $src (@dirs) {
        my $dest = $src;
        $dest =~ s|^\Q$root_ipfix\E|$root_dir|;
        unless (-d $dest) {
            mkdir $dest
                or die "$NAME: Cannot mkdir '$dest': $!\n";
        }
    }
    for my $src (@files) {
        my $dest = $src;
        $dest =~ s|^\Q$root_ipfix\E|$root_dir|;
        my $format = `$rwfileinfo --no-titles --fields=format $src 2>&1`;
        if ($format !~ /IPFIX/) {
            rename $src, $dest
                or die "$NAME: Failed to rename '$src' to '$dest': $!\n";
        }
        else {
            my $convert = "$rwipfix2silk --silk-output=$dest $src";
            check_exit_status($convert)
                or die "$NAME: Failed to convert '$src': $!\n";
            unless ($ENV{SK_TESTS_SAVEOUTPUT}) {
                unlink $src;
            }
        }
    }

    $ENV{SILK_DATA_ROOTDIR} = $root_dir;
}


sub create_ipfix_repo
{
    my ($root_dir) = @_;

    #### CREATE REPOSITORY USED BY REMAINING TESTS

    local $!;

    my $rwflowpack = check_silk_app('rwflowpack');

    my $config     = "$tmpdir/config";
    my $in_dir     = "$tmpdir/incoming";
    my $pack_log   = "$tmpdir/rwflowpack.log";
    my $proc_dir   = "$tmpdir/processing";
    my $error_dir  = "$tmpdir/error";

    for ($root_dir, $in_dir, $proc_dir, $error_dir) {
        mkdir $_
            or die "$NAME: Cannot create directory '$_': $!\n";
    }

    open PACK_LOG, ">$pack_log"
        or die "$NAME: Cannot open '$pack_log': $!\n";

    my @data_files = ($file{data}, $file{scandata});
    unless (0 == system "cp", @data_files, $in_dir) {
        die "$NAME: Cannot copy files for rwflowpack\n";
    }

    my $debug = ($ENV{SK_TESTS_LOG_DEBUG} ? "\n    level = \"debug\"," : "");

    my $config_body = <<EOF;
local rec_format = silk.file_format_id("FT_RWGENERIC")
input = {
  mode = "stream",
  probes = {
    {
      name = "respool",
      type = "silk",
      source = {
        directory = "${in_dir}",
        error_directory = "${error_dir}",
        interval = 1,
      },
      packing_function = function (probe, rec)
        write_rwrec(rec, {record_format = rec_format})
      end,
    },
  },
}
output = {
  mode = "local-storage",
  flush_interval = 10,
  processing = {
    directory = "${proc_dir}",
    error_directory = "${error_dir}",
  },
  root_directory = "${root_dir}",
}
log = {
  destination = "stdout",${debug}
}
daemon = {
  fork = false,
}
EOF

    make_config_file($config, \$config_body);

    # ignore children
    local $SIG{CHLD} = 'IGNORE';

    my @prog = ($rwflowpack, $config);
    $cmd = join " ", @prog;

    # fork and have parent read from the child
    my $pid = open PACKER, "-|";
    if (!$pid) {
        unless (defined $pid) {
            die "$NAME: Cannot fork: $!\n";
        }
        # child
        exec @prog
            or die "Cannot exec $cmd: $!\n";
    }
    # parent

    print STDERR "RUNNING: $cmd\n"
        if $ENV{SK_TESTS_VERBOSE};
    eval {
        my $data_file_count = scalar @data_files;
        my $incr_file_count = 0;
        my $rec_count = 0;
        my $timeout = 30;
        local $SIG{ALRM} = sub {die "alarm fired after $timeout seconds\n"};
        alarm $timeout;
        while (<PACKER>) {
            #print STDERR "$NAME: $_" if $ENV{SK_TESTS_VERBOSE};
            print PACK_LOG $_;

            # Look for a line noting incremental file has been read
            if (m,$proc_dir/(.+): (\d+) recs$,) {
                $rec_count -= $2;
                #my $str = (": Matched out file '$1';"
                #           ." data_file_count = $data_file_count;"
                #           ." rec_count = $rec_count\n");
                #print PACK_LOG ("$NAME ".localtime().$str);
                #print STDERR "$NAME$str"
                #    if $ENV{SK_TESTS_VERBOSE};
            }
            # Look for a line noting that an input file has been processed
            elsif (m/Processed file '(.+\.rwf)': Recs (\d+)$/) {
                --$data_file_count;
                $rec_count += $2;
                my $str = (": Matched src file '$1';"
                           ." data_file_count = $data_file_count;"
                           ." rec_count = $rec_count\n");
                print PACK_LOG ("$NAME ".localtime().$str);
                print STDERR "$NAME$str"
                    if $ENV{SK_TESTS_VERBOSE};
            }
            elsif (m/closed and queued (\d+) files\./) {
                $incr_file_count += $1;
                next;
            }
            elsif (m/: APPEND OK \S+ to \S+ \@ \d+/) {
                --$incr_file_count;
            }
            else {
                next;
            }
            # Matched one of the lines above; see if we should exit
            if (0 == $data_file_count && 0 == $rec_count
                && 0 == $incr_file_count)
            {
                print STDERR "$NAME: Met stopping condition\n";
                print PACK_LOG ("$NAME ".localtime().": kill -15 $pid\n");
                print STDERR "$NAME: kill -15 $pid\n"
                    if $ENV{SK_TESTS_VERBOSE};
                kill 15, $pid;
            }
        }
        alarm 0;
    };
    if ($@) {
        alarm 0;
        if (kill 0, $pid) {
            print PACK_LOG ("$NAME ".localtime().": kill -15 $pid\n");
            print STDERR "$NAME: kill -15 $pid\n"
                if $ENV{SK_TESTS_VERBOSE};
            kill 15, $pid;
            sleep 5;
            if (kill 0, $pid) {
                print PACK_LOG ("$NAME ".localtime().": kill -9 $pid\n");
                print STDERR "$NAME: kill -9 $pid\n"
                    if $ENV{SK_TESTS_VERBOSE};
                kill 9, $pid;
            }
        }
        die "$NAME: Error running rwflowpack: $@\n";
    }
    close PACKER;
    if (0 == $?) {
        print STDERR "$NAME: Packer exited successfully\n";
    } elsif (-1 == $?) {
        print STDERR "$NAME: Packer exited with error: $!\n";
    } elsif ($? & 127) {
        print STDERR "$NAME: Packer died by signal ", ($? & 127), "\n";
    }
    else {
        print STDERR "$NAME: Packer exited with status ", ($? >> 8), "\n";
    }
}
