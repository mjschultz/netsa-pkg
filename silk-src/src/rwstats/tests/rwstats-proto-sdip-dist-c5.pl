#! /usr/bin/perl -w
# MD5: b4685edb1ad6e3be32ab1d5295fab0ea
# TEST: ./rwstats --fields=proto --values=sip-distinct,dip-distinct --count=5 --ipv6-policy=ignore ../../tests/data.rwf

use strict;
use SiLKTests;

my $rwstats = check_silk_app('rwstats');
my %file;
$file{data} = get_data_or_exit77('data');
my $cmd = "$rwstats --fields=proto --values=sip-distinct,dip-distinct --count=5 --ipv6-policy=ignore $file{data}";
my $md5 = "b4685edb1ad6e3be32ab1d5295fab0ea";

check_md5_output($md5, $cmd);
