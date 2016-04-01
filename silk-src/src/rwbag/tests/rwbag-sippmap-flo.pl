#! /usr/bin/perl -w
# MD5: 78c950326d968a76e9e3838cee4e0a07
# TEST: ./rwbag --pmap-file=../../tests/ip-map.pmap --bag-file=sip-pmap:service-host,flows,- ../../tests/data.rwf | ./rwbagcat --pmap-file=../../tests/ip-map.pmap

use strict;
use SiLKTests;

my $rwbag = check_silk_app('rwbag');
my $rwbagcat = check_silk_app('rwbagcat');
my %file;
$file{data} = get_data_or_exit77('data');
$file{ip_map} = get_data_or_exit77('ip_map');
my $cmd = "$rwbag --pmap-file=$file{ip_map} --bag-file=sip-pmap:service-host,flows,- $file{data} | $rwbagcat --pmap-file=$file{ip_map}";
my $md5 = "78c950326d968a76e9e3838cee4e0a07";

check_md5_output($md5, $cmd);
