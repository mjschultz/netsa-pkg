#! /usr/bin/perl -w
# STATUS: OK
# TEST: ./silklua ./tests/sklua-site-test.lua ../../tests/test-site.conf

use strict;
use SiLKTests;

my $silklua = check_silk_app('silklua');
$ENV{LUA_PATH} = "$SiLKTests::srcdir/tests/\?.lua";
my $cmd = "$silklua $SiLKTests::srcdir/tests/sklua-site-test.lua $SiLKTests::top_srcdir/tests/test-site.conf";

exit (check_exit_status($cmd) ? 0 : 1);
