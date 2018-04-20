#! /usr/bin/perl -w
# MD5: 2c149844807b60bc7fcdb52d32509e35
# TEST: ./rwstats --fields=application --values=distinct:sip --presorted --ipv6-policy=ignore --count=0 ../../tests/sips-004-008.rw ../../tests/sips-004-008.rw

use strict;
use SiLKTests;

my $rwstats = check_silk_app('rwstats');
my %file;
$file{sips004} = get_data_or_exit77('sips004');
my $cmd = "$rwstats --fields=application --values=distinct:sip --presorted --ipv6-policy=ignore --count=0 $file{sips004} $file{sips004}";
my $md5 = "2c149844807b60bc7fcdb52d32509e35";

check_md5_output($md5, $cmd);
