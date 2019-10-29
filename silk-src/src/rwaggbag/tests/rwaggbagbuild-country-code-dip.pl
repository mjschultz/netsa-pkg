#! /usr/bin/perl -w
# MD5: 3893c03ab584e9955571e14c58ce7e76
# TEST: ../rwcut/rwcut --fields=application,dcc,bytes,sport ../../tests/data.rwf | ./rwaggbagbuild --fields=application,dcc,sum-bytes,ignore --constant-field=records=1 | ./rwaggbagcat

use strict;
use SiLKTests;

my $rwaggbagbuild = check_silk_app('rwaggbagbuild');
my $rwcut = check_silk_app('rwcut');
my $rwaggbagcat = check_silk_app('rwaggbagcat');
my %file;
$file{data} = get_data_or_exit77('data');
$file{fake_cc} = get_data_or_exit77('fake_cc');
$ENV{SILK_COUNTRY_CODES} = "$SiLKTests::PWD/$file{fake_cc}";
my $cmd = "$rwcut --fields=application,dcc,bytes,sport $file{data} | $rwaggbagbuild --fields=application,dcc,sum-bytes,ignore --constant-field=records=1 | $rwaggbagcat";
my $md5 = "3893c03ab584e9955571e14c58ce7e76";

check_md5_output($md5, $cmd);
