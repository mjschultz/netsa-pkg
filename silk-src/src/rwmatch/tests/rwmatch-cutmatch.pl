#! /usr/bin/perl -w
# MD5: f2635d270bf28268a81af4446954b06b
# TEST: ../rwfilter/rwfilter --daddr=192.168.x.x --dport=0-1024 --pass=stdout ../../tests/data.rwf | ../rwsort/rwsort --fields=1,4,2,3,5,9 --output-path=/tmp/rwmatch-cutmatch-incoming && ../rwfilter/rwfilter --saddr=192.168.x.x --sport=0-1024 --pass=stdout ../../tests/data.rwf | ../rwsort/rwsort --fields=2,3,1,4,5,9 --output-path=/tmp/rwmatch-cutmatch-outgoing && ./rwmatch --ipv6-policy=asv4 --time-delta=2.5 --symmetric-del --relative-del --relate=1,2 --relate=4,3 --relate=2,1 --relate=3,4 --relate=5,5 /tmp/rwmatch-cutmatch-incoming /tmp/rwmatch-cutmatch-outgoing - | ../rwcut/rwcut --plugin=cutmatch.so --ipv6-policy=asv4 --fields=match,sip,sport,dip,dport,proto,type

use strict;
use SiLKTests;

my $rwmatch = check_silk_app('rwmatch');
my $rwcut = check_silk_app('rwcut');
my $rwfilter = check_silk_app('rwfilter');
my $rwsort = check_silk_app('rwsort');
my %file;
$file{data} = get_data_or_exit77('data');
my %temp;
$temp{incoming} = make_tempname('incoming');
$temp{outgoing} = make_tempname('outgoing');
add_plugin_dirs('/src/plugins');

skip_test('Cannot load cutmatch plugin')
    unless check_app_switch($rwcut.' --plugin=cutmatch.so', 'fields', qr/match/);
my $cmd = "$rwfilter --daddr=192.168.x.x --dport=0-1024 --pass=stdout $file{data} | $rwsort --fields=1,4,2,3,5,9 --output-path=$temp{incoming} && $rwfilter --saddr=192.168.x.x --sport=0-1024 --pass=stdout $file{data} | $rwsort --fields=2,3,1,4,5,9 --output-path=$temp{outgoing} && $rwmatch --ipv6-policy=asv4 --time-delta=2.5 --symmetric-del --relative-del --relate=1,2 --relate=4,3 --relate=2,1 --relate=3,4 --relate=5,5 $temp{incoming} $temp{outgoing} - | $rwcut --plugin=cutmatch.so --ipv6-policy=asv4 --fields=match,sip,sport,dip,dport,proto,type";
my $md5 = "f2635d270bf28268a81af4446954b06b";

check_md5_output($md5, $cmd);
