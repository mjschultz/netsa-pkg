#! /usr/bin/perl -w
# STATUS: ERR
# TEST: ./rwaggbag

use strict;
use SiLKTests;

my $rwaggbag = check_silk_app('rwaggbag');
my $cmd = "$rwaggbag";

exit (check_exit_status($cmd) ? 1 : 0);
