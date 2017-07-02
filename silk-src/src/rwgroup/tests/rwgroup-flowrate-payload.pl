#! /usr/bin/perl -w
# MD5: 4d79fab412f738d4a92b06d34c33a89c
# TEST: ../rwsort/rwsort --plugin=flowrate.so --fields=payload-bytes,stime,sip ../../tests/data.rwf | ./rwgroup --plugin=flowrate.so --id-fields=payload-bytes | ../rwcat/rwcat --compression-method=none --byte-order=little --ipv4-output

use strict;
use SiLKTests;

my $rwgroup = check_silk_app('rwgroup');
my $rwcat = check_silk_app('rwcat');
my $rwsort = check_silk_app('rwsort');
my %file;
$file{data} = get_data_or_exit77('data');
add_plugin_dirs('/src/plugins');

skip_test('Cannot load flowrate plugin')
    unless check_app_switch($rwsort.' --plugin=flowrate.so', 'fields', qr/payload-rate/);
my $cmd = "$rwsort --plugin=flowrate.so --fields=payload-bytes,stime,sip $file{data} | $rwgroup --plugin=flowrate.so --id-fields=payload-bytes | $rwcat --compression-method=none --byte-order=little --ipv4-output";
my $md5 = "4d79fab412f738d4a92b06d34c33a89c";

check_md5_output($md5, $cmd);
