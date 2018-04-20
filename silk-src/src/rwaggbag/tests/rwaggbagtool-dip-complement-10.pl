#! /usr/bin/perl -w
# MD5: 0fb2c8c30de83e0edc7ad9a321a3655d
# TEST: echo 10.0.0.0/8 | ../rwset/rwsetbuild --record-version=4 - /tmp/rwaggbagtool-dip-complement-10-tmp && ./rwaggbag --key=sipv4,dipv4 --counter=sum-packets,sum-bytes ../../tests/data.rwf | ./rwaggbagtool --set-complement=dipv4=/tmp/rwaggbagtool-dip-complement-10-tmp | ./rwaggbagcat

use strict;
use SiLKTests;

my $rwaggbagtool = check_silk_app('rwaggbagtool');
my $rwaggbag = check_silk_app('rwaggbag');
my $rwaggbagcat = check_silk_app('rwaggbagcat');
my $rwsetbuild = check_silk_app('rwsetbuild');
my %file;
$file{data} = get_data_or_exit77('data');
my %temp;
$temp{tmp} = make_tempname('tmp');
my $cmd = "echo 10.0.0.0/8 | $rwsetbuild --record-version=4 - $temp{tmp} && $rwaggbag --key=sipv4,dipv4 --counter=sum-packets,sum-bytes $file{data} | $rwaggbagtool --set-complement=dipv4=$temp{tmp} | $rwaggbagcat";
my $md5 = "0fb2c8c30de83e0edc7ad9a321a3655d";

check_md5_output($md5, $cmd);
