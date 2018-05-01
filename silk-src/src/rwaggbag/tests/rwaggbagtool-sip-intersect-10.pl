#! /usr/bin/perl -w
# MD5: 8be046f58b91edf9ea70d5c642a1cc98
# TEST: ./rwaggbag --key=sipv4,dipv4 --counter=sum-packets,sum-bytes --output-path=/tmp/rwaggbagtool-sip-intersect-10-tmp ../../tests/data.rwf && echo 10.0.0.0/8 | ../rwset/rwsetbuild --record-version=4 | ./rwaggbagtool --set-intersect=sipv4=- /tmp/rwaggbagtool-sip-intersect-10-tmp | ./rwaggbagcat

use strict;
use SiLKTests;

my $rwaggbagtool = check_silk_app('rwaggbagtool');
my $rwaggbag = check_silk_app('rwaggbag');
my $rwaggbagcat = check_silk_app('rwaggbagcat');
my $rwsetbuild = check_silk_app('rwsetbuild');
my %file;
$file{data} = get_data_or_exit77('data');
my %temp;
$temp{tmp} = make_tempname('tmp');
my $cmd = "$rwaggbag --key=sipv4,dipv4 --counter=sum-packets,sum-bytes --output-path=$temp{tmp} $file{data} && echo 10.0.0.0/8 | $rwsetbuild --record-version=4 | $rwaggbagtool --set-intersect=sipv4=- $temp{tmp} | $rwaggbagcat";
my $md5 = "8be046f58b91edf9ea70d5c642a1cc98";

check_md5_output($md5, $cmd);
