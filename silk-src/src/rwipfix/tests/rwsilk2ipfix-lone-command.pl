#! /usr/bin/perl -w
# STATUS: ERR
# TEST: ./rwsilk2ipfix

use strict;
use SiLKTests;

my $rwsilk2ipfix = check_silk_app('rwsilk2ipfix');
check_features(qw(ipfix stdin_tty));
my $cmd = "$rwsilk2ipfix";

exit (check_exit_status($cmd) ? 1 : 0);
