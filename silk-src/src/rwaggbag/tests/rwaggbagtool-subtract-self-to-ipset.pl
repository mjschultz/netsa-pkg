#! /usr/bin/perl -w
# MD5: d41d8cd98f00b204e9800998ecf8427e
# TEST: ./rwaggbag --key=sipv4,dipv4 --counter=sum-packets,sum-bytes,records ../../tests/data.rwf --output-path=/tmp/rwaggbagtool-subtract-self-to-ipset-tmp && ./rwaggbagtool --subtract /tmp/rwaggbagtool-subtract-self-to-ipset-tmp /tmp/rwaggbagtool-subtract-self-to-ipset-tmp --to-ipset=sipv4 | ../rwset/rwsetcat --cidr=1

use strict;
use SiLKTests;

my $rwaggbagtool = check_silk_app('rwaggbagtool');
my $rwaggbag = check_silk_app('rwaggbag');
my $rwsetcat = check_silk_app('rwsetcat');
my %file;
$file{data} = get_data_or_exit77('data');
my %temp;
$temp{tmp} = make_tempname('tmp');
my $cmd = "$rwaggbag --key=sipv4,dipv4 --counter=sum-packets,sum-bytes,records $file{data} --output-path=$temp{tmp} && $rwaggbagtool --subtract $temp{tmp} $temp{tmp} --to-ipset=sipv4 | $rwsetcat --cidr=1";
my $md5 = "d41d8cd98f00b204e9800998ecf8427e";

check_md5_output($md5, $cmd);
