#! /usr/bin/perl -w
# MD5: eff39ae1656b8d7d577895d21c9e83d9
# TEST: ./skschema-test 2>&1

use strict;
use SiLKTests;

my $skschema_test = check_silk_app('skschema-test');
my $cmd = "$skschema_test 2>&1";
my $md5 = "eff39ae1656b8d7d577895d21c9e83d9";

check_md5_output($md5, $cmd);
