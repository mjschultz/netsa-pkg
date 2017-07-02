#! /usr/bin/perl -w
# MD5: 1035013a65322c1fbf7a166e70c3fc06
# TEST: ../rwnetmask/rwnetmask --4dip-prefix=16 ../../tests/data.rwf | ./rwstats --fields=dip --values=bytes --count=10 --bottom --ipv6-policy=ignore

use strict;
use SiLKTests;

my $rwstats = check_silk_app('rwstats');
my $rwnetmask = check_silk_app('rwnetmask');
my %file;
$file{data} = get_data_or_exit77('data');
my $cmd = "$rwnetmask --4dip-prefix=16 $file{data} | $rwstats --fields=dip --values=bytes --count=10 --bottom --ipv6-policy=ignore";
my $md5 = "1035013a65322c1fbf7a166e70c3fc06";

check_md5_output($md5, $cmd);
