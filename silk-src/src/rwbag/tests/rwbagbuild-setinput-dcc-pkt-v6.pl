#! /usr/bin/perl -w
# MD5: 7667722a25f01a02e1387b39e02de03c
# TEST: ../rwset/rwset --dip-file=stdout ../../tests/data-v6.rwf | ./rwbagbuild --set-input=- --key-type=dip-country | ./rwbagcat

use strict;
use SiLKTests;

my $rwbagbuild = check_silk_app('rwbagbuild');
my $rwset = check_silk_app('rwset');
my $rwbagcat = check_silk_app('rwbagcat');
my %file;
$file{v6data} = get_data_or_exit77('v6data');
$file{v6_fake_cc} = get_data_or_exit77('v6_fake_cc');
$ENV{SILK_COUNTRY_CODES} = "$SiLKTests::PWD/$file{v6_fake_cc}";
check_features(qw(ipv6));
my $cmd = "$rwset --dip-file=stdout $file{v6data} | $rwbagbuild --set-input=- --key-type=dip-country | $rwbagcat";
my $md5 = "7667722a25f01a02e1387b39e02de03c";

check_md5_output($md5, $cmd);
