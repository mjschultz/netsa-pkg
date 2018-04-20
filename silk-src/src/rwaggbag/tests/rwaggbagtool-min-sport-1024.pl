#! /usr/bin/perl -w
# MD5: 94c5ad72013098a89a145e65ee627c87
# TEST: ./rwaggbag --key=sport,dport,proto --counter=sum-packets,sum-bytes ../../tests/data.rwf | ./rwaggbagtool --min-field=sport=1024 | ./rwaggbagcat

use strict;
use SiLKTests;

my $rwaggbagtool = check_silk_app('rwaggbagtool');
my $rwaggbag = check_silk_app('rwaggbag');
my $rwaggbagcat = check_silk_app('rwaggbagcat');
my %file;
$file{data} = get_data_or_exit77('data');
my %temp;
$temp{tmp} = make_tempname('tmp');
my $cmd = "$rwaggbag --key=sport,dport,proto --counter=sum-packets,sum-bytes $file{data} | $rwaggbagtool --min-field=sport=1024 | $rwaggbagcat";
my $md5 = "94c5ad72013098a89a145e65ee627c87";

check_md5_output($md5, $cmd);
