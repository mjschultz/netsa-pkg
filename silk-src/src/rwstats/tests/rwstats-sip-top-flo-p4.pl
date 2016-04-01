#! /usr/bin/perl -w
# MD5: 63b20207e102b276b30127124df41783
# TEST: ./rwstats --fields=sip --percentage=4 --top --ipv6-policy=ignore ../../tests/data.rwf

use strict;
use SiLKTests;

my $rwstats = check_silk_app('rwstats');
my %file;
$file{data} = get_data_or_exit77('data');
my $cmd = "$rwstats --fields=sip --percentage=4 --top --ipv6-policy=ignore $file{data}";
my $md5 = "63b20207e102b276b30127124df41783";

check_md5_output($md5, $cmd);
