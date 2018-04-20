#! /usr/bin/perl -w
# MD5: 1e21e652af87ad79df91dd64efb88751
# TEST: ./rwaggbag --key=sport,dport,proto --counter=sum-packets,sum-bytes ../../tests/data.rwf | ./rwaggbagtool --max-field=sport=1024 --max-field=dport=1024 | ./rwaggbagcat

use strict;
use SiLKTests;

my $rwaggbagtool = check_silk_app('rwaggbagtool');
my $rwaggbag = check_silk_app('rwaggbag');
my $rwaggbagcat = check_silk_app('rwaggbagcat');
my %file;
$file{data} = get_data_or_exit77('data');
my %temp;
$temp{tmp} = make_tempname('tmp');
my $cmd = "$rwaggbag --key=sport,dport,proto --counter=sum-packets,sum-bytes $file{data} | $rwaggbagtool --max-field=sport=1024 --max-field=dport=1024 | $rwaggbagcat";
my $md5 = "1e21e652af87ad79df91dd64efb88751";

check_md5_output($md5, $cmd);
