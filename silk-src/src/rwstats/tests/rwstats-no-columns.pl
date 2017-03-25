#! /usr/bin/perl -w
# MD5: f125a8590fa4d3009335e85b8a3566b2
# TEST: ./rwstats --fields=dip --count=9 --top --no-column --column-sep=, --ipv6-policy=ignore ../../tests/data.rwf

use strict;
use SiLKTests;

my $rwstats = check_silk_app('rwstats');
my %file;
$file{data} = get_data_or_exit77('data');
my $cmd = "$rwstats --fields=dip --count=9 --top --no-column --column-sep=, --ipv6-policy=ignore $file{data}";
my $md5 = "f125a8590fa4d3009335e85b8a3566b2";

check_md5_output($md5, $cmd);
