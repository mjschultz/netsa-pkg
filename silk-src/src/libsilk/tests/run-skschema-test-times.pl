#! /usr/bin/perl -w
# MD5: 18cd6154a65f10a41f1620cf7a2054e6
# TEST: ./skschema-test-times 2>&1

use strict;
use SiLKTests;

my $skschema_test_times = check_silk_app('skschema-test-times');
my $cmd = "$skschema_test_times 2>&1";
my $md5 = "18cd6154a65f10a41f1620cf7a2054e6";

check_md5_output($md5, $cmd);
