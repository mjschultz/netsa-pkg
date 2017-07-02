#! /usr/bin/perl -w
# MD5: 965fded627d114ad3ddd7f27735df719
# TEST: ./rwsort --plugin=flowrate.so --fields=bytes/sec ../../tests/data.rwf | ../rwstats/rwuniq --plugin=flowrate.so --fields=bytes/sec --values=bytes --presorted-input

use strict;
use SiLKTests;

my $rwsort = check_silk_app('rwsort');
my $rwuniq = check_silk_app('rwuniq');
my %file;
$file{data} = get_data_or_exit77('data');
add_plugin_dirs('/src/plugins');

skip_test('Cannot load flowrate plugin')
    unless check_app_switch($rwsort.' --plugin=flowrate.so', 'fields', qr/payload-rate/);
my $cmd = "$rwsort --plugin=flowrate.so --fields=bytes/sec $file{data} | $rwuniq --plugin=flowrate.so --fields=bytes/sec --values=bytes --presorted-input";
my $md5 = "965fded627d114ad3ddd7f27735df719";

check_md5_output($md5, $cmd);
