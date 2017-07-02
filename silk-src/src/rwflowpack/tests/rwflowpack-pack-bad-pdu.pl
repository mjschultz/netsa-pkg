#! /usr/bin/perl -w
#
#
# RCSIDENT("$SiLK: rwflowpack-pack-bad-pdu.pl 23e62811e29c 2016-11-16 15:30:29Z mthomas $")

use strict;
use SiLKTests;
use File::Temp ();

my $NAME = $0;
$NAME =~ s,.*/,,;

# find the apps we need.  this will exit 77 if they're not available
my $rwflowpack = check_silk_app('rwflowpack');

# find the data files we use as sources, or exit 77
my %file;
$file{empty} = get_data_or_exit77('empty');

# prefix any existing PYTHONPATH with the proper directories
check_python_bin();

# create our tempdir
my $tmpdir = make_tempdir();

# Generate the config.lua file, and name it based on the test's name
my $config_lua = $0;
$config_lua =~ s,^.*\b(rwflowpack-.+)\.pl,$tmpdir/$1.lua,;
make_config_file($config_lua, get_config_lua_body());

# the invalid files
my %inval;

# file handle
my $fh;

# create a completely invalid file containing the file's own name
($fh, $inval{junk}) = File::Temp::tempfile("$tmpdir/junk.XXXXXX");
print $fh $inval{junk};
close $fh
    or die "$NAME: ERROR: Cannot close $inval{junk}: $!\n";

# data used to create the PDU files
my $pdu_data = pack('nnNNNNCCn',
                    # Version
                    5,
                    # Count of flows in this packet
                    0,
                    # Router Uptime, in milliseconds
                    3600_000,
                    # Current time, in epoch seconds
                    1234396800,
                    # Nanosecond resolution of current time
                    0,
                    # Number of records sent in previous packets
                    0,
                    # Engine Type / Engine Id / Sampling Interval
                    1, 2, 0);

# create a PDU file that contains no records and is too short
($fh, $inval{short}) = File::Temp::tempfile("$tmpdir/short.XXXXXX");
binmode $fh;
print $fh $pdu_data;
close $fh
    or die "$NAME: ERROR: Cannot close $inval{short}: $!\n";

# create a PDU file that contains no records and is the correct size
$pdu_data .= "\c@" x (1464 - length $pdu_data);

($fh, $inval{empty}) = File::Temp::tempfile("$tmpdir/empty.XXXXXX");
binmode $fh;
print $fh $pdu_data;
close $fh
    or die "$NAME: ERROR: Cannot close $inval{empty}: $!\n";

# create a PDU file with the wrong version
substr($pdu_data, 0, 2, pack("n", 8));

($fh, $inval{vers8}) = File::Temp::tempfile("$tmpdir/vers8.XXXXXX");
binmode $fh;
print $fh $pdu_data;
close $fh
    or die "$NAME: ERROR: Cannot close $inval{vers8}: $!\n";

# the command that wraps rwflowpack
my $cmd = join " ", ("$SiLKTests::PYTHON $srcdir/tests/rwflowpack-daemon.py",
                     "--basedir=$tmpdir",
                     "--daemon-timeout=20",
                     (map {"--move $inval{$_}:incoming"} keys %inval),
                     "--",
                     $config_lua,
    );

# run it and check the MD5 hash of its output
check_md5_output('8d06e798951bc231967e43b2f18f3499', $cmd);

# the following directories should be empty
verify_empty_dirs($tmpdir, qw(archive incoming incremental root processing));

# verify files in the error directory
verify_directory_files("$tmpdir/error", values %inval);

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
            type = "netflow-v5",
            source = {
                directory = tmpdir .. "/incoming",
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
