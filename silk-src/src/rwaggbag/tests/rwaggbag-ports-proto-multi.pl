#! /usr/bin/perl -w
# MD5: 4a8b3923f8436676975672c83c213096
# TEST: ../rwfilter/rwfilter --type=in,inweb --pass=/tmp/rwaggbag-ports-proto-multi-in ../../tests/data.rwf && ../rwfilter/rwfilter --type=in,inweb --fail=- ../../tests/data.rwf | ./rwaggbag --key=sport,dport,proto --counter=records /tmp/rwaggbag-ports-proto-multi-in - | ./rwaggbagcat

use strict;
use SiLKTests;

my $rwaggbag = check_silk_app('rwaggbag');
my $rwfilter = check_silk_app('rwfilter');
my $rwaggbagcat = check_silk_app('rwaggbagcat');
my %file;
$file{data} = get_data_or_exit77('data');
my %temp;
$temp{in} = make_tempname('in');
my $cmd = "$rwfilter --type=in,inweb --pass=$temp{in} $file{data} && $rwfilter --type=in,inweb --fail=- $file{data} | $rwaggbag --key=sport,dport,proto --counter=records $temp{in} - | $rwaggbagcat";
my $md5 = "4a8b3923f8436676975672c83c213096";

check_md5_output($md5, $cmd);
