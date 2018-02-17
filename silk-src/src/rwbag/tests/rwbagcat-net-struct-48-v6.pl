#! /usr/bin/perl -w
# MD5: 88e26bd8da3b2027db05421ccd446c00
# TEST: ./rwbag --sip-flows=stdout ../../tests/data-v6.rwf | ./rwbagcat --network-structure=v6:48,T/48,64,123,112

use strict;
use SiLKTests;

my $rwbagcat = check_silk_app('rwbagcat');
my $rwbag = check_silk_app('rwbag');
my %file;
$file{v6data} = get_data_or_exit77('v6data');
check_features(qw(ipv6));
my $cmd = "$rwbag --sip-flows=stdout $file{v6data} | $rwbagcat --network-structure=v6:48,T/48,64,123,112";
my $md5 = "88e26bd8da3b2027db05421ccd446c00";

check_md5_output($md5, $cmd);
