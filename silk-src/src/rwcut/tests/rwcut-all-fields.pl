#! /usr/bin/perl -w
# MD5: f1517556fde2efa74378e65ce2ab492f
# TEST: ./rwcut --all-fields --delimited ../../tests/data.rwf

use strict;
use SiLKTests;

my $rwcut = check_silk_app('rwcut');
my %file;
$file{data} = get_data_or_exit77('data');
my $cmd = "$rwcut --all-fields --delimited $file{data}";
my $md5 = "f1517556fde2efa74378e65ce2ab492f";

check_md5_output($md5, $cmd);
