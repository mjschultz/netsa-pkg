#! /usr/bin/perl -w
# MD5: c70d6beade1645b5b0daca34c882b74e
# TEST: ./rwaggbag --key=sipv4 --counter=sum-bytes ../../tests/data.rwf | ./rwaggbagcat

use strict;
use SiLKTests;

my $rwaggbag = check_silk_app('rwaggbag');
my $rwaggbagcat = check_silk_app('rwaggbagcat');
my %file;
$file{data} = get_data_or_exit77('data');
my $cmd = "$rwaggbag --key=sipv4 --counter=sum-bytes $file{data} | $rwaggbagcat";
my $md5 = "c70d6beade1645b5b0daca34c882b74e";

check_md5_output($md5, $cmd);
