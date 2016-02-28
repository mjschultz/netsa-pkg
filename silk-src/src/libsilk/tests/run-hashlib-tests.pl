#! /usr/bin/perl -w
# MD5: b89430181364dbd1d2ff2923771eb1ad
# TEST: ./hashlib_tests 2>&1

use strict;
use SiLKTests;

my $hashlib_tests = check_silk_app('hashlib_tests');
my $cmd = "$hashlib_tests 2>&1";
my $md5 = "b89430181364dbd1d2ff2923771eb1ad";

check_md5_output($md5, $cmd);
