#! /usr/bin/perl -w
# MD5: bc5d48751a71ceea0e633fd9e3c5ebd5
# TEST: ../rwcut/rwcut --fields=protocol,dport,packets --column-sep=, --no-title ../../tests/data.rwf | ./rwbagbuild --pmap-file=service-port:../../tests/proto-port-map.pmap --delimiter=, --bag-input=- --key-type=dport-pmap | ./rwbagcat --pmap-file=service-port:../../tests/proto-port-map.pmap

use strict;
use SiLKTests;

my $rwbagbuild = check_silk_app('rwbagbuild');
my $rwcut = check_silk_app('rwcut');
my $rwbagcat = check_silk_app('rwbagcat');
my %file;
$file{data} = get_data_or_exit77('data');
$file{proto_port_map} = get_data_or_exit77('proto_port_map');
my $cmd = "$rwcut --fields=protocol,dport,packets --column-sep=, --no-title $file{data} | $rwbagbuild --pmap-file=service-port:$file{proto_port_map} --delimiter=, --bag-input=- --key-type=dport-pmap | $rwbagcat --pmap-file=service-port:$file{proto_port_map}";
my $md5 = "bc5d48751a71ceea0e633fd9e3c5ebd5";

check_md5_output($md5, $cmd);
