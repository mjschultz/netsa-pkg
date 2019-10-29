#! /usr/bin/perl -w
# MD5: 8525098f319ddaa09ea9334db04d919c
# TEST: ../rwcut/rwcut --fields=scc,proto,bytes,packets ../../tests/data.rwf | ./rwaggbagbuild --fields=scc,proto,sum-bytes,sum-packets | ./rwaggbagcat

use strict;
use SiLKTests;

my $rwaggbagbuild = check_silk_app('rwaggbagbuild');
my $rwcut = check_silk_app('rwcut');
my $rwaggbagcat = check_silk_app('rwaggbagcat');
my %file;
$file{data} = get_data_or_exit77('data');
$file{fake_cc} = get_data_or_exit77('fake_cc');
$ENV{SILK_COUNTRY_CODES} = "$SiLKTests::PWD/$file{fake_cc}";
my $cmd = "$rwcut --fields=scc,proto,bytes,packets $file{data} | $rwaggbagbuild --fields=scc,proto,sum-bytes,sum-packets | $rwaggbagcat";
my $md5 = "8525098f319ddaa09ea9334db04d919c";

check_md5_output($md5, $cmd);
