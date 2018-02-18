#! /usr/bin/perl -w
# MD5: ca7d2624f6a54e48b8eebc0a5f531fe6
# TEST: ./rwbagcat --network-structure=v4: ../../tests/bag3-v6.bag

use strict;
use SiLKTests;

my $rwbagcat = check_silk_app('rwbagcat');
my %file;
$file{v6bag3} = get_data_or_exit77('v6bag3');
check_features(qw(ipv6));
my $cmd = "$rwbagcat --network-structure=v4: $file{v6bag3}";
my $md5 = "ca7d2624f6a54e48b8eebc0a5f531fe6";

check_md5_output($md5, $cmd);
