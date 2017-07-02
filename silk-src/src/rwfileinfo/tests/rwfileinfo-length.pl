#! /usr/bin/perl -w
# MD5: varies
# TEST: ./rwfileinfo --fields=1,5-6 --no-title ../../tests/data.rwf

use strict;
use SiLKTests;

my $rwfileinfo = check_silk_app('rwfileinfo');
my %file;
$file{data} = get_data_or_exit77('data');

my $cmd = "$rwfileinfo --fields=1,5-6 --no-title $file{data}";
my $md5 = "3cfab176436347bea1d70cb0d68e7035";

check_md5_output($md5, $cmd);
