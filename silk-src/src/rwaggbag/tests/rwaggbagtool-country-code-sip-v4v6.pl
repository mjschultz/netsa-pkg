#! /usr/bin/perl -w
# MD5: f08799a650eda95540654cdc81e25597
# TEST: ./rwaggbag --keys=scc,proto --counters=sum-bytes,sum-packets --ipv6-policy=force --output-path=/tmp/rwaggbagtool-country-code-sip-v4v6-tmp ../../tests/data.rwf && ./rwaggbag --keys=scc,proto --counters=sum-bytes,sum-packets ../../tests/data-v6.rwf | ./rwaggbagtool --add - /tmp/rwaggbagtool-country-code-sip-v4v6-tmp | ./rwaggbagcat

use strict;
use SiLKTests;

my $rwaggbagtool = check_silk_app('rwaggbagtool');
my $rwaggbag = check_silk_app('rwaggbag');
my $rwaggbagcat = check_silk_app('rwaggbagcat');
my %file;
$file{data} = get_data_or_exit77('data');
$file{v6data} = get_data_or_exit77('v6data');
$file{v6_fake_cc} = get_data_or_exit77('v6_fake_cc');
my %temp;
$temp{tmp} = make_tempname('tmp');
$ENV{SILK_COUNTRY_CODES} = "$SiLKTests::PWD/$file{v6_fake_cc}";
my $cmd = "$rwaggbag --keys=scc,proto --counters=sum-bytes,sum-packets --ipv6-policy=force --output-path=$temp{tmp} $file{data} && $rwaggbag --keys=scc,proto --counters=sum-bytes,sum-packets $file{v6data} | $rwaggbagtool --add - $temp{tmp} | $rwaggbagcat";
my $md5 = "f08799a650eda95540654cdc81e25597";

check_md5_output($md5, $cmd);
