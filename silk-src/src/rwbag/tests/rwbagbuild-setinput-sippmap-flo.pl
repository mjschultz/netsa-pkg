#! /usr/bin/perl -w
# MD5: 592a93937ad914f7db5e9970d68b9d41
# TEST: ../rwset/rwset --sip-file=- ../../tests/data.rwf | ./rwbagbuild --pmap-file=../../tests/ip-map.pmap --set-input=stdin --key-type=sip-pmap | ./rwbagcat --pmap-file=../../tests/ip-map.pmap

use strict;
use SiLKTests;

my $rwbagbuild = check_silk_app('rwbagbuild');
my $rwset = check_silk_app('rwset');
my $rwbagcat = check_silk_app('rwbagcat');
my %file;
$file{data} = get_data_or_exit77('data');
$file{ip_map} = get_data_or_exit77('ip_map');
my $cmd = "$rwset --sip-file=- $file{data} | $rwbagbuild --pmap-file=$file{ip_map} --set-input=stdin --key-type=sip-pmap | $rwbagcat --pmap-file=$file{ip_map}";
my $md5 = "592a93937ad914f7db5e9970d68b9d41";

check_md5_output($md5, $cmd);
