#! /usr/bin/perl -w
#
#  This file is not run directly; rather it is used by all the
#  sendrcv-test*.pl tests.

use strict;

# set envvar to run app under valgrind when SK_TESTS_VALGRIND is set
check_silk_app('rwreceiver');
check_silk_app('rwsender');

# prefix any existing PYTHONPATH with the proper directories
check_python_bin();

# create our tempdir
my $tmpdir = make_tempdir();

# force the Python unittest to use the temporary directory
$ENV{TMPDIR} = $tmpdir;

# the name of test to run is based on this script's name
my $test = $0;
$test =~ s,.*sendrcv-(.+)\.pl,$1,;

# verify that required features are available
if ($test =~ /TLS/) {
    check_features(qw(gnutls));
}
if ($test =~ /IPv6/i) {
    check_features(qw(inet6));
}

# get name of file containing files that were created
my $file_list_file = $ENV{SILK_SENDRCVDATA};
unless ($file_list_file && -f $file_list_file) {
    skip_test("SILK_SENDRCVDATA is not set")
        unless $file_list_file;
    skip_test("Cannot find SILK_SENDRCVDATA file '$file_list_file'");
}

# this is used to print the environment so the user can see what we
# are doing
my $env = join " ", ('top_builddir='.$SiLKTests::top_builddir,
                     'srcdir='.$SiLKTests::srcdir,
                     'top_srcdir='.$SiLKTests::top_srcdir,
                     'PYTHONPATH='.$ENV{PYTHONPATH});

# the command to run that runs rwsender and rwreceiver
my $cmd = join " ", ("$SiLKTests::PYTHON $srcdir/tests/sendrcv_tests.py",
                     #"--log-level=debug",
                     ($ENV{SK_TESTS_VERBOSE} ? "--verbose" : ()),
                     ($ENV{SK_TESTS_LOG_DEBUG} ? "--log-level=debug" : ()),
                     ($ENV{SK_TESTS_SAVEOUTPUT} ? "--save-output" : ()),
                     "--file-list-file=$file_list_file",
                     $test,
                     );

exit(check_exit_status($cmd, 'no_redirect') ? 0 : 1);
