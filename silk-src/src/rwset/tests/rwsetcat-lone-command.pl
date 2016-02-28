#! /usr/bin/perl -w
# STATUS: ERR
# TEST: ./rwsetcat

use strict;
use SiLKTests;

my $rwsetcat = check_silk_app('rwsetcat');
check_features(qw(stdin_tty));
my $cmd = "$rwsetcat";

exit (check_exit_status($cmd) ? 1 : 0);
