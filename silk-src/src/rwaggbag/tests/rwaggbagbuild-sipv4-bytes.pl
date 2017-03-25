#! /usr/bin/perl -w
# MD5: c70d6beade1645b5b0daca34c882b74e
# TEST: ../rwcut/rwcut --fields=sip,bytes --delimited=, ../../tests/data.rwf | ./rwaggbagbuild --fields=sipv4,sum-bytes --column-separator=, | ./rwaggbagcat

use strict;
use SiLKTests;

my $rwaggbagbuild = check_silk_app('rwaggbagbuild');
my $rwcut = check_silk_app('rwcut');
my $rwaggbagcat = check_silk_app('rwaggbagcat');
my %file;
$file{data} = get_data_or_exit77('data');
my $cmd = "$rwcut --fields=sip,bytes --delimited=, $file{data} | $rwaggbagbuild --fields=sipv4,sum-bytes --column-separator=, | $rwaggbagcat";
my $md5 = "c70d6beade1645b5b0daca34c882b74e";

check_md5_output($md5, $cmd);
