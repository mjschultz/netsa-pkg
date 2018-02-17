#! /usr/bin/perl -w
# MD5: f4330ef937eea082f83cb3025cc89eb0
# TEST: ./rwbag --sip-flows=stdout ../../tests/data-v6.rwf | ./rwbagcat --network-structure=v6:T60S

use strict;
use SiLKTests;

my $rwbagcat = check_silk_app('rwbagcat');
my $rwbag = check_silk_app('rwbag');
my %file;
$file{v6data} = get_data_or_exit77('v6data');
check_features(qw(ipv6));
my $cmd = "$rwbag --sip-flows=stdout $file{v6data} | $rwbagcat --network-structure=v6:T60S";
my $md5 = "f4330ef937eea082f83cb3025cc89eb0";

check_md5_output($md5, $cmd);
