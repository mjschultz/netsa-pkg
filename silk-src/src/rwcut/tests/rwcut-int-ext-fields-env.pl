#! /usr/bin/perl -w
# MD5: 5041e7ce72489f7b25257e0e74869b94
# TEST: ./rwcut --plugin=int-ext-fields.so --delimited --fields=ext-ip,ext-port,int-ip,int-port,proto,type ../../tests/data.rwf

use strict;
use SiLKTests;

my $rwcut = check_silk_app('rwcut');
my %file;
$file{data} = get_data_or_exit77('data');
$ENV{INCOMING_FLOWTYPES} = 'all/in,all/inweb';
$ENV{OUTGOING_FLOWTYPES} = 'all/out,all/outweb';
add_plugin_dirs('/src/plugins');

push @SiLKTests::DUMP_ENVVARS, qw(INCOMING_FLOWTYPES OUTGOING_FLOWTYPES);
skip_test('Cannot load int-ext-fields plugin')
    unless check_app_switch($rwcut.' --plugin=int-ext-fields.so --incoming-flowtypes=all/in,all/inweb --outgoing-flowtypes=all/out,all/outweb', 'fields', qr/int-port/);
my $cmd = "$rwcut --plugin=int-ext-fields.so --delimited --fields=ext-ip,ext-port,int-ip,int-port,proto,type $file{data}";
my $md5 = "5041e7ce72489f7b25257e0e74869b94";

check_md5_output($md5, $cmd);
