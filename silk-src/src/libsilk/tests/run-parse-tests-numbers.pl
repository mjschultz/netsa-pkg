#! /usr/bin/perl -w
# MD5: 95a7690691ff78efe02107b906358232
# TEST: ./parse-tests --numbers 2>&1

use strict;
use SiLKTests;

my $parse_tests = check_silk_app('parse-tests');
my $cmd = "$parse_tests --numbers 2>&1";
my $md5 = "95a7690691ff78efe02107b906358232";

check_md5_output($md5, $cmd);
