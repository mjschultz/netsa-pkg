#! /usr/bin/perl -w
# MD5: e5120829c7a17a94e46ec8c0ca3861a4
# TEST: ./rwcut --all-fields --delimited ../../tests/data-v6.rwf

use strict;
use SiLKTests;

my $rwcut = check_silk_app('rwcut');
my %file;
$file{v6data} = get_data_or_exit77('v6data');
check_features(qw(ipv6));
my $cmd = "$rwcut --all-fields --delimited $file{v6data}";
my $md5 = "e5120829c7a17a94e46ec8c0ca3861a4";

check_md5_output($md5, $cmd);
