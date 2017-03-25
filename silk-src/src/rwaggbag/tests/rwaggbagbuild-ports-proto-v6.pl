#! /usr/bin/perl -w
# MD5: 6d484042e8aceefbc1f06594b7a36d39
# TEST: ../rwstats/rwuniq --fields=sport,dport,proto ../../tests/data-v6.rwf --output-path=/tmp/rwaggbagbuild-ports-proto-v6-tmp && ./rwaggbagbuild /tmp/rwaggbagbuild-ports-proto-v6-tmp | ./rwaggbagcat

use strict;
use SiLKTests;

my $rwaggbagbuild = check_silk_app('rwaggbagbuild');
my $rwuniq = check_silk_app('rwuniq');
my $rwaggbagcat = check_silk_app('rwaggbagcat');
my %file;
$file{v6data} = get_data_or_exit77('v6data');
my %temp;
$temp{tmp} = make_tempname('tmp');
check_features(qw(ipv6));
my $cmd = "$rwuniq --fields=sport,dport,proto $file{v6data} --output-path=$temp{tmp} && $rwaggbagbuild $temp{tmp} | $rwaggbagcat";
my $md5 = "6d484042e8aceefbc1f06594b7a36d39";

check_md5_output($md5, $cmd);
