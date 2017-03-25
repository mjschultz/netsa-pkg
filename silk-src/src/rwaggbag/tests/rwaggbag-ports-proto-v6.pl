#! /usr/bin/perl -w
# MD5: 6d484042e8aceefbc1f06594b7a36d39
# TEST: ./rwaggbag --key=sport,dport,proto --counter=records --output-path=/tmp/rwaggbag-ports-proto-v6-tmp ../../tests/data-v6.rwf && ./rwaggbagcat /tmp/rwaggbag-ports-proto-v6-tmp

use strict;
use SiLKTests;

my $rwaggbag = check_silk_app('rwaggbag');
my $rwaggbagcat = check_silk_app('rwaggbagcat');
my %file;
$file{v6data} = get_data_or_exit77('v6data');
my %temp;
$temp{tmp} = make_tempname('tmp');
check_features(qw(ipv6));
my $cmd = "$rwaggbag --key=sport,dport,proto --counter=records --output-path=$temp{tmp} $file{v6data} && $rwaggbagcat $temp{tmp}";
my $md5 = "6d484042e8aceefbc1f06594b7a36d39";

check_md5_output($md5, $cmd);
