#! /usr/bin/perl -w
# MD5: 3893c03ab584e9955571e14c58ce7e76
# TEST: ./rwaggbag --keys=application,dcc --counters=records,sum-bytes ../../tests/data-v6.rwf | ./rwaggbagcat

use strict;
use SiLKTests;

my $rwaggbag = check_silk_app('rwaggbag');
my $rwaggbagcat = check_silk_app('rwaggbagcat');
my %file;
$file{v6data} = get_data_or_exit77('v6data');
$file{v6_fake_cc} = get_data_or_exit77('v6_fake_cc');
$ENV{SILK_COUNTRY_CODES} = "$SiLKTests::PWD/$file{v6_fake_cc}";
my $cmd = "$rwaggbag --keys=application,dcc --counters=records,sum-bytes $file{v6data} | $rwaggbagcat";
my $md5 = "3893c03ab584e9955571e14c58ce7e76";

check_md5_output($md5, $cmd);
