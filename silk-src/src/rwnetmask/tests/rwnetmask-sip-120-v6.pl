#! /usr/bin/perl -w
# MD5: 6b69018675a8c904fbcfc6cd9d70b104
# TEST: ./rwnetmask --6sip-prefix-length=120 ../../tests/data-v6.rwf | ../rwcat/rwcat --compression-method=none --byte-order=little

use strict;
use SiLKTests;

my $rwnetmask = check_silk_app('rwnetmask');
my $rwcat = check_silk_app('rwcat');
my %file;
$file{v6data} = get_data_or_exit77('v6data');
check_features(qw(ipv6));
my $cmd = "$rwnetmask --6sip-prefix-length=120 $file{v6data} | $rwcat --compression-method=none --byte-order=little";
my $md5 = "6b69018675a8c904fbcfc6cd9d70b104";

check_md5_output($md5, $cmd);
