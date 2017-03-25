#! /usr/bin/perl -w
# MD5: 4a8b3923f8436676975672c83c213096
# TEST: ./rwaggbag --key=sport,dport,proto --counter=records ../../tests/data.rwf | ./rwaggbagcat

use strict;
use SiLKTests;

my $rwaggbag = check_silk_app('rwaggbag');
my $rwaggbagcat = check_silk_app('rwaggbagcat');
my %file;
$file{data} = get_data_or_exit77('data');
my $cmd = "$rwaggbag --key=sport,dport,proto --counter=records $file{data} | $rwaggbagcat";
my $md5 = "4a8b3923f8436676975672c83c213096";

check_md5_output($md5, $cmd);
