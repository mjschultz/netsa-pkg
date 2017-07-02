#! /usr/bin/perl -w
#
#
# RCSIDENT("$SiLK: rwflowpack-pack-respool.pl 23e62811e29c 2016-11-16 15:30:29Z mthomas $")

use strict;
use SiLKTests;
use File::Find;

my $NAME = $0;
$NAME =~ s,.*/,,;

# find the apps we need.  this will exit 77 if they're not available
my $rwflowpack = check_silk_app('rwflowpack');
my $rwcat = check_silk_app('rwcat');
my $rwsplit = check_silk_app('rwsplit');
my $rwuniq  = check_silk_app('rwuniq');

# find the data files we use as sources, or exit 77
my %file;
$file{data} = get_data_or_exit77('data');

# prefix any existing PYTHONPATH with the proper directories
check_python_bin();

# create our tempdir
my $tmpdir = make_tempdir();

# Generate the data files
my $cmd = ("$rwsplit --basename=$tmpdir/rwsplit-out --byte-limit=1200000000 "
           .$file{data});
unless (check_exit_status($cmd)) {
    die "$NAME: ERROR: $rwsplit exited with error\n";
}
my @input_files = glob("$tmpdir/rwsplit-out.*");

# Generate the config.lua file, and name it based on the test's name
my $config_lua = $0;
$config_lua =~ s,^.*\b(rwflowpack-.+)\.pl,$tmpdir/$1.lua,;
make_config_file($config_lua, get_config_lua_body());

# the command that wraps rwflowpack
$cmd = join " ", ("$SiLKTests::PYTHON $srcdir/tests/rwflowpack-daemon.py",
                  "--basedir=$tmpdir",
                  (map {"--move $_:incoming"} @input_files),
                  "--limit=501876",
                  "--",
                  $config_lua,
    );

# run it and check the MD5 hash of its output
check_md5_output('a78a286719574389a972724d761c931e', $cmd);

# the following directories should be empty
verify_empty_dirs($tmpdir, qw(error incoming incremental processing));

# input files should now be in the archive directory
verify_directory_files("$tmpdir/archive", @input_files);

# path to the data directory
my $data_dir = "$tmpdir/root";
die "$NAME: ERROR: Missing data directory '$data_dir'\n"
    unless -d $data_dir;

# check the output
$cmd = ("find $data_dir -type f -print"
        ." | $rwcat --xargs"
        ." | $rwuniq --ipv6=ignore --fields=sip,sensor,type,stime"
        ." --values=records,packets,stime,etime --sort");
check_md5_output('247e19c4880a3ec12c365a46bb443766', $cmd);

# successful!
exit 0;


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
if not silk.site.have_site_config() then
  if not silk.site.init_site(nil, nil, true) then
    error("The silk.conf file was not found")
  end
end

local rec_format = silk.file_format_id("FT_RWIPV6")

local tmpdir = "${tmpdir}"
input = {
    mode = "stream",
    probes = {
        {
            name = "respool",
            type = "silk",
            source = {
                archive_policy = "flat",
                directory = tmpdir .. "/incoming",
                archive_directory = tmpdir .. "/archive",
                error_directory = tmpdir .. "/error",
                interval = 5,
            },
            packing_function = function (probe, rec)
                write_rwrec(rec, {record_format = rec_format})
            end,
        },
    },
}
output = {
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
