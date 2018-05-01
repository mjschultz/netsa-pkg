#! /usr/bin/perl -w
# MD5: d41d8cd98f00b204e9800998ecf8427e
# TEST: ./rwaggbag --key=sport,dport,proto --counter=sum-packets,sum-bytes ../../tests/data.rwf --output-path=/tmp/rwaggbagtool-subtract-self-aggbag-tmp && ./rwaggbagtool --subtract /tmp/rwaggbagtool-subtract-self-aggbag-tmp /tmp/rwaggbagtool-subtract-self-aggbag-tmp | ./rwaggbagcat --no-titles

use strict;
use SiLKTests;

my $rwaggbagtool = check_silk_app('rwaggbagtool');
my $rwaggbag = check_silk_app('rwaggbag');
my $rwaggbagcat = check_silk_app('rwaggbagcat');
my %file;
$file{data} = get_data_or_exit77('data');
my %temp;
$temp{tmp} = make_tempname('tmp');
my $cmd = "$rwaggbag --key=sport,dport,proto --counter=sum-packets,sum-bytes $file{data} --output-path=$temp{tmp} && $rwaggbagtool --subtract $temp{tmp} $temp{tmp} | $rwaggbagcat --no-titles";
my $md5 = "d41d8cd98f00b204e9800998ecf8427e";

check_md5_output($md5, $cmd);
