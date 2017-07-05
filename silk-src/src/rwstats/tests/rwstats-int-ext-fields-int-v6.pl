#! /usr/bin/perl -w
# MD5: 6552fec9d879d87e96053e55a32c76e0
# TEST: ./rwstats --plugin=int-ext-fields.so --fields=int-ip,int-port --values=packets,records --count=65 ../../tests/data-v6.rwf

use strict;
use SiLKTests;

my $rwstats = check_silk_app('rwstats');
my %file;
$file{v6data} = get_data_or_exit77('v6data');
$ENV{INCOMING_FLOWTYPES} = 'all/in,all/inweb';
$ENV{OUTGOING_FLOWTYPES} = 'all/out,all/outweb';
add_plugin_dirs('/src/plugins');
check_features(qw(ipv6));

push @SiLKTests::DUMP_ENVVARS, qw(INCOMING_FLOWTYPES OUTGOING_FLOWTYPES);
skip_test('Cannot load int-ext-fields plugin')
    unless check_app_switch($rwstats.' --plugin=int-ext-fields.so', 'fields', qr/int-port/);
my $cmd = "$rwstats --plugin=int-ext-fields.so --fields=int-ip,int-port --values=packets,records --count=65 $file{v6data}";
my $md5 = "6552fec9d879d87e96053e55a32c76e0";

check_md5_output($md5, $cmd);
