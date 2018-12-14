#! /usr/bin/perl -w
# MD5: fae5d8f08d8e3082678849ce4215bad8
# TEST: ./rwpmapcat --ip-format=zero-padded --output-type=ranges ../../tests/ip-map.pmap

use strict;
use SiLKTests;

my $rwpmapcat = check_silk_app('rwpmapcat');
my %file;
$file{ip_map} = get_data_or_exit77('ip_map');
my $cmd = "$rwpmapcat --ip-format=zero-padded --output-type=ranges $file{ip_map}";
my $md5 = "fae5d8f08d8e3082678849ce4215bad8";

check_md5_output($md5, $cmd);
