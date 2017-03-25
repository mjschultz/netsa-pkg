#! /usr/bin/perl -w
# MD5: 3677d3da40803d98298314b69fadf06a
# TEST: ./rwaggbag --key=sipv4,sport,dport,proto --counter=records ../../tests/data.rwf | ./rwaggbagtool --to-ipset=sipv4 --output-path=/tmp/rwaggbagtool-to-ipset-sipv4-tmp && ../rwset/rwsetcat /tmp/rwaggbagtool-to-ipset-sipv4-tmp

use strict;
use SiLKTests;

my $rwaggbagtool = check_silk_app('rwaggbagtool');
my $rwaggbag = check_silk_app('rwaggbag');
my $rwsetcat = check_silk_app('rwsetcat');
my %file;
$file{data} = get_data_or_exit77('data');
my %temp;
$temp{tmp} = make_tempname('tmp');
my $cmd = "$rwaggbag --key=sipv4,sport,dport,proto --counter=records $file{data} | $rwaggbagtool --to-ipset=sipv4 --output-path=$temp{tmp} && $rwsetcat $temp{tmp}";
my $md5 = "3677d3da40803d98298314b69fadf06a";

check_md5_output($md5, $cmd);
