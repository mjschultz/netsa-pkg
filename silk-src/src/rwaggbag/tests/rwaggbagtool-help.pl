#! /usr/bin/perl -w
# STATUS: OK
# TEST: ./rwaggbagtool --help

use strict;
use SiLKTests;

my $rwaggbagtool = check_silk_app('rwaggbagtool');
my $cmd = "$rwaggbagtool --help";

exit (check_exit_status($cmd) ? 0 : 1);
