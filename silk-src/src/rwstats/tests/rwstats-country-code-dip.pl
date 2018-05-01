#! /usr/bin/perl -w
# MD5: d6e31268a50f36f954fd05779944e0f4
# TEST: ./rwstats --fields=dcc --values=dip-distinct --ipv6-policy=ignore --count=10 ../../tests/data.rwf

use strict;
use SiLKTests;

my $rwstats = check_silk_app('rwstats');
my %file;
$file{data} = get_data_or_exit77('data');
$file{fake_cc} = get_data_or_exit77('fake_cc');
$ENV{SILK_COUNTRY_CODES} = "$SiLKTests::PWD/$file{fake_cc}";
my $cmd = "$rwstats --fields=dcc --values=dip-distinct --ipv6-policy=ignore --count=10 $file{data}";
my $md5 = "d6e31268a50f36f954fd05779944e0f4";

check_md5_output($md5, $cmd);
