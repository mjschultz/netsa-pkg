#! /usr/bin/perl -w
# MD5: d79a4dbf5917c4dd712e1ca4a00780e9
# TEST: ../rwnetmask/rwnetmask --4sip-prefix=24 ../../tests/data.rwf | ./rwstats --fields=sip --values=packets --percentage=2 --top --ipv6-policy=ignore

use strict;
use SiLKTests;

my $rwstats = check_silk_app('rwstats');
my $rwnetmask = check_silk_app('rwnetmask');
my %file;
$file{data} = get_data_or_exit77('data');
my $cmd = "$rwnetmask --4sip-prefix=24 $file{data} | $rwstats --fields=sip --values=packets --percentage=2 --top --ipv6-policy=ignore";
my $md5 = "d79a4dbf5917c4dd712e1ca4a00780e9";

check_md5_output($md5, $cmd);
