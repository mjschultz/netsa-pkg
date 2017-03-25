#! /usr/bin/perl -w
# MD5: e8d77070ea37ab1e6d0cf9631f4c906c
# TEST: ../rwfilter/rwfilter --proto=1 --pass=- ../../tests/data.rwf | ./rwaggbag --key=icmpType,icmpCode --counter=records | ./rwaggbagcat

use strict;
use SiLKTests;

my $rwaggbag = check_silk_app('rwaggbag');
my $rwfilter = check_silk_app('rwfilter');
my $rwaggbagcat = check_silk_app('rwaggbagcat');
my %file;
$file{data} = get_data_or_exit77('data');
my $cmd = "$rwfilter --proto=1 --pass=- $file{data} | $rwaggbag --key=icmpType,icmpCode --counter=records | $rwaggbagcat";
my $md5 = "e8d77070ea37ab1e6d0cf9631f4c906c";

check_md5_output($md5, $cmd);
