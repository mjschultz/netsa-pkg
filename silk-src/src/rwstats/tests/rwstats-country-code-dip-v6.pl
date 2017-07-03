#! /usr/bin/perl -w
# MD5: d6e31268a50f36f954fd05779944e0f4
# TEST: ./rwstats --fields=dcc --values=dip-distinct --count=10 ../../tests/data-v6.rwf

use strict;
use SiLKTests;

my $rwstats = check_silk_app('rwstats');
my %file;
$file{v6data} = get_data_or_exit77('v6data');
$file{v6_fake_cc} = get_data_or_exit77('v6_fake_cc');
$ENV{SILK_COUNTRY_CODES} = "$SiLKTests::PWD/$file{v6_fake_cc}";
my $cmd = "$rwstats --fields=dcc --values=dip-distinct --count=10 $file{v6data}";
my $md5 = "d6e31268a50f36f954fd05779944e0f4";

check_md5_output($md5, $cmd);
