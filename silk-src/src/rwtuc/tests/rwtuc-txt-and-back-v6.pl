#! /usr/bin/perl -w
# MD5: 5e3644eb255d8e76fbc217763d61fade
# TEST: ../rwcut/rwcut --fields=sip,dip,sport,dport,proto,packets,bytes,stime,dur,sensor,class,type,in,out,application,initialflags,sessionflags,attributes ../../tests/data-v6.rwf | ./rwtuc | ../rwcat/rwcat --compression-method=none --byte-order=little

use strict;
use SiLKTests;

my $rwtuc = check_silk_app('rwtuc');
my $rwcut = check_silk_app('rwcut');
my $rwcat = check_silk_app('rwcat');
my %file;
$file{v6data} = get_data_or_exit77('v6data');
my $cmd = "$rwcut --fields=sip,dip,sport,dport,proto,packets,bytes,stime,dur,sensor,class,type,in,out,application,initialflags,sessionflags,attributes $file{v6data} | $rwtuc | $rwcat --compression-method=none --byte-order=little";
my $md5 = "5e3644eb255d8e76fbc217763d61fade";

check_md5_output($md5, $cmd);
