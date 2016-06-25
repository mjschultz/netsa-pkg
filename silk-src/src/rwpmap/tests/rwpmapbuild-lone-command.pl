#! /usr/bin/perl -w
# STATUS: ERR
# TEST: ./rwpmapbuild

use strict;
use SiLKTests;

my $rwpmapbuild = check_silk_app('rwpmapbuild');
check_features(qw(stdin_tty));
my $cmd = "$rwpmapbuild";

exit (check_exit_status($cmd) ? 1 : 0);
