#! /usr/bin/perl -w
# MD5: 98ef6e18dd7cb8c06b2d3a3751ffa4ff
# TEST: ./rwbag --sip-flows=stdout ../../tests/data-v6.rwf | ./rwbagcat --network-structure=v6:

use strict;
use SiLKTests;

my $rwbagcat = check_silk_app('rwbagcat');
my $rwbag = check_silk_app('rwbag');
my %file;
$file{v6data} = get_data_or_exit77('v6data');
check_features(qw(ipv6));
my $cmd = "$rwbag --sip-flows=stdout $file{v6data} | $rwbagcat --network-structure=v6:";
my $md5 = "98ef6e18dd7cb8c06b2d3a3751ffa4ff";

check_md5_output($md5, $cmd);
