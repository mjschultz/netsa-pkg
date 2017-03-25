#! /usr/bin/perl -w
# STATUS: OK
# TEST: ./rwaggbagbuild --version

use strict;
use SiLKTests;

my $rwaggbagbuild = check_silk_app('rwaggbagbuild');
my $cmd = "$rwaggbagbuild --version";

exit (check_exit_status($cmd) ? 0 : 1);
