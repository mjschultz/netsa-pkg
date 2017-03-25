#! /usr/bin/perl -w
# MD5: e849c87d4da1898739035f0a69acec3a
# TEST: ./rwaggbag --key=sport --counter=records ../../tests/data.rwf | ./rwaggbagcat --delimited

use strict;
use SiLKTests;

my $rwaggbag = check_silk_app('rwaggbag');
my $rwaggbagcat = check_silk_app('rwaggbagcat');
my %file;
$file{data} = get_data_or_exit77('data');
my $cmd = "$rwaggbag --key=sport --counter=records $file{data} | $rwaggbagcat --delimited";
my $md5 = "e849c87d4da1898739035f0a69acec3a";

check_md5_output($md5, $cmd);
