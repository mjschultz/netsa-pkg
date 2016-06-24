#! /usr/bin/perl -w
# MD5: 4fd9be3abee4aa25997ac7a824e7f569
# TEST: ./rwbag --bag-file=proto,packet,stdout ../../tests/data.rwf | ./rwbagcat --sort-counter=decreasing

use strict;
use SiLKTests;

my $rwbagcat = check_silk_app('rwbagcat');
my $rwbag = check_silk_app('rwbag');
my %file;
$file{data} = get_data_or_exit77('data');
my $cmd = "$rwbag --bag-file=proto,packet,stdout $file{data} | $rwbagcat --sort-counter=decreasing";
my $md5 = "4fd9be3abee4aa25997ac7a824e7f569";

check_md5_output($md5, $cmd);
