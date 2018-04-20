#! /usr/bin/perl -w
# MD5: c83dd71269726bed997713ac037a0caa
# TEST: ./rwstats --fields=sport --values=sip-distinct --threshold=5000 --ipv6-policy=ignore ../../tests/data.rwf

use strict;
use SiLKTests;

my $rwstats = check_silk_app('rwstats');
my %file;
$file{data} = get_data_or_exit77('data');
my $cmd = "$rwstats --fields=sport --values=sip-distinct --threshold=5000 --ipv6-policy=ignore $file{data}";
my $md5 = "c83dd71269726bed997713ac037a0caa";

check_md5_output($md5, $cmd);
