#! /usr/bin/perl -w
# MD5: 37f2024b31b14c5d0ca9c02fb08cbcc8
# TEST: ./rwaggbag --key=sport --counter=records ../../tests/data.rwf | ./rwaggbagcat --no-column --column-sep=,

use strict;
use SiLKTests;

my $rwaggbag = check_silk_app('rwaggbag');
my $rwaggbagcat = check_silk_app('rwaggbagcat');
my %file;
$file{data} = get_data_or_exit77('data');
my $cmd = "$rwaggbag --key=sport --counter=records $file{data} | $rwaggbagcat --no-column --column-sep=,";
my $md5 = "37f2024b31b14c5d0ca9c02fb08cbcc8";

check_md5_output($md5, $cmd);
