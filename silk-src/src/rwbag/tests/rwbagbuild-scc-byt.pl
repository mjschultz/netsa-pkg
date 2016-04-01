#! /usr/bin/perl -w
# MD5: 89b3eee74c39e7c06fbf8abc0576b386
# TEST: ../rwcut/rwcut --no-columns --fields=sip,bytes --no-title ../../tests/data.rwf | ./rwbagbuild --bag-input=- --key-type=sip-country | ./rwbagcat

use strict;
use SiLKTests;

my $rwbagbuild = check_silk_app('rwbagbuild');
my $rwcut = check_silk_app('rwcut');
my $rwbagcat = check_silk_app('rwbagcat');
my %file;
$file{data} = get_data_or_exit77('data');
$file{fake_cc} = get_data_or_exit77('fake_cc');
$ENV{SILK_COUNTRY_CODES} = "$SiLKTests::PWD/$file{fake_cc}";
my $cmd = "$rwcut --no-columns --fields=sip,bytes --no-title $file{data} | $rwbagbuild --bag-input=- --key-type=sip-country | $rwbagcat";
my $md5 = "89b3eee74c39e7c06fbf8abc0576b386";

check_md5_output($md5, $cmd);
