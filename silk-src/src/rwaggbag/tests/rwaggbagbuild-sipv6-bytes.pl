#! /usr/bin/perl -w
# MD5: 5d26c024dd7ceaf264d008706fca5fd9
# TEST: ../rwcut/rwcut --fields=sip,bytes --delimited --no-title ../../tests/data-v6.rwf | ./rwaggbagbuild --fields=sipv6,sum-bytes --output-path=/tmp/rwaggbagbuild-sipv6-bytes-tmp --compression-method=best && ./rwaggbagcat /tmp/rwaggbagbuild-sipv6-bytes-tmp

use strict;
use SiLKTests;

my $rwaggbagbuild = check_silk_app('rwaggbagbuild');
my $rwcut = check_silk_app('rwcut');
my $rwaggbagcat = check_silk_app('rwaggbagcat');
my %file;
$file{v6data} = get_data_or_exit77('v6data');
my %temp;
$temp{tmp} = make_tempname('tmp');
check_features(qw(ipv6));
my $cmd = "$rwcut --fields=sip,bytes --delimited --no-title $file{v6data} | $rwaggbagbuild --fields=sipv6,sum-bytes --output-path=$temp{tmp} --compression-method=best && $rwaggbagcat $temp{tmp}";
my $md5 = "5d26c024dd7ceaf264d008706fca5fd9";

check_md5_output($md5, $cmd);
