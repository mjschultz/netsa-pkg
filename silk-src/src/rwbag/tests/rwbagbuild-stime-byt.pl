#! /usr/bin/perl -w
# MD5: a44fd193c0cfac68716b93216f61d4ab
# TEST: ../rwcut/rwcut --timestamp-format=epoch,no-msec --fields=stime,bytes --no-title ../../tests/data.rwf | ./rwbagbuild --bag-input=- --key-type=stime | ./rwbagcat --key-format=iso-time

use strict;
use SiLKTests;

my $rwbagbuild = check_silk_app('rwbagbuild');
my $rwcut = check_silk_app('rwcut');
my $rwbagcat = check_silk_app('rwbagcat');
my %file;
$file{data} = get_data_or_exit77('data');
my $cmd = "$rwcut --timestamp-format=epoch,no-msec --fields=stime,bytes --no-title $file{data} | $rwbagbuild --bag-input=- --key-type=stime | $rwbagcat --key-format=iso-time";
my $md5 = "a44fd193c0cfac68716b93216f61d4ab";

check_md5_output($md5, $cmd);
