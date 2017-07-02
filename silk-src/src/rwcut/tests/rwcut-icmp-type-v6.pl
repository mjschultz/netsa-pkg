#! /usr/bin/perl -w
# MD5: 455710e0d43176afec48a916ab9546fb
# TEST: ../rwfilter/rwfilter --proto=58 --pass=- ../../tests/data-v6.rwf | ./rwcut --fields=iCode,proto,iType

use strict;
use SiLKTests;

my $rwcut = check_silk_app('rwcut');
my $rwfilter = check_silk_app('rwfilter');
my %file;
$file{v6data} = get_data_or_exit77('v6data');
check_features(qw(ipv6));
my $cmd = "$rwfilter --proto=58 --pass=- $file{v6data} | $rwcut --fields=iCode,proto,iType";
my $md5 = "455710e0d43176afec48a916ab9546fb";

check_md5_output($md5, $cmd);
