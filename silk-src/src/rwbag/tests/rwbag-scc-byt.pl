#! /usr/bin/perl -w
# MD5: 89b3eee74c39e7c06fbf8abc0576b386
# TEST: ./rwbag --bag-file=sip-country,bytes,- ../../tests/data.rwf | ./rwbagcat

use strict;
use SiLKTests;

my $rwbag = check_silk_app('rwbag');
my $rwbagcat = check_silk_app('rwbagcat');
my %file;
$file{data} = get_data_or_exit77('data');
$file{fake_cc} = get_data_or_exit77('fake_cc');
$ENV{SILK_COUNTRY_CODES} = "$SiLKTests::PWD/$file{fake_cc}";
my $cmd = "$rwbag --bag-file=sip-country,bytes,- $file{data} | $rwbagcat";
my $md5 = "89b3eee74c39e7c06fbf8abc0576b386";

check_md5_output($md5, $cmd);
