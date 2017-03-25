#! /usr/bin/perl -w
# MD5: 4a8b3923f8436676975672c83c213096
# TEST: ../rwfilter/rwfilter --type=in,inweb --pass=- ../../tests/data.rwf | ./rwaggbag --key=sport,dport,proto --counter=records --output-path=/tmp/rwaggbagtool-add-bags-in && ../rwfilter/rwfilter --type=out,outweb --pass=- ../../tests/data.rwf | ./rwaggbag --key=sport,dport,proto --counter=records --output-path=/tmp/rwaggbagtool-add-bags-out && ./rwaggbagtool --add /tmp/rwaggbagtool-add-bags-in /tmp/rwaggbagtool-add-bags-out | ./rwaggbagcat

use strict;
use SiLKTests;

my $rwaggbagtool = check_silk_app('rwaggbagtool');
my $rwfilter = check_silk_app('rwfilter');
my $rwaggbag = check_silk_app('rwaggbag');
my $rwaggbagcat = check_silk_app('rwaggbagcat');
my %file;
$file{data} = get_data_or_exit77('data');
my %temp;
$temp{in} = make_tempname('in');
$temp{out} = make_tempname('out');
my $cmd = "$rwfilter --type=in,inweb --pass=- $file{data} | $rwaggbag --key=sport,dport,proto --counter=records --output-path=$temp{in} && $rwfilter --type=out,outweb --pass=- $file{data} | $rwaggbag --key=sport,dport,proto --counter=records --output-path=$temp{out} && $rwaggbagtool --add $temp{in} $temp{out} | $rwaggbagcat";
my $md5 = "4a8b3923f8436676975672c83c213096";

check_md5_output($md5, $cmd);
