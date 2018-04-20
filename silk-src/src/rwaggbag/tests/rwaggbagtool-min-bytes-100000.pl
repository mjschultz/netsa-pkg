#! /usr/bin/perl -w
# MD5: c236b522880417fb5120f3055035df5d
# TEST: ./rwaggbag --key=sport,dport,proto --counter=sum-packets,sum-bytes ../../tests/data.rwf | ./rwaggbagtool --min-field=sum-bytes=100000 | ./rwaggbagcat

use strict;
use SiLKTests;

my $rwaggbagtool = check_silk_app('rwaggbagtool');
my $rwaggbag = check_silk_app('rwaggbag');
my $rwaggbagcat = check_silk_app('rwaggbagcat');
my %file;
$file{data} = get_data_or_exit77('data');
my %temp;
$temp{tmp} = make_tempname('tmp');
my $cmd = "$rwaggbag --key=sport,dport,proto --counter=sum-packets,sum-bytes $file{data} | $rwaggbagtool --min-field=sum-bytes=100000 | $rwaggbagcat";
my $md5 = "c236b522880417fb5120f3055035df5d";

check_md5_output($md5, $cmd);
