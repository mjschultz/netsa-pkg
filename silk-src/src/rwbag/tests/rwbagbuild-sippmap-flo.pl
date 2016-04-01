#! /usr/bin/perl -w
# MD5: 78c950326d968a76e9e3838cee4e0a07
# TEST: ../rwcut/rwcut --no-final-delimiter --fields=sip --no-title ../../tests/data.rwf | ./rwbagbuild --pmap-file=../../tests/ip-map.pmap --bag-input=- --key-type=sip-pmap | ./rwbagcat --pmap-file=../../tests/ip-map.pmap

use strict;
use SiLKTests;

my $rwbagbuild = check_silk_app('rwbagbuild');
my $rwcut = check_silk_app('rwcut');
my $rwbagcat = check_silk_app('rwbagcat');
my %file;
$file{data} = get_data_or_exit77('data');
$file{ip_map} = get_data_or_exit77('ip_map');
my $cmd = "$rwcut --no-final-delimiter --fields=sip --no-title $file{data} | $rwbagbuild --pmap-file=$file{ip_map} --bag-input=- --key-type=sip-pmap | $rwbagcat --pmap-file=$file{ip_map}";
my $md5 = "78c950326d968a76e9e3838cee4e0a07";

check_md5_output($md5, $cmd);
