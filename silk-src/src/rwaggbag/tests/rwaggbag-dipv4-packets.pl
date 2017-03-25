#! /usr/bin/perl -w
# MD5: 7b881b469fa9eb12fbdd09cec5ca9c3e
# TEST: ./rwaggbag --key=dipv4 --counter=sum-packets ../../tests/data.rwf | ./rwaggbagcat --ip-format=zero-padded

use strict;
use SiLKTests;

my $rwaggbag = check_silk_app('rwaggbag');
my $rwaggbagcat = check_silk_app('rwaggbagcat');
my %file;
$file{data} = get_data_or_exit77('data');
my $cmd = "$rwaggbag --key=dipv4 --counter=sum-packets $file{data} | $rwaggbagcat --ip-format=zero-padded";
my $md5 = "7b881b469fa9eb12fbdd09cec5ca9c3e";

check_md5_output($md5, $cmd);
