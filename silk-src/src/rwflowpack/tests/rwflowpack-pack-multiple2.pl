#! /usr/bin/perl -w
#
#
# RCSIDENT("$SiLK: rwflowpack-pack-multiple2.pl 23e62811e29c 2016-11-16 15:30:29Z mthomas $")

use strict;
use SiLKTests;
use File::Find;

my $NAME = $0;
$NAME =~ s,.*/,,;

# find the apps we need.  this will exit 77 if they're not available
my $rwflowpack = check_silk_app('rwflowpack');
my $rwcut = check_silk_app('rwcut');
my $rwsort = check_silk_app('rwsort');

# find the data files we use as sources, or exit 77
my %file;
$file{data} = get_data_or_exit77('data');
$file{pdu} = get_data_or_exit77('pdu_small');

# prefix any existing PYTHONPATH with the proper directories
check_python_bin();

# create our tempdir
my $tmpdir = make_tempdir();

# Generate the config.lua file, and name it based on the test's name
my $config_lua = $0;
$config_lua =~ s,^.*\b(rwflowpack-.+)\.pl,$tmpdir/$1.lua,;
make_config_file($config_lua, get_config_lua_body());

# the command that wraps rwflowpack
my $cmd = join " ", ("$SiLKTests::PYTHON $srcdir/tests/rwflowpack-daemon.py",
                     "--basedir=$tmpdir",
                     "--daemon-timeout=90",
                     "--copy $file{data}:incoming",
                     "--copy $file{data}:incoming2",
                     "--copy $file{pdu}:incoming3",
                     "--copy $file{pdu}:incoming4",
                     "--limit=1103752",
                     "--",
                     $config_lua,
    );

# run it and check the MD5 hash of its output
check_md5_output('e4d9a9fe18a95da02c3cf1123e9b8139', $cmd);

# the following directories should be empty
verify_empty_dirs($tmpdir, qw(error incoming incoming2 incoming3 incoming4
                              incremental processing));

# input files should now be in the archive directory
verify_archived_files("$tmpdir/archive", $file{data}, $file{pdu});

# path to the data directory
my $data_dir = "$tmpdir/root";
die "$NAME: ERROR: Missing data directory '$data_dir'\n"
    unless -d $data_dir;

# number of files to find in the data directory
my $expected_count = 0;
my $file_count = 0;

# read in the MD5s for every packed file we expect to find.
my %md5_map;
my $md5_file = "$0.txt";
# The checksum is the same as that for the other multiple test
$md5_file =~ s/(multiple)2/$1/;

open F, $md5_file
    or die "$NAME: ERROR: Cannot open $md5_file: $!\n";
while (my $lines = <F>) {
    my ($md5, $path) = split " ", $lines;
    $md5_map{$path} = $md5;
    ++$expected_count;
}
close F;

# find the files in the data directory and compare their MD5 hashes
File::Find::find({wanted => \&check_file, no_chdir => 1}, $data_dir);

# did we find all our files?
die "$NAME: ERROR: Found $file_count files in root; expected $expected_count\n"
    unless ($file_count == $expected_count);

# successful!
exit 0;


# this is called by File::Find::find.  The full path to the file is in
# the $_ variable
sub check_file
{
    # skip anything that is not a file
    return unless -f $_;
    my $path = $_;
    # set $_ to just be the file basename
    s,^.*/,,;
    die "$NAME: ERROR: Unexpected file $path\n"
        unless $md5_map{$_};
    ++$file_count;

    my $check_cmd
        = ("$rwsort --fields=".join(",", qw(stime sip dip sport dport proto
                                            sensor application
                                            flags initialflags sessionflags))
           ." $path"
           ." | $rwcut --delim=,"
           ." --ip-format=hexadecimal --timestamp-format=epoch"
           ." --fields=".join(",", qw(sip dip sport dport proto
                                      packets bytes flags stime etime sensor
                                      initialFlags sessionFlags attributes
                                      application type iType iCode)));

    # do the MD5 sums match?
    check_md5_output($md5_map{$_}, $check_cmd);
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
if not silk.site.have_site_config() then
  if not silk.site.init_site(nil, nil, true) then
    error("The silk.conf file was not found")
  end
end

local file_info = {
  record_format = silk.file_format_id("FT_RWIPV6"),
}

-- For determining if a flow record is incoming, outgoing, or null
local ipblocks_internal = silk.ipwildcard("192.168.x.x")
local ipblocks_external = silk.ipwildcard("10.0.0.0/8")
local ipblocks_null     = silk.ipwildcard("172.16.0.0/13")

local function determine_flowtype (probe, rec)
  -- Determine flowtype
  local saddr = rec.sip
  local daddr = rec.dip

  if ipblocks_external[saddr] then
    -- Came from an external address and...
    if ipblocks_internal[daddr] then
      -- ...went to an internal address (incoming)
      if silk.rwrec_is_web(rec) then
        return "iw"
      else
        return "in"
      end
    elseif ipblocks_null[daddr] then
      -- ...went to the null address
      return "innull"
    elseif ipblocks_external[daddr] then
      -- ...went back to an external address (external to external)
      return "ext2ext"
    end
  elseif ipblocks_internal[saddr] then
    -- Came from an internal address and...
    if ipblocks_external[daddr] then
      -- ...went to an external address (outgoing)
      if silk.rwrec_is_web(rec) then
        return "ow"
      else
        return "out"
      end
    elseif ipblocks_null[daddr] then
      -- ...went to the null address
      return "outnull"
    elseif ipblocks_internal[daddr] then
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
        P0_silk = {
            name = "P0-silk",
            type = "silk",
            source = {
                directory = tmpdir .. "/incoming",
                interval = 5,
                archive_directory = tmpdir .. "/archive",
                error_directory = tmpdir .. "/error",
            },
            packing_function = pack_function,
            vars = {
                sensor = silk.site.sensor_id("S0"),
            },
        },
        P1 = {
            name = "P1",
            type = "silk",
            source = {
                directory = tmpdir .. "/incoming2",
                interval = 5,
                archive_directory = tmpdir .. "/archive",
                error_directory = tmpdir .. "/error",
            },
            packing_function = pack_function,
            vars = {
                sensor = silk.site.sensor_id("S1"),
            },
        },
        P0_pdu = {
            name = "P0-pdu",
            type = "netflow-v5",
            source = {
                directory = tmpdir .. "/incoming3",
                interval = 5,
                archive_directory = tmpdir .. "/archive",
                error_directory = tmpdir .. "/error",
            },
            packing_function = pack_function,
            vars = {
                sensor = silk.site.sensor_id("S0"),
            },
        },
        P2 = {
            name = "P2",
            type = "netflow-v5",
            source = {
                directory = tmpdir .. "/incoming4",
                interval = 5,
                archive_directory = tmpdir .. "/archive",
                error_directory = tmpdir .. "/error",
            },
            packing_function = pack_function,
            vars = {
                sensor = silk.site.sensor_id("S2"),
            },
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
