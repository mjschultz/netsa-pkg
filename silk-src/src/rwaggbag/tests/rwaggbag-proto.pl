#! /usr/bin/perl -w
# MD5: 0d0186e8f404e76f44c996dc26359857
# TEST: ./rwaggbag --key=proto --counter=records ../../tests/data.rwf | ./rwaggbagcat

use strict;
use SiLKTests;

my $rwaggbag = check_silk_app('rwaggbag');
my $rwaggbagcat = check_silk_app('rwaggbagcat');
my %file;
$file{data} = get_data_or_exit77('data');
my $cmd = "$rwaggbag --key=proto --counter=records $file{data} | $rwaggbagcat";
my $md5 = "0d0186e8f404e76f44c996dc26359857";

check_md5_output($md5, $cmd);
