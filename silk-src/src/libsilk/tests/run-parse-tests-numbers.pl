#! /usr/bin/perl -w
# MD5: 14d5d950455aa9006c56765be2df7044
# TEST: ./parse-tests --numbers 2>&1

use strict;
use SiLKTests;

my $parse_tests = check_silk_app('parse-tests');
my $cmd = "$parse_tests --numbers 2>&1";
my $md5 = "14d5d950455aa9006c56765be2df7044";

check_md5_output($md5, $cmd);
