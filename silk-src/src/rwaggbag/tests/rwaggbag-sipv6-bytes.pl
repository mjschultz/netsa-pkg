#! /usr/bin/perl -w
# MD5: 5d26c024dd7ceaf264d008706fca5fd9
# TEST: ./rwaggbag --key=sipv6 --counter=sum-bytes ../../tests/data-v6.rwf | ./rwaggbagcat

use strict;
use SiLKTests;

my $rwaggbag = check_silk_app('rwaggbag');
my $rwaggbagcat = check_silk_app('rwaggbagcat');
my %file;
$file{v6data} = get_data_or_exit77('v6data');
check_features(qw(ipv6));
my $cmd = "$rwaggbag --key=sipv6 --counter=sum-bytes $file{v6data} | $rwaggbagcat";
my $md5 = "5d26c024dd7ceaf264d008706fca5fd9";

check_md5_output($md5, $cmd);
