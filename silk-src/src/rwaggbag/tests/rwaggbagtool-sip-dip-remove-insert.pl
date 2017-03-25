#! /usr/bin/perl -w
# MD5: 15f62794b41e71d7047f7d138667d764
# TEST: ./rwaggbag --key=sipv4,dipv4 --counter=sum-packets,sum-bytes ../../tests/data.rwf --output-path=/tmp/rwaggbagtool-sip-dip-remove-insert-tmp && ./rwaggbagtool --remove=dipv4,sum-packets --insert-field=dipv4=0.0.0.0 /tmp/rwaggbagtool-sip-dip-remove-insert-tmp | ./rwaggbagcat

use strict;
use SiLKTests;

my $rwaggbagtool = check_silk_app('rwaggbagtool');
my $rwaggbag = check_silk_app('rwaggbag');
my $rwaggbagcat = check_silk_app('rwaggbagcat');
my %file;
$file{data} = get_data_or_exit77('data');
my %temp;
$temp{tmp} = make_tempname('tmp');
my $cmd = "$rwaggbag --key=sipv4,dipv4 --counter=sum-packets,sum-bytes $file{data} --output-path=$temp{tmp} && $rwaggbagtool --remove=dipv4,sum-packets --insert-field=dipv4=0.0.0.0 $temp{tmp} | $rwaggbagcat";
my $md5 = "15f62794b41e71d7047f7d138667d764";

check_md5_output($md5, $cmd);
