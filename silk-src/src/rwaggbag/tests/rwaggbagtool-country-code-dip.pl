#! /usr/bin/perl -w
# MD5: 56e79305c21a1fa8ab07bd0c3def7821
# TEST: ./rwaggbag --keys=application,dcc --counters=records,sum-bytes --ipv6-policy=ignore ../../tests/data.rwf | ./rwaggbagtool --insert-field=any-cc=uu | ./rwaggbagcat

use strict;
use SiLKTests;

my $rwaggbagtool = check_silk_app('rwaggbagtool');
my $rwaggbag = check_silk_app('rwaggbag');
my $rwaggbagcat = check_silk_app('rwaggbagcat');
my %file;
$file{data} = get_data_or_exit77('data');
$file{fake_cc} = get_data_or_exit77('fake_cc');
$ENV{SILK_COUNTRY_CODES} = "$SiLKTests::PWD/$file{fake_cc}";
my $cmd = "$rwaggbag --keys=application,dcc --counters=records,sum-bytes --ipv6-policy=ignore $file{data} | $rwaggbagtool --insert-field=any-cc=uu | $rwaggbagcat";
my $md5 = "56e79305c21a1fa8ab07bd0c3def7821";

check_md5_output($md5, $cmd);
