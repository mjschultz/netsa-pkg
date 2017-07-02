#! /usr/bin/perl -w
# MD5: 651856e0e1cd6d09328050bde2a2da7b
# TEST: ./rwstats --plugin=int-ext-fields.so --fields=ext-ip,ext-port --values=packets,records --count=35 --delimited ../../tests/data.rwf

use strict;
use SiLKTests;

my $rwstats = check_silk_app('rwstats');
my %file;
$file{data} = get_data_or_exit77('data');
$ENV{INCOMING_FLOWTYPES} = 'all/in,all/inweb';
$ENV{OUTGOING_FLOWTYPES} = 'all/out,all/outweb';
add_plugin_dirs('/src/plugins');

push @SiLKTests::DUMP_ENVVARS, qw(INCOMING_FLOWTYPES OUTGOING_FLOWTYPES);
skip_test('Cannot load int-ext-fields plugin')
    unless check_app_switch($rwstats.' --plugin=int-ext-fields.so', 'fields', qr/int-port/);
my $cmd = "$rwstats --plugin=int-ext-fields.so --fields=ext-ip,ext-port --values=packets,records --count=35 --delimited $file{data}";
my $md5 = "651856e0e1cd6d09328050bde2a2da7b";

check_md5_output($md5, $cmd);
