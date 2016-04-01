#! /usr/bin/perl -w
# MD5: 868c375a7c78296db90ead45f2ba6ed8
# TEST: ./rwbag --bag-file=dip-country,sum-packets,- ../../tests/data-v6.rwf | ./rwbagcat --delimited

use strict;
use SiLKTests;

my $rwbag = check_silk_app('rwbag');
my $rwbagcat = check_silk_app('rwbagcat');
my %file;
$file{v6data} = get_data_or_exit77('v6data');
$file{v6_fake_cc} = get_data_or_exit77('v6_fake_cc');
$ENV{SILK_COUNTRY_CODES} = "$SiLKTests::PWD/$file{v6_fake_cc}";
check_features(qw(ipv6));
my $cmd = "$rwbag --bag-file=dip-country,sum-packets,- $file{v6data} | $rwbagcat --delimited";
my $md5 = "868c375a7c78296db90ead45f2ba6ed8";

check_md5_output($md5, $cmd);
