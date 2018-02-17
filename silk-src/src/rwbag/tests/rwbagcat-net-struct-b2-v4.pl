#! /usr/bin/perl -w
# MD5: 61302e0177eac1cfd031e76afebdd163
# TEST: ./rwbagcat --network-structure ../../tests/bag2-v4.bag

use strict;
use SiLKTests;

my $rwbagcat = check_silk_app('rwbagcat');
my %file;
$file{v4bag2} = get_data_or_exit77('v4bag2');
my $cmd = "$rwbagcat --network-structure $file{v4bag2}";
my $md5 = "61302e0177eac1cfd031e76afebdd163";

check_md5_output($md5, $cmd);
