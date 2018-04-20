#! /usr/bin/perl -w
# MD5: 4723bef4df2379f64de3bb1677b59d42
# TEST: ../rwsort/rwsort --fields=sport ../../tests/data-v6.rwf | ./rwuniq --fields=sport --sip-distinct --presorted-input

use strict;
use SiLKTests;

my $rwuniq = check_silk_app('rwuniq');
my $rwsort = check_silk_app('rwsort');
my %file;
$file{v6data} = get_data_or_exit77('v6data');
check_features(qw(ipv6));
my $cmd = "$rwsort --fields=sport $file{v6data} | $rwuniq --fields=sport --sip-distinct --presorted-input";
my $md5 = "4723bef4df2379f64de3bb1677b59d42";

check_md5_output($md5, $cmd);
