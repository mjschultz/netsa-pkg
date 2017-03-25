#! /usr/bin/perl -w
# STATUS: ERR
# TEST: ./rwaggbagbuild --fields=protocol,records >/dev/null

use strict;
use SiLKTests;

my $rwaggbagbuild = check_silk_app('rwaggbagbuild');
check_features(qw(stdin_tty));
my $cmd = "$rwaggbagbuild --fields=protocol,records >/dev/null";

exit (check_exit_status($cmd) ? 1 : 0);
