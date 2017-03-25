#! /usr/bin/perl -w
# MD5: 7a223b2c7f004f6f3c445cdbf177d812
# TEST: ./rwaggbag --key=sipv4,sport,dport,proto --counter=sum-bytes,sum-packets ../../tests/data.rwf | ./rwaggbagtool --select=sport,dport,proto,sum-bytes | ./rwaggbagcat

use strict;
use SiLKTests;

my $rwaggbagtool = check_silk_app('rwaggbagtool');
my $rwaggbag = check_silk_app('rwaggbag');
my $rwaggbagcat = check_silk_app('rwaggbagcat');
my %file;
$file{data} = get_data_or_exit77('data');
my $cmd = "$rwaggbag --key=sipv4,sport,dport,proto --counter=sum-bytes,sum-packets $file{data} | $rwaggbagtool --select=sport,dport,proto,sum-bytes | $rwaggbagcat";
my $md5 = "7a223b2c7f004f6f3c445cdbf177d812";

check_md5_output($md5, $cmd);
