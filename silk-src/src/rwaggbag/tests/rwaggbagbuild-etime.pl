#! /usr/bin/perl -w
# MD5: 0cdab78207c390dfe60cea93f6a57b75
# TEST: ../rwstats/rwuniq --fields=etime ../../tests/data.rwf | ./rwaggbagbuild | ./rwaggbagcat --timestamp-format=epoch

use strict;
use SiLKTests;

my $rwaggbagbuild = check_silk_app('rwaggbagbuild');
my $rwuniq = check_silk_app('rwuniq');
my $rwaggbagcat = check_silk_app('rwaggbagcat');
my %file;
$file{data} = get_data_or_exit77('data');
my $cmd = "$rwuniq --fields=etime $file{data} | $rwaggbagbuild | $rwaggbagcat --timestamp-format=epoch";
my $md5 = "0cdab78207c390dfe60cea93f6a57b75";

check_md5_output($md5, $cmd);
