#! /usr/bin/perl -w
# MD5: c17d1b4d092c2a77c9c85d4548696fcf
# TEST: ./rwuniq --fields=stime,dur --bin-time=0.001 --values=bytes,packets,flows --sort-output ../../tests/data.rwf

use strict;
use SiLKTests;

my $rwuniq = check_silk_app('rwuniq');
my %file;
$file{data} = get_data_or_exit77('data');
my $cmd = "$rwuniq --fields=stime,dur --bin-time=0.001 --values=bytes,packets,flows --sort-output $file{data}";
my $md5 = "c17d1b4d092c2a77c9c85d4548696fcf";

check_md5_output($md5, $cmd);
