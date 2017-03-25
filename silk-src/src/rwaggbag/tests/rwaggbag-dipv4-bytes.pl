#! /usr/bin/perl -w
# MD5: 81503a4604a5fcc874abce70604f24c1
# TEST: ./rwaggbag --key=dipv4 --counter=sum-bytes ../../tests/data.rwf | ./rwaggbagcat --ip-format=decimal

use strict;
use SiLKTests;

my $rwaggbag = check_silk_app('rwaggbag');
my $rwaggbagcat = check_silk_app('rwaggbagcat');
my %file;
$file{data} = get_data_or_exit77('data');
my $cmd = "$rwaggbag --key=dipv4 --counter=sum-bytes $file{data} | $rwaggbagcat --ip-format=decimal";
my $md5 = "81503a4604a5fcc874abce70604f24c1";

check_md5_output($md5, $cmd);
