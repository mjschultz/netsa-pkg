#! /usr/bin/perl -w
# MD5: a8e51ccd9b5d6ffe95428bb30ee10af9
# TEST: ./rwuniq --plugin=int-ext-fields.so --fields=int-ip,int-port --sort-output ../../tests/data-v6.rwf

use strict;
use SiLKTests;

my $rwuniq = check_silk_app('rwuniq');
my %file;
$file{v6data} = get_data_or_exit77('v6data');
$ENV{INCOMING_FLOWTYPES} = 'all/in,all/inweb';
$ENV{OUTGOING_FLOWTYPES} = 'all/out,all/outweb';
add_plugin_dirs('/src/plugins');
check_features(qw(ipv6));

push @SiLKTests::DUMP_ENVVARS, qw(INCOMING_FLOWTYPES OUTGOING_FLOWTYPES);
skip_test('Cannot load int-ext-fields plugin')
    unless check_app_switch($rwuniq.' --plugin=int-ext-fields.so', 'fields', qr/int-port/);
my $cmd = "$rwuniq --plugin=int-ext-fields.so --fields=int-ip,int-port --sort-output $file{v6data}";
my $md5 = "a8e51ccd9b5d6ffe95428bb30ee10af9";

check_md5_output($md5, $cmd);
