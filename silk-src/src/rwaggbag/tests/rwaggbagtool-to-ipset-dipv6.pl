#! /usr/bin/perl -w
# MD5: 24055bf22079a1f7f08940b20db2b14c
# TEST: ./rwaggbag --key=sipv6,dipv6 --counter=records ../../tests/data-v6.rwf | ./rwaggbagtool --to-ipset=dipv6 | ../rwset/rwsetcat

use strict;
use SiLKTests;

my $rwaggbagtool = check_silk_app('rwaggbagtool');
my $rwaggbag = check_silk_app('rwaggbag');
my $rwsetcat = check_silk_app('rwsetcat');
my %file;
$file{v6data} = get_data_or_exit77('v6data');
check_features(qw(ipv6));
my $cmd = "$rwaggbag --key=sipv6,dipv6 --counter=records $file{v6data} | $rwaggbagtool --to-ipset=dipv6 | $rwsetcat";
my $md5 = "24055bf22079a1f7f08940b20db2b14c";

check_md5_output($md5, $cmd);
