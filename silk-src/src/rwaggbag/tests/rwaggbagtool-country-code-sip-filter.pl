#! /usr/bin/perl -w
# MD5: 118d0dde0db73123d2b54503dbeb6527
# TEST: ./rwaggbag --keys=scc,proto --counters=sum-bytes,sum-packets ../../tests/data.rwf | ./rwaggbagtool --min-field=scc=xx --max-field=scc=xx stdin | ./rwaggbagcat

use strict;
use SiLKTests;

my $rwaggbagtool = check_silk_app('rwaggbagtool');
my $rwaggbag = check_silk_app('rwaggbag');
my $rwaggbagcat = check_silk_app('rwaggbagcat');
my %file;
$file{data} = get_data_or_exit77('data');
$file{fake_cc} = get_data_or_exit77('fake_cc');
$ENV{SILK_COUNTRY_CODES} = "$SiLKTests::PWD/$file{fake_cc}";
my $cmd = "$rwaggbag --keys=scc,proto --counters=sum-bytes,sum-packets $file{data} | $rwaggbagtool --min-field=scc=xx --max-field=scc=xx stdin | $rwaggbagcat";
my $md5 = "118d0dde0db73123d2b54503dbeb6527";

check_md5_output($md5, $cmd);
