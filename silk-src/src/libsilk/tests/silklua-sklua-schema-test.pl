#! /usr/bin/perl -w
# STATUS: OK
# TEST: ./silklua ./tests/sklua-schema-test.lua ../../tests/data.ipfix ../../tests/data-v6.ipfix ../../tests/empty.ipfix /tmp/silklua-sklua-schema-test-a /tmp/silklua-sklua-schema-test-b /tmp/silklua-sklua-schema-test-c /tmp/silklua-sklua-schema-test-d /tmp/silklua-sklua-schema-test-e

use strict;
use SiLKTests;

my $silklua = check_silk_app('silklua');
my %file;
$file{data_ipfix} = get_data_or_exit77('data_ipfix');
$file{v6data_ipfix} = get_data_or_exit77('v6data_ipfix');
$file{empty_ipfix} = get_data_or_exit77('empty_ipfix');
my %temp;
$temp{a} = make_tempname('a');
$temp{b} = make_tempname('b');
$temp{c} = make_tempname('c');
$temp{d} = make_tempname('d');
$temp{e} = make_tempname('e');
$ENV{LUA_PATH} = "$SiLKTests::srcdir/tests/\?.lua";
my $cmd = "$silklua $SiLKTests::srcdir/tests/sklua-schema-test.lua $file{data_ipfix} $file{v6data_ipfix} $file{empty_ipfix} $temp{a} $temp{b} $temp{c} $temp{d} $temp{e}";

exit (check_exit_status($cmd) ? 0 : 1);
