#! /usr/bin/perl -w
# STATUS: OK
# TEST: ./rwaggbag --version

use strict;
use SiLKTests;

my $rwaggbag = check_silk_app('rwaggbag');
my $cmd = "$rwaggbag --version";

exit (check_exit_status($cmd) ? 0 : 1);
