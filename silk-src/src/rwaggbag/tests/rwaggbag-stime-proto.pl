#! /usr/bin/perl -w
# MD5: 8e68fdd738330d38540925361a59e2d9
# TEST: ./rwaggbag --key=stime,proto --counter=records ../../tests/data.rwf | ./rwaggbagcat

use strict;
use SiLKTests;

my $rwaggbag = check_silk_app('rwaggbag');
my $rwaggbagcat = check_silk_app('rwaggbagcat');
my %file;
$file{data} = get_data_or_exit77('data');
my $cmd = "$rwaggbag --key=stime,proto --counter=records $file{data} | $rwaggbagcat";
my $md5 = "8e68fdd738330d38540925361a59e2d9";

check_md5_output($md5, $cmd);
