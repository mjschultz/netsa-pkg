#! /usr/bin/perl -w
# MD5: 46dc1289082f2e4195971b5f946ed40a
# TEST: ./rwuniq --fields=application --delimited --sort-output --values=distinct:proto,distinct:dip,distinct:sport,distinct:bytes,distinct:dport,distinct:sip,distinct:packet ../../tests/sips-004-008.rw ../../tests/sips-004-008.rw

use strict;
use SiLKTests;

my $rwuniq = check_silk_app('rwuniq');
my %file;
$file{sips004} = get_data_or_exit77('sips004');
my $cmd = "$rwuniq --fields=application --delimited --sort-output --values=distinct:proto,distinct:dip,distinct:sport,distinct:bytes,distinct:dport,distinct:sip,distinct:packet $file{sips004} $file{sips004}";
my $md5 = "46dc1289082f2e4195971b5f946ed40a";

check_md5_output($md5, $cmd);
