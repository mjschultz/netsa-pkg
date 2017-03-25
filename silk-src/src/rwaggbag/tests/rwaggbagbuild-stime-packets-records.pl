#! /usr/bin/perl -w
# MD5: e1085d609b56dcfc2dfb4f3edc0f8681
# TEST: ../rwcut/rwcut --fields=stime,packets,protocol ../../tests/data.rwf | ./rwaggbagbuild --fields=stime,sum-packets,ignore --constant-field=records=1 | ./rwaggbagcat

use strict;
use SiLKTests;

my $rwaggbagbuild = check_silk_app('rwaggbagbuild');
my $rwcut = check_silk_app('rwcut');
my $rwaggbagcat = check_silk_app('rwaggbagcat');
my %file;
$file{data} = get_data_or_exit77('data');
my $cmd = "$rwcut --fields=stime,packets,protocol $file{data} | $rwaggbagbuild --fields=stime,sum-packets,ignore --constant-field=records=1 | $rwaggbagcat";
my $md5 = "e1085d609b56dcfc2dfb4f3edc0f8681";

check_md5_output($md5, $cmd);
