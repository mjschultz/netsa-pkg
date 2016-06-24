#! /usr/bin/perl -w
# MD5: 09c0bf6eeed620548bec055d659c6b04
# TEST: ./rwbag --bag-file=sipv4,byte,stdout ../../tests/data.rwf | ./rwbagcat --key-format=zero-padded --sort-counter

use strict;
use SiLKTests;

my $rwbagcat = check_silk_app('rwbagcat');
my $rwbag = check_silk_app('rwbag');
my %file;
$file{data} = get_data_or_exit77('data');
my $cmd = "$rwbag --bag-file=sipv4,byte,stdout $file{data} | $rwbagcat --key-format=zero-padded --sort-counter";
my $md5 = "09c0bf6eeed620548bec055d659c6b04";

check_md5_output($md5, $cmd);
