#! /usr/bin/perl -w
# MD5: 96da04a874000ec7f32732e25344bc4e
# TEST: ./rwaggbag --key=dport --counter=records,sum-packets,sum-bytes ../../tests/data.rwf | ./rwaggbagcat

use strict;
use SiLKTests;

my $rwaggbag = check_silk_app('rwaggbag');
my $rwaggbagcat = check_silk_app('rwaggbagcat');
my %file;
$file{data} = get_data_or_exit77('data');
my $cmd = "$rwaggbag --key=dport --counter=records,sum-packets,sum-bytes $file{data} | $rwaggbagcat";
my $md5 = "96da04a874000ec7f32732e25344bc4e";

check_md5_output($md5, $cmd);
