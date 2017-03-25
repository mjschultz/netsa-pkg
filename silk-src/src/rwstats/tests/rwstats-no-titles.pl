#! /usr/bin/perl -w
# MD5: 64af832cf406c8df000906b40741b461
# TEST: ./rwstats --fields=dip --count=9 --top --no-titles --ipv6-policy=ignore ../../tests/data.rwf

use strict;
use SiLKTests;

my $rwstats = check_silk_app('rwstats');
my %file;
$file{data} = get_data_or_exit77('data');
my $cmd = "$rwstats --fields=dip --count=9 --top --no-titles --ipv6-policy=ignore $file{data}";
my $md5 = "64af832cf406c8df000906b40741b461";

check_md5_output($md5, $cmd);
