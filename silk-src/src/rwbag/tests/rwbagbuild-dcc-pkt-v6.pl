#! /usr/bin/perl -w
# MD5: 868c375a7c78296db90ead45f2ba6ed8
# TEST: ../rwcut/rwcut --delimited --fields=dip,packets --no-title ../../tests/data-v6.rwf | ./rwbagbuild --bag-input=- --key-type=dip-country | ./rwbagcat --delimited

use strict;
use SiLKTests;

my $rwbagbuild = check_silk_app('rwbagbuild');
my $rwcut = check_silk_app('rwcut');
my $rwbagcat = check_silk_app('rwbagcat');
my %file;
$file{v6data} = get_data_or_exit77('v6data');
$file{v6_fake_cc} = get_data_or_exit77('v6_fake_cc');
$ENV{SILK_COUNTRY_CODES} = "$SiLKTests::PWD/$file{v6_fake_cc}";
check_features(qw(ipv6));
my $cmd = "$rwcut --delimited --fields=dip,packets --no-title $file{v6data} | $rwbagbuild --bag-input=- --key-type=dip-country | $rwbagcat --delimited";
my $md5 = "868c375a7c78296db90ead45f2ba6ed8";

check_md5_output($md5, $cmd);
