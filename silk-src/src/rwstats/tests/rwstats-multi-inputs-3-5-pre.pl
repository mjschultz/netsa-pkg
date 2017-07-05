#! /usr/bin/perl -w
# MD5: e2ddc6cb13c8956ce2906ef6faf9b794
# TEST: ../rwfilter/rwfilter --type=in,inweb --pass=stdout ../../tests/data.rwf | ../rwsort/rwsort --fields=3-5 --output-path=/tmp/rwstats-multi-inputs-3-5-pre-in && ../rwfilter/rwfilter --type=in,inweb --fail=stdout ../../tests/data.rwf | ../rwsort/rwsort --fields=3-5 --output-path=/tmp/rwstats-multi-inputs-3-5-pre-out && ./rwstats --fields=3-5 --values=bytes,packets --threshold=30000000 --presorted-input /tmp/rwstats-multi-inputs-3-5-pre-in /tmp/rwstats-multi-inputs-3-5-pre-out

use strict;
use SiLKTests;

my $rwstats = check_silk_app('rwstats');
my $rwfilter = check_silk_app('rwfilter');
my $rwsort = check_silk_app('rwsort');
my %file;
$file{data} = get_data_or_exit77('data');
my %temp;
$temp{in} = make_tempname('in');
$temp{out} = make_tempname('out');
my $cmd = "$rwfilter --type=in,inweb --pass=stdout $file{data} | $rwsort --fields=3-5 --output-path=$temp{in} && $rwfilter --type=in,inweb --fail=stdout $file{data} | $rwsort --fields=3-5 --output-path=$temp{out} && $rwstats --fields=3-5 --values=bytes,packets --threshold=30000000 --presorted-input $temp{in} $temp{out}";
my $md5 = "e2ddc6cb13c8956ce2906ef6faf9b794";

check_md5_output($md5, $cmd);
