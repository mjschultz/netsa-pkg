------------------------------------------------------------------------
-- Copyright (C) 2014-2017 by Carnegie Mellon University.
--
-- @OPENSOURCE_LICENSE_START@
-- See license information in ../../../LICENSE.txt
-- @OPENSOURCE_LICENSE_END@
--
------------------------------------------------------------------------

------------------------------------------------------------------------
-- $SiLK: silk.lua efd886457770 2017-06-21 18:43:23Z mthomas $
------------------------------------------------------------------------

local init = {}

-- Function that builds silk module
function init.make_silk_module (objects, functions, internal, export)

  -- Cache global functions to avoid looking up in _ENV
  local pairs = pairs
  local ipairs = ipairs
  local type = type
  local rawequal = rawequal
  local getuservalue = debug.getuservalue
  local setuservalue = debug.setuservalue

  -- Fix-up ipaddr
  do
    -- Change the create static methods to ordinary functions
    functions.ipv4addr = objects.ipaddr.static_methods.create_v4
    objects.ipaddr.static_methods.create_v4 = nil
    functions.ipv6addr = objects.ipaddr.static_methods.create_v6
    objects.ipaddr.static_methods.create_v6 = nil
  end

  -- Augment ipsets
  do
    local ipset = objects.ipset
    local statics = ipset.static_methods
    local meta = ipset.metatable
    local methods = ipset.methods
    local ipaddr = objects.ipaddr.constructor

    local function checkset(caller, set)
      if getmetatable(set) ~= meta then
        error(string.format(
                "bad argument #1 to '%s' (silk.ipset expected, got %s)",
                caller, export.type(set)), 2)
      end
    end

    -- Constructor makes a v4 set or loads a file by default.
    local ipset_v4 = statics.create_v4

    -- =pod
    --
    -- =item silk.B<ipset(>[I<element_array>]B<)>
    --
    -- Create an IPset.
    --
    -- Without any arguments, create an empty IPset.
    --
    -- With an I<element_array> argument, an empty IPset is
    -- created, and then each element in the I<element_array> is
    -- added to the IPset using the B<ipset_add> function.
    --
    -- =cut
    ipset.constructor = function (arg)
      local set = ipset_v4()
      if arg ~= nil then
        if type(arg) ~= "table" then
          error(string.format(
                  "bad argument #1 to 'ipset' (table expected, got %s)",
                  export.type(arg)))
        end
        for i, v in ipairs(arg) do
          set[v] = true
        end
      end
      return set
    end

    local copy = methods.copy

    -- Create subtraction (set difference) method (set - set)
    function meta.__sub (self, set)
      local val = copy(self)
      val[set] = false
      return val
    end

    -- Create addition (union) method (set + set)
    function meta.__add (self, set)
      local val = copy(self)
      val[set] = true
      return val
    end

    -- Create is_subset method (<=)
    -- =pod
    --
    -- =item silk.B<ipset_is_subset(>I<ipset1>, I<ipset2>B<)>
    --
    -- Return B<true> if every IP address in I<ipset1> is also in
    -- I<ipset2>.  Return B<false> otherwise.
    --
    -- =cut
    function meta.__le (self, set)
      return rawequal(self, set) or #(self - set) == 0
    end
    methods.is_subset = meta.__le

    -- Create is_superset method (>=)
    -- =pod
    --
    -- =item silk.B<ipset_is_superset(>I<ipset1>, I<ipset2>B<)>
    --
    -- Return B<true> if every IP address in I<ipset2> is also in
    -- I<ipset1>.  Return B<false> otherwise.
    --
    -- =cut
    function meta.__ge (self, set)
      return rawequal(self, set) or #(set - self) == 0
    end
    methods.is_superset = meta.__ge

    -- fold_ipset(caller, binary_fn)
    --
    -- Given a binary function whose first argument is an IPset,
    -- return a function that repeatedly applies the binary function
    -- to all arguments in a varargs, converting arguments to IPsets
    -- as necessary.
    --
    -- The returned function verifies that the first argument is an
    -- IPset.  The 'caller' argument is a string containing the name
    -- of the function, and it is used when reporting the invalid
    -- argument.
    --
    local fold_ipset
    do
      local ipwildcard = objects.ipwildcard.constructor
      local ipset = ipset_v4
      local ipaddr_meta = objects.ipaddr.metatable

      -- Methods to convert from a given type to an ipset
      local convert = {
        ['string'] = function (x)
          local val = ipset()
          val[ipwildcard(x)] = true
          return val
        end,
        ['ipwildcard'] = function (x)
          local val = ipset()
          val[x] = true
          return val
        end,
        ['table'] = function (x)
          local val = ipset()
          for _, v in ipairs(x) do
            if getmetatable(v) ~= ipaddr_meta then
              v = ipwildcard(v)
            end
            val[v] = true
          end
          return val
        end,
        ['ipset'] = function (x) return x end
      }

      -- A calling convert(x) will convert x to an ipset
      local convert_mt = {
        __call = function (self, x)
          local fn = self[export.type(x)]
          if fn then
            return fn(x)
          end
          error (string.format("Cannot convert a %s to an ipset",
                               export.type(x)))
        end
      }
      setmetatable(convert, convert_mt)

      -- The fold_ipset function (documented above)
      fold_ipset = function (caller, binary_fn)
        return function (acc, x, ...)
          checkset(caller, acc)
          acc = binary_fn(acc, convert(x))
          local set = {...}
          for i = 1, #set do
            acc = binary_fn(acc, convert(set[i]))
          end
          return acc
        end
      end
    end

    -- =pod
    --
    -- =item silk.B<ipset_update(>I<ipset>, I<other>[, ...]B<)>
    --
    -- Add to I<ipset> any IP addresses found in I<other>s.  I<other>s
    -- may contain ipsets, L<ipwildcards|/IP Wildcard>, ipwildcard
    -- strings, or arrays of L<ipaddrs|/IP Address> and ipwildcard
    -- strings.  Return I<ipset>.
    --
    -- =cut
    methods.update = fold_ipset('ipset_update',
      function (a, b)
        a[b] = true
        return a
      end
    )

    -- =pod
    --
    -- =item silk.B<ipset_difference_update(>I<ipset>, I<other>[, ...]B<)>
    --
    -- Remove from I<ipset> any IP addresses found in I<other>s.
    -- I<other>s may contain ipsets, L<ipwildcards|/IP Wildcard>,
    -- ipwildcard strings, or arrays of L<ipaddrs|/IP Address> and
    -- ipwildcard strings.  Return I<ipset>.
    --
    -- =cut
    methods.difference_update = fold_ipset('ipset_difference_update',
      function (a, b)
        a[b] = false
        return a
      end
    )

    -- ipset intersection update method (defined in C)
    methods.intersection_update = fold_ipset('ipset_intersection_update',
                                             methods.intersection_update)

    -- =pod
    --
    -- =item silk.B<ipset_union(>I<ipset>, I<other>[, ...]B<)>
    --
    -- Return a new IPset containing the IP addresses in I<ipset> and
    -- all I<other>s.  I<other>s may contain ipsets,
    -- L<ipwildcards|/IP Wildcard>, ipwildcard strings, or arrays of
    -- L<ipaddrs|/IP Address> and ipwildcard strings.
    --
    -- =cut
    local update = methods.update
    function methods.union (self, ...)
      return update(copy(self), ...)
    end

    -- =pod
    --
    -- =item silk.B<ipset_difference(>I<ipset>, I<other>[, ...]B<)>
    --
    -- Return a new IPset containing the IP addresses in I<ipset> but
    -- not in I<other>s.  I<other>s may contain ipsets,
    -- L<ipwildcards|/IP Wildcard>, ipwildcard strings, or arrays of
    -- L<ipaddrs|/IP Address> and ipwildcard strings.
    --
    -- =cut
    local difference_update = methods.difference_update
    function methods.difference (self, ...)
      return difference_update(copy(self), ...)
    end

    -- =pod
    --
    -- =item silk.B<ipset_add(>I<ipset>[, I<element>[, ...]]B<)>
    --
    -- Add each I<element> to the I<ipset>.  Each I<element> may be an
    -- L<ipaddr|/IP Address>, an L<ipwildcard|/IP Wildcard>, an
    -- ipwildcard string, or an ipset.
    --
    -- =cut
    function methods.add (self, ...)
      checkset('ipset_add', self)
      local set = {...}
      for i = 1, #set do
        self[set[i]] = true
      end
    end

    -- =pod
    --
    -- =item silk.B<ipset_discard(>I<ipaddr>[, I<other>[, ...]]B<)>
    --
    -- Remove from I<ipset> any IP addresses found in I<other>s.
    -- I<other>s may contain ipsets, L<ipwildcards|/IP Wildcard>,
    -- ipwildcard strings, or L<ipaddrs|/IP Address>.
    --
    -- =cut
    function methods.discard (self, ...)
      checkset('ipset_discard', self)
      local set = {...}
      for i = 1, #set do
        self[set[i]] = false
      end
    end

    -- =pod
    --
    -- =item silk.B<ipset_remove(>I<ipaddr> I<other>B<)>
    --
    -- Remove from I<ipset> any IP addresses found in I<other>.  Raise
    -- an error if no IP address in I<other> is in I<ipset>.  I<other>
    -- may be an ipset, an L<ipwildcard|/IP Wildcard>, an ipwildcard
    -- string, or an L<ipaddr|/IP Address>.
    --
    -- =cut
    function methods.remove (self, x)
      checkset('ipset_remove', self)
      if not self[x] then error("Not a valid key") end
      self[x] = false
    end

    -- =pod
    --
    -- =item silk.B<ipset_contains(>I<ipset>, I<addr>B<)>
    --
    -- Return B<true> if I<ipset> contains I<addr>.  Return B<false>
    -- otherwise.  I<addr> may be an L<ipaddr|/IP Address> or a string
    -- representation of an ipaddr.
    --
    -- =cut
    methods.contains = function (self, x)
      return self[ipaddr(x)]
    end


    -- =pod
    --
    -- =item silk.B<ipset_intersection(>I<ipset>, I<other>[, ...]B<)>
    --
    -- Return a new IPset containing the IP addresses common to
    -- I<ipset> and I<other>s.  I<other>s may contain ipsets,
    -- L<ipwildcards|/IP Wildcard>, ipwildcard strings, or arrays of
    -- L<ipaddrs|/IP Address> and ipwildcard strings.
    --
    -- =cut
    local intersection_update = methods.intersection_update
    function methods.intersection (self, ...)
      return intersection_update(copy(self), ...)
    end

    -- =pod
    --
    -- =item silk.B<ipset_symmetric_difference_update(>I<ipset>, I<other>[, ...]B<)>
    --
    -- Update I<ipset>, keeping the IP addresses found in I<ipset> or
    -- in I<other> but not in both. I<other>s may contain ipsets,
    -- L<ipwildcards|/IP Wildcard>, ipwildcard strings, or arrays of
    -- L<ipaddrs|/IP Address> and ipwildcard strings.  Return
    -- I<ipset>.
    --
    -- =cut
    local intersection = methods.intersection
    methods.symmetric_difference_update = fold_ipset(
      'ipset_symmetric_difference_update',
      function (a, b)
        local val = intersection(a, b)
        a[b] = true
        a[val] = false
        return a
      end
    )

    -- =pod
    --
    -- =item silk.B<ipset_symmetric_difference(>I<ipset>, I<other>[, ...]B<)>
    --
    -- Return a new IPset containing the IP addresses in either
    -- I<ipset> or in I<other> but not in both.  I<other>s may contain
    -- ipsets, L<ipwildcards|/IP Wildcard>, ipwildcard strings, or
    -- arrays of L<ipaddrs|/IP Address> and ipwildcard strings.
    --
    -- =cut
    local symmetric_difference_update = methods.symmetric_difference_update
    function methods.symmetric_difference (self, ...)
      return symmetric_difference_update(copy(self), ...)
    end

    -- ipset equality
    local symmetric_difference = methods.symmetric_difference
    function meta.__eq (self, set)
      return #(symmetric_difference(self, set)) == 0
    end

    -- =pod
    --
    -- =item silk.B<ipset_is_disjoint(>I<ipset>, I<element>B<)>
    --
    -- Return B<true> when none of the IP addresses in I<element> are
    -- present in I<ipset>.  Return B<false> otherwise.  I<element>
    -- may be an L<ipaddr|/IP Address>, an L<ipwildcard|/IP Wildcard>,
    -- an ipwildcard string, or an ipset.
    --
    -- =cut
    function methods.is_disjoint (self, set)
      checkset('ipset_is_disjoint', self)
      return not self[set]
    end
  end -- do ... end  augmenting ipset

  -- Augment datetime
  do
    local datetime = objects.datetime
    local meta = datetime.metatable
    local to_number = datetime.methods.to_number

    function meta.__eq (a, b)
      return to_number(a) == to_number(b)
    end

    function meta.__lt (a, b)
      return to_number(a) < to_number(b)
    end

    function meta.__le (a, b)
      return to_number(a) <= to_number(b)
    end

  end -- do ... end  augmenting datetime

  -- Augment sidecar
  do
    local sidecar = objects.sidecar
    local meta = sidecar.metatable
    local methods = sidecar.methods
    local sc_elem_create = internal.sc_elem_create
    local sidecar_freeze_helper = internal.sidecar_freeze_helper

    -- get the table of valid types (e.g., "uint8", etc)
    local sc_elem_types = internal.sc_elem_make_type_table()

    -- make our own version of the sk_lua_sc_elem_key_name[] table
    -- from sklua-silk.c.  In this version, the value is the type of
    -- value the type expects.
    local sc_elem_keys = {
      type           = 'string',
      list_elem_type = 'string',
      enterprise_id  = 'number',
      element_id     = 'number'
    }

    --  freeze_table(tbl, flat, parent_key)
    --
    --    This function is a helper for the sidecar_freeze() function
    --    defined below, and it may call itself recursively.
    --
    --    The function takes a table to freeze.  The function verifies
    --    that each key in the table is a string.  It also verifies
    --    that each value is one of the following:
    --
    --    (1) a silk.sidecar_elem userdata, in which case nothing else
    --    needs to be done with the value.
    --
    --    (2) a string, in which case the function checks if the
    --    string names a sidecar data type.  If so, the value is
    --    converted to a sidecar_elem; otherwise an error is raised.
    --
    --    (3) a table. In this case, the value may describe a
    --    sidecar_elem (in which case the only keys should be those
    --    given in the 'sc_elem_keys' table above) or the value may be
    --    a structured data item that needs to be handled recursively.
    --    If it describes a sidecar_elem, the elem is created,
    --    otherwise the table is visited.
    --
    --    Each key,value pair this function visits is added to the
    --    'flat' array.  The key is appended to 'parent_key' (if any)
    --    with a NULL character, and a NULL character is appended.
    --    The value is a sidecar_elem.  N.B. Flat must be an array to
    --    ensure that the table name is seen before elements that
    --    belong in the table.
    --
    --    Once all members of the table are visited, the table is made
    --    read-only.  This is done by creating an emtpy table,
    --    creating a meta-table for the empty table, and setting this
    --    function's 'tbl' argument as the meta-table's __index
    --    method.  The empty table is returned, and the caller should
    --    replace use of the 'tbl' argument with the table that this
    --    function returns.
    --
    local function freeze_table (t, flat, parent_key)
      local flat_key
      local ty
      for k,v in pairs(t) do
        -- ensure key is a string or a number and does not contain
        -- embedded NULL characters
        ty = type(k)
        if ty == 'string' then
          if "\000" == string.match(k, "\000") then
            error(string.format("invalid sidecar key %q: keys may"
                                  .." not contain embedded NULL",
                                k))
          end
        elseif ty ~= 'number' then
          error("invalid sidecar key: keys must be string or number, got "..ty)
        end

        -- append key to parent_key to make 'flat_key'
        if parent_key then
          flat_key = parent_key.."\000"..k.."\000"
        else
          flat_key = k.."\000"
        end

        -- check the type of the value
        ty = type(v)
        if "userdata" == ty then
          local mt = getmetatable(v)
          if type(mt) ~= 'table' or type(mt.__name) ~= 'string' then
            error("invalid sidecar value: string, table, or sidecar_elem"
                    .." expected, got "..ty)
          elseif mt.__name ~= "silk.sidecar_elem" then
            error("invalid sidecar value: string, table, or sidecar_elem"
                    .." expected, got "..mt.__name)
          end
          -- value is already a sidecar_elem
          flat[1 + #flat] = {flat_key, v}

        elseif "string" == ty then
          -- allow a string value to designate the type
          if not sc_elem_types[v] or v == 'table' then
            error("invalid sidecar value: attempted to treat string as an"
                    .." element type but '".. v .."' is not a valid type")
          end
          t[k] = sc_elem_create(v)
          flat[1 + #flat] = {flat_key, t[k]}

        elseif "table" == ty then
          -- this could be a table that represents a single
          -- sidecar_elem, or it could be a structured data
          local is_elem = true
          if type(v.type) ~= sc_elem_keys['type'] then
            is_elem = false
          else
            for a,b in pairs(v) do
              if sc_elem_keys[a] == nil then
                -- the table 'v' contains a key that cannot be used to
                -- create a sidecar_elem
                is_elem = false
                break
              elseif type(b) ~= sc_elem_keys[a] then
                -- the key is not of the proper type
                is_elem = false
                break
              end
            end
          end
          if not is_elem then
            -- treat it as a structured data
            flat[1 + #flat] = {flat_key, sc_elem_create("table")}
            t[k] = freeze_table(v, flat, flat_key)
          elseif v.type == 'list' then
            t[k] = sc_elem_create(v.type, v.list_elem_type,
                                  v.element_id, v.enterprise_id)
            flat[1 + #flat] = {flat_key, t[k]}
          elseif  not sc_elem_types[v.type] or v.type == 'table' then
            error("invalid sidecar element: '"..v.type
                    .."' is not a valid element type")
          else
            t[k] = sc_elem_create(v.type, v.element_id, v.enterprise_id)
            flat[1 + #flat] = {flat_key, t[k]}
          end

        else
          error("invalid sidecar value: string, table, or sidecar_elem"
                  .." expected, got "..ty)
        end
      end -- for k,v in pairs(t)

      -- make the table read only
      local mt = {}
      mt.__index = t
      -- FIXME: See if we can make a metatable for 'mt' that handles
      -- these common functions.
      mt.__len = function (_) return #t end
      mt.__pairs = function (_) return pairs(t) end
      mt.__tostring = function (_) return tostring(t) end
      mt.__newindex = function (_) error("sidecar is frozen") end
      return setmetatable({}, mt)
    end -- freeze_table

    --  =pod
    --
    --  =item silk.B<sidecar_freeze(>I<sidecar>B<)>
    --
    --  Freeze the sidecar description I<sidecar> so that it may no
    --  longer be modified.  The sidecar must be frozen before it can
    --  be added to a L<stream|/Stream>.  Freezing a sidecar changes
    --  the values to L<sidecar_elem|/Sidecar Element> objects.
    --
    --  Do nothing when I<sidecar> is already frozen.
    --
    --  Raise an error when the value for a key does not contain the
    --  required elements.
    --
    --  =cut
    --
    --  Freezing a sidecar also updates the C structure with all the
    --  elements that have been added to the sidecar's table structure
    --  in Lua.
    --
    --  FIXME: This does not handle error conditions well, and will
    --  leave the table in a weird state.  The freeze_table() function
    --  above needs to do a better job of handling a table that has
    --  been partially frozen.  Or perhaps we should build the frozen
    --  table separate from the table we are given, and only return
    --  the frozen table on success.
    --
    function methods.freeze (s)
      -- the uservalue has the table that reflects the structure of
      -- the sidecar elements
      local uvalue = getuservalue(s)
      if uvalue == nil then
        local mt = getmetatable(t)
        if type(mt) ~= 'table' or type(mt.__name) ~= 'string' then
          error('silk.sidecar expected, got '.. type(s))
        elseif mt.__name ~= 'silk.sidecar' then
          error('silk.sidecar expected, got '.. mt.__name)
        end
      end

      -- check if already frozen
      if uvalue[2] then
        -- it is already frozen
        return
      end

      -- visit the table recursively and fill 'flat'
      local flat = {}
      uvalue[1] = freeze_table(uvalue[1], flat, nil)
      uvalue[2] = true

      -- call function to update the C sidecar object
      sidecar_freeze_helper(s, flat)
    end

  end -- do ... end  augmenting sidecar


  export = silkutils.realize_object_table(objects, export)

  -- Add functions to export table
  for k, v in pairs(functions) do
    export[k] = v
  end

  -- Return the export table
  return export
end

return init


-- Local Variables:
-- mode:lua
-- indent-tabs-mode:nil
-- lua-indent-level:2
-- End:
