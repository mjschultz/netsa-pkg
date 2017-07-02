#! /usr/bin/perl -w
#
#
# RCSIDENT("$SiLK: rwflowpack-append-bad.pl 087c94d95f37 2016-05-19 21:29:35Z mthomas $")

use strict;
use SiLKTests;
use File::Find;
use File::Temp ();

my $NAME = $0;
$NAME =~ s,.*/,,;

# find the apps we need.  this will exit 77 if they're not available
my $rwflowpack = check_silk_app('rwflowpack');
my $rwcut = check_silk_app('rwcut');
my $rwfilter = check_silk_app('rwfilter');

# find the data files we use as sources, or exit 77
my %file;
$file{data} = get_data_or_exit77('data');
$file{empty} = get_data_or_exit77('empty');

# prefix any existing PYTHONPATH with the proper directories
check_python_bin();

# create our tempdir
my $tmpdir = make_tempdir();

# create the files
my %input_files = (
    badname   => File::Temp::mktemp("$tmpdir/bad-name.XXXXXX"),
    nonsilk   => File::Temp::mktemp("$tmpdir/in-S8_20090212.01.XXXXXX"),
    empty     => File::Temp::mktemp("$tmpdir/in-S8_20090212.01.XXXXXX"),
    truncate1 => File::Temp::mktemp("$tmpdir/in-S8_20090212.02.XXXXXX"),
    truncate2 => File::Temp::mktemp("$tmpdir/in-S8_20090212.02.XXXXXX"),
    );

# the empty file contains no records
system("cp", $file{empty}, $input_files{empty})
    and die "$NAME: ERROR: Cannot copy file to '$input_files{empty}'\n";

# badname is a valid SiLK file but does not have the proper header or
# proper name
link $input_files{empty}, $input_files{badname}
    or die "$NAME: ERROR: Cannot create link '$input_files{badname}': $!\n";

# create a completely invalid SiLK flow file by having a file that
# contains the file's name as text
open F, ">$input_files{nonsilk}"
    or die "$NAME: ERROR: Unable to open '$input_files{nonsilk}: $!\n";
print F $input_files{nonsilk};
close F;

# create a file with two valid records, then shave a few bytes off the
# second record to test a short-read; use this file twice
my $cmd = ("$rwfilter --type=in --sensor=S8 --pass=$input_files{truncate1}"
           ." --stime=2009/02/12:02-2009/02/12:02 --max-pass=2"
           ." --print-volume --compression-method=none $file{data} 2>&1");
check_md5_output('8132a5d39d1b4caed01861fe029c703e', $cmd);

open F, "+<", $input_files{truncate1}
    or die "$NAME: ERROR: Cannot open '$input_files{truncate1}' for update: $!'\n";
binmode F;
truncate F, ((-s F) - 3)
    or die "$NAME: ERROR: Cannot truncate '$input_files{truncate1}': $!\n";
close F
    or die "$NAME: ERROR: Cannot close '$input_files{truncate1}': $!\n";

link $input_files{truncate1}, $input_files{truncate2}
    or die "$NAME: ERROR: Cannot create link '$input_files{truncate2}': $!\n";

unless (defined $ENV{RWFLOWAPPEND}) {
    $ENV{RWFLOWAPPEND} = check_silk_app('rwflowpack');
}

# Generate the config.lua file, and name it based on the test's name
my $config_lua = $0;
$config_lua =~ s,^.*\b(rwflowpack-.+)\.pl,$tmpdir/$1.lua,;
make_config_file($config_lua, get_config_lua_body());

# the command that wraps rwflowpack
$cmd = join " ", ("$SiLKTests::PYTHON $srcdir/tests/rwflowappend-daemon.py",
                  "--basedir=$tmpdir",
                  "--daemon-timeout=15",
                  (map {"--copy $input_files{$_}:incoming"} keys %input_files),
                  "--",
                  $config_lua,
    );

# run it and check the MD5 hash of its output
check_md5_output('959cfff5b697a519cdefaf21b16e328e', $cmd);

# the following directories should be empty
verify_empty_dirs($tmpdir, qw(incoming processing));

# verify files are in the archive directory
verify_archived_files(
    "$tmpdir/archive",
    map {$input_files{$_}} grep {!/(nonsilk|badname)/} keys %input_files);

# verify files are in the error directory
verify_directory_files(
    "$tmpdir/error",
    map {$input_files{$_}} grep {/(nonsilk|badname)/} keys %input_files);

# path to the data directory
my $data_dir = "$tmpdir/root";
die "$NAME: ERROR: Missing data directory '$data_dir'\n"
    unless -d $data_dir;

# number of files to find in the data directory
my $expected_count = 1;
my $file_count = 0;

# check for other files in the data directory
File::Find::find({wanted => \&check_file, no_chdir => 1}, $data_dir);

# did we find all our files?
die "$NAME: ERROR: Found $file_count files in root; expected $expected_count\n"
    unless ($file_count == $expected_count);

# expected data file
my $data_file = "$data_dir/in/2009/02/12/in-S8_20090212.02";
die "$NAME: ERROR: Missing data file '$data_file'\n"
    unless -f $data_file;

# compute MD5 of data file
my $check_cmd
    = ("$rwcut --delim=,"
       ." --ip-format=hexadecimal --timestamp-format=epoch"
       ." --fields=".join(",", qw(sip dip sport dport proto
                                  packets bytes flags stime etime sensor
                                  initialFlags sessionFlags attributes
                                  application type iType iCode)));

$cmd = "$check_cmd $data_file";
check_md5_output('76e64ff8a717f96246e06528d9305ded', $cmd);

exit 0;


# this is called by File::Find::find.  The full path to the file is in
# the $_ variable
sub check_file
{
    # skip anything that is not a file
    ++$file_count if -f $_;
}


sub get_config_lua_body
{
    my $debug = ($ENV{SK_TESTS_LOG_DEBUG} ? "\n    level = \"debug\"," : "");

    # The Lua configuration file is the text after __END__.  Read it,
    # perform variable substitution, and return a scalar reference.
    local $/ = undef;
    my $text = <DATA>;
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
    repository_writer_threads = 4,
    mode = "local-storage",
    flush_interval = 10,
    processing = {
        directory = tmpdir .. "/processing",
        error_directory = tmpdir .. "/error",
    },
    root_directory = tmpdir .. "/root",
}
log = {
    destination = "stderr",${debug}
}
daemon = {
    fork = false,
}
