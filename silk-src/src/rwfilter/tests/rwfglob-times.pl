#! /usr/bin/perl -w

use strict;
use SiLKTests;

my $rwfglob = check_silk_app('rwfglob');

# the times on each row are equivalent
my @start = (qw(2009/02/13 2009/02/13:00 1234483200),
             qw(2009/02/13T14:15:16 1234534516));

my @end = (qw(NONE),
           qw(2009/02/13),
           qw(2009/02/14 1234569600),
           qw(2009/02/13T15:16:17 1234538177));

# MD5 checksums for each run, where outer key is start date and inner
# key is end date
my %md5 = (
    "2009/02/13" => {
        # all files for 2009/02/13
        "NONE"                => "3519a5bf80272770207ab42e9c1f30c2",
        # all files for 2009/02/13
        "2009/02/13"          => "3519a5bf80272770207ab42e9c1f30c2",
        # all files for 2009/02/13 and 2009/02/14
        "2009/02/14"          => "455c62a6f746b25582133495492d890a",
        # files 2009/02/13.00 to 2009/02/14.00
        "1234569600"          => "dbfc3db0d3553955213ea84a0c4db267",
        # all files for 2009/02/13 (end hour ignored)
        "2009/02/13T15:16:17" => "3519a5bf80272770207ab42e9c1f30c2",
        # files 2009/02/13.00 to 2009/02/13.15
        "1234538177"          => "a5f5d98016d9b9292488ab6e97740ab8",
    },
    "2009/02/13:00" => {
        # single file 2009/02/13.00
        "NONE"                => "26107db388b3b6e68391c858f14c7413",
        # single file 2009/02/13.00
        "2009/02/13"          => "26107db388b3b6e68391c858f14c7413",
        # files 2009/02/13.00 to 2009/02/14.00
        "2009/02/14"          => "dbfc3db0d3553955213ea84a0c4db267",
        # files 2009/02/13.00 to 2009/02/14.00
        "1234569600"          => "dbfc3db0d3553955213ea84a0c4db267",
        # files 2009/02/13.00 to 2009/02/13.15
        "2009/02/13T15:16:17" => "a5f5d98016d9b9292488ab6e97740ab8",
        # files 2009/02/13.00 to 2009/02/13.15
        "1234538177"          => "a5f5d98016d9b9292488ab6e97740ab8",
    },
    "1234483200" => {
        # single file 2009/02/13.00
        "NONE"                => "26107db388b3b6e68391c858f14c7413",
        # all files for 2009/02/13
        "2009/02/13"          => "3519a5bf80272770207ab42e9c1f30c2",
        # all files for 2009/02/13 and 2009/02/14
        "2009/02/14"          => "455c62a6f746b25582133495492d890a",
        # files 2009/02/13.00 to 2009/02/14.00
        "1234569600"          => "dbfc3db0d3553955213ea84a0c4db267",
        # all files for 2009/02/13 (end hour ignored)
        "2009/02/13T15:16:17" => "3519a5bf80272770207ab42e9c1f30c2",
        # files 2009/02/13.00 to 2009/02/13.15
        "1234538177"          => "a5f5d98016d9b9292488ab6e97740ab8",
    },
    "2009/02/13T14:15:16" => {
        # single file 2009/02/13.14
        "NONE"                => "6116c155abdc73507e7c8794b109b1a8",
        # single file 2009/02/13.14
        "2009/02/13"          => "6116c155abdc73507e7c8794b109b1a8",
        # files 2009/02/13.14 to 2009/02/14.14
        "2009/02/14"          => "0516b525d531a8d717415595c2e366dc",
        # files 2009/02/13.14 to 2009/02/14.00
        "1234569600"          => "99e2c6962808e94c70102a6063a48bc4",
        # files 2009/02/13.14 to 2009/02/13.15
        "2009/02/13T15:16:17" => "07ddc0231d6fc8ad4a79fc643e0e4df4",
        # files 2009/02/13.14 to 2009/02/13.15
        "1234538177"          => "07ddc0231d6fc8ad4a79fc643e0e4df4",
    },
    "1234534516" => {
        # single file 2009/02/13.14
        "NONE"                => "6116c155abdc73507e7c8794b109b1a8",
        # single file 2009/02/13.14
        "2009/02/13"          => "6116c155abdc73507e7c8794b109b1a8",
        # files 2009/02/13.14 to 2009/02/14.14
        "2009/02/14"          => "0516b525d531a8d717415595c2e366dc",
        # files 2009/02/13.14 to 2009/02/14.00
        "1234569600"          => "99e2c6962808e94c70102a6063a48bc4",
        # files 2009/02/13.14 to 2009/02/13.15
        "2009/02/13T15:16:17" => "07ddc0231d6fc8ad4a79fc643e0e4df4",
        # files 2009/02/13.14 to 2009/02/13.15
        "1234538177"          => "07ddc0231d6fc8ad4a79fc643e0e4df4",
    },
    );


my $cmd = ("%s --data-rootdir=. --print-missing --no-summary"
           ." --sensors=S13 --type=out --start-date=%s%s 2>&1");

for my $e (@end) {
    my $ed = (('NONE' eq $e) ? "" : " --end-date=$e");
    for my $s (@start) {
        my $c = sprintf($cmd, $rwfglob, $s, $ed);
        check_md5_output($md5{$s}{$e}, $c);
    }
}
