#! /usr/bin/perl -w
# MD5: caa006b7bc7ecce4819dacdd465ff8ad
# TEST: ./rwaggbag --key=dipv6 --counter=sum-packets --ipv6-policy=force ../../tests/data-v6.rwf | ./rwaggbagcat

use strict;
use SiLKTests;

my $rwaggbag = check_silk_app('rwaggbag');
my $rwaggbagcat = check_silk_app('rwaggbagcat');
my %file;
$file{v6data} = get_data_or_exit77('v6data');
check_features(qw(ipv6));
my $cmd = "$rwaggbag --key=dipv6 --counter=sum-packets --ipv6-policy=force $file{v6data} | $rwaggbagcat";
my $md5 = "caa006b7bc7ecce4819dacdd465ff8ad";

check_md5_output($md5, $cmd);
