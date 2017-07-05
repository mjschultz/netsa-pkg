#! /usr/bin/perl -w
# MD5: 44acd5910a25f168eba9ab8825f55809
# TEST: ./rwcut --plugin=flowrate.so --fields=bytes,packets,dur,pckts/sec,bytes/sec,bytes/packet,payload-bytes,payload-rate ../../tests/data.rwf

use strict;
use SiLKTests;

my $rwcut = check_silk_app('rwcut');
my %file;
$file{data} = get_data_or_exit77('data');
add_plugin_dirs('/src/plugins');

skip_test('Cannot load flowrate plugin')
    unless check_app_switch($rwcut.' --plugin=flowrate.so', 'fields', qr/payload-rate/);
my $cmd = "$rwcut --plugin=flowrate.so --fields=bytes,packets,dur,pckts/sec,bytes/sec,bytes/packet,payload-bytes,payload-rate $file{data}";
my $md5 = "44acd5910a25f168eba9ab8825f55809";

check_md5_output($md5, $cmd);
