#! /usr/bin/perl -w
# MD5: 9804e8a805598986a20a64c8fbf4e1d3
# TEST: cat ../../tests/data.rwf | ./rwbag --bag-file=proto,packet,-  | ./rwbagcat --sort-counter=increasing

use strict;
use SiLKTests;

my $rwbagcat = check_silk_app('rwbagcat');
my $rwbag = check_silk_app('rwbag');
my %file;
$file{data} = get_data_or_exit77('data');
my $cmd = "cat $file{data} | $rwbag --bag-file=proto,packet,-  | $rwbagcat --sort-counter=increasing";
my $md5 = "9804e8a805598986a20a64c8fbf4e1d3";

check_md5_output($md5, $cmd);
