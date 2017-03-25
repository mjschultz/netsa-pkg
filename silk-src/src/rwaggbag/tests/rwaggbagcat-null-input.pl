#! /usr/bin/perl -w
# STATUS: ERR
# TEST: ./rwaggbagcat /dev/null

use strict;
use SiLKTests;

my $rwaggbagcat = check_silk_app('rwaggbagcat');
my $cmd = "$rwaggbagcat /dev/null";

exit (check_exit_status($cmd) ? 1 : 0);
