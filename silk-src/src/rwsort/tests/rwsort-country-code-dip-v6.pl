#! /usr/bin/perl -w
# MD5: 9727e33350b4cad1aa5c7da3613e67f4
# TEST: ./rwsort --fields=dcc ../../tests/data-v6.rwf | ../rwstats/rwuniq --fields=dcc --values=distinct:dip --presorted-input

use strict;
use SiLKTests;

my $rwsort = check_silk_app('rwsort');
my $rwuniq = check_silk_app('rwuniq');
my %file;
$file{v6data} = get_data_or_exit77('v6data');
$file{v6_fake_cc} = get_data_or_exit77('v6_fake_cc');
$ENV{SILK_COUNTRY_CODES} = "$SiLKTests::PWD/$file{v6_fake_cc}";
my $cmd = "$rwsort --fields=dcc $file{v6data} | $rwuniq --fields=dcc --values=distinct:dip --presorted-input";
my $md5 = "9727e33350b4cad1aa5c7da3613e67f4";

check_md5_output($md5, $cmd);
