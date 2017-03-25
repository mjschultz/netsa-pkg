#! /usr/bin/perl -w
# MD5: c24fce91b45ebaed9dcaf48bb74a1b9b
# TEST: ./rwaggbag --key=sensor,class,type --counter=records ../../tests/data.rwf | ./rwaggbagcat

use strict;
use SiLKTests;

my $rwaggbag = check_silk_app('rwaggbag');
my $rwaggbagcat = check_silk_app('rwaggbagcat');
my %file;
$file{data} = get_data_or_exit77('data');
my $cmd = "$rwaggbag --key=sensor,class,type --counter=records $file{data} | $rwaggbagcat";
my $md5 = "c24fce91b45ebaed9dcaf48bb74a1b9b";

check_md5_output($md5, $cmd);
