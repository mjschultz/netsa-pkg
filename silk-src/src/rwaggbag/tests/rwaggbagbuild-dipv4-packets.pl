#! /usr/bin/perl -w
# MD5: 7b881b469fa9eb12fbdd09cec5ca9c3e
# TEST: ../rwstats/rwuniq --fields=dip --value=packets --no-final ../../tests/data.rwf | sed 1s/dIP/dIPv4/ | sed 1s/Packets/sum-Packets/ | ./rwaggbagbuild | ./rwaggbagcat --ip-format=zero-padded

use strict;
use SiLKTests;

my $rwaggbagbuild = check_silk_app('rwaggbagbuild');
my $rwuniq = check_silk_app('rwuniq');
my $rwaggbagcat = check_silk_app('rwaggbagcat');
my %file;
$file{data} = get_data_or_exit77('data');
my $cmd = "$rwuniq --fields=dip --value=packets --no-final $file{data} | sed 1s/dIP/dIPv4/ | sed 1s/Packets/sum-Packets/ | $rwaggbagbuild | $rwaggbagcat --ip-format=zero-padded";
my $md5 = "7b881b469fa9eb12fbdd09cec5ca9c3e";

check_md5_output($md5, $cmd);
