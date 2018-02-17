#! /usr/bin/perl -w
# MD5: 6c80660db1a00d095f591ff3baf99a95
# TEST: ./rwbagcat --network-structure=v6:61,63T/60,61,63,64,62,41 ../../tests/bag2-v6.bag

use strict;
use SiLKTests;

my $rwbagcat = check_silk_app('rwbagcat');
my %file;
$file{v6bag2} = get_data_or_exit77('v6bag2');
check_features(qw(ipv6));
my $cmd = "$rwbagcat --network-structure=v6:61,63T/60,61,63,64,62,41 $file{v6bag2}";
my $md5 = "6c80660db1a00d095f591ff3baf99a95";

check_md5_output($md5, $cmd);
