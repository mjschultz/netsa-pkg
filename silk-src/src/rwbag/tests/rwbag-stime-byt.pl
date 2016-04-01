#! /usr/bin/perl -w
# MD5: a44fd193c0cfac68716b93216f61d4ab
# TEST: ./rwbag --bag-file=stime,sum-bytes,stdout ../../tests/data.rwf | ./rwbagcat --key-format=iso-time

use strict;
use SiLKTests;

my $rwbag = check_silk_app('rwbag');
my $rwbagcat = check_silk_app('rwbagcat');
my %file;
$file{data} = get_data_or_exit77('data');
my $cmd = "$rwbag --bag-file=stime,sum-bytes,stdout $file{data} | $rwbagcat --key-format=iso-time";
my $md5 = "a44fd193c0cfac68716b93216f61d4ab";

check_md5_output($md5, $cmd);
