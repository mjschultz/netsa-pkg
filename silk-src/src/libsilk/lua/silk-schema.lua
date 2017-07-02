------------------------------------------------------------------------
-- Copyright (C) 2014-2017 by Carnegie Mellon University.
--
-- @OPENSOURCE_LICENSE_START@
-- See license information in ../../../LICENSE.txt
-- @OPENSOURCE_LICENSE_END@
--
------------------------------------------------------------------------

------------------------------------------------------------------------
-- $SiLK: silk-schema.lua efd886457770 2017-06-21 18:43:23Z mthomas $
------------------------------------------------------------------------

local objects, functions, internal, silk = ...

-- Holds the functions exported to sklua-schema.c (not user-visible)
local export = {}

-- Cache these so we don't have to look them up in the _ENV all the time
local pairs = pairs
local ipairs = ipairs
local type = type
local next = next
local setmetatable = setmetatable

--  Helper function:  is_callable(x)
--
--     Determine whether 'x' can be called like a function.
--
local function is_callable (x)
  if type(x) == 'function' then
    return true
  end
  local meta = getmetatable(x)
  if meta then
    local call = meta.__call
    return call and is_callable(call)
  end
  return false
end


--  export.index_ies(ie_sequence)
--
--    Update the sequence of field objects in 'ie_sequence' to contain
--    a mapping from IE_name to field.  In addition, return a second
--    argument that is a table mapping from an sk_field_ident_t to
--    field.
--
function export.index_ies (ies)
  local ie_index = {}
  for i, ie in ipairs(ies) do

    --  Add a name index for the element
    local name = ie.name
    if ies[name] then
      -- If an IE exists more than once, make name_1, name_2, name_N
      -- aliases for these.  It makes ie.name == ie.name_1 in this
      -- case.
      local sub = 1
      repeat
        sub = sub + 1
        name = ie.name .. "_" .. tostring(sub)
      until ies[name] == nil
      if sub == 2 then
        ies[ie.name .. "_1"] = ies[ie.name]
      end
    end
    ies[name] = ie

    -- Add PEN/ID index entry
    if ie.enterpriseId then
      ie_index[((ie.enterpriseId << 32) | ie.elementId)] = ie
    else
      ie_index[ie.elementId] = ie
    end
  end
  return ies, ie_index
end  -- export.index_ies()


--  There are two times we must verify whether an ie_spec (that is, a
--  table describing an information element) contains the necessary
--  key/value pairs.  One is used by silk.infomodel_augment() when
--  adding an IE to the information model.  The other may be used by
--  silk.plugin_register_field() when registering a plug-in field if
--  the plug-in is creating a new IE.
--
--  This set of variables and functions are used by both.
--
local check_ie_spec = {}
do
  -- Types for which native byte order may be different than network
  -- byte order
  check_ie_spec.endian_types = internal.make_table_ie_endian_typed_names()

  -- Data Type names
  check_ie_spec.data_types = internal.make_table_ie_type_names()

  -- Data Type Semantic names
  check_ie_spec.data_type_semantics = internal.make_table_ie_semantic_names()

  -- Unit names
  check_ie_spec.units = internal.make_table_ie_semantic_units()

  -- Plug-in Field Lookup Type names
  check_ie_spec.lookup_types =internal.make_table_field_computed_lookup_names()

  -- Valid sizes for each datatype.  This can be a number (number of
  -- octets), a sequence of two numbers (minimum and maximum (also
  -- default) number of octets), or a sequence of a function and a
  -- number, where the function returns true if it is given a valid
  -- number of bytes, and the number is the default value.
  local valid_sizes = {
    unsigned8 = 1;
    unsigned16 = {1, 2};
    unsigned32 = {1, 4};
    unsigned64 = {1, 8};
    signed8 = 1;
    signed16 = {1, 2};
    signed32 = {1, 4};
    signed64 = {1, 8};
    float32 = 4;
    float64 = {function (x) return x == 4 or x == 8 end, 8};
    dateTimeSeconds = 4;
    dateTimeMilliseconds = 8;
    dateTimeMicroseconds = 8;
    dateTimeNanoseconds = 8;
    ipv4Address = 4;
    ipv6Address = 16;
    macAddress = 6;
    boolean = 1;
  }

  local NIL = {}
  check_ie_spec.NIL = NIL

  local varlen = 65535
  check_ie_spec.varlen = varlen

  check_ie_spec.max_id = 0x7fff
  check_ie_spec.max_pen = 0xffffffff

  -- Helper function:  check_ie_spec.check_length( DATA_TYPE, LEN )
  --
  -- Verify that LEN is a valid length for DATA_TYPE.  Return the
  -- length or throw an error.
  --
  function check_ie_spec.check_length (data_type, len)
    local size = valid_sizes[data_type]

    if type(size) == "number" then
      if len == nil or len == size then
        return size
      end
    elseif type(size) == "table" then
      local a, b = table.unpack(size)
      if type(a) == "function" then
        if len == nil then
          return b
        end
        if a(len) then
          return len
        end
      elseif type(len) == "number" and len >= a and len <= b then
        return len
      end
    elseif size == nil then
      if len == nil or len == "varlen" then
        return varlen
      end
      return len
    else
      assert(nil, "Illegal size in valid_sizes for " .. tostring(data_type))
    end
    error(string.format("Length %s is not valid for type %s",
                        tostring(len), data_type))
  end -- check_length


  --  check_ie_spec.verify_ie_spec(ie_spec, normalize_table)
  --
  --    Verify the ie_spec against the entries in the normalize_table
  --    which is a table that describes the specification.  The keys
  --    match the allowable keys of the incoming ie_spec.  The value
  --    for each key is a table which is used to check the value the
  --    caller gave for that feature.
  --
  --    The keys of the subtable are:
  --
  --    type: the expected type for this key.  If this is a table, then
  --    the caller's value for this key must be a string that matches
  --    one of the keys in the table.
  --
  --    default: whether there is a default value.  If none is
  --    specified, the key is required.
  --
  --    valid: a function to call the user's valid to see if is correct.
  --
  --    failmsg: a string to append to the error when the valid function
  --    fails.
  --
  --    final: a function to call once all keys have been checked.  For
  --    example, for checking the length given the data type.
  --
  function check_ie_spec.verify_ie_spec (ie_spec, normalize_table)
    -- Check for valid table keys
    for k, v in pairs(ie_spec) do
      if normalize_table[k] == nil then
        error(string.format("Unrecognized key '%s'", k))
      end
    end

    -- Check and normalize table keys
    for k, v in pairs(normalize_table) do
      local val = ie_spec[k]
      if val == nil then
        local default = v.default
        if default == nil then
          error(string.format("Required key %s is missing", k))
        elseif type(default) == "function" then
          ie_spec[k] = default(ie_spec)
        elseif default ~= NIL then
          ie_spec[k] = default
        end
      else
        local expected = "string"
        if type(v.type) == "string" then
          expected = v.type
        end
        if v.type ~= nil and type(val) ~= expected then
          error(string.format("Expected a %s for key %s; got a %s",
                              expected, k, type(val)))
        end
        if type(v.type) == "table" and v.type[val] == nil then
          error(string.format("Value '%s' is not valid for key %s",
                              val, k))
        end
        if v.valid and not v.valid(val) then
          error(string.format("Value '%s' is not valid for key %s: %s",
                              val, k, v.failmsg))
        end
      end -- if val == nil
    end -- for k, v in pairs(normalize_table)

    -- final checks on normalized table
    for k, v in pairs(normalize_table) do
      if v.final ~= nil then
        v.final(ie_spec, ie_spec[k])
      end
    end -- for k, v in pairs(ie)

    return ie_spec
  end -- function check_ie_spec.verify_ie_spec()
end  -- close of do..end for defining members of check_ie_spec


--  export.normalize_ie(ie_spec)
--
--    Verify the IE specification 'ie_spec' when adding an IE to the
--    information model.
--
do
  --  Define the table that describes the specification.
  --
  local augment_normalize_table = {
    name = {
      type = "string";
    };
    elementId = {
      type = "number";
      valid = function (x) return x >= 1 and x <= check_ie_spec.max_id end;
      failmsg = string.format("Must be between 1 and %d",
                              check_ie_spec.max_id);
    };
    enterpriseId = {
      type = "number";
      default = check_ie_spec.NIL;
      valid = function (x)
        return x >= 0 and x <= check_ie_spec.max_pen
      end;
      failmsg = string.format("Must be between 0 and %d",
                              check_ie_spec.max_pen);
    };
    description = {
      type = "string";
      default = check_ie_spec.NIL;
    };
    dataType = {
      type = check_ie_spec.data_types;
    };
    dataTypeSemantics = {
      type = check_ie_spec.data_type_semantics;
      default = check_ie_spec.NIL;
    };
    units = {
      type = check_ie_spec.units;
      default = check_ie_spec.NIL;
    };
    rangemin = {
      type = "number";
      default = 0;
    };
    rangemax = {
      type = "number";
      default = 0;
    };
    endian = {
      type = "boolean";
      default = check_ie_spec.NIL;
      final = function (ie_spec, x)
        if nil == x then
          ie_spec.endian = (check_ie_spec.endian_types[ie_spec.dataType]
                              and true
                              or false)
        end
      end;
    };
    reversible = {
      type = "boolean";
      default = false;
    };
    length = {
      valid = function (x)
        return ((type(x) == "number" and (x >= 0 and x < check_ie_spec.varlen))
                  or x == "varlen")
      end; -- valid

      failmsg = string.format(
        "Must be a number between 0 and %d or the string 'varlen'",
        check_ie_spec.varlen-1);

      final = function (ie_spec, x)
        ie_spec.length = check_ie_spec.check_length(ie_spec.dataType, x)
      end; -- final
    };
  }
  -- close of augment_normalize_table

  function export.normalize_ie (ie_spec)
    return check_ie_spec.verify_ie_spec(ie_spec, augment_normalize_table)
  end
end -- Implement export.normalize_ie()


--  export.fixlist_append_normalize(fixlist, ...)
--
--    Helper function for silk.fixlist_append() in sklua-schema.c.
--
--    Converts all non-fixrecs in {...} to fixrecs according to the
--    type of list in 'fixlist'.  For any fixrecs in {...}, ensures
--    the fixrec's schema matches that expected by 'fixlist'.
--
do
  -- create local cache of functions
  local sk_fixlist_get_schema = objects.fixlist.methods.get_schema
  local sk_fixlist_get_type = objects.fixlist.methods.get_type
  local sk_fixrec = objects.fixrec.constructor
  local sk_fixrec_get_schema = objects.fixrec.methods.get_schema
  local sk_schema = objects.schema.constructor
  local sk_schemas_match = internal.schemas_match
  local fixrec_meta = objects.fixrec.metatable

  local function is_fixrec(r)
    return rawequal(getmetatable(r), fixrec_meta)
  end

  function export.fixlist_append_normalize (fixlist, ...)
    local list_type = sk_fixlist_get_type(fixlist)
    local fixrecs = {}
    if "basicList" == list_type then
      local schema = sk_fixlist_get_schema(fixlist)
      for i,v in ipairs{...} do
        if is_fixrec(v) then
          if sk_schemas_match(schema, sk_fixrec_get_schema(v)) then
            fixrecs[1+#fixrecs] = v
          else
            error(string.format("schema of %s does not match that of %s",
                                tostring(fixlist), tostring(v)))
          end
        else
          fixrecs[1+#fixrecs] = sk_fixrec(schema, { v })
        end
      end
    elseif "subTemplateList" == list_type then
      local schema = sk_fixlist_get_schema(fixlist)
      for i,v in ipairs{...} do
        if is_fixrec(v) then
          if sk_schemas_match(schema, sk_fixrec_get_schema(v)) then
            fixrecs[1+#fixrecs] = v
          else
            error(string.format("schema of %s does not match that of %s",
                                tostring(fixlist), tostring(v)))
          end
        else
          fixrecs[1+#fixrecs] = sk_fixrec(schema, v )
        end
      end
    elseif "subTemplateMultiList" == list_type then
      for i,v in ipairs{...} do
        if is_fixrec(v) then
          fixrecs[1+#fixrecs] = v
        else
          local schema = sk_schema(table.unpack(v[1]))
          for j = 2,#v do
            fixrecs[1+#fixrecs] = sk_fixrec(schema, v[j] )
          end
        end
      end
    else
      error("unexpected list type " .. list_type)
    end
    return fixlist, fixrecs
  end
end


--  =pod
--
--  =item silk.B<plugin_register_field(>I<description_table>B<)>
--
--  Register a plug-in field (also known as a computed schema field)
--  as described by I<description_table>.
--
--  A registered field is available for use by the B<--fields> switch
--  on various applications but is only active when it is explicitly
--  requested by the user.
--
--  The keys of the I<description_table> and their corresponding
--  values are:
--
--  =over 4
--
--  =item name
--
--  The value is a string that names the field, and this key is
--  required.  The user may request this field by specifying the
--  string C<plugin.> followed by the name of the field.  The value in
--  this field may also be used as specified below under the C<lookup>
--  key.
--
--  =item update
--
--  The value is a function that is called for each fixrec for the
--  purpose of setting the computed field's value.  This key is
--  required.  The function is passed either two or three arguments.
--  The first argument is the L<fixrec|/Record> object to update, and
--  the second argument is a L<field|/Field> object that references
--  the computed field that was registered.  The final argument is a
--  sequence of field objects that correspond to the field names
--  specified in the C<prerequisite> key of the I<description_table>.
--  For each of those field names, this sequence includes the (first)
--  field object on the fixrec with that name or B<nil> if the fixrec
--  does not contain that field.  If no prerequisite fields are
--  specified, only two arguments are passed to this function.
--
--  =item prerequisite
--
--  The value is a sequence of strings specifying the names of the
--  L<field|/Field> objects that this computed field uses.  For each
--  field name, an entry is added to the sequence passed as the final
--  argument to the C<update> function.
--
--  =item lookup
--
--  The value of this required key is one of the strings C<name>,
--  C<ident>, or C<create>.  The value determines how the computed
--  field is located on the incoming schemas.
--
--  =over 4
--
--  =item *
--
--  The string C<name> indicates that the value given in the C<name>
--  key is a known information element (IE).
--
--  =item *
--
--  The string I<ident> indicates the C<elementId> and C<enterpriseId>
--  values name a known information element.
--
--  =item *
--
--  The string C<create> indicates a new information element should be
--  created unless it already exists.  In this case, the IE is given
--  the name given by the C<name> key and it is given the ID specified
--  in the C<elementId> and C<enterpriseId> keys.  If those values are
--  0, a new ID is generated.
--
--  =back
--
--  =item elementId
--
--  The identifier for the IE as described under C<lookup>.  This is
--  not required when C<lookup> is C<name>.
--
--  =item enterpriseId
--
--  The Private Enterprise Number (PEN) for the IE as described under
--  C<lookup>.  An unspecified value (or a value of B<nil>) is the
--  same as 0.
--
--  =item dataType
--
--  The type of the IE.  This must be specified when C<lookup> is
--  C<create>.  The set of allowable values are the same as for
--  silk.infomodel_augment().
--
--  =item length
--
--  The data length of the IE in octets.  This must be specified when
--  C<lookup> is C<create>.  The set of allowable values are the same
--  as for silk.infomodel_augment().
--
--  =item dataTypeSemantics
--
--  The data type semantics of the IE.  This may be set when creating
--  a new IE.  The set of allowable values are the same as for
--  silk.infomodel_augment().
--
--  =item units
--
--  The units of the IE.  This may be set when creating a new IE.  The
--  set of allowable values are the same as for
--  silk.infomodel_augment().
--
--  =item rangemin
--
--  The minimal value of the IE.  This may be set when creating a new
--  IE.  The set of allowable values are the same as for
--  silk.infomodel_augment().
--
--  =item rangemax
--
--  The maximum value of the IE.  This may be set when creating a new
--  IE.  The set of allowable values are the same as for
--  silk.infomodel_augment().
--
--  =item initialize
--
--  The value of this optional key is a function to call when the user
--  selects the field and before fixrecs are processed.  The function
--  is called with no arguments.  If it throws an error or returns a
--  value other than B<nil> or 0, the application exists with an
--  error.
--
--  =item cleanup
--
--  The value of this optional key is a function to call once all
--  fixrecs have been processed.  The function is called with no
--  arguments and its return value is ignored.
--
--  =back
--
--  =cut
--
do
  -- The _registered_fields table holds fields that have been
  -- registered.
  --
  -- When register_field() is called from within Lua, first the name
  -- of the field is checked in _registered_fields.name so a repeat
  -- field raises an error.  If the name is new, the argument is
  -- verified and normalized. If the field looks good, its data is
  -- added to the _registered_fields.ie_specs sequence and its name is
  -- added to the _names table.
  --
  -- When it is time for a C application to register fields, the C
  -- code calls get_plugin_fields() to get this _registered_fields
  -- table.  It then processes the ie_specs to create the appropriate
  -- fields.
  local _registered_fields = {names = {}, ie_specs = {}}

  -- Called by the C code to get the registered plugin fields.
  function export.get_plugin_fields ()
    return _registered_fields.ie_specs
  end

  -- Helper function:  required_key_err( KEY, LOOKUP )
  --
  -- Throw an error that the KEY key is required when the lookup
  -- member of the description_table is LOOKUP.
  --
  local function required_key_err (key, lookup)
    error(string.format("The '%s' key required when lookup is %s",
                        key, lookup))
  end

  -- Table of each IE feature, where each value is a table which is
  -- used to type-check an entry for that feature
  local plugin_normalize_table = {
    update = {
      type = "function";
    };
    lookup = {
      type = check_ie_spec.lookup_types;
    };
    name = {
      type = "string";
      --default = NIL,
      --final = function (ie_spec, x)
      --  if nil == x then
      --    if "ident" == ie_spec.lookup then
      --      return;
      --    end
      --    required_key_err("name", ie_spec.lookup)
      --  end
      --end;
    };
    elementId = {
      type = "number";
      default = check_ie_spec.NIL;
      valid = function (x) return x >= 0 and x <= check_ie_spec.max_id end;
      failmsg = string.format("Must be between 0 and %d", check_ie_spec.max_id);
      final = function (ie_spec, x)
        if nil == x then
          if "name" == ie_spec.lookup then
            return;
          end
          required_key_err("elementId", ie_spec.lookup)
        end
      end;
    };
    enterpriseId = {
      type = "number";
      valid = function (x) return x >= 0 and x <= check_ie_spec.max_pen end;
      failmsg = string.format("Must be between 0 and %d",
                              check_ie_spec.max_pen);
      default = check_ie_spec.NIL;
      final = function (ie_spec, x)
        if nil ~= x and 0 == ie_spec.elementId then
          error("The enterpriseId must be nil when elementId is 0")
        end
      end
    };
    dataType = {
      type = check_ie_spec.data_types;
      default = check_ie_spec.NIL;
      final = function (ie_spec, x)
        if "create" == ie_spec.lookup and nil == x then
          required_key_err("dataType", ie_spec.lookup)
        end
        -- should we error when dataType is ignored?
      end
    };
    prerequisite = {
      type = "table";
      default = check_ie_spec.NIL;
      failmsg = "Must be a sequence of strings";
      valid = function (t)
        for i,v in ipairs(t) do
          if type(v) ~= "string" then
            return false;
          end
        end
        return true
      end;
      final = function (ie_spec, x)
        if nil ~= x then
          if #x == 0 then
            ie_spec.prerequisite = nil
          end
        end
      end;
    };
    initialize = {
      type = "function";
      default = check_ie_spec.NIL;
    };
    cleanup = {
      type = "function";
      default = check_ie_spec.NIL;
    };
    description = {
      type = "string";
      default = check_ie_spec.NIL;
    };
    dataTypeSemantics = {
      type = check_ie_spec.data_type_semantics;
      default = check_ie_spec.NIL;
    };
    units = {
      type = check_ie_spec.units;
      default = check_ie_spec.NIL;
    };
    rangemin = {
      type = "number";
      default = 0;
    };
    rangemax = {
      type = "number";
      default = 0;
    };
    length = {
      default = check_ie_spec.NIL;
      valid = function (x)
        return (x == nil
                  or (type(x) == "number"
                        and (x >= 0 and x < check_ie_spec.varlen))
                  or x == "varlen")
      end; -- valid
      failmsg = string.format(
        "Must be a number between 0 and %d or the string 'varlen'",
        check_ie_spec.varlen-1);

      final = function (ie_spec, x)
        if "create" ~= ie_spec.lookup then
          return
        end
        if nil == x then
          required_key_err("length", ie_spec.lookup)
        end
        ie_spec.length = check_ie_spec.check_length(ie_spec.dataType, x)
      end; -- final
    };
  }
  -- close of plugin_normalize_table

  function silk.plugin_register_field (description_table)
    if type(description_table) ~= "table" then
      error("bad argument #1 to 'plugin_register_field' (table expected, got "
              .. type(description_table) .. ")")
    end

    local ie_spec = check_ie_spec.verify_ie_spec(description_table,
                                                 plugin_normalize_table)
    if (_registered_fields.names[ie_spec.name]) then
      error(string.format("A field '%s' has alread been registered",
                          ie_spec.name))
    end

    -- Add to the list of fields
    local idx = 1 + #_registered_fields.ie_specs
    _registered_fields.ie_specs[idx] = ie_spec
    _registered_fields.names[ie_spec.name] = idx
  end
end
--  End of plugin_register_field()


-- Build silk table
do
  silk = silk or {}

  local function is_padding (field)
    return field.name == "paddingOctets"
  end

  -- Implement fixrec_get_stime and fixrec_get_etime
  do
    local meth = objects.fixrec.methods
    local get_schema = meth.get_schema
    local cache = setmetatable({}, {__mode='k'})
    local zero_time = function () return silk.datetime(0) end

    local function handle_new_schema (schema)
      local get_stime = zero_time
      local get_etime = zero_time
      local sfield = schema['flowStartMilliseconds'] or
        schema['flowStartMicroseconds'] or
        schema['flowStartNanoseconds'] or
        schema['flowStartSeconds']
      if sfield then
        get_stime = function (rec) return rec[sfield] end
      else
        sfield = schema['flowStartDeltaMicroseconds']
        if sfield then
          get_stime = function (rec, export)
            return silk.datetime_add_duration(export, rec[sfield])
          end
        else
          sfield = schema['flowStartSysUpTime']
          local init = schema['systemInitTimeMilliseconds']
          if sfield and init then
            get_stime = function (rec)
              return silk.datetime_add_duration(rec[init], rec[sfield])
            end
          else
            sfield = nil
          end
        end
      end

      local efield = schema['flowEndMilliseconds'] or
        schema['flowEndMicroseconds'] or
        schema['flowEndNanoseconds'] or
        schema['flowEndSeconds']
      if efield then
        get_etime = function (rec) return rec[efield] end
      else
        efield = schema['flowEndDeltaMicroseconds']
        if efield then
          get_etime = function (rec, export)
            return silk.datetime_add_duration(export, rec[efield])
          end
        else
          efield = schema['flowEndSysUpTime']
          local init = schema['systemInitTimeMilliseconds']
          if efield and init then
            get_etime = function (rec)
              return silk.datetime_add_duration(rec[init], rec[efield])
            end
          elseif get_stime then
            local dur = schema['flowDurationMilliseconds']
            if dur then
              get_etime = function (rec)
                return silk.datetime_add_duration(get_stime(rec), rec[dur])
              end
            else
              dur = schema['flowDurationMicroseconds']
              if dur then
                get_etime = function (rec)
                  return silk.datetime_add_duration(get_stime(rec),
                                                    rec[dur] // 1000)
                end -- get_etime
              else
                get_etime = get_stime
              end -- if dur else
            end -- if dur
          end -- elseif get_stime
        end -- if efield else
      end -- if efield else

      local data = {get_stime = get_stime, get_etime = get_etime}
      cache[schema] = data
      return data
    end -- local function handle_new_schema

    -- =pod
    --
    -- =item silk.B<fixrec_get_stime(>I<fixrec>[, I<export_time>]B<)>
    --
    -- Return a L<datetime|/Datetime> representing the start time of
    -- I<fixrec>.  The optional argument is the export time returned
    -- by
    -- L<silk.B<stream_read()>|/"silk.B<stream_read(>I<stream>[, I<arg>]B<)>">.
    -- This function examines the fixrec's schema to determine which
    -- of the several possible IPFIX time fields are being used.
    --
    -- =cut
    function meth.get_stime (rec, export_time)
      local schema = get_schema(rec)
      local fns = cache[schema] or handle_new_schema(schema)
      return fns.get_stime(rec, export_time)
    end

    -- =pod
    --
    -- =item silk.B<fixrec_get_etime(>I<fixrec>[, I<export_time>]B<)>
    --
    -- Return a L<datetime|/Datetime> representing the end time of
    -- I<fixrec>.  The optional argument is the export time returned
    -- by
    -- L<silk.B<stream_read()>|/"silk.B<stream_read(>I<stream>[, I<arg>]B<)>">.
    -- This function examines the fixrec's schema to determine which
    -- of the several possible IPFIX time fields are being used.
    --
    -- =cut
    function meth.get_etime (rec, export_time)
      local schema = get_schema(rec)
      local fns = cache[schema] or handle_new_schema(schema)
      return fns.get_etime(rec, export_time)
    end

  end -- Implement fixrec_get_stime and fixrec_get_etime

  -- __pairs and to_string functions for fields
  do
    local get_info = internal.field_get_info_table
    local meta = objects.field.metatable
    local methods = objects.field.methods

    -- =pod
    --
    -- =item B<pairs(>I<field>B<)>
    --
    -- Return an iterator designed for the Lua B<for> statement that
    -- iterates over the (string, value) pairs of I<field>.  The
    -- string represents an attribute of I<field>; the list of
    -- attributes is documented by the
    -- L<silk.B<field_get_attribute()>|/"silk.B<field_get_attribute(>I<field>, I<attribute>B<)>">
    -- function.  The iterator skips attributes whose value is B<nil>.
    -- May be used as
    -- B<for I<attr_name>, I<value> in pairs(I<field>) do...end>
    --
    -- =cut
    function meta.__pairs (field)
      return pairs(get_info(field))
    end

    -- =pod
    --
    -- =item silk.B<field_to_string(>I<field>B<)>
    --
    -- Return the name attribute of I<field>.
    --
    -- =cut
    function methods.to_string (field)
      return field.name
    end

  end -- __pairs and to_string functions for fields


  -- __pairs and to_string functions for schemas
  do
    local get_fields = objects.schema.methods.get_fields
    local meta = objects.schema.metatable
    local methods = objects.schema.methods

    -- Determine whether 'obj' is a schema, if not report an error in
    -- the function 'caller'
    local function checkschema(caller, obj)
      if getmetatable(obj) ~= meta then
        error(string.format(
                "bad argument #1 to '%s' (silk.schema expected, got %s)",
                caller, type(obj)), 2)
      end
    end

    -- =pod
    --
    -- =item B<pairs(>I<schema>B<)>
    --
    -- Return an iterator designed for the Lua B<for> statement that
    -- iterates over (name, L<field|/Field>) pairs of the schema in
    -- position order.  May be used as
    -- B<for I<name>, I<field> in pairs(I<schema>) do...end>
    --
    -- =cut
    function meta.__pairs (schema)
      local fields = get_fields(schema)
      local len = #fields
      local i = 0
      local function it ()
        i = i + 1
        if i > len then return nil end
        local f = fields[i]
        return f.name, f
      end
      return it, nil, nil
    end

    -- =pod
    --
    -- =item silk.B<schema_to_string(>I<schema>[, I<sep>]B<)>
    --
    -- Return a string consisting of the names of the fields of
    -- I<schema> in position order.  The names are separated by the
    -- string I<sep> or by C<|> when I<sep> is B<nil>.
    --
    -- =cut
    function methods.to_string (schema, sep)
      checkschema('schema_to_string', schema)
      local fields = {}
      if nil == sep then sep = "|" end
      for i, v in ipairs(schema) do
        if not is_padding(v) then
          fields[#fields + 1] = v.name
        end
      end
      return table.concat(fields, sep)
    end

  end -- __pairs and to_string functions for schemas

  -- __len, __pairs, and to_string functions for fixrecs
  do
    local get_fields = objects.schema.methods.get_fields
    local get_schema = objects.fixrec.methods.get_schema
    local meta = objects.fixrec.metatable
    local methods = objects.fixrec.methods

    -- =pod
    --
    -- =item B<#>I<fixrec>
    --
    -- An alias for
    -- L<silk.B<fixrec_count_fields()>|/"silk.B<fixrec_count_fields(>I<fixrec>B<)>">.
    --
    -- =cut
    function meta.__len (rec)
      return #get_schema(rec)
    end

    -- =pod
    --
    -- =item silk.B<fixrec_count_fields(>I<fixrec>B<)>
    --
    -- Return the number of fields in I<fixrec>.
    --
    -- =cut
    function methods.count_fields (rec)
      return #get_schema(rec)
    end

    -- =pod
    --
    -- =item B<pairs(>I<fixrec>B<)>
    --
    -- Return an iterator designed for the Lua B<for> statement that
    -- iterates over (name, value) pairs of the fixrec in position
    -- order, where name is the string name of the field and value is
    -- that field's value in I<fixrec>.  May be used as
    -- B<for I<name>, I<value> in pairs(I<fixrec>) do...end>
    --
    -- =cut
    function meta.__pairs (rec)
      local fields = get_fields(get_schema(rec))
      local len = #fields
      local i = 0
      local function it ()
        i = i + 1
        if i > len then return nil end
        local f = fields[i]
        return f.name, rec[f]
      end
      return it, nil, nil
    end

    -- =pod
    --
    -- =item silk.B<fixrec_to_string(>I<fixrec>[, I<sep>]B<)>
    --
    -- Return a string consisting of the values of the fields of
    -- I<fixrec> in position order.  The values are separated by the
    -- string I<sep> or by C<|> when I<sep> is B<nil>.
    --
    -- =cut
    function methods.to_string (rec, sep)
      local values = {}
      if nil == sep then sep = "|" end
      for i, f in ipairs(get_schema(rec)) do
        if not is_padding(f) then
          values[#values + 1] = tostring(rec[f])
        end
      end
      return table.concat(values, sep)
    end

  end -- __len, __pairs, and to_string functions for fixrecs

  -- Create fixrec copying functions
  do
    local create =  internal.schemamap_create
    local apply =   internal.schemamap_apply
    local to_name = internal.field_to_name
    local get_fields = objects.schema.methods.get_fields
    local get_schema = objects.fixrec.methods.get_schema

    -- =pod
    --
    -- =item silk.B<fixrec_copier(>I<spec>[, I<copy_rest>]B<)>
    --
    -- Return a function which copies data between fixrecs, based on
    -- the given I<spec> table.  The returned function takes a source
    -- fixrec as the first argument, the destination fixrec as the
    -- second argument, and returns the destination fixrec.
    -- Specifically, the returned function (called I<copier> below)
    -- has the following signature:
    --
    -- I<dest_rec> = I<copier>(I<source_rec>, I<dest_rec>)
    --
    -- The spec table contains key,value pairs where the key is the
    -- destination field and the value is either a source field or a
    -- function.  Specifically:
    --
    -- =over 4
    --
    -- =item I<destination_ie_specifier> = I<source_ie_specifier>
    --
    -- Set the I<destination_ie_specifier> field on the destination
    -- fixrec to the value of the I<source_ie_specifier> field on the
    -- source fixrec.
    --
    -- =item I<destination_ie_specifier> = I<function>
    --
    -- Set the I<destination_ie_specifier> field on the destination
    -- fixrec to the result of calling I<function> with the source
    -- fixrec as the only argument.
    --
    -- =back
    --
    -- Each I<ie_specifier> may be anything accepted by the
    -- L<silk.B<schema()>|/"silk.B<schema(>[I<elem>[, ...]]B<)>">
    -- function.
    --
    -- If I<copy_rest> is present and true (not nil or false), the
    -- copier will copy any field that is in both the source and
    -- destination fixrec that is not already specified in the
    -- I<spec>.
    --
    -- As an example, the following I<spec> copies the octetDeltaCount
    -- field from the source to the octetTotalCount field on the
    -- destination, copies the packetDeltaCount field (IE 2) from the
    -- source to the packetTotalCount field on the destination, and
    -- computes a value for flowDurationMilliseconds field
    --
    --  {
    --    octetTotalCount = "octetDeltaCount",
    --    packetTotalCount = 2,
    --    flowDurationMilliseconds = function (r)
    --        return silk.datetime_difference(r.flowEndMilliseconds,
    --                                        r.flowStartMilliseconds)
    --    end
    --  }
    --
    -- =cut
    function functions.fixrec_copier (spec, copy_rest)
      -- cache is a two-level cache.  The key is the source schema,
      -- the result is a table whose key is the dest schema.  The
      -- value in that table is a function that copies data from a
      -- source fixrec to a dest fixrec.
      local cache = setmetatable({}, {__mode='k'})
      return function (source, dest)
        local source_schema = get_schema(source)
        local dest_schema = get_schema(dest)
        local level1 = cache[source_schema]

        -- Look up the source/dest schema combo in the cache
        if level1 ~= nil then
          local copier = level1[dest_schema]
          if copier ~= nil then
            -- If found, take the returned function and use it
            return copier(source, dest)
          end
        end

        -- No copying function was found.  We have to create one.
        do
          local dest_fields = get_fields(dest_schema)
          local source_fields = get_fields(source_schema)

          -- map from destination field names to a function which
          -- fills that field
          local field_fns = {}
          -- map from destination field names to source field names
          local schemamap_spec = {}

          -- Look through each pair in the spec
          for k, v in pairs(spec) do
            k = to_name(k)
            if dest_fields[k] then
              if field_fns[k] or schemamap_spec[k] then
                error(string.format("Duplicate key %q in spec", k))
              end
              if is_callable(v) then
                -- If the value is a function, add to the field_fns
                field_fns[k] = v
              else
                -- If the value is a field, add to the schemamap_spec
                v = to_name(v)
                if source_fields[v] then
                  schemamap_spec[k] = v
                end
              end
            end
          end

          -- If copy_rest is true, add to the spec any fields in both
          -- source and dest that don't already have a mapping.
          if copy_rest then
            for i, v in ipairs(dest_fields) do
              local k = v.name
              if source_fields[k] and field_fns[k] == nil
                and schemamap_spec[k] == nil
              then
                schemamap_spec[k] = k
              end
            end
          end

          -- Create a schemamap from the spec
          local map = next(schemamap_spec)
            and create(schemamap_spec, dest_schema, source_schema)

          -- Generate the function we will return (and cache)
          local function copier (source, dest)
            -- Apply the map
            if map then
              apply(map, source, dest)
            end
            -- Set all the values that come from field functions
            for k, v in pairs(field_fns) do
              dest[k] = v(source)
            end
            -- Return the destination fixrec
            return dest
          end

          -- Add the function to the cache
          if level1 == nil then
            level1 = setmetatable({}, {__mode='k'})
            cache[source_schema] = level1
          end
          level1[dest_schema] = copier

          -- Call the function
          return copier(source, dest)
        end -- do
      end  -- return function (source, dest)
    end -- function functions.fixrec_copier (spec, copy_rest)
  end  -- Create fixrec copying functions


  silk = silkutils.realize_object_table(objects, silk)

  for name, func in pairs(functions) do
    silk[name] = func
  end
end

return silk, export


-- Local Variables:
-- mode:lua
-- indent-tabs-mode:nil
-- lua-indent-level:2
-- End:
