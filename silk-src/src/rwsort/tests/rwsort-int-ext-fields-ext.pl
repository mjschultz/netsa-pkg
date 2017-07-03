#! /usr/bin/perl -w
# MD5: 1298b3f0ab5316ced66ec23c98ef9f10
# TEST: ./rwsort --plugin=int-ext-fields.so --fields=ext-ip,ext-port ../../tests/data.rwf | ../rwstats/rwuniq --plugin=int-ext-fields.so --delimited --fields=ext-ip,ext-port --presorted-input

use strict;
use SiLKTests;

my $rwsort = check_silk_app('rwsort');
my $rwuniq = check_silk_app('rwuniq');
my %file;
$file{data} = get_data_or_exit77('data');
$ENV{INCOMING_FLOWTYPES} = 'all/in,all/inweb';
$ENV{OUTGOING_FLOWTYPES} = 'all/out,all/outweb';
add_plugin_dirs('/src/plugins');

push @SiLKTests::DUMP_ENVVARS, qw(INCOMING_FLOWTYPES OUTGOING_FLOWTYPES);
skip_test('Cannot load int-ext-fields plugin')
    unless check_app_switch($rwsort.' --plugin=int-ext-fields.so', 'fields', qr/int-port/);
my $cmd = "$rwsort --plugin=int-ext-fields.so --fields=ext-ip,ext-port $file{data} | $rwuniq --plugin=int-ext-fields.so --delimited --fields=ext-ip,ext-port --presorted-input";
my $md5 = "1298b3f0ab5316ced66ec23c98ef9f10";

check_md5_output($md5, $cmd);
