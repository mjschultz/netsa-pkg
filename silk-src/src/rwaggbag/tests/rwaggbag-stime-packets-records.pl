#! /usr/bin/perl -w
# MD5: e1085d609b56dcfc2dfb4f3edc0f8681
# TEST: ./rwaggbag --key=stime --counter=sum-packets,records ../../tests/data.rwf | ./rwaggbagcat

use strict;
use SiLKTests;

my $rwaggbag = check_silk_app('rwaggbag');
my $rwaggbagcat = check_silk_app('rwaggbagcat');
my %file;
$file{data} = get_data_or_exit77('data');
my $cmd = "$rwaggbag --key=stime --counter=sum-packets,records $file{data} | $rwaggbagcat";
my $md5 = "e1085d609b56dcfc2dfb4f3edc0f8681";

check_md5_output($md5, $cmd);
