#! /usr/bin/perl -w
# MD5: b5262064a5a9bd9c8af3eda93469557d
# TEST: ./rwaggbag --keys=scc,proto --counters=sum-bytes,sum-packets ../../tests/data-v6.rwf | ./rwaggbagcat

use strict;
use SiLKTests;

my $rwaggbag = check_silk_app('rwaggbag');
my $rwaggbagcat = check_silk_app('rwaggbagcat');
my %file;
$file{v6data} = get_data_or_exit77('v6data');
$file{v6_fake_cc} = get_data_or_exit77('v6_fake_cc');
$ENV{SILK_COUNTRY_CODES} = "$SiLKTests::PWD/$file{v6_fake_cc}";
my $cmd = "$rwaggbag --keys=scc,proto --counters=sum-bytes,sum-packets $file{v6data} | $rwaggbagcat";
my $md5 = "b5262064a5a9bd9c8af3eda93469557d";

check_md5_output($md5, $cmd);
