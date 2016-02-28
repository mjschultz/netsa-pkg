#! /usr/bin/perl -w
# MD5: d02eef0a5ac7a1281192d07fe3b4a112
# TEST: ./parse-tests --signals 2>&1

use strict;
use SiLKTests;

my $parse_tests = check_silk_app('parse-tests');
my $cmd = "$parse_tests --signals 2>&1";
my $md5 = "d02eef0a5ac7a1281192d07fe3b4a112";

check_md5_output($md5, $cmd);
