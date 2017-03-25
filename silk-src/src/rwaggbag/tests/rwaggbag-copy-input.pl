#! /usr/bin/perl -w
# MD5: fa4980304e68ab1a62b2e24366c80ba1
# TEST: ./rwaggbag --key=sport --counter=records --output-path=/dev/null --copy-input=stdout ../../tests/data.rwf | ./rwaggbag --key=sport --counter=records | ./rwaggbagcat

use strict;
use SiLKTests;

my $rwaggbag = check_silk_app('rwaggbag');
my $rwaggbagcat = check_silk_app('rwaggbagcat');
my %file;
$file{data} = get_data_or_exit77('data');
my $cmd = "$rwaggbag --key=sport --counter=records --output-path=/dev/null --copy-input=stdout $file{data} | $rwaggbag --key=sport --counter=records | $rwaggbagcat";
my $md5 = "fa4980304e68ab1a62b2e24366c80ba1";

check_md5_output($md5, $cmd);
