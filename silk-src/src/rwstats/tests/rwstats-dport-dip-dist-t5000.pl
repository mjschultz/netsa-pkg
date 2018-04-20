#! /usr/bin/perl -w
# MD5: 5e45a97228ead801f3e2d37702012e0d
# TEST: ./rwstats --fields=dport --values=dip-distinct,records --threshold=5000 --ipv6-policy=ignore ../../tests/data.rwf

use strict;
use SiLKTests;

my $rwstats = check_silk_app('rwstats');
my %file;
$file{data} = get_data_or_exit77('data');
my $cmd = "$rwstats --fields=dport --values=dip-distinct,records --threshold=5000 --ipv6-policy=ignore $file{data}";
my $md5 = "5e45a97228ead801f3e2d37702012e0d";

check_md5_output($md5, $cmd);
