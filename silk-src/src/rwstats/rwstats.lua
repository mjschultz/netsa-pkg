------------------------------------------------------------------------
-- Copyright (C) 2016-2017 by Carnegie Mellon University.
--
-- @OPENSOURCE_LICENSE_START@
-- See license information in ../../LICENSE.txt
-- @OPENSOURCE_LICENSE_END@
--
------------------------------------------------------------------------

------------------------------------------------------------------------
-- $SiLK: rwstats.lua efd886457770 2017-06-21 18:43:23Z mthomas $
------------------------------------------------------------------------

--  All functions that are to be exported to the C code must we added
--  to this table.  This table is the return value of this file.
--
local export = {}

--   This function is called from the C code to load and execute a
--   file specified via --lua-file
--
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


--  Local helper function to check the type of an argument to a
--  function.
--
--  Check whether the type of 'fn_arg' is in the list given in
--  'fn_expected' and ... (a list of strings).  If the type is one of
--  those expected, return true.  Otherwise raise an error that
--  argument number 'fn_pos' to function 'fn_func_name' has a type
--  different that the expected type(s).
--
local function check_type (fn_arg, fn_pos, fn_func_name, fn_expected, ...)
  local fn_type_list = {fn_expected, ...}
  local t = type(fn_arg)
  for _,wanted in ipairs(fn_type_list) do
    if t == wanted then return true end
  end
  local expected = nil
  if #fn_type_list == 1 then
    expected = fn_type_list[1]
  elseif #fn_type_list == 2 then
    expected = string.format("%s or %s", fn_type_list[1], fn_type_list[2])
  else
    expected = table.concat(fn_type_list, ",")
  end
  error(string.format("bad argument #%d to '%s' (%s expected, got %s)",
                      fn_pos, fn_func_name, expected, t),
        0)
end


-- Implementation of register_field(), add_sidecar_field(), and the
-- functions that the C code must call to add the sidecar fields to
-- the records
--
do
  --  A table of field names to functions.  The function is called
  --  when the C code calls activate_field() once the user has
  --  selected the field via the --fields switch.
  --
  local potential_fields = nil

  --  A local sidecar object to hold the potential fields the user may
  --  select via the --fields switch
  --
  local sidecar = nil

  --  Valid names for types of sidecar fields
  --
  local sc_type_names = {
    uint8 = "uint8",
    uint16 = "uint16",
    uint32 = "uint32",
    uint64 = "uint64",
    double = "double",
    string = "string",
    binary = "binary",
    ip4 = "ip4",
    ip6 = "ip6",
    datetime = "datetime",
    boolean = "boolean",
    empty = "empty",
    -- list = "list",
    -- table = "table",
  }


  --  Register a potential sidecar field named 'sc_name' having type
  --  'sc_type'.  'sc_name' is added to the list of fields that the
  --  user may select via the --fields switch.  If 'sc_name' is
  --  selected, then 'sc_selected_fn' is invoked with 'sc_name' and
  --  'sc_type' as arguments.  That function should call
  --  add_sidecar_field() to register the function that will be called
  --  for each record to add the sidecar field(s) to the record.
  --  'sc_selected_fn' may be nil.
  --
  function export.register_field (sc_name, sc_type, sc_selected_fn)
    local fun_name = 'register_field'
    check_type(sc_name, 1, fun_name, 'string')
    check_type(sc_type, 2, fun_name, 'string')
    check_type(sc_selected_fn, 1, fun_name, 'function', 'nil')

    if sidecar and sidecar[sc_name] then
      error(string.format("bad argument #%d to '%s' (name '%s' already in use)",
                          1, fun_name, sc_name))
    end
    if nil == sc_type_names[sc_type] then
      error(string.format("bad argument #%d to '%s' (invalid type '%s')",
                          1, fun_name, sc_type))
    end

    if nil == sidecar then
      sidecar = silk.sidecar_create()
    end
    sidecar[sc_name] = sc_type

    if sc_selected_fn then
      if nil == potential_fields then
        potential_fields = {}
      end
      potential_fields[sc_name] = sc_selected_fn
    end
  end
  -- export.register_field


  --  This function is invoked by the C code to activate a field
  --  registered by register_field() once that field is selected by
  --  the user specifying it in --fields.  This function calls the
  --  function that was specified by 'sc_selected_fn' when the field
  --  was registered.
  --
  function export.activate_field (sc_name)
    local callback = potential_fields[sc_name]
    if callback then
      local sc_type = silk.sidecar_elem_get_type(sidecar[sc_name])
      callback(sc_name, sc_type)
    end
  end


  --  This function is invoked by the C code to freeze the sidecar
  --  object and push it onto the stack.  It pushes nil if no sidecar
  --  fields were requested.  If an error occurs freezing the sidecar
  --  object, the error string is returned.
  --
  function export.get_sidecar ()
    if nil == sidecar then
      return nil
    end
    local ok,msg = pcall(silk.sidecar_freeze, sidecar)
    if not ok then
      return msg
    end
    return sidecar
  end

end


--    Support for functions to call on each record
--
do
  --  The list of functions that are called for each record to add
  --  sidecar fields to the record.  The user adds to this list via
  --  add_sidecar_field().  The C code invokes this list of functions
  --  via apply_sidecar().
  --
  local sidecar_fns = nil

  --  Register the function 'sc_func' that is called on each record.
  --  The function may modify the record to add entries to the Lua
  --  table that contains the record's sidecar fields.
  --
  --  Typically this function (add_sidecar_field()) is called by the
  --  'sc_selected_fn' callback of register_field() once it is
  --  determined that the user wants to include a field in the output
  --  of rwcut.
  --
  --  The function callbacks that are added by this function are
  --  called in the order in which they are added.
  --
  function export.add_sidecar_field (sc_func)
    check_type(sc_func, 1, 'add_sidecar_field', 'function')
    if nil == sidecar_fns then
      sidecar_fns = {}
    end
    sidecar_fns[1 + #sidecar_fns] = sc_func
  end
  -- export.add_sidecar_field

  --  Return the number of sidecar functions registered
  --
  function export.count_functions ()
    if sidecar_fns then
      return #sidecar_fns
    end
    return 0
  end

  --  This function is invoked by the C code to apply the functions
  --  that were registered with add_sidecar_field to the rwrec 'r'.
  --  The function returns the record.
  --
  function export.apply_sidecar (r)
    if nil == r.sidecar then
      r.sidecar = {}
    end
    for i = 1,#sidecar_fns do
      sidecar_fns[i](r)
    end
    return r
  end

end


--    Support for functions to call as rwcut exits
--
do
  --  The list of teardown functions that are called during clean-up.
  --  The user adds to this list via register_teardown().  The C code
  --  invokes this list of functions via invoke_teardown().
  --
  local teardown_fns = nil

  --  Register the function 'td_func' to be called during shutdown.
  --  The teardown functions are called in reverse order in which they
  --  are registered.
  --
  function export.register_teardown (td_func)
    check_type(td_func, 1, 'register_teardown', 'function')
    if nil == teardown_fns then
      teardown_fns = {}
    end
    table.insert(teardown_fns, 1, td_func)
  end

  --  This function is invoked by the C code to invoke the teardown
  --  functions that were registered by register_teardown().
  --
  function export.invoke_teardown ()
    if teardown_fns then
      local results = {}
      for i = 1,#teardown_fns do
        local ok,msg = pcall(teardown_fns[i])
        results[1 + #results] = { ok, msg }
      end
      -- return the results table if any result is not okay
      for i = 1,#results do
        if not results[i][1] then
          return results
        end
      end
    end
    -- either no teardown functions or all results were okay
    return nil
  end

end


return export


-- Local Variables:
-- mode:lua
-- indent-tabs-mode:nil
-- lua-indent-level:2
-- End:
