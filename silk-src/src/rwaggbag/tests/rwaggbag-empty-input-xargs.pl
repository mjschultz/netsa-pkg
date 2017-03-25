#! /usr/bin/perl -w
# MD5: 4283e5db4bfc0b7684fdae42d5baca11
# TEST: cat /dev/null | ./rwaggbag --key=sport --counter=records -xargs=- | ./rwaggbagcat

use strict;
use SiLKTests;

my $rwaggbag = check_silk_app('rwaggbag');
my $rwaggbagcat = check_silk_app('rwaggbagcat');
my $cmd = "cat /dev/null | $rwaggbag --key=sport --counter=records -xargs=- | $rwaggbagcat";
my $md5 = "4283e5db4bfc0b7684fdae42d5baca11";

check_md5_output($md5, $cmd);
