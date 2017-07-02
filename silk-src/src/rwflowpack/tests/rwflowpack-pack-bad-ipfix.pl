#! /usr/bin/perl -w
#
#
# RCSIDENT("$SiLK: rwflowpack-pack-bad-ipfix.pl 23e62811e29c 2016-11-16 15:30:29Z mthomas $")

use strict;
use SiLKTests;
use File::Temp ();

my $NAME = $0;
$NAME =~ s,.*/,,;

# find the apps we need.  this will exit 77 if they're not available
my $rwflowpack = check_silk_app('rwflowpack');

# find the data files we use as sources, or exit 77
my %file;
$file{empty_ipfix} = get_data_or_exit77('empty_ipfix');

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
($fh, $inval{junk}) = File::Temp::tempfile("$tmpdir/invalid.XXXXXX");
print $fh $inval{junk};
close $fh
    or die "$NAME: ERROR: Cannot close $inval{junk}: $!\n";

# create an IPFIX file that contains no records
$inval{empty} = File::Temp::mktemp("$tmpdir/empty.XXXXXX");
system "/bin/cp", $file{empty_ipfix}, $inval{empty}
    and die "$NAME: ERROR: Failed to copy file\n";

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
verify_empty_dirs($tmpdir, qw(incoming incremental root processing));

# verify files in the archive directory
verify_archived_files("$tmpdir/archive", $inval{empty});

# verify files in the error directory
verify_directory_files("$tmpdir/error", (grep {!/empty/} values %inval));

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

-- Given a probe definition, an rwrec, and the corresponding fixrec,
-- write the rwrec to appropriate outputs.
local function pack_function (probe, fwd_rec, rev_rec, fixrec)
  local flowtype = determine_flowtype(probe, fwd_rec)

  -- Set flowtype and sensor
  fwd_rec.classtype_id = silk.site.flowtype_id(flowtype)
  fwd_rec.sensor_id = probe.sensor

  -- Write record
  write_rwrec(fwd_rec, file_info)

  if rev_rec then
    flowtype = determine_flowtype(probe, rev_rec)

    -- Set flowtype and sensor
    rev_rec.classtype_id = silk.site.flowtype_id(flowtype)
    rev_rec.sensor_id = probe.sensor

    -- Write record
    write_rwrec(rev_rec, file_info)
  end
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
            type = "ipfix",
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
