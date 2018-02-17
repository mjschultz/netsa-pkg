#! /usr/bin/perl -w
# MD5: b3cc08131172a4a54c78f468ea73d7a5
# TEST: ./rwbagcat --network-structure=v4:T/8,13,14,15,16 ../../tests/bag1-v4.bag

use strict;
use SiLKTests;

my $rwbagcat = check_silk_app('rwbagcat');
my %file;
$file{v4bag1} = get_data_or_exit77('v4bag1');
my $cmd = "$rwbagcat --network-structure=v4:T/8,13,14,15,16 $file{v4bag1}";
my $md5 = "b3cc08131172a4a54c78f468ea73d7a5";

check_md5_output($md5, $cmd);
