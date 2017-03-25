#! /usr/bin/perl -w
# STATUS: ERR
# TEST: ./rwaggbag --key=protocol --counter=records >/dev/null

use strict;
use SiLKTests;

my $rwaggbag = check_silk_app('rwaggbag');
my $cmd = "$rwaggbag --key=protocol --counter=records >/dev/null";

exit (check_exit_status($cmd) ? 1 : 0);
