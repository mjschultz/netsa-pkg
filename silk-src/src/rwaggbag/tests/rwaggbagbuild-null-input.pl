#! /usr/bin/perl -w
# MD5: 57ca7eaf22fa614b392e48290349c00d
# TEST: ./rwaggbagbuild --fields=protocol,records </dev/null | ./rwaggbagcat

use strict;
use SiLKTests;

my $rwaggbagbuild = check_silk_app('rwaggbagbuild');
my $rwaggbagcat = check_silk_app('rwaggbagcat');
my $cmd = "$rwaggbagbuild --fields=protocol,records </dev/null | $rwaggbagcat";
my $md5 = "57ca7eaf22fa614b392e48290349c00d";

check_md5_output($md5, $cmd);
