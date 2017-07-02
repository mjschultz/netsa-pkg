#! /usr/bin/perl -w
# STATUS: OK
# TEST: ./rwfilter --all=/dev/null /dev/null

use strict;
use SiLKTests;

my $rwfilter = check_silk_app('rwfilter');
my $cmd = "$rwfilter --all=/dev/null /dev/null";

exit (check_exit_status($cmd) ? 0 : 1);
