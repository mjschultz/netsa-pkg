#! /usr/bin/perl -w
# MD5: 7ac81abd69e6be8a75bc2a199cc2b625
# TEST: ./rwstats --fields=dip --count=9 --delimited=, --top --ipv6-policy=ignore ../../tests/data.rwf

use strict;
use SiLKTests;

my $rwstats = check_silk_app('rwstats');
my %file;
$file{data} = get_data_or_exit77('data');
my $cmd = "$rwstats --fields=dip --count=9 --delimited=, --top --ipv6-policy=ignore $file{data}";
my $md5 = "7ac81abd69e6be8a75bc2a199cc2b625";

check_md5_output($md5, $cmd);
