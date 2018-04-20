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
$cmd = "$rwgeoip2ccmap --input-path $SiLKTests::top_srcdir/tests/fake-cc.csv --output-path $temp{country_pmap}";
check_md5_output($md5, $cmd);

$md5 = "e8cfbb3e6fd9f87ae1db589c4684a8b6";
$cmd = "$rwpmapcat --country-codes=$temp{country_pmap} --no-cidr-blocks";
check_md5_output($md5, $cmd);

$md5 = "9bdc2b99792c39e9375d232d43b759c6";
$cmd = "cat $temp{country_pmap} | $rwfileinfo --fields=format,record-version,count-records -";
check_md5_output($md5, $cmd);
