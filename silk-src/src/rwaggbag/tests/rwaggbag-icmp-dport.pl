#! /usr/bin/perl -w
# MD5: daef91318f332a691a9258a602a2c91c
# TEST: ./rwaggbag --key=icmpType,icmpCode,dport,proto --counter=records ../../tests/data.rwf | ./rwaggbagcat

use strict;
use SiLKTests;

my $rwaggbag = check_silk_app('rwaggbag');
my $rwaggbagcat = check_silk_app('rwaggbagcat');
my %file;
$file{data} = get_data_or_exit77('data');
my $cmd = "$rwaggbag --key=icmpType,icmpCode,dport,proto --counter=records $file{data} | $rwaggbagcat";
my $md5 = "daef91318f332a691a9258a602a2c91c";

check_md5_output($md5, $cmd);
