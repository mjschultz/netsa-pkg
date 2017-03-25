#! /usr/bin/perl -w
# MD5: 4a8b3923f8436676975672c83c213096
# TEST: ../rwcut/rwcut --fields=sport,dport,proto ../../tests/data.rwf | ./rwaggbagbuild --constant-field=records=1 | ./rwaggbagcat

use strict;
use SiLKTests;

my $rwaggbagbuild = check_silk_app('rwaggbagbuild');
my $rwcut = check_silk_app('rwcut');
my $rwaggbagcat = check_silk_app('rwaggbagcat');
my %file;
$file{data} = get_data_or_exit77('data');
my $cmd = "$rwcut --fields=sport,dport,proto $file{data} | $rwaggbagbuild --constant-field=records=1 | $rwaggbagcat";
my $md5 = "4a8b3923f8436676975672c83c213096";

check_md5_output($md5, $cmd);
