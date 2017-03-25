#! /usr/bin/perl -w
# STATUS: OK
# TEST: ./rwaggbagtool --version

use strict;
use SiLKTests;

my $rwaggbagtool = check_silk_app('rwaggbagtool');
my $cmd = "$rwaggbagtool --version";

exit (check_exit_status($cmd) ? 0 : 1);
