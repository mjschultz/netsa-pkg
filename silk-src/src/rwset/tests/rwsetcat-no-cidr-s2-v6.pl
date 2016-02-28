#! /usr/bin/perl -w
# MD5: 12b0ffcce0ed731c08da0dda635f2867
# TEST: ./rwsetcat --cidr-blocks=0 ../../tests/set2-v6.set | head -n 257

use strict;
use SiLKTests;

my $rwsetcat = check_silk_app('rwsetcat');
my %file;
$file{v6set2} = get_data_or_exit77('v6set2');
check_features(qw(ipset_v6));
my $cmd = "$rwsetcat --cidr-blocks=0 $file{v6set2} | head -n 257";
my $md5 = "12b0ffcce0ed731c08da0dda635f2867";

check_md5_output($md5, $cmd);
