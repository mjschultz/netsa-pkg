#! /usr/bin/perl -w
# MD5: e35889f1e3676d63ec3808825ec048f8
# TEST: ./rwstats --plugin=flowrate.so --fields=pckts/sec --values=packets --count=10 ../../tests/data.rwf

use strict;
use SiLKTests;

my $rwstats = check_silk_app('rwstats');
my %file;
$file{data} = get_data_or_exit77('data');
add_plugin_dirs('/src/plugins');

skip_test('Cannot load flowrate plugin')
    unless check_app_switch($rwstats.' --plugin=flowrate.so', 'fields', qr/payload-rate/);
my $cmd = "$rwstats --plugin=flowrate.so --fields=pckts/sec --values=packets --count=10 $file{data}";
my $md5 = "e35889f1e3676d63ec3808825ec048f8";

check_md5_output($md5, $cmd);
