#! /usr/bin/perl -w
#
#
# RCSIDENT("$SiLK: rwflowpack-pack-silk-cmd.pl 23e62811e29c 2016-11-16 15:30:29Z mthomas $")

use strict;
use SiLKTests;
use File::Find;

my $NAME = $0;
$NAME =~ s,.*/,,;

# find the apps we need.  this will exit 77 if they're not available
my $rwflowpack = check_silk_app('rwflowpack');
my $rwfilter = check_silk_app('rwfilter');
my $rwcut = check_silk_app('rwcut');
my $rwsort = check_silk_app('rwsort');

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

# Generate the config.lua file, and name it based on the test's name
my $config_lua = $0;
$config_lua =~ s,^.*\b(rwflowpack-.+)\.pl,$tmpdir/$1.lua,;
make_config_file($config_lua, get_config_lua_body());

# create the input files
my @input_files = (
    File::Temp::mktemp("$tmpdir/file0.XXXXXX"),
    File::Temp::mktemp("$tmpdir/file1.XXXXXX"),
    File::Temp::mktemp("$tmpdir/file2.XXXXXX"),
    );

my $cmd = ("$rwfilter --type=in --sensor=S8 --pass=-"
           ." --stime=2009/02/12:01-2009/02/12:01 $file{data}"
           ." | $rwfilter --proto=6 --pass=$input_files[0] --fail=- -"
           ." | $rwfilter --proto=17 --print-volume"
           ." --pass=$input_files[1] --fail=$input_files[2] - 2>&1");
check_md5_output('2e4cc5e48c26a9d44c71fd7e977a9258', $cmd);

# the command that wraps rwflowpack
$cmd = join " ", ("$SiLKTests::PYTHON $srcdir/tests/rwflowpack-daemon.py",
                  "--basedir=$tmpdir",
                  "--copy $input_files[0]:incoming",
                  "--copy $input_files[1]:incoming",
                  "--copy $input_files[2]:incoming",
                  "--limit=298",
                  "--",
                  $config_lua,
    );

# run it and check the MD5 hash of its output
check_md5_output('f67164d8e418abe9ca7c495e078cbb26', $cmd);

# the following directories should be empty
verify_empty_dirs($tmpdir, qw(error incoming processing root));

# verify files are in the archive directory
verify_archived_files("$tmpdir/archive", @input_files);

# verify files are in the $cmd_dir
for my $f (@input_files) {
    $f =~ s,.*/,$cmd_dir/,;
    die "$NAME: ERROR: Missing post-archive-command file '$f'\n"
        unless -f $f;
}

# path to the incremental directory
my $data_dir = "$tmpdir/incremental";
die "$NAME: ERROR: Missing data directory '$data_dir'\n"
    unless -d $data_dir;

# combine the files in incremental dir and check the output
$cmd = ("$rwsort --fields=stime,sip"
        ." ".join(" ", glob("$data_dir/?*"))
        ." | $rwcut --delim=,"
        ." --ip-format=hexadecimal --timestamp-format=epoch"
        ." --fields=".join(",", qw(stime sip dip sport dport proto
                                   packets bytes flags etime sensor
                                   initialFlags sessionFlags attributes
                                   application type iType iCode)));

check_md5_output('ec956df5b9e4b176716c37c51459148c', $cmd);

exit 0;


sub get_config_lua_body
{
    my $debug = ($ENV{SK_TESTS_LOG_DEBUG} ? "\n    level = \"debug\"," : "");

    # The Lua configuration file is the text after __END__.  Read it,
    # perform variable substitution, and return a scalar reference.
    local $/ = undef;
    my $text = <DATA>;
    $text =~ s/\$\{cmd_dir\}/$cmd_dir/g;
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

local file_info = {
  record_format = silk.file_format_id("FT_RWIPV6"),
}

local function determine_flowtype (probe, rec)
  -- Determine flowtype
  local saddr = rec.sip
  local daddr = rec.dip

  if probe.external[saddr] then
    -- Came from an external address and...
    if probe.internal[daddr] then
      -- ...went to an internal address (incoming)
      if silk.rwrec_is_web(rec) then
        return "iw"
      else
        return "in"
      end
    elseif probe.null[daddr] then
      -- ...went to the null address
      return "innull"
    elseif probe.external[daddr] then
      -- ...went back to an external address (external to external)
      return "ext2ext"
    end
  elseif probe.internal[saddr] then
    -- Came from an internal address and...
    if probe.external[daddr] then
      -- ...went to an external address (outgoing)
      if silk.rwrec_is_web(rec) then
        return "ow"
      else
        return "out"
      end
    elseif probe.null[daddr] then
      -- ...went to the null address
      return "outnull"
    elseif probe.internal[daddr] then
      -- ...went to another internal address (internal to internal)
      return "int2int"
    end
  end
  -- At least one half of flow had an unrecognized IP.
  return "other"
end

-- Given a probe definition and an rwrec, write the rwRec to
-- appropriate outputs.
local function pack_function (probe, rec)
  local flowtype = determine_flowtype(probe, rec)

  -- Set flowtype and sensor
  rec.classtype_id = silk.site.flowtype_id(flowtype)
  rec.sensor_id = probe.sensor

  -- Write record
  write_rwrec(rec, file_info)
end


--  ------------------------------------------------------------------
--
--  Configuration
--
local tmpdir = "${tmpdir}"

input = {
    mode = "stream",
    probes = {
        P0 = {
            name = "P0",
            type = "silk",
            source = {
                directory = tmpdir .. "/incoming",
                post_archive_command = 'cp %s ${cmd_dir}/.',
                interval = 5,
                archive_directory = tmpdir .. "/archive",
                error_directory = tmpdir .. "/error",
            },
            packing_function = pack_function,
            vars = {
                internal = silk.ipwildcard("192.168.x.x"),
                external = silk.ipwildcard("10.0.0.0/8"),
                null = silk.ipwildcard("172.16.0.0/13"),
                sensor = silk.site.sensor_id("S0"),
            },
        },
    },
}
output = {
    mode = "incremental-files",
    output_directory = tmpdir .. "/incremental",
    flush_interval = 5,
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
