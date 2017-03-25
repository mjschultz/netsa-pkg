#! /usr/bin/perl -w
# MD5: cc2aeaac8db65c28a31e2edeb6e42dac
# TEST: ../rwfilter/rwfilter --type=in,inweb --pass=- ../../tests/data.rwf | ./rwaggbag --key=sport,dport,proto --counter=records --output-path=/tmp/rwaggbagtool-subtract-bags-in && ../rwfilter/rwfilter --type=out,outweb --pass=- ../../tests/data.rwf | ./rwaggbag --key=sport,dport,proto --counter=records | ./rwaggbagtool --subtract /tmp/rwaggbagtool-subtract-bags-in - | ./rwaggbagcat

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
my $cmd = "$rwfilter --type=in,inweb --pass=- $file{data} | $rwaggbag --key=sport,dport,proto --counter=records --output-path=$temp{in} && $rwfilter --type=out,outweb --pass=- $file{data} | $rwaggbag --key=sport,dport,proto --counter=records | $rwaggbagtool --subtract $temp{in} - | $rwaggbagcat";
my $md5 = "cc2aeaac8db65c28a31e2edeb6e42dac";

check_md5_output($md5, $cmd);
