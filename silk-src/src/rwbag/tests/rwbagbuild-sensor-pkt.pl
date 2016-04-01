#! /usr/bin/perl -w
# MD5: 2bf9df159b8d37b4f87bf86a408cc51e
# TEST: ../rwcut/rwcut --integer-sensor --fields=sensor,packets --no-title ../../tests/data.rwf | ./rwbagbuild --bag-input=stdin --key-type=sensor | ./rwbagcat --delimited

use strict;
use SiLKTests;

my $rwbagbuild = check_silk_app('rwbagbuild');
my $rwcut = check_silk_app('rwcut');
my $rwbagcat = check_silk_app('rwbagcat');
my %file;
$file{data} = get_data_or_exit77('data');
my $cmd = "$rwcut --integer-sensor --fields=sensor,packets --no-title $file{data} | $rwbagbuild --bag-input=stdin --key-type=sensor | $rwbagcat --delimited";
my $md5 = "2bf9df159b8d37b4f87bf86a408cc51e";

check_md5_output($md5, $cmd);
