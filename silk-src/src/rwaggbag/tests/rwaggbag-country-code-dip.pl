#! /usr/bin/perl -w
# MD5: 3893c03ab584e9955571e14c58ce7e76
# TEST: ./rwaggbag --keys=application,dcc --counters=records,sum-bytes --ipv6-policy=ignore ../../tests/data.rwf | ./rwaggbagcat

use strict;
use SiLKTests;

my $rwaggbag = check_silk_app('rwaggbag');
my $rwaggbagcat = check_silk_app('rwaggbagcat');
my %file;
$file{data} = get_data_or_exit77('data');
$file{fake_cc} = get_data_or_exit77('fake_cc');
$ENV{SILK_COUNTRY_CODES} = "$SiLKTests::PWD/$file{fake_cc}";
my $cmd = "$rwaggbag --keys=application,dcc --counters=records,sum-bytes --ipv6-policy=ignore $file{data} | $rwaggbagcat";
my $md5 = "3893c03ab584e9955571e14c58ce7e76";

check_md5_output($md5, $cmd);
