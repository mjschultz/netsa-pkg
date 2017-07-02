#! /usr/bin/perl -w
# MD5: d4c6c4188dfe5c183645a9fdd43d28e6
# TEST: ./rwuniq --plugin=flowrate.so --fields=pckts/sec --values=packets --sort-output ../../tests/data.rwf

use strict;
use SiLKTests;

my $rwuniq = check_silk_app('rwuniq');
my %file;
$file{data} = get_data_or_exit77('data');
add_plugin_dirs('/src/plugins');

skip_test('Cannot load flowrate plugin')
    unless check_app_switch($rwuniq.' --plugin=flowrate.so', 'fields', qr/payload-rate/);
my $cmd = "$rwuniq --plugin=flowrate.so --fields=pckts/sec --values=packets --sort-output $file{data}";
my $md5 = "d4c6c4188dfe5c183645a9fdd43d28e6";

check_md5_output($md5, $cmd);
