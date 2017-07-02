#! /usr/bin/perl -w
# MD5: 0e8be8c6a942e62086628467e8d050a7
# TEST: ./rwsort --fields=dip ../../tests/data-v6.rwf | ../rwcat/rwcat --compression-method=none --byte-order=little

use strict;
use SiLKTests;

my $rwsort = check_silk_app('rwsort');
my $rwcat = check_silk_app('rwcat');
my %file;
$file{v6data} = get_data_or_exit77('v6data');
check_features(qw(ipv6));
my $cmd = "$rwsort --fields=dip $file{v6data} | $rwcat --compression-method=none --byte-order=little";
my $md5 = "0e8be8c6a942e62086628467e8d050a7";

check_md5_output($md5, $cmd);
