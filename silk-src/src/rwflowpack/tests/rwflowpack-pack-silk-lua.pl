#! /usr/bin/perl -w
#
#
# RCSIDENT("$SiLK: rwflowpack-pack-silk-lua.pl 23e62811e29c 2016-11-16 15:30:29Z mthomas $")

use strict;
use SiLKTests;
use File::Find;

my $NAME = $0;
$NAME =~ s,.*/,,;

# find the apps we need.  this will exit 77 if they're not available
my $rwflowpack = check_silk_app('rwflowpack');
my $rwuniq = check_silk_app('rwuniq');

# find the data files we use as sources, or exit 77
my %file;
$file{data} = get_data_or_exit77('data');

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
                     "--copy $file{data}:incoming",
                     "--limit=501876",
                     "--",
                     $config_lua,
    );

# run it and check the MD5 hash of its output
check_md5_output('a78a286719574389a972724d761c931e', $cmd);

# the following directories should be empty
verify_empty_dirs($tmpdir, qw(error incoming incremental root processing));

# input files should now be in the archive directory
verify_directory_files("$tmpdir/archive", $file{data});

# path to the destination file
my $one_dest = "$tmpdir/one-dest.rw";

# check the output
$cmd = ("cat $one_dest"
        ." | $rwuniq --ipv6=ignore --fields=sip,sensor,type,stime"
        ." --values=records,packets,stime,etime --sort");
check_md5_output('3135fa86995d5b525086eca240add57f', $cmd);

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
  write_rwrec(rec)
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
                archive_policy = "flat",
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
    mode = "one-destination",
    flush_interval = 10,
    destination_file = tmpdir .. "/one-dest.rw",
    file_info = {
      record_format = silk.file_format_id("FT_RWIPV6"),
    },
}
log = {
    destination = "stderr",${debug}
}
daemon = {
    fork = false,
}