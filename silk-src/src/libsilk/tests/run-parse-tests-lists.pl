#! /usr/bin/perl -w
# MD5: d7aad5f9b02c8ca1b3651696b67b9fe4
# TEST: ./parse-tests --lists 2>&1

use strict;
use SiLKTests;

my $parse_tests = check_silk_app('parse-tests');
my $cmd = "$parse_tests --lists 2>&1";
my $md5 = "d7aad5f9b02c8ca1b3651696b67b9fe4";

check_md5_output($md5, $cmd);
