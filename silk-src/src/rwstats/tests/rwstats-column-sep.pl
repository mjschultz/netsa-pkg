#! /usr/bin/perl -w
# MD5: 32aca6a2c406f4524f62083b857226b1
# TEST: ./rwstats --count=9 --fields=dip --column-sep=/ --top --ipv6-policy=ignore ../../tests/data.rwf

use strict;
use SiLKTests;

my $rwstats = check_silk_app('rwstats');
my %file;
$file{data} = get_data_or_exit77('data');
my $cmd = "$rwstats --count=9 --fields=dip --column-sep=/ --top --ipv6-policy=ignore $file{data}";
my $md5 = "32aca6a2c406f4524f62083b857226b1";

check_md5_output($md5, $cmd);
