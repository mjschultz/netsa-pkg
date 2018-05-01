#! /usr/bin/perl -w
# MD5: b921bf25a4969ecefa5fbe3347e1488f
# TEST: ./rwstats --fields=stype --values=sip-distinct --delimited --ipv6-policy=ignore --count=2 ../../tests/data.rwf

use strict;
use SiLKTests;

my $rwstats = check_silk_app('rwstats');
my %file;
$file{data} = get_data_or_exit77('data');
$file{address_types} = get_data_or_exit77('address_types');
$ENV{SILK_ADDRESS_TYPES} = "$SiLKTests::PWD/$file{address_types}";
my $cmd = "$rwstats --fields=stype --values=sip-distinct --delimited --ipv6-policy=ignore --count=2 $file{data}";
my $md5 = "b921bf25a4969ecefa5fbe3347e1488f";

check_md5_output($md5, $cmd);
