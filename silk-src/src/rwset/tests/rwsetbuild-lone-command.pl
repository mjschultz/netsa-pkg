#! /usr/bin/perl -w
# STATUS: ERR
# TEST: ./rwsetbuild

use strict;
use SiLKTests;

my $rwsetbuild = check_silk_app('rwsetbuild');
check_features(qw(stdin_tty));
my $cmd = "$rwsetbuild";

exit (check_exit_status($cmd) ? 1 : 0);
