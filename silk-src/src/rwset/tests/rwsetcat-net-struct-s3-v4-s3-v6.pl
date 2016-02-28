#! /usr/bin/perl -w
# MD5: 0266117d401fe4325c549002d43fcbff
# TEST: ./rwsettool --union ../../tests/set3-v4.set ../../tests/set3-v6.set | ./rwsetcat --network-structure=v4:8TS

use strict;
use SiLKTests;

my $rwsetcat = check_silk_app('rwsetcat');
my $rwsettool = check_silk_app('rwsettool');
my %file;
$file{v4set3} = get_data_or_exit77('v4set3');
$file{v6set3} = get_data_or_exit77('v6set3');
check_features(qw(ipset_v6));
my $cmd = "$rwsettool --union $file{v4set3} $file{v6set3} | $rwsetcat --network-structure=v4:8TS";
my $md5 = "0266117d401fe4325c549002d43fcbff";

check_md5_output($md5, $cmd);
