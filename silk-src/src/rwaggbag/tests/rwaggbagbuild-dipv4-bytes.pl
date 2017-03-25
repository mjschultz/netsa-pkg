#! /usr/bin/perl -w
# MD5: 81503a4604a5fcc874abce70604f24c1
# TEST: ../rwcut/rwcut --fields=dip,bytes ../../tests/data.rwf | ./rwaggbagbuild --fields=dipv4,sum-bytes | ./rwaggbagcat --ip-format=decimal

use strict;
use SiLKTests;

my $rwaggbagbuild = check_silk_app('rwaggbagbuild');
my $rwcut = check_silk_app('rwcut');
my $rwaggbagcat = check_silk_app('rwaggbagcat');
my %file;
$file{data} = get_data_or_exit77('data');
my $cmd = "$rwcut --fields=dip,bytes $file{data} | $rwaggbagbuild --fields=dipv4,sum-bytes | $rwaggbagcat --ip-format=decimal";
my $md5 = "81503a4604a5fcc874abce70604f24c1";

check_md5_output($md5, $cmd);
