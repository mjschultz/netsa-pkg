#! /usr/bin/perl -w
# MD5: 6b25fe527045c15ce14f2c6864c107e1
# TEST: ./rwsort --plugin=int-ext-fields.so --fields=int-ip,int-port ../../tests/data-v6.rwf | ../rwstats/rwuniq --plugin=int-ext-fields.so --delimited --fields=int-ip,int-port --presorted-input

use strict;
use SiLKTests;

my $rwsort = check_silk_app('rwsort');
my $rwuniq = check_silk_app('rwuniq');
my %file;
$file{v6data} = get_data_or_exit77('v6data');
$ENV{INCOMING_FLOWTYPES} = 'all/in,all/inweb';
$ENV{OUTGOING_FLOWTYPES} = 'all/out,all/outweb';
add_plugin_dirs('/src/plugins');
check_features(qw(ipv6));

push @SiLKTests::DUMP_ENVVARS, qw(INCOMING_FLOWTYPES OUTGOING_FLOWTYPES);
skip_test('Cannot load int-ext-fields plugin')
    unless check_app_switch($rwsort.' --plugin=int-ext-fields.so', 'fields', qr/int-port/);
my $cmd = "$rwsort --plugin=int-ext-fields.so --fields=int-ip,int-port $file{v6data} | $rwuniq --plugin=int-ext-fields.so --delimited --fields=int-ip,int-port --presorted-input";
my $md5 = "6b25fe527045c15ce14f2c6864c107e1";

check_md5_output($md5, $cmd);
