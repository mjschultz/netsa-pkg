#! /usr/bin/perl -w
# STATUS: OK
# TEST: ./silklua ./tests/sklua-test.lua

use strict;
use SiLKTests;

my $silklua = check_silk_app('silklua');
my %file;
$file{ip_map} = get_data_or_exit77('ip_map');
$file{v6_ip_map} = get_data_or_exit77('v6_ip_map');
$file{proto_port_map} = get_data_or_exit77('proto_port_map');
$ENV{LUA_PATH} = "$SiLKTests::srcdir/tests/\?.lua";
my $cmd = "$silklua $SiLKTests::srcdir/tests/sklua-test.lua";

exit (check_exit_status($cmd) ? 0 : 1);
