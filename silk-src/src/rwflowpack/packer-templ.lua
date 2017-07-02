-- Lua configuration file for rwflowpack

--  For general help on Lua, see <http://www.lua.org/docs.html>.  The
--  Lua reference manual is also included in the SiLK sources.  See
--  silk-4.0.0/lua/doc/contents.html
--
--  For help on functions in the "silk" module, see the silklua manual
--  page.

--  Define a convenience variable to define top level directory
local topdir = "/home/silk4"

--  Define a convenience variable giving the location of the data
--  repository
local rootdir = topdir .. "/data"

--  Ensure the silk.conf file is available.  This checks for it in
--  "rootdir/silk.conf"
--
if not silk.site.have_site_config() then
  if not silk.site.init_site(nil, rootdir, true) then
    error("The silk.conf file was not found")
  end
end

--  Define a convenience table for mapping flowtype names to integer
--  values
local ft_name_id_map = {
  inweb = silk.site.flowtype_id("iw"),
  in_nonweb = silk.site.flowtype_id("in"),
  innull = silk.site.flowtype_id("innull"),

  outweb = silk.site.flowtype_id("ow"),
  out_nonweb = silk.site.flowtype_id("out"),
  outnull = silk.site.flowtype_id("outnull"),

  ext2ext = silk.site.flowtype_id("ext2ext"),
  int2int = silk.site.flowtype_id("int2int"),

  other = silk.site.flowtype_id("other"),
}


--  Define variables that determine what goes into the files that
--  rwflowpack creates.
--
--  Either the 'file_info' or the 'file_info_sc' variable is passed
--  into the write_rwrec() function, and it determines the data that
--  is included in the files written to the data repository.  The keys
--  of this table that are understood by write_rwrec() are
--
--  'file_format' : The SiLK Flow record format to use.  This should
--  be a file_format_id.  Common values are:
--
--      "FT_RWIPV6ROUTING": A complete IPv6 SiLK Flow record
--
--      "FT_RWIPV6": Support for IPv6 records, but does not include
--      the next hop IP or the SNMP input and output interfaces.
--
--      "FT_RWGENERIC": A complete IPv4 SiLK Flow record.
--
--      "FT_RWAUGROUTING: A complete IPv4 SiLK Flow record, but with
--      some bit-shaving and bytes stored as a bytes-per-packet ratio.
--
--      "FT_RWAUGMENTED": Similar to RWAUGROUTING, but without the
--      next hop IP and SNMP input and output interfaces.
--
--      Additional formats exist.  See silk/src/libsilk/*io.c.
--
--  'file_version' : A number that is the version of the 'file_format'
--  to use.  Normally this is not specified which indicates that the
--  default version of that format is to be used.  These are only
--  documented in the source files.
--
--  'sidecar' : A silk.sidear description object that describes the
--  fields that MAY be included as sidecar data on each record.  If an
--  individual record includes fields in its sidecar table that are
--  not included in this description object, those fields are NOT
--  stored in the repository.
--
--  'file_info' is used for files that do NOT include sidecar fields
--
local file_info = {
  file_format = silk.file_format_id("FT_RWIPV6"),
}
--
--  'file_info_sc' is used for files that DO include sidecar fields
--
local file_info_sc = {
  file_format = file_info.file_format
}
do
  -- Sidecar template for DNS data
  local CERT_PEN = 6871

  local sidecar = silk.sidecar_create()
  sidecar["dnsQName"] = {
    type = "string", enterprise_id = CERT_PEN, element_id = 179,
  }
  sidecar["dnsTTL"] = {
    type = "uint32", enterprise_id = CERT_PEN, element_id = 199,
  }
  sidecar["dnsQRType"] = {
    type = "uint16", enterprise_id = CERT_PEN, element_id = 175,
  }
  sidecar["dnsQueryResponse"] = {
    type = "boolean", enterprise_id = CERT_PEN, element_id = 174,
  }
  sidecar["dnsAuthoritative"] = {
    type = "boolean", enterprise_id = CERT_PEN, element_id = 176,
  }
  sidecar["dnsNXDomain"] = {
    type = "uint8", enterprise_id = CERT_PEN, element_id = 177,
  }
  sidecar["dnsRRSection"] = {
    type = "uint8", enterprise_id = CERT_PEN, element_id = 178,
  }
  sidecar["dnsID"] = {
    type = "uint16", enterprise_id = CERT_PEN, element_id = 226,
  }
  silk.sidecar_freeze(sidecar)
  file_info_sc.sidecar = sidecar
end


--  Define a helper function used by the pack_function() below.
--
--  This function determines the forward and reverse flowtypes and the
--  repository file format for the record 'rec' that was collected
--  from probe 'probe'.  Each call to this function returns three
--  values.
--
local function determine_flowtype (probe, rec)
  local saddr = rec.sip
  local daddr = rec.dip

  if silk.rwrec_is_ipv6(rec) then
    -- My sample data is almost completely IPv4, and any IPv6 data I
    -- have goes into the "other" type.
    --
    -- Although the next line would be easier to read if the list of
    -- return values were in parens or something, that is not
    -- supported by the Lua language
    return ft_name_id_map["other"], ft_name_id_map["other"], file_info
  end

  -- Compare the source and destination IP addresses on each record to
  -- the list of IPs defined on the probe to determine whether the
  -- data is incoming or outgoing.
  --
  if not probe.internal[saddr] then
    -- Flow came from an external address and...
    if probe.internal[daddr] then
      -- ...went to an internal address (incoming)
      if silk.rwrec_is_web(rec) then
        return ft_name_id_map["inweb"], ft_name_id_map["outweb"], file_info
      else
        return ft_name_id_map["in_nonweb"], ft_name_id_map["out_nonweb"], file_info_sc
      end
    else
      -- ...went back to an external address (external to external)
      return ft_name_id_map["ext2ext"], ft_name_id_map["ext2ext"], file_info_sc
    end
  elseif probe.internal[saddr] then
    -- Flow came from an internal address and...
    if not probe.internal[daddr] then
      -- ...went to an external address (outgoing)
      if silk.rwrec_is_web(rec) then
        return ft_name_id_map["outweb"], ft_name_id_map["inweb"], file_info
      else
        return ft_name_id_map["out_nonweb"], ft_name_id_map["in_nonweb"], file_info_sc
      end
    else
      -- ...went to another internal address (internal to internal)
      return ft_name_id_map["int2int"], ft_name_id_map["int2int"], file_info_sc
    end
  end
  -- This should never occur.
  return ft_name_id_map["other"], ft_name_id_map["other"], file_info
end

--  If you want to do something for each schema you see, it is useful
--  to keep a cache of data for each schema.  That is the purpose of
--  this variable.
--local schemas = {}

--  Define the packing function that is used when the probe is
--  defined.
--
--  Given a probe definition where a record was collected, a SiLK
--  rwrec representation of the fowrard record, a SiLK rwrec
--  representation of the reverse record (or nil for a uni-flow), and
--  the source IPFIX record as a fixrec object, write the SiLK
--  record(s) to the appropriate output(s).  This function must call
--  write_rwrec() to store the record in the SiLK data repository.
--
--  The probe has a 'repo_key' member.  That table is passed to
--  write_rwrec() to determine where in the repository the record gets
--  written---that is, the flowtype, sensor, and start-hour triple.
--
function pack_function (probe, fwd_rec, rev_rec, fixrec)
  -- local s = silk.fixrec_get_schema(fixrec)
  -- if not schemas[s] then
  --   schemas[s] = true
  --   --io.stderr:write(string.format("%s :: ", s))
  --   for _,k in ipairs(s) do
  --     --io.stderr:write(string.format("%s, ", k.name))
  --   end
  --   --io.stderr:write("\n")
  -- end

  --  Determine the flowtypes and file format
  local flowtype, rflowtype, fformat = determine_flowtype(probe, fwd_rec)

  --  Set flowtype and sensor on the forward record.  The sensor we
  --  get from the 'repo_key' member of the 'probe'.
  fwd_rec.classtype_id = flowtype
  fwd_rec.sensor_id = probe.repo_key.sensor

  --  Update probe.repo_key with the flowtype and the starting hour of
  --  this record
  probe.repo_key.flowtype = fwd_rec.classtype_id
  probe.repo_key.timestamp = fwd_rec.stime

  --  Look for DNS deep packet inspection data in the IPFIX and set
  --  the appropriate fields on the forward record.  If this is a
  --  bi-flow, we probably should be setting the DNS query in the
  --  forward record and the DNS response on the reverse record, but
  --  we are not doing that yet.  Instead, we grab the first entry of
  --  DNS data and store that on both the forward and reverse records.
  local s = nil
  if 53 == fwd_rec.application then
    -- This is not a loop, just something I can "break" out of.
    repeat
      local stml = silk.fixrec_get_value(fixrec, "subTemplateMultiList", nil)
      if not stml then break end

      --  Go through the STML to the DNS DPI data.  According to the
      --  yafdpi man page, the template ID we want is 0xCE00.
      --  (Ideally we would be determining this by examining the
      --  templates, not by using the template IDs.)
      --
      local i = 1
      local p = stml[i]
      while (p and
             silk.schema_get_template_id(silk.fixrec_get_schema(p)) ~= 0xce00) do
        i = i + 1
        p = stml[i]
      end
      if not p then break end

      local stl = silk.fixrec_get_value(p, "subTemplateList", nil)
      if not stl then break end

      i = 1
      local q = stl[i]
      if q then
        s = {}
        s.dnsQName = silk.fixrec_get_value(q, "dnsQName", nil)
        s.dnsTTL = silk.fixrec_get_value(q, "dnsTTL", nil)
        s.dnsQRType = silk.fixrec_get_value(q, "dnsQRType", nil)
        s.dnsQueryResponse = silk.fixrec_get_value(q, "dnsQueryResponse", nil)
        s.dnsAuthoritative = silk.fixrec_get_value(q, "dnsAuthoritative", nil)
        s.dnsNXDomain = silk.fixrec_get_value(q, "dnsNXDomain", nil)
        s.dnsRRSection = silk.fixrec_get_value(q, "dnsRRSection", nil)
        s.dnsID = silk.fixrec_get_value(q, "dnsID", nil)
        fwd_rec.sidecar = s
      end
    until true
  end

  -- Write the forward record
  write_rwrec(fwd_rec, probe.repo_key, fformat)

  if rev_rec then
    -- Set flowtype and sensor on the reverse record.
    rev_rec.classtype_id = rflowtype
    rev_rec.sensor_id = probe.repo_key.sensor

    -- Update the probe_key with the flowtype and the starting hour
    probe.repo_key.flowtype = rev_rec.classtype_id
    probe.repo_key.timestamp = rev_rec.stime

    if s then
      rev_rec.sidecar = s
    end

    -- Write the reverse record
    write_rwrec(rev_rec, probe.repo_key, fformat)
  end
end


--  ------------------------------------------------------------------
--
--  Configuration
--

--  An IPset holding internal addresses
local set = silk.ipset_create_v4()
-- Primary address used by the 2005 sample data
silk.ipset_add(set, silk.ipwildcard("199.1.76.x"))
-- These reserved addresses also appear in the data
silk.ipset_add(set, silk.ipwildcard("169.254.0.0/16"))
silk.ipset_add(set, silk.ipwildcard("240.0.0.0/4"))

--  The rwflowpack configuration requires a table named 'input' that
--  specifies the source of flow records
input = {
  mode = "stream",
  probes = {
    P0 = {
      name = "P0",
      --  'type' is the type of the data.
      type = "ipfix",
      --  'packing_function' is called for each packet.  it is the
      --  function defined above
      packing_function = pack_function,
      --  Data that appears in 'vars' is made available to the packing
      --  function as part of the 'probe' parameter to that function.
      vars = {
        internal = set,
        repo_key = {
          sensor = silk.site.sensor_id("S0"),
          timestamp = 0,
          flowtype = 0,
        },
      },
      -- 'source' says where the data for this probe comes from
      source = {
        directory = topdir .. "/incoming",
        interval = 5,
        archive_directory = topdir .. "/archive",
        error_directory = topdir .. "/error",
        archive_subdirectory_policy = "flat",
      },
      --  Use something like this instead to listen on the network
      --source = {
      --  listen = "${host}:${port1}",
      --  accept = { "${host}" },
      --  protocol  = "udp",
      --}
    },
    -- You may define more probes here.
  },
}

--  The rwflowpack configuration requires a table named 'output' that
--  specifies the final location of flow records.
output = {
  mode = "local-storage",
  flush_interval = 10,
  processing = {
    directory = topdir .. "/processing",
    error_directory = topdir .. "/error",
  },
  root_directory = rootdir,
}

--  The rwflowpack configuration requires a table named 'log'
log = {
  directory = topdir .. "/log",
  level = "debug",
  -- destination = "",
  -- pathname = "",
  -- basename = "",
  -- post_rotate = "",
  -- sysfacility = "",
}

--  The rwflowpack configuration understands a table named 'daemon'
daemon = {
  pid_file = topdir .. "/run/rwflowpack.pid",
  -- fork = true,
  -- chdir = true,
}
