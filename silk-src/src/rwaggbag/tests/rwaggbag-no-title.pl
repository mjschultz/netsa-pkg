#! /usr/bin/perl -w
# MD5: 8dae8a4e2ad9878a4c8f2817aa6cb925
# TEST: ./rwaggbag --key=sport --counter=records ../../tests/data.rwf | ./rwaggbagcat --no-titles

use strict;
use SiLKTests;

my $rwaggbag = check_silk_app('rwaggbag');
my $rwaggbagcat = check_silk_app('rwaggbagcat');
my %file;
$file{data} = get_data_or_exit77('data');
my $cmd = "$rwaggbag --key=sport --counter=records $file{data} | $rwaggbagcat --no-titles";
my $md5 = "8dae8a4e2ad9878a4c8f2817aa6cb925";

check_md5_output($md5, $cmd);
