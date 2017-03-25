#! /usr/bin/perl -w
# STATUS: ERR
# TEST: ./rwaggbagbuild >/dev/null

use strict;
use SiLKTests;

my $rwaggbagbuild = check_silk_app('rwaggbagbuild');
my $cmd = "$rwaggbagbuild >/dev/null";

exit (check_exit_status($cmd) ? 1 : 0);
