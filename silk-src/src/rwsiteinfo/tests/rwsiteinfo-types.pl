#! /usr/bin/perl -w
# MD5: 31d703812c2d857ca893eb57f6e18c4f
# TEST: ./rwsiteinfo --fields=type --site-config-file ../../tests/test-site.conf

use strict;
use SiLKTests;

my $rwsiteinfo = check_silk_app('rwsiteinfo');
my $cmd = "$rwsiteinfo --fields=type --site-config-file $SiLKTests::top_srcdir/tests/test-site.conf";
my $md5 = "31d703812c2d857ca893eb57f6e18c4f";

check_md5_output($md5, $cmd);
