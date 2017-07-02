#! /usr/bin/perl -w
# MD5: 965fded627d114ad3ddd7f27735df719
# TEST: ./rwuniq --plugin=flowrate.so --fields=bytes/sec --values=bytes --sort-output ../../tests/data.rwf

use strict;
use SiLKTests;

my $rwuniq = check_silk_app('rwuniq');
my %file;
$file{data} = get_data_or_exit77('data');
add_plugin_dirs('/src/plugins');

skip_test('Cannot load flowrate plugin')
    unless check_app_switch($rwuniq.' --plugin=flowrate.so', 'fields', qr/payload-rate/);
my $cmd = "$rwuniq --plugin=flowrate.so --fields=bytes/sec --values=bytes --sort-output $file{data}";
my $md5 = "965fded627d114ad3ddd7f27735df719";

check_md5_output($md5, $cmd);
