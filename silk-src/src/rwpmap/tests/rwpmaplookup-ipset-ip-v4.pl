#! /usr/bin/perl -w
# MD5: e1c6faa930123fe9e4cbc687d1c7f46e
# TEST: echo 192.168.72.72 | ../rwset/rwsetbuild | ./rwpmaplookup --ipset-files --map-file=../../tests/ip-map.pmap --ip-format=decimal --fields=key,value,input

use strict;
use SiLKTests;

my $rwpmaplookup = check_silk_app('rwpmaplookup');
my $rwsetbuild = check_silk_app('rwsetbuild');
my %file;
$file{ip_map} = get_data_or_exit77('ip_map');
my $cmd = "echo 192.168.72.72 | $rwsetbuild | $rwpmaplookup --ipset-files --map-file=$file{ip_map} --ip-format=decimal --fields=key,value,input";
my $md5 = "e1c6faa930123fe9e4cbc687d1c7f46e";

check_md5_output($md5, $cmd);
