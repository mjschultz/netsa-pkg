------------------------------------------------------------------------
-- Copyright (C) 2014-2017 by Carnegie Mellon University.
--
-- @OPENSOURCE_LICENSE_START@
-- See license information in ../../../LICENSE.txt
-- @OPENSOURCE_LICENSE_END@
--
------------------------------------------------------------------------

------------------------------------------------------------------------
-- $SiLK: silkutils.lua efd886457770 2017-06-21 18:43:23Z mthomas $
------------------------------------------------------------------------

-- Let's make sure we aren't setting global variables when we don't
-- need to.
-- dont_allow_setting_globals = true
if dont_allow_setting_globals then
  local function disallow_newindex (t, k, v)
    if debug.getinfo(2).what == 'C' then
      rawset(t, k, v)
    else
      error(string.format([[
You are setting the global variable '%s'.
You probably meant to use a local variable.
Use rawset(_ENV, "%s", value) to really set a global.

%s
]], k, k, debug.traceback()))
    end
  end
  setmetatable(_ENV, {__newindex = disallow_newindex})
end

-- A table of internal functions is passed as the sole argument to
-- this file
local internal = ...
local get_pointer_string = internal.get_pointer_string

-- The exported library
local silkutils = {}

-- Take an objects table created by sk_lua_add_to_object_table and
-- realize it within the given module table 'mod'.  (If 'mod' is nil,
-- this will create and return a new module table.)  Modules created
-- by this function have the following attributes:
--
-- * Contains a type() function that can return the name of any of the
--   new objects.
-- * Contains a entry named "metatables" which is a table of (name,
--   metatable) pairs, mapping object names to their metatables.
-- * Contains an is_foo() function for each object foo.
--
-- This version of realize_object_table eschews the colon-based method
-- invocation style and instead makes the methods into functions that
-- are prefixed by the object name and take the object as its first
-- argument.  Static methods are also similarly prefixed.
function silkutils.realize_object_table (objects, mod)
  -- Modify given module, or create a new one
  mod = mod or {}

  -- Create a 'type' function that understands the new objects.  Use
  -- any existing type function in the module as a base.
  local typefunc = mod.type or type
  local type_table = {}
  local function mod_type (x)
    return rawget(type_table, getmetatable(x)) or typefunc(x)
  end
  mod.type = mod_type

  -- Create a 'to_string' function that understands the new objects.
  -- Use any existing to_string in the module as a base.
  local tostringfunc = mod.tostring or tostring
  local tostring_table = {}
  function mod.to_string (x)
    return (rawget(tostring_table, getmetatable(x)) or tostringfunc)(x)
  end

  -- Ensure or create a table of metatables
  mod.metatables = mod.metatables or {}

  -- For each object...
  for name, data in pairs(objects) do
    local meta = data.metatable
    local methods = data.methods or {}

    -- update type_table
    type_table[meta] = name

    -- Update metatable table
    mod.metatables[name] = meta

    -- Add __tostring metamethod, if none exists
    if meta.__tostring == nil then
      function meta.__tostring (obj)
        return name .. ": " .. get_pointer_string(obj)
      end
    end

    -- Add to_string method, if none exist
    if methods.to_string == nil then
      function methods.to_string (obj)
        if mod_type(obj) == name then
          return tostring(obj)
        end
        error("Not a " .. name .. " value")
      end
    end

    -- Update tostring table
    tostring_table[meta] = methods.to_string

    -- Add methods.  In this case, make the methods into functions
    -- with the object name as prefix.
    for k, v in pairs(methods) do
      mod[name .. "_" .. k] = v
    end

    -- Add static methods.  In this case, make the methods into
    -- functions with the object name as prefix.
    for k, v in pairs(data.static_methods) do
      mod[name .. "_" .. k] = v
    end

    -- Add constructor
    mod[name] = data.constructor

    -- Add is_type function
    mod["is_" .. name] = function (obj)
      return rawequal(getmetatable(obj), meta)
    end

  end -- for name, data in pairs(objects)

  return mod
end


--  silkutils.make_table_read_only(t)
--
--    Return a new table that is a read-only copy of the table t.
--
--    Recursively visits all subtables of t.
do
  local typefunc = type

  local function make_read_only (tbl)
    -- recurse over the elements of the table
    for k,v in pairs(tbl) do
      if typefunc(v) == "table" then
        k = make_read_only(v)
      end
    end

    -- create the meta table
    meta = {}
    meta.__index = tbl
    meta.__len = function (_) return #tbl end
    meta.__pairs = function (_) return pairs(tbl) end
    meta.__tostring = function (_) return tostring(tbl) end

    meta.__newindex = function (_)
      error("attempt to modify a readonly table")
    end

    -- return a new table in place of tbl
    return setmetatable({}, meta)
  end

  silkutils.make_table_read_only = make_read_only
end


--  Modify the settings of the garbage collector
--
--    There are GC settings SiLK wants to use, but these may be
--    changed by setting the SILK_LUA_GC_PAUSE and SILK_LUA_GC_STEPMUL
--    environment variables to non-negative numbers.  When the envvar
--    is 0, the SiLK settings are ignored and the system default is
--    used.
--
--    When SILK_LUA_GC_VALUES exists in the environment, Lua writes
--    the GC values to the standard error.
--
do
  -- The GC values to use unless changed by the environment; a value
  -- of 0 means to use the system default
  local gc_param = {
    stepmul = 0,
    pause = 100,
  }

  -- Holds strings describing the values
  local gc_str = {}

  -- Whether to print the settings to stderr
  local print_values = os.getenv("SILK_LUA_GC_VALUES")

  for k,v in pairs(gc_param) do
    -- environment variable to check
    local env_name = "SILK_LUA_GC_" .. string.upper(k)
    -- check the environment; update v if a non-negative number
    local env_val = tonumber(os.getenv(env_name))
    if nil ~= env_val and env_val >= 0 then
      v = env_val
    end

    -- first argument to collectgarbage()
    local parm = "set" .. k

    if v ~= 0 then
      -- change the value
      local val = collectgarbage(parm, v)
      if print_values then
        gc_str[k] = string.format("%s = %d (system default %d)", k, v, val)
      end
    elseif print_values then
      -- change the value so we can get its current setting, then
      -- restore it
      local dummy = 100
      local val = collectgarbage(parm, dummy)
      collectgarbage(parm, val)
      gc_str[k] = string.format("%s = %d", k, val)
    end
  end

  if print_values then
    io.stderr:write(gc_str["stepmul"] .. ", " .. gc_str["pause"] .. "\n")
  end
end


local function install_silkutils (mod)
  mod = mod or {}
  for k, v in pairs(silkutils) do
    mod[k] = v
  end
  return mod
end

return install_silkutils


-- Local Variables:
-- mode:lua
-- indent-tabs-mode:nil
-- lua-indent-level:2
-- End:
