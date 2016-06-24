#! /usr/bin/perl -w
#
#
# RCSIDENT("$SiLK: rwflowpack-pack-respool.pl 8b44d08476b6 2016-06-03 22:17:44Z mthomas $")

use strict;
use SiLKTests;
use File::Find;

my $rwflowpack = check_silk_app('rwflowpack');

# find the apps we need.  this will exit 77 if they're not available
my $rwcat   = check_silk_app('rwcat');
my $rwsplit = check_silk_app('rwsplit');
my $rwuniq  = check_silk_app('rwuniq');

# find the data files we use as sources, or exit 77
my %file;
$file{data} = get_data_or_exit77('data');

# prefix any existing PYTHONPATH with the proper directories
check_python_bin();

# set the environment variables required for rwflowpack to find its
# packing logic plug-in
add_plugin_dirs('/site/twoway');

# create our tempdir
my $tmpdir = make_tempdir();

# create the incoming directory
my $incoming_dir = "$tmpdir/incoming";
mkdir $incoming_dir
    or die "ERROR: Cannot create directory $incoming_dir: $!\n";

# Generate the data files
my $cmd = join " ", ($rwsplit,
                     "--basename=$incoming_dir/rwsplit-out",
                     "--byte-limit=1200000000",
                     $file{data});
unless (check_exit_status($cmd)) {
    die "ERROR: $rwsplit exited with error\n";
}
my @input_files = glob("$incoming_dir/rwsplit-out.*");

# the command that wraps rwflowpack
$cmd = join " ", ("$SiLKTests::PYTHON $srcdir/tests/rwflowpack-daemon.py",
                  ($ENV{SK_TESTS_VERBOSE} ? "--verbose" : ()),
                  ($ENV{SK_TESTS_LOG_DEBUG} ? "--log-level=debug" : ()),
                  "--limit=501876",
                  "--basedir=$tmpdir",
                  "--",
                  "--input-mode=respool",
                  "--incoming-directory=$tmpdir/incoming",
                  "--polling-interval=5",
                  "--flat-archive",
    );

# run it and check the MD5 hash of its output
check_md5_output('a78a286719574389a972724d761c931e', $cmd);

# the following directories should be empty
verify_empty_dirs($tmpdir, qw(error incoming incremental sender));

# input files should now be in the archive directory
verify_directory_files("$tmpdir/archive", @input_files);

# path to the data directory
my $data_dir = "$tmpdir/root";
die "ERROR: Missing data directory '$data_dir'\n"
    unless -d $data_dir;

# check the output
$cmd = ("find $data_dir -type f -print"
        ." | $rwcat --xargs"
        ." | $rwuniq --ipv6=ignore --fields=sip,sensor,type,stime"
        ." --values=records,packets,stime,etime --sort");
check_md5_output('247e19c4880a3ec12c365a46bb443766', $cmd);

# successful!
exit 0;
