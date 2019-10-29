#! /usr/bin/perl -w
# MD5: 8525098f319ddaa09ea9334db04d919c
# TEST: ./rwaggbag --keys=scc,proto --counters=sum-bytes,sum-packets --ipv6-policy=ignore ../../tests/data.rwf | ./rwaggbagcat | ./rwaggbagbuild | ./rwaggbagcat

use strict;
use SiLKTests;

my $rwaggbagbuild = check_silk_app('rwaggbagbuild');
my $rwaggbag = check_silk_app('rwaggbag');
my $rwaggbagcat = check_silk_app('rwaggbagcat');
my %file;
$file{data} = get_data_or_exit77('data');
$file{fake_cc} = get_data_or_exit77('fake_cc');
$ENV{SILK_COUNTRY_CODES} = "$SiLKTests::PWD/$file{fake_cc}";
my $cmd = "$rwaggbag --keys=scc,proto --counters=sum-bytes,sum-packets --ipv6-policy=ignore $file{data} | $rwaggbagcat | $rwaggbagbuild | $rwaggbagcat";
my $md5 = "8525098f319ddaa09ea9334db04d919c";

check_md5_output($md5, $cmd);
