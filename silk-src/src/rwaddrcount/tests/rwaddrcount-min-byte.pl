#! /usr/bin/perl -w
# MD5: 406b13faa33aaeb86c71dee7bc2b5cf6
# TEST: ./rwaddrcount --print-rec --sort-ips --ip-format=decimal --min-byte=2000 ../../tests/data.rwf

use strict;
use SiLKTests;

my $rwaddrcount = check_silk_app('rwaddrcount');
my %file;
$file{data} = get_data_or_exit77('data');
my $cmd = "$rwaddrcount --print-rec --sort-ips --ip-format=decimal --min-byte=2000 $file{data}";
my $md5 = "406b13faa33aaeb86c71dee7bc2b5cf6";

check_md5_output($md5, $cmd);
