#! /usr/bin/perl -w
# MD5: 5e3644eb255d8e76fbc217763d61fade
# TEST: ./rwsilk2ipfix ../../tests/data-v6.rwf | ./rwipfix2silk --silk-output=stdout | ../rwcat/rwcat --compression-method=none --byte-order=little

use strict;
use SiLKTests;

my $rwsilk2ipfix = check_silk_app('rwsilk2ipfix');
my $rwipfix2silk = check_silk_app('rwipfix2silk');
my $rwcat = check_silk_app('rwcat');
my %file;
$file{v6data} = get_data_or_exit77('v6data');
my $cmd = "$rwsilk2ipfix $file{v6data} | $rwipfix2silk --silk-output=stdout | $rwcat --compression-method=none --byte-order=little";
my $md5 = "5e3644eb255d8e76fbc217763d61fade";

check_md5_output($md5, $cmd);
