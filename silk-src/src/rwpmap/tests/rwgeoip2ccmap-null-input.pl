#! /usr/bin/perl -w
# STATUS: OK
# TEST: ./rwgeoip2ccmap </dev/null

use strict;
use SiLKTests;

my $rwgeoip2ccmap = check_silk_app('rwgeoip2ccmap');
my $cmd = "$rwgeoip2ccmap </dev/null";

exit (check_exit_status($cmd) ? 0 : 1);
