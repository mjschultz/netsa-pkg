#! /usr/bin/perl -w
#
#
# RCSIDENT("$SiLK: rwflowpack-split-rwflowappend.pl 622e2f0a4dd8 2016-09-22 13:47:50Z mthomas $")
#
# PURPOSE: Check whether rwflowappend properly handles combining about
# 16,925 incremental files into 432 hourly files.  The incremental
# files exist in rwflowappend's incoming directory when it is invoked.
# To create an each hourly file, rwflowappend will combine
# approximately 39 incrementail files.  The input files will be
# deleted.  When rwflowappend receives a signal, it should shut down
# cleanly.  To create the incremental files, the test runs rwflowpack
# in sending mode which creates 432 incremental files, and then the
# test runs rwsplit on each of those files.  This test uses input
# files that contain only IPv4 data.
#

use strict;
use SiLKTests;
use File::Find;

my $rwflowpack = check_silk_app('rwflowpack');

# find the apps we need.  this will exit 77 if they're not available
my $rwflowappend = check_silk_app('rwflowappend');
my $rwsplit = check_silk_app('rwsplit');
my $rwuniq = check_silk_app('rwuniq');

# find the data files we use as sources, or exit 77
my %file;
$file{data} = get_data_or_exit77('data');

# prefix any existing PYTHONPATH with the proper directories
check_python_bin();

# set the environment variables required for rwflowpack to find its
# packing logic plug-in
add_plugin_dirs('/site/twoway');

# Skip this test if we cannot load the packing logic
check_exit_status("$rwflowpack --sensor-conf=$srcdir/tests/sensor77.conf"
                  ." --verify-sensor-conf")
    or skip_test("Cannot load packing logic");

# create our tempdir
my $tmpdir = make_tempdir();

# Generate the sensor.conf file
my $sensor_conf = "$tmpdir/sensor-templ.conf";
make_packer_sensor_conf($sensor_conf, 'silk', 0, 'polldir');

# the command that wraps rwflowpack
my $cmd = join " ", ("$SiLKTests::PYTHON $srcdir/tests/rwflowpack-daemon.py",
                     ($ENV{SK_TESTS_VERBOSE} ? "--verbose" : ()),
                     ($ENV{SK_TESTS_LOG_DEBUG} ? "--log-level=debug" : ()),
                     "--sensor-conf=$sensor_conf",
                     "--output-mode=sending",
                     "--copy $file{data}:incoming",
                     "--limit=501876",
                     "--basedir=$tmpdir",
                     "--flush-timeout=5",
                     "--",
                     "--pack-interfaces",
                     "--polling-interval=5",
                     "--file-cache-size=8",
                     "--flat-archive",
    );

# run it and check the MD5 hash of its output
check_md5_output('a78a286719574389a972724d761c931e', $cmd);

# the following directories should be empty
verify_empty_dirs($tmpdir, qw(error incoming incremental root));

# input files should now be in the archive directory
verify_directory_files("$tmpdir/archive", $file{data});

# path to the output directory
my $sender_dir = "$tmpdir/sender";
die "ERROR: Missing data directory '$sender_dir'\n"
    unless -d $sender_dir;

# path to the incoming directory
my $incoming_dir = "$tmpdir/incoming";

# run rwsplit over the files in the sender directory and write the
# output files to the incoming direcotry
File::Find::find({wanted => \&split_files, no_chdir => 1}, $sender_dir);

# Determine the number of incremental files that rwflowappend should
# see.  In addition, push the files into an array if the output is
# being saved.
my $expected = 0;
my @incremental_files;
opendir D, $incoming_dir
    or die "ERROR: Cannot open directory '$incoming_dir': $!\n";
while (defined(my $entry = readdir(D))) {
    next if $entry =~ m,^\.\.?$,;
    ++$expected;
    if ($ENV{SK_TESTS_SAVEOUTPUT}) {
        push @incremental_files, $entry;
    }
}
closedir D;

# Compute a checksum that can be compared with the output of
# rwflowappend-daemon.  Normally there are 16925 files and the
# checksum is 'd2fdc1d1e8fc8c4fb09e80c52f5f6657', but there may be
# more if rwflowpack flushes the files multiple times.
my $md5 = Digest::MD5::md5_hex("File count: $expected\n");

# run rwflowappend to join the files together.  What I would really
# like to be able to do is to run multiple rwflowappend processes over
# the files to check file locking, but that is difficult with the
# current testing code.
$cmd = join " ", ("$SiLKTests::PYTHON $srcdir/tests/rwflowappend-daemon.py",
                  ($ENV{SK_TESTS_VERBOSE} ? "--verbose" : ()),
                  ($ENV{SK_TESTS_LOG_DEBUG} ? "--log-level=debug" : ()),
                  ($ENV{SK_TESTS_SAVEOUTPUT} ? () : "--no-archive"),
                  "--file-limit=$expected",
                  "--basedir=$tmpdir",
                  "--",
                  "--polling-interval=5",
                  "--threads=3",
                  "--flat-archive",
    );
# run it and check the MD5 hash of its output
check_md5_output($md5, $cmd);

# the following directories should be empty
verify_empty_dirs($tmpdir, qw(error incoming incremental));

# input files should now be in the archive directory
verify_directory_files("$tmpdir/archive", $file{data}, @incremental_files);

# path to the data directory
my $data_dir = "$tmpdir/root";
die "ERROR: Missing data directory '$data_dir'\n"
    unless -d $data_dir;

# check the output
$cmd = ("find $data_dir -type f -print"
        ." | $rwuniq --xargs --ipv6=ignore --fields=sip,sensor,type,stime"
        ." --values=records,packets,stime,etime --sort");
check_md5_output('3135fa86995d5b525086eca240add57f', $cmd);

# successful!
exit 0;

# this is called by File::Find::find.  The full path to the file is in
# the $_ variable
sub split_files
{
    # skip anything that is not a file
    return unless -f $_;
    my $path = $_;
    # rename $_ to point into the incoming directory
    s,\Q$sender_dir/\E,$incoming_dir/,o;
    my $split_cmd = "$rwsplit --flow-limit=30 --basename=$_ $path";
    unless (check_exit_status($split_cmd)) {
        die "Problem running rwsplit\n";
    }
}
