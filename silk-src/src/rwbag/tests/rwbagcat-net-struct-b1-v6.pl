#! /usr/bin/perl -w
# MD5: 0b6f14c21dffd60ceaf7dfbddade3b56
# TEST: ./rwbagcat --network-structure=v6:T/48,61,62,63,64 ../../tests/bag1-v6.bag

use strict;
use SiLKTests;

my $rwbagcat = check_silk_app('rwbagcat');
my %file;
$file{v6bag1} = get_data_or_exit77('v6bag1');
check_features(qw(ipv6));
my $cmd = "$rwbagcat --network-structure=v6:T/48,61,62,63,64 $file{v6bag1}";
my $md5 = "0b6f14c21dffd60ceaf7dfbddade3b56";

check_md5_output($md5, $cmd);
