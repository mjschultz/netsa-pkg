-----------------------------------------------------------------------
-- Copyright (C) 2014-2017 by Carnegie Mellon University.
--
-- @OPENSOURCE_LICENSE_START@
-- See license information in ../../../LICENSE.txt
-- @OPENSOURCE_LICENSE_END@
--
-----------------------------------------------------------------------

-----------------------------------------------------------------------
-- $SiLK: silk-site.lua efd886457770 2017-06-21 18:43:23Z mthomas $
-----------------------------------------------------------------------

-- Retrieve the internal function table and the export table
local internal, export = ...

local site_configured = internal.site_configured

-- Local stashes of global functions
local pairs  = pairs
local ipairs = ipairs

-- Local stashes of site data
local sensor_map = {}
local sensors = {}
local class_map = {}
local classes = {}
local flowtype_map = {}
local classtype_map = {}
local classtypes = {}
local flowtypes = {}
local default_class = nil
local si = {}
local ci = {}
local fi = {}

-- Create an immutable table (using proxies)
local function immutable (t)
  local proxy = {}
  local mt = {}
  local len = #t
  function mt.__newindex () error("immutable") end
  function mt.__len () return len end
  function mt.__pairs () return pairs(t) end
  mt.__index = t
  setmetatable(proxy, mt)
  return proxy
end

-- Simple shallow copy of table data
local function copy(from, to)
  for k, v in pairs(from) do
    to[k] = v
  end
end

-- Like pairs(), but returns entries sorted in key order
local function sorted_pairs (t)
  local sorted_keys = {}
  for k, v in pairs(t) do
    sorted_keys[#sorted_keys + 1] = k
  end
  table.sort(sorted_keys)
  local i = 0
  local len = #sorted_keys
  return function ()
    i = i + 1
    if i > len then
      return nil
    end
    local k = sorted_keys[i]
    return k, t[k]
  end
end

--Currently unused.  Using an __index metamethod instead.
--|-- Raise an error about bad argument 'n' in function 'caller' when
--|-- 'str' is not a string.  If 'n' is nil, 1 is assumed.
--|local function expect_string (str, caller, n)
--|  if type(str) ~= 'string' then
--|    if n == nil then
--|      n = 1
--|    end
--|    error(string.format(
--|            "bad argument #%d to 'silk.site.%s' (string expected, got %s)",
--|            n, caller, type(str)))
--|  end
--|end

local maps_initialized = false

-- Ensure that all map data is initialized
local function init_maps()

  if maps_initialized
    or not (site_configured() or internal.init_site())
  then
    -- Maps are initialized (or cannot be).  Do nothing
    return
  end

  -- Use copy instead of assignment to make export.debug will work
  copy(internal.get_sensor_info(), si)
  copy(internal.get_class_info(), ci)
  copy(internal.get_flowtype_info(), fi)

  local default_classid = ci.default
  local cdata = ci.data
  default_class = cdata[default_classid].name

  -- Loop through sensor data
  for _, item in sorted_pairs(si) do
    for i, v in ipairs(item.classes) do
      item.classes[i] = cdata[v].name
    end
    -- build map from names to sensor data
    sensor_map[item.name] = item
    -- build list of sensor names
    sensors[#sensors + 1] = item.name
  end

  -- Loop through class data
  for _, item in sorted_pairs(cdata) do
    for i, v in ipairs(item.sensors) do
      -- Change sensor ids to names
      item.sensors[i] = si[v].name
    end
    for i, v in ipairs(item.flowtypes) do
      -- Change flowtype ids to names
      item.flowtypes[i] = fi[v].name
    end
    for i, v in ipairs(item.default_flowtypes) do
      -- Change default_flowtype ids to names
      item.default_flowtypes[i] = fi[v].name
    end
    -- build map from names to classes
    class_map[item.name] = item
    -- build list of classes
    classes[#classes + 1] = item.name
  end

  -- loop through flowtype data
  for _, item in sorted_pairs(fi) do
    local class = cdata[item.class]
    -- Change the class id to a name
    item.class = class.name
    -- build map from names to flowtypes
    flowtype_map[item.name] = item

    -- build map from class names to maps from type names to flowtypes
    local cdict = classtype_map[item.class] or {}
    classtype_map[item.class] = cdict
    cdict[item.type] = item
    classtypes[#classtypes + 1] = immutable{class.name, item.type}

    -- build list of flowtypes
    flowtypes[#flowtypes + 1] = item.name
  end

  -- return reasonable error messages when a look-up fails

  -- sets __index entry of the metatable of 'table' to a function that
  -- raises an error.  'lookup_type' is the a string describing the
  -- thing being searched for, such as "sensor name"
  local function update_metatable (table, lookup_type)
    setmetatable(table,
                 {
                   __index = function (t,k)
                     error(string.format("invalid %s '%s'", lookup_type, k),2)
                   end
                 }
    )
  end

  update_metatable(sensor_map, 'sensor name')
  update_metatable(class_map, 'class name')
  update_metatable(flowtype_map, 'flowtype name')
  update_metatable(si, 'sensor id')
  update_metatable(fi, 'flowtype id')

  update_metatable(classtype_map, 'class name')
  for cls, v in pairs(classtype_map) do
    update_metatable(v, string.format("type in class %s", cls))
  end

  -- Initialization is done
  maps_initialized = true
end -- function init_maps ()

-- =pod
--
-- =item silk.site.B<default_class(>I<>B<)>
--
-- Return the default class name.  Implicitly calls
-- L<silk.site.B<init_site()>|/"silk.site.B<init_site(>[I<siteconf>B<,> I<rootdir>[, I<verbose>]]B<)>">
-- with no arguments if
-- L<silk.site.B<have_site_config()>|/"silk.site.B<have_site_config(>I<>B<)>">
-- returns
-- B<false>.  Return B<nil> if no site file is available.
--
-- =cut
function export.default_class ()
  init_maps()
  return default_class
end

-- =pod
--
-- =item silk.site.B<sensors(>I<>B<)>
--
-- Return a sequence containing valid sensor names.  Implicitly calls
-- L<silk.site.B<init_site()>|/"silk.site.B<init_site(>[I<siteconf>B<,> I<rootdir>[, I<verbose>]]B<)>">
-- with no arguments if
-- L<silk.site.B<have_site_config()>|/"silk.site.B<have_site_config(>I<>B<)>">
-- returns
-- B<false>.  Return an empty sequence if no site file is available.
--
-- =cut
function export.sensors ()
  init_maps()
  return immutable(sensors)
end

-- =pod
--
-- =item silk.site.B<class_sensors(>I<class>B<)>
--
-- Return a sequence containing sensors that are in class I<class> (a
-- string).  Implicitly
-- calls
-- L<silk.site.B<init_site()>|/"silk.site.B<init_site(>[I<siteconf>B<,> I<rootdir>[, I<verbose>]]B<)>">
-- with no arguments if
-- L<silk.site.B<have_site_config()>|/"silk.site.B<have_site_config(>I<>B<)>">
-- returns
-- B<false>.  Raise an error if no site file is available or if
-- I<class> is not a valid class name.
--
-- =cut
function export.class_sensors (class)
  init_maps()
  --expect_string(class, 'class_sensors')
  return immutable(class_map[class].sensors)
end

-- =pod
--
-- =item silk.site.B<classes(>I<>B<)>
--
-- Return a sequence containing valid class names.  Implicitly calls
-- L<silk.site.B<init_site()>|/"silk.site.B<init_site(>[I<siteconf>B<,> I<rootdir>[, I<verbose>]]B<)>">
-- with no arguments if
-- L<silk.site.B<have_site_config()>|/"silk.site.B<have_site_config(>I<>B<)>">
-- returns
-- B<false>.  Return an empty sequence if no site file is available.
--
-- =cut
function export.classes ()
  init_maps()
  return immutable(classes)
end

-- =pod
--
-- =item silk.site.B<sensor_classes(>I<sensor>B<)>
--
-- Return a sequence containing class names that are associated with
-- sensor I<sensor> (a string).
-- Implicitly calls
-- L<silk.site.B<init_site()>|/"silk.site.B<init_site(>[I<siteconf>B<,> I<rootdir>[, I<verbose>]]B<)>">
-- with no
-- arguments if
-- L<silk.site.B<have_site_config()>|/"silk.site.B<have_site_config(>I<>B<)>">
-- returns B<false>.  Raise an error if no site file is available or
-- if I<sensor> is not a valid sensor name.
--
-- =cut
function export.sensor_classes (sensor)
  init_maps()
  --expect_string(sensor, 'sensor_classes')
  return immutable(sensor_map[sensor].classes)
end

-- =pod
--
-- =item silk.site.B<sensor_description(>I<sensor>B<)>
--
-- Return the description of sensor I<sensor> (a string) as a string,
-- or B<nil> if there is no description.  Implicitly calls
-- L<silk.site.B<init_site()>|/"silk.site.B<init_site(>[I<siteconf>B<,> I<rootdir>[, I<verbose>]]B<)>">
-- with no arguments if
-- L<silk.site.B<have_site_config()>|/"silk.site.B<have_site_config(>I<>B<)>">
-- returns
-- B<false>.  Raise an error if no site file is available or if
-- I<sensor> is not a valid sensor name.
--
-- =cut
function export.sensor_description (sensor)
  init_maps()
  --expect_string(sensor, 'sensor_description')
  return sensor_map[sensor].description
end

-- =pod
--
-- =item silk.site.B<classtypes(>I<>B<)>
--
-- Return a sequence containing sequences, where each inner sequence
-- contains
-- two elements representing a valid class name and type name.
-- Implicitly calls
-- L<silk.site.B<init_site()>|/"silk.site.B<init_site(>[I<siteconf>B<,> I<rootdir>[, I<verbose>]]B<)>">
-- with no
-- arguments if
-- L<silk.site.B<have_site_config()>|/"silk.site.B<have_site_config(>I<>B<)>">
-- returns B<false>.  Return an empty sequence if no site file is
-- available.
--
-- =cut
function export.classtypes ()
  init_maps()
  return immutable(classtypes)
end

-- =pod
--
-- =item silk.site.B<flowtypes(>I<>B<)>
--
-- Return a sequence containing valid flowtype names.  Implicitly calls
-- L<silk.site.B<init_site()>|/"silk.site.B<init_site(>[I<siteconf>B<,> I<rootdir>[, I<verbose>]]B<)>">
-- with no arguments if
-- L<silk.site.B<have_site_config()>|/"silk.site.B<have_site_config(>I<>B<)>">
-- returns
-- B<false>.  Return an empty sequence if no site file is available.
--
-- =cut
function export.flowtypes ()
  init_maps()
  return immutable(flowtypes)
end

-- =pod
--
-- =item silk.site.B<types(>I<class>B<)>
--
-- Return a sequence containing valid type names for class I<class> (a
-- string).  Implicitly
-- calls
-- L<silk.site.B<init_site()>|/"silk.site.B<init_site(>[I<siteconf>B<,> I<rootdir>[, I<verbose>]]B<)>">
-- with no arguments if
-- L<silk.site.B<have_site_config()>|/"silk.site.B<have_site_config(>I<>B<)>">
-- returns
-- B<false>.  Raise an error if no site file is available or if
-- I<class> is not a valid class name.
--
-- =cut
function export.types (class)
  init_maps()
  --expect_string(class, 'types')
  local list = {}
  for i, v in ipairs(class_map[class].flowtypes) do
    list[i] = flowtype_map[v].type
  end
  return immutable(list)
end

-- =pod
--
-- =item silk.site.B<default_types(>I<class>B<)>
--
-- Return a sequence containing default type names associated with class
-- I<class> (a string).
-- Implicitly calls
-- L<silk.site.B<init_site()>|/"silk.site.B<init_site(>[I<siteconf>B<,> I<rootdir>[, I<verbose>]]B<)>">
-- with no
-- arguments if
-- L<silk.site.B<have_site_config()>|/"silk.site.B<have_site_config(>I<>B<)>">
-- returns B<false>.  Raise an error if no site file is available or
-- if I<class> is not a valid class name.
--
-- =cut
function export.default_types (class)
  init_maps()
  --expect_string(class, 'default_types')
  local list = {}
  for i, v in ipairs(class_map[class].default_flowtypes) do
    list[i] = flowtype_map[v].type
  end
  return immutable(list)
end

-- =pod
--
-- =item silk.site.B<sensor_id(>I<sensor>B<)>
--
-- Return the numeric sensor ID associated with the sensor I<sensor>
-- (a string).
-- Implicitly calls
-- L<silk.site.B<init_site()>|/"silk.site.B<init_site(>[I<siteconf>B<,> I<rootdir>[, I<verbose>]]B<)>">
-- with no
-- arguments if
-- L<silk.site.B<have_site_config()>|/"silk.site.B<have_site_config(>I<>B<)>">
-- returns B<false>.  Raise an error if no site file is available or
-- if I<sensor> is not a valid sensor name.
--
-- =cut
function export.sensor_id (sensor)
  init_maps()
  --expect_string(sensor, 'sensor_id')
  return sensor_map[sensor].id
end

-- =pod
--
-- =item silk.site.B<sensor_from_id(>I<id>B<)>
--
-- Return the sensor name associated with the numeric sensor ID I<id>.
-- Implicitly calls
-- L<silk.site.B<init_site()>|/"silk.site.B<init_site(>[I<siteconf>B<,> I<rootdir>[, I<verbose>]]B<)>">
-- no
-- arguments if
-- L<silk.site.B<have_site_config()>|/"silk.site.B<have_site_config(>I<>B<)>">
-- returns B<false>.  Raise an error if no site file is available or
-- if I<id> is not a valid sensor identifier.
--
-- =cut
function export.sensor_from_id (id)
  init_maps()
  return si[id].name
end

-- =pod
--
-- =item silk.site.B<classtype_id(>I<class>B<,> I<type>B<)>
--
-- =item silk.site.B<classtype_id(>{I<class>B<,> I<type>}B<)>
--
-- Return the numeric flowtype ID associated with I<class> and I<type>
-- (both strings).
-- Implicitly calls
-- L<silk.site.B<init_site()>|/"silk.site.B<init_site(>[I<siteconf>B<,> I<rootdir>[, I<verbose>]]B<)>">
-- with no
-- arguments if
-- L<silk.site.B<have_site_config()>|/"silk.site.B<have_site_config(>I<>B<)>">
-- returns B<false>. Raise an error if no site file is available, if
-- I<class> is not a valid class name, or if I<type> is does not name
-- a valid type in I<class>.
--
-- =cut
function export.classtype_id (class, typ)
  init_maps()
  if typ == nil and type(class) == 'table' and #class == 2 then
    class, typ = class[1], class[2]
  end
  --expect_string(class, 'classtype_id', 1)
  --expect_string(typ, 'classtype_id', 2)
  return  classtype_map[class][typ].id
end

-- =pod
--
-- =item silk.site.B<flowtype_id(>I<flowtype>B<)>
--
-- Return the numeric flowtype ID associated with the flowtype
-- I<flowtype> (a string).  Implicitly
-- calls
-- L<silk.site.B<init_site()>|/"silk.site.B<init_site(>[I<siteconf>B<,> I<rootdir>[, I<verbose>]]B<)>">
-- with no arguments if
-- L<silk.site.B<have_site_config()>|/"silk.site.B<have_site_config(>I<>B<)>">
-- returns
-- B<false>. Raise an error if no site file is available or if
-- I<flowtype> is not a valid flowtype name.
--
-- =cut
function export.flowtype_id (flowtype)
  init_maps()
  --expect_string(flowtype, 'flowtype_id')
  return flowtype_map[flowtype].id
end

-- =pod
--
-- =item silk.site.B<classtype_from_id(>I<id>B<)>
--
-- Return a sequence containing two elements: the I<class> and
-- I<type> name pair associated with the numeric flowtype ID I<id>.
-- Implicitly
-- calls
-- L<silk.site.B<init_site()>|/"silk.site.B<init_site(>[I<siteconf>B<,> I<rootdir>[, I<verbose>]]B<)>">
-- with no arguments if
-- L<silk.site.B<have_site_config()>|/"silk.site.B<have_site_config(>I<>B<)>">
-- returns
-- B<false>.  Raise an error if no site file is available or if I<id>
-- is not a valid identifier.
--
-- =cut
function export.classtype_from_id (id)
  init_maps()
  local f = fi[id]
  local c = f.class
  local t = f.type
  return immutable{c, t}
end

-- =pod
--
-- =item silk.site.B<flowtype_from_id(>I<id>B<)>
--
-- Return the flowtype name associated with the numeric ID I<id>.
-- Implicitly calls
-- L<silk.site.B<init_site()>|/"silk.site.B<init_site(>[I<siteconf>B<,> I<rootdir>[, I<verbose>]]B<)>">
-- with no
-- arguments if
-- L<silk.site.B<have_site_config()>|/"silk.site.B<have_site_config(>I<>B<)>">
-- returns B<false>.  Raise an error if no site file is available or
-- if I<id> is not a valid identifier.
--
-- =cut
function export.flowtype_from_id (id)
  init_maps()
  return fi[id].name
end


-- =pod
--
-- =item silk.site.B<classtype_from_flowtype(>I<flowtype>B<)>
--
-- Return a sequence containing two elements: the I<class> and
-- I<type> name pair associated with the flowtype I<flowtype> (a
-- string).
-- Implicitly calls
-- L<silk.site.B<init_site()>|/"silk.site.B<init_site(>[I<siteconf>B<,> I<rootdir>[, I<verbose>]]B<)>">
-- with no
-- arguments if
-- L<silk.site.B<have_site_config()>|/"silk.site.B<have_site_config(>I<>B<)>">
-- returns B<false>.  Raise an error if no site file is available or
-- if I<flowtype> is not a valid flowtype name.
--
-- =cut
function export.classtype_from_flowtype (flowtype)
  return export.classtype_from_id(export.flowtype_id(flowtype))
end

-- =pod
--
-- =item silk.site.B<flowtype_from_classtype(>I<class>, I<type>B<)>
--
-- =item silk.site.B<flowtype_from_classtype(>{I<class>, I<type>}B<)>
--
-- Return the flowtype name associated with I<class> and I<type> (both
-- strings).
-- Implicitly calls
-- L<silk.site.B<init_site()>|/"silk.site.B<init_site(>[I<siteconf>B<,> I<rootdir>[, I<verbose>]]B<)>">
-- with no
-- arguments if
-- L<silk.site.B<have_site_config()>|/"silk.site.B<have_site_config(>I<>B<)>">
-- returns B<false>.  Raise an error if no site file is available, if
-- I<class> is not a valid class name, or if I<type> does not name a
-- valid type is I<class>.
--
-- =cut
function export.flowtype_from_classtype (class, typ)
  return export.flowtype_from_id(export.classtype_id(class, typ))
end

-- =pod
--
-- =item silk.site.B<init_site(>[I<siteconf>B<,> I<rootdir>[, I<verbose>]]B<)>
--
-- Initialize the SiLK system's site configuration.  The I<siteconf>
-- parameter, if given and non-nil, should be the path and name of a
-- SiLK site configuration file (see B<silk.conf(3)>).  If I<siteconf>
-- is omitted or nil, the value specified in the environment variable
-- SILK_CONFIG_FILE will be used as the name of the configuration
-- file.  If SILK_CONFIG_FILE is not set, the module looks for a file
-- named F<silk.conf> in the following directories: the directory
-- specified by the I<rootdir> argument, the directory specified in
-- the SILK_DATA_ROOTDIR environment variable; the data root directory
-- that is compiled into SiLK (@SILK_DATA_ROOTDIR@); the directories
-- F<$SILK_PATH/share/silk/> and F<$SILK_PATH/share/>.
--
-- The I<rootdir> parameter, if given and non-nil, should be the path
-- to a SiLK data repository that a configuration that matches the
-- SiLK site configuration.  If I<rootdir> is omitted or nil, the
-- value specified in the SILK_DATA_ROOTDIR environment variable will
-- be used, or if that variable is not set, the data root directory
-- that is compiled into SiLK (@SILK_DATA_ROOTDIR@).  The I<rootdir>
-- may be specified without a I<siteconf> argument by using nil for
-- the I<siteconf> argument.  I.e., B<init_site(nil, "/data")>.
--
-- If I<verbose> is true, this function will report failures to open
-- the file as errors.  If verbose is nil, false, or not set, only
-- parsing failures will be reported.  (For example, if you intend to
-- try opening several files in a row to find the correct file,
-- verbose should be nil to avoid several reports of open failures.)
--
-- This function should not generally be called explicitly unless one
-- wishes to use a non-default site configuration file.
--
-- The B<init_site()> function can only be called successfully once.
-- The return value of B<init_site()> will be true if the site
-- configuration was successful, or B<false> if a site configuration
-- file was not found.  If a I<siteconf> parameter was specified but
-- not found, or if a site configuration file was found but did not
-- parse properly, an exception will be raised instead.  Once
-- I<init_site()> has been successfully invoked,
-- silk.site.B<have_site_config()> will return B<true>, and subsequent
-- invocations of B<init_site()> will raise an error.
--
-- =cut
function export.init_site (site_path, rootdir_path, verbose)
  if not site_configured() then
    return internal.init_site(site_path, rootdir_path, verbose)
  end
  error("Site already configured")
end

-- =pod
--
-- =item silk.site.B<have_site_config(>I<>B<)>
--
-- Return B<true> if
-- L<silk.site.B<init_site()>|/"silk.site.B<init_site(>[I<siteconf>B<,> I<rootdir>[, I<verbose>]]B<)>">
-- has been called and was able to successfully find and load a SiLK
-- configuration file.  Return B<false> otherwise.
--
-- =cut
export.have_site_config = site_configured


-- Export local values in export.debug for debugging
--[[
local _x = {}
local _name, _value
local _i = 1
repeat
  _name, _value = debug.getlocal(1, _i)
  if _name then
    _x[_name] = _value
  end
  _i = _i + 1
until _name == nil
export.debug = _x
--]]

return export

-- Local Variables:
-- mode:lua
-- indent-tabs-mode:nil
-- lua-indent-level:2
-- End:
