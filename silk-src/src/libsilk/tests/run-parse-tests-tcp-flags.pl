#! /usr/bin/perl -w
# MD5: c3052f29f0a691e6e929dbdb4d248c8b
# TEST: ./parse-tests --tcp-flags 2>&1

use strict;
use SiLKTests;

my $parse_tests = check_silk_app('parse-tests');
my $cmd = "$parse_tests --tcp-flags 2>&1";
my $md5 = "c3052f29f0a691e6e929dbdb4d248c8b";

check_md5_output($md5, $cmd);
