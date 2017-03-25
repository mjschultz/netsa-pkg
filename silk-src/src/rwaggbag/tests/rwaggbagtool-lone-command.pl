#! /usr/bin/perl -w
# STATUS: ERR
# TEST: ./rwaggbagtool

use strict;
use SiLKTests;

my $rwaggbagtool = check_silk_app('rwaggbagtool');
my $cmd = "$rwaggbagtool";

exit (check_exit_status($cmd) ? 1 : 0);
