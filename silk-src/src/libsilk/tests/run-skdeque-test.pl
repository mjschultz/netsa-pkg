#! /usr/bin/perl -w
# STATUS: OK
# TEST: ./skdeque-test 2>&1

use strict;
use SiLKTests;

my $skdeque_test = check_silk_app('skdeque-test');
my $cmd = "$skdeque_test 2>&1";

exit (check_exit_status($cmd) ? 0 : 1);
