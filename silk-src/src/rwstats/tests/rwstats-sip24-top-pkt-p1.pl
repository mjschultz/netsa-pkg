#! /usr/bin/perl -w
# MD5: d772133f1a95ac50fd6196ea81ffdf15
# TEST: ../rwnetmask/rwnetmask --4sip-prefix=24 ../../tests/data.rwf | ./rwstats --fields=sip --values=packets --percentage=1 --top --ipv6-policy=ignore

use strict;
use SiLKTests;

my $rwstats = check_silk_app('rwstats');
my $rwnetmask = check_silk_app('rwnetmask');
my %file;
$file{data} = get_data_or_exit77('data');
my $cmd = "$rwnetmask --4sip-prefix=24 $file{data} | $rwstats --fields=sip --values=packets --percentage=1 --top --ipv6-policy=ignore";
my $md5 = "d772133f1a95ac50fd6196ea81ffdf15";

check_md5_output($md5, $cmd);
