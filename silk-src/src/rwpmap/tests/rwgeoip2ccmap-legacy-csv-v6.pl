#! /usr/bin/perl -w
# MD5: multiple
# TEST: multiple

use strict;
use SiLKTests;

my $rwgeoip2ccmap = check_silk_app('rwgeoip2ccmap');
my $rwfileinfo = check_silk_app('rwfileinfo');
my $rwpmapcat = check_silk_app('rwpmapcat');
my %temp;
$temp{country_pmap} = make_tempname('country_pmap');

my ($cmd, $md5);

$md5 = "d41d8cd98f00b204e9800998ecf8427e";
$cmd = "$rwgeoip2ccmap --input-path $SiLKTests::top_srcdir/tests/fake-cc-v6.csv --output-path $temp{country_pmap}";
check_md5_output($md5, $cmd);

# produces a prefixmap with only root entry when no IPv6 support
$md5 = (($SiLKTests::SK_ENABLE_IPV6)
        ? "2f09e7a8c4d1a42aef3a5420bfd12a84"
        : "5c17c0828205b015d600a76c5f37b446");
$cmd = "$rwpmapcat --country-codes=$temp{country_pmap} --no-cidr-blocks";
check_md5_output($md5, $cmd);

$md5 = (($SiLKTests::SK_ENABLE_IPV6)
        ? "e23056f2d198311eda0d150a1bc5517f"
        : "a81e69b1644d233f88bb6ea3c99cd446");
$cmd = "cat $temp{country_pmap} | $rwfileinfo --fields=format,record-version,count-records -";
check_md5_output($md5, $cmd);
