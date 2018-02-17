#! /usr/bin/perl -w
# MD5: 4b9b009fdb8f60ed7ac69f398884f286
# TEST: ./rwbagcat --network-structure=14TS,14 ../../tests/bag2-v4.bag

use strict;
use SiLKTests;

my $rwbagcat = check_silk_app('rwbagcat');
my %file;
$file{v4bag2} = get_data_or_exit77('v4bag2');
my $cmd = "$rwbagcat --network-structure=14TS,14 $file{v4bag2}";
my $md5 = "4b9b009fdb8f60ed7ac69f398884f286";

check_md5_output($md5, $cmd);
