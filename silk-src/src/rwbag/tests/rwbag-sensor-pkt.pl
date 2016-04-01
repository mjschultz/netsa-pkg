#! /usr/bin/perl -w
# MD5: 2bf9df159b8d37b4f87bf86a408cc51e
# TEST: ./rwbag --bag-file=sensor,sum-packets,- ../../tests/data.rwf | ./rwbagcat --delimited

use strict;
use SiLKTests;

my $rwbag = check_silk_app('rwbag');
my $rwbagcat = check_silk_app('rwbagcat');
my %file;
$file{data} = get_data_or_exit77('data');
my $cmd = "$rwbag --bag-file=sensor,sum-packets,- $file{data} | $rwbagcat --delimited";
my $md5 = "2bf9df159b8d37b4f87bf86a408cc51e";

check_md5_output($md5, $cmd);
