#! /usr/bin/perl -w
# MD5: bc5d48751a71ceea0e633fd9e3c5ebd5
# TEST: ./rwbag --pmap-file=service-port:../../tests/proto-port-map.pmap --bag-file=dport-pmap:service-port,packets,- ../../tests/data.rwf | ./rwbagcat --pmap-file=service-port:../../tests/proto-port-map.pmap

use strict;
use SiLKTests;

my $rwbag = check_silk_app('rwbag');
my $rwbagcat = check_silk_app('rwbagcat');
my %file;
$file{data} = get_data_or_exit77('data');
$file{proto_port_map} = get_data_or_exit77('proto_port_map');
my $cmd = "$rwbag --pmap-file=service-port:$file{proto_port_map} --bag-file=dport-pmap:service-port,packets,- $file{data} | $rwbagcat --pmap-file=service-port:$file{proto_port_map}";
my $md5 = "bc5d48751a71ceea0e633fd9e3c5ebd5";

check_md5_output($md5, $cmd);
