#! /usr/bin/perl -w
# MD5: 964aedf5104f9f7407fdbd7ae76d9486
# TEST: ./rwstats --fields=sport --percentage=5 ../../tests/data.rwf

use strict;
use SiLKTests;

my $rwstats = check_silk_app('rwstats');
my %file;
$file{data} = get_data_or_exit77('data');
my $cmd = "$rwstats --fields=sport --percentage=5 $file{data}";
my $md5 = "964aedf5104f9f7407fdbd7ae76d9486";

check_md5_output($md5, $cmd);
