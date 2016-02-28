#! /usr/bin/perl -w
# MD5: 720e074ea3b1ad7b040cf099192bb868
# TEST: ./skvector-test 2>&1

use strict;
use SiLKTests;

my $skvector_test = check_silk_app('skvector-test');
my $cmd = "$skvector_test 2>&1";
my $md5 = "720e074ea3b1ad7b040cf099192bb868";

check_md5_output($md5, $cmd);
