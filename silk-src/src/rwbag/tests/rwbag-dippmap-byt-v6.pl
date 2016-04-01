#! /usr/bin/perl -w
# MD5: ad5369bd1b803bdd9f577fef2d61178b
# TEST: ./rwbag --pmap-file=../../tests/ip-map-v6.pmap --bag-file=dip-pmap:service-host,bytes,- ../../tests/data-v6.rwf | ./rwbagcat --pmap-file=../../tests/ip-map-v6.pmap

use strict;
use SiLKTests;

my $rwbag = check_silk_app('rwbag');
my $rwbagcat = check_silk_app('rwbagcat');
my %file;
$file{v6data} = get_data_or_exit77('v6data');
$file{v6_ip_map} = get_data_or_exit77('v6_ip_map');
check_features(qw(ipv6));
my $cmd = "$rwbag --pmap-file=$file{v6_ip_map} --bag-file=dip-pmap:service-host,bytes,- $file{v6data} | $rwbagcat --pmap-file=$file{v6_ip_map}";
my $md5 = "ad5369bd1b803bdd9f577fef2d61178b";

check_md5_output($md5, $cmd);
