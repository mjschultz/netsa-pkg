#! /usr/bin/perl -w
# MD5: 8e68fdd738330d38540925361a59e2d9
# TEST: ../rwcut/rwcut --fields=stime,protocol ../../tests/data.rwf | ./rwaggbagbuild --constant=records=1 | ./rwaggbagcat

use strict;
use SiLKTests;

my $rwaggbagbuild = check_silk_app('rwaggbagbuild');
my $rwcut = check_silk_app('rwcut');
my $rwaggbagcat = check_silk_app('rwaggbagcat');
my %file;
$file{data} = get_data_or_exit77('data');
my $cmd = "$rwcut --fields=stime,protocol $file{data} | $rwaggbagbuild --constant=records=1 | $rwaggbagcat";
my $md5 = "8e68fdd738330d38540925361a59e2d9";

check_md5_output($md5, $cmd);
