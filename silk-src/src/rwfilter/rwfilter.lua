------------------------------------------------------------------------
-- Copyright (C) 2014-2017 by Carnegie Mellon University.
--
-- @OPENSOURCE_LICENSE_START@
-- See license information in ../../LICENSE.txt
-- @OPENSOURCE_LICENSE_END@
--
------------------------------------------------------------------------

------------------------------------------------------------------------
-- $SiLK: rwfilter.lua efd886457770 2017-06-21 18:43:23Z mthomas $
------------------------------------------------------------------------

-- The table to return
local export = {}

-- Load and execute a file specified via --lua-file
function export.load_lua_file (file)
  local code, err = loadfile(file, "bt")
  if code == nil then
    error(err)
  end
  local ok, msg = pcall(code)
  if not ok then
    error(string.format("Error executing file '%s':\n%s", file, msg))
  end
end

-- Implementation of register_filter()
do
  local reg_filter_initialize = {}
  local reg_filter_filter = {}
  local reg_filter_finalize = {}

  function export.register_filter (filter_func, opt_table)
    local fun_name = 'register_filter'
    if type(filter_func) ~= 'function' then
      error(string.format("bad argument #%d to '%s' (%s expected, got %s)",
                          1, fun_name, 'function', type(filter_func)))
    end
    if type(opt_table) == 'nil' then
      opt_table = {}
    elseif type(opt_table) ~= 'table' then
      error(string.format("bad argument #%d to '%s' (%s expected, got %s)",
                          2, fun_name, 'table or nil', type(filter_func)))
    else
      for name,func in pairs(opt_table) do
        if name == 'initialize' or name == 'finalize' then
          if type(func) ~= 'function' then
            error(string.format("bad value for '%s' in argument #%d of '%s'"
                                  .." (%s expected, got %s)",
                                name, 2, fun_name, 'fun_name', type(func)))
          end
        else
          error(string.format("unregonized key '%s' in argument #%d of '%s'",
                              name, 2, fun_name))
        end
      end
    end

    reg_filter_filter[1 + #reg_filter_filter] = filter_func
    if opt_table.initialize then
      reg_filter_initialize[1 + #reg_filter_initialize] = opt_table.initialize
    end
    if opt_table.finalize then
      table.insert(reg_filter_finalize, 1, opt_table.finalize)
    end
  end

  function export.run_initialize ()
    for _,fn in ipairs(reg_filter_initialize) do
      local ok,msg = pcall(fn)
      if not ok then
        error(string.format("Error in initialization function\n%s", msg))
      end
    end
  end

  function export.run_finalize ()
    for _,fn in ipairs(reg_filter_finalize) do
      local ok,msg = pcall(fn)
      if not ok then
        error(string.format("Error in finalization function\n%s", msg))
      end
    end
  end

  function export.run_filter (rec)
    for _,fn in ipairs(reg_filter_filter) do
      local ok,result = pcall(fn, rec)
      if not ok then
        error(string.format("Error in filter function\n%s", result))
      end
      if not result then
        return false
      end
    end
    return true
  end

  function export.count_filters ()
    return #reg_filter_filter
  end
end


-- Parse a filter expression and register it as a function
function export.parse_lua_expression (filter_string)
  local func_text = "return function (rec) return " .. filter_string .. " end"
  local fn,err = load(func_text, "=lua-expression", "t")
  if fn == nil then
    error(err, 2)
  end
  export.register_filter(fn())
end


return export


-- Local Variables:
-- mode:lua
-- indent-tabs-mode:nil
-- lua-indent-level:2
-- End:
