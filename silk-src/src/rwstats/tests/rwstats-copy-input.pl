#! /usr/bin/perl -w
# MD5: 09dc844c11f47ce482e1c6ca4826060d
# TEST: ./rwstats --fields=sip --top --count=10 --output-path=/dev/null --copy-input=stdout ../../tests/data.rwf | ./rwstats --fields=dip --count=9 --ipv6-policy=ignore

use strict;
use SiLKTests;

my $rwstats = check_silk_app('rwstats');
my %file;
$file{data} = get_data_or_exit77('data');
my $cmd = "$rwstats --fields=sip --top --count=10 --output-path=/dev/null --copy-input=stdout $file{data} | $rwstats --fields=dip --count=9 --ipv6-policy=ignore";
my $md5 = "09dc844c11f47ce482e1c6ca4826060d";

check_md5_output($md5, $cmd);
