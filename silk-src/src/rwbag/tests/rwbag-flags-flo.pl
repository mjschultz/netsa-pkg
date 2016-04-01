#! /usr/bin/perl -w
# MD5: 49e23e404b1b7b59ec75dcf4999154b0
# TEST: ./rwbag --bag-file=flags,records,- ../../tests/data.rwf | ./rwbagcat

use strict;
use SiLKTests;

my $rwbag = check_silk_app('rwbag');
my $rwbagcat = check_silk_app('rwbagcat');
my %file;
$file{data} = get_data_or_exit77('data');
my $cmd = "$rwbag --bag-file=flags,records,- $file{data} | $rwbagcat";
my $md5 = "49e23e404b1b7b59ec75dcf4999154b0";

check_md5_output($md5, $cmd);
