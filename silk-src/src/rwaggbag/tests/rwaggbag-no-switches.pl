#! /usr/bin/perl -w
# STATUS: ERR
# TEST: ./rwaggbag ../../tests/empty.rwf >/dev/null

use strict;
use SiLKTests;

my $rwaggbag = check_silk_app('rwaggbag');
my %file;
$file{empty} = get_data_or_exit77('empty');
my $cmd = "$rwaggbag $file{empty} >/dev/null";

exit (check_exit_status($cmd) ? 1 : 0);
