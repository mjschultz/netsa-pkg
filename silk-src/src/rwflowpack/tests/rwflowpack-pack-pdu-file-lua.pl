#! /usr/bin/perl -w
#
#
# RCSIDENT("$SiLK: rwflowpack-pack-pdu-file-lua.pl 23e62811e29c 2016-11-16 15:30:29Z mthomas $")

use strict;
use SiLKTests;
use File::Find;

my $NAME = $0;
$NAME =~ s,.*/,,;

# find the apps we need.  this will exit 77 if they're not available
my $rwflowpack = check_silk_app('rwflowpack');
my $rwcut = check_silk_app('rwcut');

# find the data files we use as sources, or exit 77
my %file;
$file{pdu} = get_data_or_exit77('pdu_small');

# prefix any existing PYTHONPATH with the proper directories
check_python_bin();

# create our tempdir
my $tmpdir = make_tempdir();

# create a copy of the PDU input file
my $pdus = File::Temp::mktemp("$tmpdir/pdu.XXXXXX");
system "cp", $file{pdu}, $pdus;

# Generate the config.lua file, and name it based on the test's name
my $config_lua = $0;
$config_lua =~ s,^.*\b(rwflowpack-.+)\.pl,$tmpdir/$1.lua,;
make_config_file($config_lua, get_config_lua_body());

# the command that wraps rwflowpack
my $cmd = join " ", ("$SiLKTests::PYTHON $srcdir/tests/rwflowpack-daemon.py",
                     "--basedir=$tmpdir",
                     "--",
                     $config_lua,
    );

# run it and check the MD5 hash of its output
check_md5_output('dba69618fe40eafc6a1dca7d888db2b0', $cmd);

# the following directories should be empty
verify_empty_dirs($tmpdir, qw(archive error incoming incremental processing));

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

# MD5 sums are the same as for the poll-dir version
$md5_file =~ s/-file-lua/-dir/;

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
        = ("$rwcut --delim=,"
           ." --ip-format=hexadecimal --timestamp-format=epoch"
           ." --fields=".join(",", qw(sip dip sport dport proto
                                      packets bytes flags stime etime sensor
                                      initialFlags sessionFlags attributes
                                      application type iType iCode))
           ." $path");

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
    $text =~ s/\$\{pdus\}/$pdus/;
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
    mode = "single-file",
    probes = {
        P0 = {
            name = "P0",
            type = "netflow-v5",
            source = {
                file = "${pdus}",
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