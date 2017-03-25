#! /usr/bin/perl -w
# MD5: 2abc6763e88b2aeecba03fbbf6ec12b3
# TEST: ./rwaggbag --key=sport,dport --counter=sum-bytes ../../tests/data.rwf | ./rwaggbagtool --to-bag=sport,sum-bytes | ../rwbag/rwbagcat

use strict;
use SiLKTests;

my $rwaggbagtool = check_silk_app('rwaggbagtool');
my $rwaggbag = check_silk_app('rwaggbag');
my $rwbagcat = check_silk_app('rwbagcat');
my %file;
$file{data} = get_data_or_exit77('data');
my $cmd = "$rwaggbag --key=sport,dport --counter=sum-bytes $file{data} | $rwaggbagtool --to-bag=sport,sum-bytes | $rwbagcat";
my $md5 = "2abc6763e88b2aeecba03fbbf6ec12b3";

check_md5_output($md5, $cmd);
