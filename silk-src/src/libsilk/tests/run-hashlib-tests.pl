#! /usr/bin/perl -w
# MD5: 9e9b3a5f07154f3dcaf45f84f123bbf9
# TEST: ./hashlib_tests 2>&1

use strict;
use SiLKTests;

my $hashlib_tests = check_silk_app('hashlib_tests');
my $cmd = "$hashlib_tests 2>&1";
my $md5 = "9e9b3a5f07154f3dcaf45f84f123bbf9";

check_md5_output($md5, $cmd);
