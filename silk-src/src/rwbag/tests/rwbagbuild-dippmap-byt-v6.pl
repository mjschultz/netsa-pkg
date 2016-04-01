#! /usr/bin/perl -w
# MD5: ad5369bd1b803bdd9f577fef2d61178b
# TEST: ../rwcut/rwcut --fields=dip,bytes --no-title ../../tests/data-v6.rwf | ./rwbagbuild --bag-input=- --key-type=dip-pmap --pmap-file=../../tests/ip-map-v6.pmap | ./rwbagcat --pmap-file=../../tests/ip-map-v6.pmap

use strict;
use SiLKTests;

my $rwbagbuild = check_silk_app('rwbagbuild');
my $rwcut = check_silk_app('rwcut');
my $rwbagcat = check_silk_app('rwbagcat');
my %file;
$file{v6data} = get_data_or_exit77('v6data');
$file{v6_ip_map} = get_data_or_exit77('v6_ip_map');
check_features(qw(ipv6));
my $cmd = "$rwcut --fields=dip,bytes --no-title $file{v6data} | $rwbagbuild --bag-input=- --key-type=dip-pmap --pmap-file=$file{v6_ip_map} | $rwbagcat --pmap-file=$file{v6_ip_map}";
my $md5 = "ad5369bd1b803bdd9f577fef2d61178b";

check_md5_output($md5, $cmd);
