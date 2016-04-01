#! /usr/bin/perl -w
# MD5: 49e23e404b1b7b59ec75dcf4999154b0
# TEST: ../rwcut/rwcut --integer-tcp-flags --fields=flags --delimited --no-title ../../tests/data.rwf | ./rwbagbuild --bag-input=- --key-type=flags | ./rwbagcat

use strict;
use SiLKTests;

my $rwbagbuild = check_silk_app('rwbagbuild');
my $rwcut = check_silk_app('rwcut');
my $rwbagcat = check_silk_app('rwbagcat');
my %file;
$file{data} = get_data_or_exit77('data');
my $cmd = "$rwcut --integer-tcp-flags --fields=flags --delimited --no-title $file{data} | $rwbagbuild --bag-input=- --key-type=flags | $rwbagcat";
my $md5 = "49e23e404b1b7b59ec75dcf4999154b0";

check_md5_output($md5, $cmd);
