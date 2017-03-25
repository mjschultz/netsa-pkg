#! /usr/bin/perl -w
# MD5: 4283e5db4bfc0b7684fdae42d5baca11
# TEST: ./rwaggbag --key=sport --counter=records ../../tests/empty.rwf | ./rwaggbagcat

use strict;
use SiLKTests;

my $rwaggbag = check_silk_app('rwaggbag');
my $rwaggbagcat = check_silk_app('rwaggbagcat');
my %file;
$file{empty} = get_data_or_exit77('empty');
my $cmd = "$rwaggbag --key=sport --counter=records $file{empty} | $rwaggbagcat";
my $md5 = "4283e5db4bfc0b7684fdae42d5baca11";

check_md5_output($md5, $cmd);
