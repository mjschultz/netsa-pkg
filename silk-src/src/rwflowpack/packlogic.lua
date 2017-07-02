------------------------------------------------------------------------
-- Copyright (C) 2014-2017 by Carnegie Mellon University.
--
-- @OPENSOURCE_LICENSE_START@
-- See license information in ../../LICENSE.txt
-- @OPENSOURCE_LICENSE_END@
--
------------------------------------------------------------------------

------------------------------------------------------------------------
-- $SiLK: packlogic.lua efd886457770 2017-06-21 18:43:23Z mthomas $
------------------------------------------------------------------------

local export = {}

-- A replacement for the function for reading ipsets from files is
-- supplied to this script as the first argument.  This next line
-- extracts this argument.  (Arguments to scripts are in the vararg
-- '...' parameter.
local ipset_read = select(1, ...)
assert(ipset_read)

-- Override standard ipset loading to be our own function which will
-- share ipsets between lua states using the ipset_cache functions.
silk.ipset_load = ipset_read

-- Extract and prepare probe information for the probe named 'name' in
-- the config environment 'env'
function export.prepare_probe (env, name)
  local input = env.input
  local probes = input and input.probes
  if probes then
    -- find the probe which matches the name
    for k, probe in pairs(probes) do
      if probe.name == name then
        if probe.packing_function == nil then
          error("Probe '" .. probe.name .. "' does not contain a "
                  .. "'packing_function' member")
        end
        -- Ensure there is a probe variable table
        probe.vars = probe.vars or {}
        return probe
      end -- if probe.name == name
    end -- for probes
  end -- if probes
  error("Could not find Probe '" .. probe.name .. "' in the configuration")
end

return export

-- Local Variables:
-- mode:lua
-- indent-tabs-mode:nil
-- lua-indent-level:2
-- End:
