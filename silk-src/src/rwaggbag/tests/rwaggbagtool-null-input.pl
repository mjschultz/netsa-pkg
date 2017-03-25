#! /usr/bin/perl -w
# STATUS: ERR
# TEST: ./rwaggbagtool --output-path=/dev/null </dev/null

use strict;
use SiLKTests;

my $rwaggbagtool = check_silk_app('rwaggbagtool');
my $cmd = "$rwaggbagtool --output-path=/dev/null </dev/null";

exit (check_exit_status($cmd) ? 1 : 0);
