#! /usr/bin/perl -w
#
#
# RCSIDENT("$SiLK: rwflowpack-append-hours.pl 087c94d95f37 2016-05-19 21:29:35Z mthomas $")

use strict;
use SiLKTests;
use File::Temp ();

my $NAME = $0;
$NAME =~ s,.*/,,;

# find the apps we need.  this will exit 77 if they're not available
my $rwflowpack = check_silk_app('rwflowpack');
my $rwtuc = check_silk_app('rwtuc');

# prefix any existing PYTHONPATH with the proper directories
check_python_bin();

# create our tempdir
my $tmpdir = make_tempdir();

# the incremental files
my %input_files;

# the relative directory we expect
my %dirpath;

# get current time
my $t1 = time;

# create 16 files whose times differ from each other by two hours and
# range from 20 hours ago to 10 hours into future.  Stepping by two
# hours avoids issues if hour rolls over during the test
for (my $h = -20; $h <= 10; $h += 2) {
    my $ht = $t1 + $h * 3600;
    my @gmt = gmtime($ht);
    $dirpath{$h} =  sprintf("in/%04d/%02d/%02d",
                            $gmt[5] + 1900, $gmt[4] + 1, $gmt[3]);
    my $f = sprintf("%s/in-S8_%04d%02d%02d.%02d.XXXXXX",
                    $tmpdir, $gmt[5] + 1900, $gmt[4] + 1, $gmt[3], $gmt[2]);
    $input_files{$h} = File::Temp::mktemp($f);
    my $cmd = ("echo 10.$gmt[4].$gmt[3].$gmt[2],$ht"
               ." | $rwtuc --fields=sip,stime --column-sep=,"
               ." --output-path=$input_files{$h}");
    check_md5_output('d41d8cd98f00b204e9800998ecf8427e', $cmd);
}

# time window outside of which to reject data.
#
# the following causes the test to pass 11 files and fail 5 files,
# which is required for the MD5 to succeed.
my $reject_future = 2 * int(rand 6);
if (defined $ENV{REJECT_FUTURE} && $ENV{REJECT_FUTURE} =~ /^(\d+)/) {
    if ($1 > 5) {
        die "$NAME: Maximum value for REJECT_FUTURE is 5\n";
    }
    $reject_future = $1;
}

# use 21 instead of 20 in case the hour rolls over
my $reject_past = 21 - $reject_future;

# don't provide the switch when we're at the edge of the time window
if ($reject_future == 10) {
    undef $reject_future;
}
elsif ($reject_past == 21) {
    undef $reject_past;
}

unless (defined $ENV{RWFLOWAPPEND}) {
    $ENV{RWFLOWAPPEND} = check_silk_app('rwflowpack');
}

# Generate the config.lua file, and name it based on the test's name
my $config_lua = $0;
$config_lua =~ s,^.*\b(rwflowpack-.+)\.pl,$tmpdir/$1.lua,;
make_config_file($config_lua, get_config_lua_body());

# the command that wraps rwflowpack
my $cmd = join " ", ("$SiLKTests::PYTHON $srcdir/tests/rwflowappend-daemon.py",
                     "--basedir=$tmpdir",
                     "--daemon-timeout=15",
                     (map {"--copy $input_files{$_}:incoming"}
                      keys %input_files),
                     "--",
                     $config_lua,
    );

# run it
check_md5_output('cc4fbfb7b7fc7d2d063da4488a5df1b8', $cmd);

# the following directories should be empty
verify_empty_dirs($tmpdir, qw(incoming processing));

# verify files are in proper directory
my @valid_keys;
my @error_keys;

for my $k (sort {$a <=> $b} keys %input_files) {
    if ((defined($reject_past) && $k < -$reject_past)
        || (defined($reject_future) && $k > $reject_future))
    {
        # should be in error directory
        push @error_keys, $k;
    }
    else {
        # should be in archive and data directories
        push @valid_keys, $k;
    }
}

verify_directory_files("$tmpdir/error", map {$input_files{$_}} @error_keys);

verify_archived_files("$tmpdir/archive", map {$input_files{$_}} @valid_keys);

my $data_dir = "$tmpdir/root";
for my $k (@valid_keys) {
    my $f = $input_files{$k};
    $f =~ s,.*/,$data_dir/$dirpath{$k}/,;
    $f =~ s/(\.\d\d)\.......$/$1/;
    die "$NAME: ERROR: Missing data file '$f'\n"
        unless -f $f;
}


sub get_config_lua_body
{
    my $rej_past = "";
    my $rej_future = "";
    my $debug = "";

    if (defined $reject_past) {
        $rej_past = "\n    reject_hours_past = $reject_past,";
    }
    if (defined $reject_future) {
        $rej_future = "\n    reject_hours_future = $reject_future,";
    }
    if ($ENV{SK_TESTS_LOG_DEBUG}) {
        $debug = "\n    level = \"debug\",";
    }

    # The Lua configuration file is the text after __END__.  Read it,
    # perform variable substitution, and return a scalar reference.
    local $/ = undef;
    my $text = <DATA>;
    $text =~ s/\$\{rej_past\}/$rej_past/;
    $text =~ s/\$\{rej_future\}/$rej_future/;
    $text =~ s/\$\{tmpdir\}/$tmpdir/;
    $text =~ s/\$\{debug\}/$debug/;

    # Check for unexpanded variable
    if ($text =~ /(\$\{?\w+\}?)/) {
        die "$NAME: Unknown variable '$1' in Lua configuration\n";
    }
    return \$text;
}

__END__
local tmpdir = "${tmpdir}"
input = {
    mode = "append-incremental",
    incoming = {
        directory = tmpdir .. "/incoming",
        archive_directory = tmpdir .. "/archive",
        error_directory = tmpdir .. "/error",
        interval = 5,
    }
}
output = {
    mode = "local-storage",
    flush_interval = 10,
    processing = {
        directory = tmpdir .. "/processing",
        error_directory = tmpdir .. "/error",
    },
    root_directory = tmpdir .. "/root",${rej_past}${rej_future}
}
log = {
    destination = "stderr",${debug}
}
daemon = {
    fork = false,
}
