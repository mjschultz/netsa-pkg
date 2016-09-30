#! /usr/bin/perl -w
# MD5: eef2b3bd1bc3f58e8605b38fa4794ed2
# TEST: ./rwsettool --symmetric-difference ../../tests/set1-v4.set ../../tests/set2-v4.set | ./rwsetcat --cidr

use strict;
use SiLKTests;

my $rwsettool = check_silk_app('rwsettool');
my $rwsetcat = check_silk_app('rwsetcat');
my %file;
$file{v4set1} = get_data_or_exit77('v4set1');
$file{v4set2} = get_data_or_exit77('v4set2');
my $cmd = "$rwsettool --symmetric-difference $file{v4set1} $file{v4set2} | $rwsetcat --cidr";
my $md5 = "eef2b3bd1bc3f58e8605b38fa4794ed2";

check_md5_output($md5, $cmd);
