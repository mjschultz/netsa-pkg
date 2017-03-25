#! /usr/bin/perl -w
# MD5: 6ab12fb588ac3d7cad0d95ec01369f13
# TEST: ./rwaggbag --key=dur --counter=sum-bytes ../../tests/data.rwf | ./rwaggbagcat

use strict;
use SiLKTests;

my $rwaggbag = check_silk_app('rwaggbag');
my $rwaggbagcat = check_silk_app('rwaggbagcat');
my %file;
$file{data} = get_data_or_exit77('data');
my $cmd = "$rwaggbag --key=dur --counter=sum-bytes $file{data} | $rwaggbagcat";
my $md5 = "6ab12fb588ac3d7cad0d95ec01369f13";

check_md5_output($md5, $cmd);
