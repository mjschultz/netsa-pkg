#! /usr/bin/perl -w
# MD5: bf1a941b20fd7a57af7123c217c32079
# TEST: ../rwsort/rwsort --fields=1,2,9 ../../tests/data-v6.rwf | ./rwgroup --id-fields=1,2 | ../rwcat/rwcat --compression-method=none --byte-order=little

use strict;
use SiLKTests;

my $rwgroup = check_silk_app('rwgroup');
my $rwcat = check_silk_app('rwcat');
my $rwsort = check_silk_app('rwsort');
my %file;
$file{v6data} = get_data_or_exit77('v6data');
check_features(qw(ipv6));
my $cmd = "$rwsort --fields=1,2,9 $file{v6data} | $rwgroup --id-fields=1,2 | $rwcat --compression-method=none --byte-order=little";
my $md5 = "bf1a941b20fd7a57af7123c217c32079";

check_md5_output($md5, $cmd);
