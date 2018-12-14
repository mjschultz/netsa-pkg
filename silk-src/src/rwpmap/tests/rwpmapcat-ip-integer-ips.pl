#! /usr/bin/perl -w
# MD5: 75e17af494f4810bfc78e9ce0236fac0
# TEST: ./rwpmapcat --ip-format=decimal --no-columns --output-path=stdout --map-file ../../tests/ip-map.pmap

use strict;
use SiLKTests;

my $rwpmapcat = check_silk_app('rwpmapcat');
my %file;
$file{ip_map} = get_data_or_exit77('ip_map');
my $cmd = "$rwpmapcat --ip-format=decimal --no-columns --output-path=stdout --map-file $file{ip_map}";
my $md5 = "75e17af494f4810bfc78e9ce0236fac0";

check_md5_output($md5, $cmd);
