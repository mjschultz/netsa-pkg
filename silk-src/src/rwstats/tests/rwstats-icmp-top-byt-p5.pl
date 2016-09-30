#! /usr/bin/perl -w
# MD5: 77646ee10637c1741b66f381d6c2e8f6
# TEST: ../rwfilter/rwfilter --proto=1 --pass=- ../../tests/data.rwf | ./rwstats --fields=iType,iCode --byte --percentage=5

use strict;
use SiLKTests;

my $rwstats = check_silk_app('rwstats');
my $rwfilter = check_silk_app('rwfilter');
my %file;
$file{data} = get_data_or_exit77('data');
my $cmd = "$rwfilter --proto=1 --pass=- $file{data} | $rwstats --fields=iType,iCode --byte --percentage=5";
my $md5 = "77646ee10637c1741b66f381d6c2e8f6";

check_md5_output($md5, $cmd);
