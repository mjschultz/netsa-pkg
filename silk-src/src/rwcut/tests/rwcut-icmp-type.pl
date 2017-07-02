#! /usr/bin/perl -w
# MD5: 7163931251659542ce69e89cd0e1613d
# TEST: ../rwfilter/rwfilter --proto=1 --pass=- ../../tests/data.rwf | ./rwcut --fields=iCode,proto,iType

use strict;
use SiLKTests;

my $rwcut = check_silk_app('rwcut');
my $rwfilter = check_silk_app('rwfilter');
my %file;
$file{data} = get_data_or_exit77('data');
my $cmd = "$rwfilter --proto=1 --pass=- $file{data} | $rwcut --fields=iCode,proto,iType";
my $md5 = "7163931251659542ce69e89cd0e1613d";

check_md5_output($md5, $cmd);
