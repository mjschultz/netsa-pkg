#! /usr/bin/perl -w
# MD5: b43dcea6089a4d405183a79500c4bcad
# TEST: ./rwsettool --symmetric-difference ../../tests/set2-v6.set ../../tests/set1-v6.set | ./rwsetcat --cidr

use strict;
use SiLKTests;

my $rwsettool = check_silk_app('rwsettool');
my $rwsetcat = check_silk_app('rwsetcat');
my %file;
$file{v6set1} = get_data_or_exit77('v6set1');
$file{v6set2} = get_data_or_exit77('v6set2');
check_features(qw(ipset_v6));
my $cmd = "$rwsettool --symmetric-difference $file{v6set2} $file{v6set1} | $rwsetcat --cidr";
my $md5 = "b43dcea6089a4d405183a79500c4bcad";

check_md5_output($md5, $cmd);
