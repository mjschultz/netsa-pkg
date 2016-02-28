#! /usr/bin/perl -w
# MD5: a75cde6d6539d9a257fabcaca702ff5e
# TEST: ./parse-tests --dates 2>&1

use strict;
use SiLKTests;

my $parse_tests = check_silk_app('parse-tests');
my $cmd = "$parse_tests --dates 2>&1";
my $md5 = "a75cde6d6539d9a257fabcaca702ff5e";

check_md5_output($md5, $cmd);
