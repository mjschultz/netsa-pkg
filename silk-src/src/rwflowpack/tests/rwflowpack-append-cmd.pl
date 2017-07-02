#! /usr/bin/perl -w
#
#
# RCSIDENT("$SiLK: rwflowpack-append-cmd.pl 087c94d95f37 2016-05-19 21:29:35Z mthomas $")

use strict;
use SiLKTests;
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

# prefix any existing PYTHONPATH with the proper directories
check_python_bin();

# create our tempdir
my $tmpdir = make_tempdir();

# the directory to hold the result of running the commands
my $cmd_dir = "$tmpdir/cmdout";
mkdir $cmd_dir
    or skip_test("Cannot create cmdout directory: $!");
# the name of the hourly file is written here
my $hourly_out = "$cmd_dir/hourly.txt";
my $hourly_format = "new hourly %s";


# create the two files
my %input_files = (
    dns  => File::Temp::mktemp("$tmpdir/in-S8_20090212.01.XXXXXX"),
    rest => File::Temp::mktemp("$tmpdir/in-S8_20090212.01.XXXXXX"),
    );

my $cmd = ("$rwfilter --type=in --sensor=S8 --pass=stdout"
           ." --stime=2009/02/12:01-2009/02/12:01 $file{data}"
           ." | $rwfilter --sport=53 --print-volume"
           ." --pass=$input_files{dns} --fail=$input_files{rest} - 2>&1");
check_md5_output('5d44a50315bfe60379fc1b0e7fec5a04', $cmd);


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
                  "--copy $input_files{dns}:incoming",
                  "--copy $input_files{rest}:incoming",
                  "--",
                  $config_lua,
    );

# run it and check the MD5 hash of its output
check_md5_output('be50bfa0b38f0179132c2d2319ef1ad6', $cmd);

# the following directories should be empty
verify_empty_dirs($tmpdir, qw(error incoming processing));

# verify files are in the archive directory
verify_archived_files("$tmpdir/archive", values %input_files);

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
my $data_file = "$data_dir/in/2009/02/12/in-S8_20090212.01";
die "$NAME: ERROR: Missing data file '$data_file'\n"
    unless -f $data_file;

# verify files are in the cmd_out directory
for my $k (keys %input_files) {
    my $f = $input_files{$k};
    $f =~ s,.*/,$cmd_dir/,;
    die "$NAME: ERROR: Missing post-command file '$f'\n"
        unless -f $f;
}
if (! -f $hourly_out) {
    die "$NAME: ERROR: Missing hour-file-command file '$hourly_out'\n";
}
else {
    open HOURLY, $hourly_out
        or die "$NAME: ERROR: Cannot open '$hourly_out': $!\n";
    local $/;
    my $content = <HOURLY>;
    close HOURLY;
    die "$NAME: ERROR: Bad hourly file '$hourly_out': undefined\n"
        unless defined $content;
    die ("$NAME: ERROR: Bad hourly file '$hourly_out' [$content] [",
         sprintf("$hourly_format\n", $data_file),"]\n")
        unless $content eq sprintf("$hourly_format\n", $data_file);
}

# compute MD5 of data file
my $data_md5;
my $check_cmd
    = ("$rwcut --delim=,"
       ." --ip-format=hexadecimal --timestamp-format=epoch"
       ." --fields=".join(",", qw(sip dip sport dport proto
                                  packets bytes flags stime etime sensor
                                  initialFlags sessionFlags attributes
                                  application type iType iCode)));

$cmd = "$check_cmd $data_file";
compute_md5(\$data_md5, $cmd);

# compute MD5 of the joining of the inputs files.  we don't know in
# which order things happened, so handle both cases.
my $input_keys = [sort keys %input_files];

for my $key_order ($input_keys, [reverse @$input_keys]) {
    my $input_md5;
    $cmd = join " ", ($check_cmd,
                      (map {$input_files{$_}} @$key_order));
    compute_md5(\$input_md5, $cmd);
    if ($input_md5 eq $data_md5) {
        exit 0;
    }
}

die "$NAME: ERROR: checksum mismatch [$data_md5] ($cmd)\n";


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
    $text =~ s/\$\{cmd_dir\}/$cmd_dir/;
    $text =~ s/\$\{hourly_format\}/$hourly_format/;
    $text =~ s/\$\{hourly_out\}/$hourly_out/;
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
        post_archive_command = 'cp %s ${cmd_dir}/.',
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
    root_directory = tmpdir .. "/root",
    hour_file_command = 'echo ${hourly_format} >> ${hourly_out}',
}
log = {
    destination = "stderr",${debug}
}
daemon = {
    fork = false,
}
