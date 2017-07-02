#! /usr/bin/perl -w
# MD5: varies
# TEST: ./parse-tests --ip-addresses 2>&1

use strict;
use SiLKTests;

my $parse_tests = check_silk_app('parse-tests');
my $cmd = "$parse_tests --ip-addresses 2>&1";
my $md5 = "2b52bb67540b7f48ed74f31e9fc22cee";
check_md5_output($md5, $cmd);
