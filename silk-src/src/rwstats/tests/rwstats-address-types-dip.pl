#! /usr/bin/perl -w
# MD5: a9c1ff598bc13095364103a531d80ad7
# TEST: ./rwstats --fields=dtype --values=dip-distinct --delimited --ipv6-policy=ignore --count=2 ../../tests/data.rwf

use strict;
use SiLKTests;

my $rwstats = check_silk_app('rwstats');
my %file;
$file{data} = get_data_or_exit77('data');
$file{address_types} = get_data_or_exit77('address_types');
$ENV{SILK_ADDRESS_TYPES} = "$SiLKTests::PWD/$file{address_types}";
my $cmd = "$rwstats --fields=dtype --values=dip-distinct --delimited --ipv6-policy=ignore --count=2 $file{data}";
my $md5 = "a9c1ff598bc13095364103a531d80ad7";

check_md5_output($md5, $cmd);
