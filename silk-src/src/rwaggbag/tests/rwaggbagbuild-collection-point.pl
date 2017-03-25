#! /usr/bin/perl -w
# MD5: c24fce91b45ebaed9dcaf48bb74a1b9b
# TEST: ../rwstats/rwuniq --fields=sensor,class,type ../../tests/data.rwf | ./rwaggbagbuild | ./rwaggbagcat

use strict;
use SiLKTests;

my $rwaggbagbuild = check_silk_app('rwaggbagbuild');
my $rwuniq = check_silk_app('rwuniq');
my $rwaggbagcat = check_silk_app('rwaggbagcat');
my %file;
$file{data} = get_data_or_exit77('data');
my $cmd = "$rwuniq --fields=sensor,class,type $file{data} | $rwaggbagbuild | $rwaggbagcat";
my $md5 = "c24fce91b45ebaed9dcaf48bb74a1b9b";

check_md5_output($md5, $cmd);
