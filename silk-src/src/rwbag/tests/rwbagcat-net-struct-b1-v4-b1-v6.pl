#! /usr/bin/perl -w
# MD5: 092a01b378937611b1f2eda303e191a4
# TEST: ./rwbagtool --add ../../tests/bag1-v4.bag ../../tests/bag1-v6.bag | ./rwbagcat --network=v4:TH14/

use strict;
use SiLKTests;

my $rwbagcat = check_silk_app('rwbagcat');
my $rwbagtool = check_silk_app('rwbagtool');
my %file;
$file{v4bag1} = get_data_or_exit77('v4bag1');
$file{v6bag1} = get_data_or_exit77('v6bag1');
check_features(qw(ipv6));
my $cmd = "$rwbagtool --add $file{v4bag1} $file{v6bag1} | $rwbagcat --network=v4:TH14/";
my $md5 = "092a01b378937611b1f2eda303e191a4";

check_md5_output($md5, $cmd);
