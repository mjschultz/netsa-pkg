#! /usr/bin/perl -w
# MD5:
# TEST:

use strict;
use SiLKTests;

my $rwsettool = check_silk_app('rwsettool');
my $rwsetbuild = check_silk_app('rwsetbuild');
my $rwsetcat = check_silk_app('rwsetcat');
check_features(qw(ipset_v6));

my $seta = make_seta(make_tempname('seta.set'));
my $setb = make_setb(make_tempname('setb.set'));
my $setc = make_setc(make_tempname('setc.set'));

my ($cmd, $md5);

$cmd = "$rwsettool --difference $setb $seta | $rwsetcat --cidr=1 --ip-format=map-v4";
$md5 = 'd41d8cd98f00b204e9800998ecf8427e';
check_md5_output($md5, $cmd);

$cmd = "$rwsettool --difference $seta $setb | $rwsetcat --cidr=1 --ip-format=map-v4";
$md5 = 'cd055aef88b5d308c9f32578de51a477';
check_md5_output($md5, $cmd);

$cmd = "$rwsettool --difference $setc $seta | $rwsetcat --cidr=1";
$md5 = 'a0238acabba8dee04eb1c53a3298bbdb';
check_md5_output($md5, $cmd);

$cmd = "$rwsettool --difference $seta $setc | $rwsetcat --cidr=1 --ip-format=map-v4";
$md5 = 'cd055aef88b5d308c9f32578de51a477';
check_md5_output($md5, $cmd);

exit 0;


sub build_set
{
    my ($file, $data) = @_;

    my $pid = open RWSETBUILD, '|-';
    if (!$pid) {
        die "Unable to fork: $!"
            unless defined $pid;

        # child
        exec "$rwsetbuild - '$file'"
            or die "Unable to exec $rwsetbuild: $!";
    }
    print RWSETBUILD ${$data};
    close RWSETBUILD
        or die "Unable to close $rwsetbuild: $!\n";
    return $file;
}


sub make_seta
{
    my ($file) = @_;

    my $data = <<'EOF';
::ffff:e5e5:e5e4/127
::ffff:dead:dead
::dead:0:f0/124
::dead:0:0
2011:db8::feed
EOF

    return build_set($file, \$data);
}


sub make_setb
{
    my ($file) = @_;

    my $data = <<'EOF';
229.229.229.229
EOF

    return build_set($file, \$data);
}


sub make_setc
{
    my ($file) = @_;

    my $data = <<'EOF';
229.229.229.229
255.0.0.0
EOF

    return build_set($file, \$data);
}
