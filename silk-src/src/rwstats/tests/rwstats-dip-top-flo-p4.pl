#! /usr/bin/perl -w
# MD5: cc3dbb0dc6981d21e0fd22cbdb1b315c
# TEST: ./rwstats --fields=dip --values=records --percentage=4 --ipv6-policy=ignore ../../tests/data.rwf

use strict;
use SiLKTests;

my $rwstats = check_silk_app('rwstats');
my %file;
$file{data} = get_data_or_exit77('data');
my $cmd = "$rwstats --fields=dip --values=records --percentage=4 --ipv6-policy=ignore $file{data}";
my $md5 = "cc3dbb0dc6981d21e0fd22cbdb1b315c";

check_md5_output($md5, $cmd);
