#! /usr/bin/perl -w
# MD5: 2c1e91253d2bf74e84c1ee8b917c65bf
# TEST: ./rwcut --plugin=int-ext-fields.so --delimited --incoming-flowtypes=all/in,all/inweb --outgoing-flowtypes=all/out,all/outweb --fields=ext-ip,ext-port,int-ip,int-port,proto,type ../../tests/data-v6.rwf

use strict;
use SiLKTests;

my $rwcut = check_silk_app('rwcut');
my %file;
$file{v6data} = get_data_or_exit77('v6data');
add_plugin_dirs('/src/plugins');
check_features(qw(ipv6));

push @SiLKTests::DUMP_ENVVARS, qw(INCOMING_FLOWTYPES OUTGOING_FLOWTYPES);
skip_test('Cannot load int-ext-fields plugin')
    unless check_app_switch($rwcut.' --plugin=int-ext-fields.so --incoming-flowtypes=all/in,all/inweb --outgoing-flowtypes=all/out,all/outweb', 'fields', qr/int-port/);
my $cmd = "$rwcut --plugin=int-ext-fields.so --delimited --incoming-flowtypes=all/in,all/inweb --outgoing-flowtypes=all/out,all/outweb --fields=ext-ip,ext-port,int-ip,int-port,proto,type $file{v6data}";
my $md5 = "2c1e91253d2bf74e84c1ee8b917c65bf";

check_md5_output($md5, $cmd);
