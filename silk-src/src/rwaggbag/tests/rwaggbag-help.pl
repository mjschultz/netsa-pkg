#! /usr/bin/perl -w
# STATUS: OK
# TEST: ./rwaggbag --help

use strict;
use SiLKTests;

my $rwaggbag = check_silk_app('rwaggbag');
my $cmd = "$rwaggbag --help";

exit (check_exit_status($cmd) ? 0 : 1);
