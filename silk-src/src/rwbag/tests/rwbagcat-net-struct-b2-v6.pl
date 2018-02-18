#! /usr/bin/perl -w
# MD5: cf2b65f8d177f26dc799501b1e87a87a
# TEST: ./rwbagcat --network-structure=v6: ../../tests/bag2-v6.bag

use strict;
use SiLKTests;

my $rwbagcat = check_silk_app('rwbagcat');
my %file;
$file{v6bag2} = get_data_or_exit77('v6bag2');
check_features(qw(ipv6));
my $cmd = "$rwbagcat --network-structure=v6: $file{v6bag2}";
my $md5 = "cf2b65f8d177f26dc799501b1e87a87a";

check_md5_output($md5, $cmd);
