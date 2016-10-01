#! /usr/bin/perl -w
# MD5
# TEST: ../rwcat/rwcat --compression-method=none ../../tests/empty.rwf | ./rwfileinfo --fields=compression --no-title -

use strict;
use SiLKTests;

my $rwfileinfo = check_silk_app('rwfileinfo');
my $rwcat = check_silk_app('rwcat');
my %file;
$file{data} = get_data_or_exit77('data');

my @methods = (
    {option => 'none',
     id => 0,
     avail => 1,
     md5 => '3fb4b62925e27472f3306e0482a07504'},
    {option => 'zlib',
     id => 1,
     avail => $SiLKTests::SK_ENABLE_ZLIB,
     md5 => '50bad70c9fd25b5018b197da25318c19'},
    {option => 'lzo1x',
     id => 2,
     avail => $SiLKTests::SK_ENABLE_LZO,
     md5 => 'e3dcb0daad0f8a053350c78d496dd123'},
    {option => 'snappy',
     id => 3,
     avail => $SiLKTests::SK_ENABLE_SNAPPY,
     md5 => '1703dfaed2dba83e42085ca00d117af0'},
    );

for my $m (@methods) {
    next unless $m->{avail};
    my $cmd = ("$rwcat --compression-method=".$m->{option}." ".$file{data}
               ." | $rwfileinfo --fields=compression --no-title -");
    check_md5_output($m->{md5}, $cmd);
}
