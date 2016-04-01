#! /usr/bin/perl -w
#
#
# RCSIDENT("$SiLK: rwpollexec-zero.pl be00e4ce901e 2016-02-25 18:50:11Z mthomas $")

use strict;
use SiLKTests;
use File::Temp ();


# set envvar to run app under valgrind when SK_TESTS_VALGRIND is set
check_silk_app('rwpollexec');

# prefix any existing PYTHONPATH with the proper directories
check_python_bin();

# create our tempdir
my $tmpdir = make_tempdir();

# work around issue on OS X when System Integrity Protection enabled
rwpollexec_use_alternate_shell($tmpdir);

# the command that wraps rwpollexec
my $rwpollexec_py = "$SiLKTests::PYTHON $srcdir/tests/rwpollexec-daemon.py";
my $cmd = join " ", ($rwpollexec_py,
                     ($ENV{SK_TESTS_VERBOSE} ? "--verbose" : ()),
                     ($ENV{SK_TESTS_LOG_DEBUG} ? "--log-level=debug" : ()),
                     "--return 0",
                     "--return 0",
                     "--basedir=$tmpdir",
                     "--",
                     "--flat-archive",
                     "--polling-interval=3",
                     "--command \"$rwpollexec_py --exec %s\"",
    );

my @expected_archive = qw(0 1);
my @expected_error   = qw();

# run it and check the MD5 hash of its output
check_md5_output('b5fac3b7ca874a8324cc9098c21021c1', $cmd);


# the following directories should be empty
verify_empty_dirs($tmpdir, qw(incoming));

# verify files are in the archive directory
verify_directory_files("$tmpdir/archive", @expected_archive);

# verify files are in the error directory
verify_directory_files("$tmpdir/error", @expected_error);

