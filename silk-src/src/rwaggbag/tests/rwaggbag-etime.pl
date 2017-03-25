#! /usr/bin/perl -w
# MD5: 0cdab78207c390dfe60cea93f6a57b75
# TEST: ./rwaggbag --key=etime --counter=records ../../tests/data.rwf | ./rwaggbagcat --timestamp-format=epoch

use strict;
use SiLKTests;

my $rwaggbag = check_silk_app('rwaggbag');
my $rwaggbagcat = check_silk_app('rwaggbagcat');
my %file;
$file{data} = get_data_or_exit77('data');
my $cmd = "$rwaggbag --key=etime --counter=records $file{data} | $rwaggbagcat --timestamp-format=epoch";
my $md5 = "0cdab78207c390dfe60cea93f6a57b75";

check_md5_output($md5, $cmd);
