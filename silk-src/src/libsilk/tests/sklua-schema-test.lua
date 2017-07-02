local testfiles =  {
  data = arg[1];
  datav6 = arg[2];
  empty = arg[3];
}
local loc_a, loc_b, loc_c, loc_d, loc_e =
  arg[4], arg[5], arg[6], arg[7], arg[8]

local LuaUnit = require 'luaunit'

local schema            = silk.schema
local schema_get_fields = silk.schema_get_fields
local schema_iter       = silk.schema_iter
local fixrec            = silk.fixrec
local fixrec_copy       = silk.fixrec_copy
local fixrec_get_schema = silk.fixrec_get_schema
local datetime          = silk.datetime

local cert = 6871

local new_ies =
  {
    {name="testOctetArray", enterpriseId=cert, elementId=1000,
     dataType="octetArray", length="varlen"},
    {name="testUnsigned8", enterpriseId=cert, elementId=1001,
     dataType="unsigned8", length=1},
    {name="testUnsigned16", enterpriseId=cert, elementId=1002,
     dataType="unsigned16", length=2},
    {name="testUnsigned32", enterpriseId=cert, elementId=1003,
     dataType="unsigned32", length=4},
    {name="testUnsigned64", enterpriseId=cert, elementId=1004,
     dataType="unsigned64", length=8},
    {name="testSigned8", enterpriseId=cert, elementId=1005,
     dataType="signed8", length=1},
    {name="testSigned16", enterpriseId=cert, elementId=1006,
     dataType="signed16", length=2},
    {name="testSigned32", enterpriseId=cert, elementId=1007,
     dataType="signed32", length=4},
    {name="testSigned64", enterpriseId=cert, elementId=1008,
     dataType="signed64", length=8},
    {name="testFloat32", enterpriseId=cert, elementId=1009,
     dataType="float32", length=4},
    {name="testFloat64", enterpriseId=cert, elementId=1010,
     dataType="float64", length=8},
    {name="testBoolean", enterpriseId=cert, elementId=1011,
     dataType="boolean", length=1},
    {name="testMacAddress", enterpriseId=cert, elementId=1012,
     dataType="macAddress", length=6},
    {name="testString", enterpriseId=cert, elementId=1013,
     dataType="string", length="varlen"},
    {name="testDateTimeSeconds", enterpriseId=cert, elementId=1014,
     dataType="dateTimeSeconds", length=4},
    {name="testDateTimeMilliseconds", enterpriseId=cert, elementId=1015,
     dataType="dateTimeMilliseconds", length=8},
    {name="testDateTimeMicroseconds", enterpriseId=cert, elementId=1016,
     dataType="dateTimeMicroseconds", length=8},
    {name="testDateTimeNanoseconds", enterpriseId=cert, elementId=1017,
     dataType="dateTimeNanoseconds", length=8},
    {name="testIpv4Address", enterpriseId=cert, elementId=1018,
     dataType="ipv4Address", length=4},
    {name="testIpv6Address", enterpriseId=cert, elementId=1019,
     dataType="ipv6Address", length=16},
  }

silk.infomodel_augment(new_ies)

local all_ies = {}
for i, ie in ipairs(new_ies) do
  all_ies[i] = ie.name
end

test_schema = {}
do
  local zerov6 = silk.ipaddr("::")

  function test_schema:test_schema()
    local s
    s = schema()
    assertEquals(#schema_get_fields(s), 0)
    assertEquals(#s, 0)
    assertEquals(silk.to_string(s), "")
    s = schema("protocolIdentifier");
    assertEquals(#s, 1)
    assertEquals(schema_get_fields(s)[1].name, "protocolIdentifier")
    assertEquals(s[1].name, "protocolIdentifier")
    assertEquals(silk.to_string(s), "protocolIdentifier")
    s = schema(4);
    assertEquals(#s, 1)
    assertEquals(s[1].name, "protocolIdentifier")
    s = schema("protocolIdentifier", 4)
    assertEquals(#s, 2)
    assertEquals(s[1].name, "protocolIdentifier")
    assertEquals(s[2].name, "protocolIdentifier")
    assertEquals(silk.to_string(s), "protocolIdentifier|protocolIdentifier")
    s = schema({name="testUnsigned64", length=2},
               {field = s[1]},
               {enterpriseId = cert, elementId = 1013, length=4})
    assertEquals(#s, 3)
    assertEquals(s[1].name, "testUnsigned64")
    assertEquals(s[2].name, "protocolIdentifier")
    assertEquals(s[3].name, "testString")
    assertEquals(silk.to_string(s),
                 "testUnsigned64|protocolIdentifier|testString")
    s = schema({name="testUnsigned64", length=2,
                enterpriseId = cert, elementId = 1004})
    assertEquals(#s, 1)
    assertEquals(s[1].name, "testUnsigned64")
  end

  function test_schema:test_schema_iter()
    local ies = {1, 2, 4, 5, 6, 7}
    local s = schema(table.unpack(ies))
    local i = 0
    for f in schema_iter(s) do
      i = i + 1
      assertEquals(f.elementId, ies[i])
    end
    assertEquals(i, 6)
    i = 0
    for x, f in ipairs(s) do
      i = i + 1
      assertEquals(x, i)
      assertEquals(f.elementId, ies[x])
    end
    assertEquals(i, 6)
    i = 0
    for n, f in pairs(s) do
      i = i + 1
      assertEquals(f.elementId, ies[i])
      assertEquals(n, f.name)
    end
    assertEquals(i, 6)
  end

  function test_schema:test_new_rec()
    local s1 = schema()
    local r1 = fixrec(s1)
    assertEquals(s1, fixrec_get_schema(r1))
    assertEquals(#r1, #s1)
    local r1_1 = fixrec(s1)
    assertEquals(s1, fixrec_get_schema(r1_1))
    local s2 = schema("protocolIdentifier", "testString");
    local r2 = fixrec(s2)
    assertEquals(s2, fixrec_get_schema(r2))
    assertEquals(#r2, #s2)
    local r2_2 = fixrec(s2)
    assertEquals(s2, fixrec_get_schema(r2_2))
  end

  function test_schema:test_getset_rec()
    local s = schema(table.unpack(all_ies))
    local rec = fixrec(s)
    rec[1] = "1"
    assertEquals(rec[1], "1")
    assertEquals(rec[s[1]], "1")
    assertEquals(rec["testOctetArray"], "1")
    rec[2] = 2
    assertEquals(rec[2], 2)
    assertEquals(rec[s[2]], 2)
    assertEquals(rec["testUnsigned8"], 2)
    rec[3] = 3
    assertEquals(rec[3], 3)
    assertEquals(rec[s[3]], 3)
    assertEquals(rec["testUnsigned16"], 3)
    rec[4] = 4
    assertEquals(rec[4], 4)
    assertEquals(rec[s[4]], 4)
    assertEquals(rec["testUnsigned32"], 4)
    rec[5] = 5
    assertEquals(rec[5], 5)
    assertEquals(rec[s[5]], 5)
    assertEquals(rec["testUnsigned64"], 5)
    rec[6] = 6
    assertEquals(rec[6], 6)
    assertEquals(rec[s[6]], 6)
    assertEquals(rec["testSigned8"], 6)
    rec[7] = 7
    assertEquals(rec[7], 7)
    assertEquals(rec[s[7]], 7)
    assertEquals(rec["testSigned16"], 7)
    rec[8] = 8
    assertEquals(rec[8], 8)
    assertEquals(rec[s[8]], 8)
    assertEquals(rec["testSigned32"], 8)
    rec[9] = 9
    assertEquals(rec[9], 9)
    assertEquals(rec[s[9]], 9)
    assertEquals(rec["testSigned64"], 9)
    rec[10] = 10
    assertEquals(rec[10], 10)
    assertEquals(rec[s[10]], 10)
    assertEquals(rec["testFloat32"], 10)
    rec[11] = 11
    assertEquals(rec[11], 11)
    assertEquals(rec[s[11]], 11)
    assertEquals(rec["testFloat64"], 11)
    rec[12] = false
    assertEquals(rec[12], false)
    assertEquals(rec[s[12]], false)
    assertEquals(rec["testBoolean"], false)
    rec[13] = "    13"
    assertEquals(rec[13], "    13")
    assertEquals(rec[s[13]], "    13")
    assertEquals(rec["testMacAddress"], "    13")
    rec[14] = "14"
    assertEquals(rec[14], "14")
    assertEquals(rec[s[14]], "14")
    assertEquals(rec["testString"], "14")
    local dt = datetime(15000)
    rec[15] = dt
    assertEquals(rec[15], dt)
    assertEquals(rec[s[15]], dt)
    assertEquals(rec["testDateTimeSeconds"], dt)
    dt = datetime(16000)
    rec[16] = dt
    assertEquals(rec[16], dt)
    assertEquals(rec[s[16]], dt)
    assertEquals(rec["testDateTimeMilliseconds"], dt)
    dt = datetime(17000)
    rec[17] = dt
    assertEquals(rec[17], dt)
    assertEquals(rec[s[17]], dt)
    assertEquals(rec["testDateTimeMicroseconds"], dt)
    dt = datetime(18000)
    rec[18] = dt
    assertEquals(rec[18], dt)
    assertEquals(rec[s[18]], dt)
    assertEquals(rec["testDateTimeNanoseconds"], dt)
    rec[19] = silk.ipaddr("19.19.19.19")
    assertEquals(rec[19], silk.ipaddr("19.19.19.19"))
    assertEquals(rec[s[19]], silk.ipaddr("19.19.19.19"))
    assertEquals(rec["testIpv4Address"], silk.ipaddr("19.19.19.19"))
    rec[19] = silk.ipaddr("::ffff:19.19.19.19")
    assertEquals(rec[19], silk.ipaddr("19.19.19.19"))
    assertEquals(rec[s[19]], silk.ipaddr("19.19.19.19"))
    assertEquals(rec["testIpv4Address"], silk.ipaddr("19.19.19.19"))
    rec[20] = silk.ipaddr("20::20")
    assertEquals(rec[20], silk.ipaddr("20::20"))
    assertEquals(rec[s[20]], silk.ipaddr("20::20"))
    assertEquals(rec["testIpv6Address"], silk.ipaddr("20::20"))
  end

  function test_schema:test_rec_iter()
    local ies = {1, 2, 4}
    local s = schema(table.unpack(ies))
    local r = fixrec(s)
    for i, f in ipairs(s) do
      r[f] = i
    end
    for i, v in ipairs(r) do
      assertEquals(i, v)
    end
    local i = 0
    for n, v in pairs(r) do
      i = i + 1
      assertEquals(n, s[i].name)
      assertEquals(v, i)
    end
  end

  function test_schema:test_copy_rec()
    local x = schema(1,2)
    local y = schema(2,4)
    local a = fixrec(x)
    local b = fixrec(y)
    a[1] = 1
    a[2] = 2
    b[1] = 3
    b[2] = 4
    -- copy with no second arg
    local c = fixrec_copy(b)
    assertEquals((c == b), false)
    assertEquals(c[1], 3)
    assertEquals(c[2], 4)
    -- copy into different schema, 1 common field
    local d = fixrec_copy(a, y)
    assertEquals((a == d), false)
    assertEquals(fixrec_get_schema(d), y)
    assertEquals(d[1], 2)
    d[1] = 0
    d[2] = 0
    -- copy into destination fixrec, 1 common field
    d = fixrec_copy(a, b)
    assertEquals(d, b)
    assertEquals(a[2], b[1])
    assertEquals(b[1], 2)
    assertEquals(b[2], 4)
    -- copy into destination fixrec, 1 common field
    d = fixrec_copy(c, a)
    assertEquals(d, a)
    assertEquals(a[2], c[1])
    assertEquals(a[1], 1)
    assertEquals(a[2], 3)
    -- copy with own schema as second arg
    d = fixrec_copy(b, y)
    assertEquals((d == b), false)
    assertEquals(fixrec_get_schema(d), y)
    assertEquals(d[1], 2)
    assertEquals(d[2], 4)
    -- copy with itself as second arg
    d = fixrec_copy(a, a)
    assertEquals(d, a)
    assertEquals(fixrec_get_schema(d), x)
    assertEquals(a[1], 1)
    assertEquals(a[2], 3)
    -- schema with same fields as y
    local v = schema(2, 4)
    assertEquals((v == y), false)
    -- copy into schema with same fields
    d = fixrec_copy(b, v)
    assertEquals((d == b), false)
    assertEquals(fixrec_get_schema(d), v)
    assertEquals(d[1], 2)
    assertEquals(d[2], 4)
    -- schema with no fields in common with others
    local u = schema(7)
    local e = fixrec(u)
    e[1] = 5
    -- copy with no second argument
    d = fixrec_copy(e)
    assertEquals((d == e), false)
    assertEquals(fixrec_get_schema(d), u)
    assertEquals(d[1], 5)
    assertEquals(d[2], nil)
    -- copy into schema with no common fields
    d = fixrec_copy(a, u)
    assertEquals((d == a), false)
    assertEquals(fixrec_get_schema(d), u)
    assertEquals(d[1], 0)
    d[1] = 9
    -- copy into schema with no common fields
    d = fixrec_copy(e, x)
    assertEquals((d == e), false)
    assertEquals(fixrec_get_schema(d), x)
    assertEquals(d[1], 0)
    assertEquals(d[2], 0)
    d[1] = 9
    d[2] = 9
    -- copy into fixrec with no common fields
    d = fixrec_copy(a, e)
    assertEquals(d, e)
    assertEquals(fixrec_get_schema(d), u)
    assertEquals(e[1], 5)
    assertEquals(e[2], nil)
    -- copy into fixrec with no common fields
    d = fixrec_copy(e, b)
    assertEquals(d, b)
    assertEquals(fixrec_get_schema(d), y)
    assertEquals(b[1], 2)
    assertEquals(b[2], 4)
  end

  function test_schema:test_getset_fail_rec()
    local s = schema(table.unpack(all_ies))
    local rec = fixrec(s)
    local function set(x, y) rec[x] = y end
    local b = true
    local n = 100
    local s = "test"
    local d = datetime(0)
    local a4 = silk.ipaddr("1.2.3.4")
    local a6 = silk.ipaddr("::1.2.3.4")
    assertError(set, "testOctetArray", b)
    assertError(set, "testOctetArray", a4)
    assertError(set, "testOctetArray", d)
    assertError(set, "testUnsigned8", b)
    assertError(set, "testUnsigned8", a4)
    assertError(set, "testUnsigned8", s)
    assertError(set, "testUnsigned8", d)
    assertError(set, "testUnsigned16", b)
    assertError(set, "testUnsigned16", a4)
    assertError(set, "testUnsigned16", s)
    assertError(set, "testUnsigned16", d)
    assertError(set, "testUnsigned32", b)
    assertError(set, "testUnsigned32", a4)
    assertError(set, "testUnsigned32", s)
    assertError(set, "testUnsigned32", d)
    assertError(set, "testUnsigned64", b)
    assertError(set, "testUnsigned64", a4)
    assertError(set, "testUnsigned64", s)
    assertError(set, "testUnsigned64", d)
    assertError(set, "testSigned8", b)
    assertError(set, "testSigned8", a4)
    assertError(set, "testSigned8", s)
    assertError(set, "testSigned8", d)
    assertError(set, "testSigned16", b)
    assertError(set, "testSigned16", a4)
    assertError(set, "testSigned16", s)
    assertError(set, "testSigned16", d)
    assertError(set, "testSigned32", b)
    assertError(set, "testSigned32", a4)
    assertError(set, "testSigned32", s)
    assertError(set, "testSigned32", d)
    assertError(set, "testSigned64", b)
    assertError(set, "testSigned64", a4)
    assertError(set, "testSigned64", s)
    assertError(set, "testSigned64", d)
    assertError(set, "testFloat32", b)
    assertError(set, "testFloat32", s)
    assertError(set, "testFloat32", d)
    assertError(set, "testFloat32", a4)
    assertError(set, "testFloat64", b)
    assertError(set, "testFloat64", s)
    assertError(set, "testFloat64", d)
    assertError(set, "testFloat64", a4)
    assertError(set, "testMacAddress", b)
    assertError(set, "testMacAddress", n)
    assertError(set, "testMacAddress", s)
    assertError(set, "testMacAddress", d)
    assertError(set, "testMacAddress", a4)
    assertError(set, "testString", b)
    assertError(set, "testString", a4)
    assertError(set, "testString", d)
    assertError(set, "testDateTimeSeconds", b)
    assertError(set, "testDateTimeSeconds", a4)
    assertError(set, "testDateTimeSeconds", s)
    assertError(set, "testDateTimeSeconds", n)
    assertError(set, "testDateTimeMilliseconds", b)
    assertError(set, "testDateTimeMilliseconds", a4)
    assertError(set, "testDateTimeMilliseconds", s)
    assertError(set, "testDateTimeMilliseconds", n)
    assertError(set, "testDateTimeMicroseconds", b)
    assertError(set, "testDateTimeMicroseconds", a4)
    assertError(set, "testDateTimeMicroseconds", s)
    assertError(set, "testDateTimeMicroseconds", n)
    assertError(set, "testDateTimeNanoseconds", b)
    assertError(set, "testDateTimeNanoseconds", a4)
    assertError(set, "testDateTimeNanoseconds", s)
    assertError(set, "testDateTimeNanoseconds", n)
    assertError(set, "testIpv4Address", b)
    assertError(set, "testIpv4Address", n)
    assertError(set, "testIpv4Address", s)
    assertError(set, "testIpv4Address", d)
    assertError(set, "testIpv4Address", a6)
    assertError(set, "testIpv6Address", b)
    assertError(set, "testIpv6Address", n)
    assertError(set, "testIpv6Address", s)
    assertError(set, "testIpv6Address", d)
  end

  function test_schema:initial_setup()
    local schema_a = silk.schema("sourceIPv4Address", "protocolIdentifier")
    local schema_b = silk.schema("sourceIPv6Address", "protocolIdentifier")

    local output_a = silk.stream_open_writer(loc_a, "ipfix")
    local output_b = silk.stream_open_writer(loc_b, "ipfix")
    local output_c = silk.stream_open_writer(loc_c, "ipfix")

    local inputs = {testfiles.empty, testfiles.data, testfiles.empty,
                    testfiles.datav6, testfiles.empty}

    self.counts = {output_a = 0; output_b = 0; output_c = 0}
    self.count = 0

    for _, fn in ipairs(inputs) do
      f = silk.stream_open_reader(fn, "ipfix")
      for rec in silk.stream_iter(f) do
        self.count = self.count + 1
        if rec.sourceIPv6Address == zerov6 then
          silk.stream_write(output_a, rec, schema_a)
          self.counts.output_a = self.counts.output_a + 1
        else
          silk.stream_write(output_b, rec, schema_b)
          self.counts.output_b = self.counts.output_b + 1
        end
        silk.stream_write(output_c, rec)
        self.counts.output_c = self.counts.output_c + 1
      end
    end
    silk.stream_close(output_a)
    silk.stream_close(output_b)
    silk.stream_close(output_c)
  end

  local function multi_file_iter (...)
    local files = {...}
    local i = 1
    local cfile
    if files[1] then
      cfile = silk.stream_open_reader(files[1], "ipfix")
    else
      return function () return nil end
    end
    local function it()
      local rec = silk.stream_read(cfile)
      if rec == nil then
        i = i + 1
        if files[i] then
          cfile = silk.stream_open_reader(files[i], "ipfix")
        else
          return nil
        end
        return it()
      end
      return rec
    end
    return it
  end

  function test_schema:test_readback()
    local counts = {a = 0; b = 0; c = 0}
    local fx, sx, vx = multi_file_iter(testfiles.data, testfiles.datav6)
    local ia = silk.stream_open_reader(loc_a, "ipfix")
    local fa, sa, va = silk.stream_iter(ia)
    local ib = silk.stream_open_reader(loc_b, "ipfix")
    local fb, sb, vb = silk.stream_iter(ib)
    local ic = silk.stream_open_reader(loc_c, "ipfix")
    local fc, sc, vc = silk.stream_iter(ic)

    vx = fx(sx, vx)
    va = fa(sa, va)
    vb = fb(sb, vb)
    vc = fc(sc, vc)
    while (vx) do
      if vx.sourceIPv6Address == zerov6 then
        assertEquals(vx.sourceIPv4Address, va.sourceIPv4Address)
        assertEquals(vx.protocolIdentifier, va.protocolIdentifier)
        counts.a = counts.a + 1
        va = fa(sa, va)
      else
        assertEquals(vx.sourceIPv6Address, vb.sourceIPv6Address)
        assertEquals(vx.protocolIdentifier, vb.protocolIdentifier)
        counts.b = counts.b + 1
        vb = fb(sb, vb)
      end
      assertEquals(vx.sourceIPv4Address, vc.sourceIPv4Address)
      assertEquals(vx.sourceIPv6Address, vc.sourceIPv6Address)
      assertEquals(vx.protocolIdentifier, vc.protocolIdentifier)
      vx = fx(sx, vx)
      vc = fc(sc, vc)
      counts.c = counts.c + 1
    end
    assertEquals(va, nil)
    assertEquals(vb, nil)
    assertEquals(vc, nil)

    assertEquals(counts.a, self.counts.output_a)
    assertEquals(counts.b, self.counts.output_b)
    assertEquals(counts.c, self.counts.output_c)
    assertEquals(counts.c, counts.a + counts.b)
  end

  function test_schema:test_multi()
    local counts = {a = 0; b = 0; c = 0}
    local ia = silk.stream_open_reader(loc_a, "ipfix")
    local fa, sa, va = silk.stream_iter(ia)
    local ib = silk.stream_open_reader(loc_b, "ipfix")
    local fb, sb, vb = silk.stream_iter(ib)
    local output_d = silk.stream_open_writer(loc_d, "ipfix")
    va = fa(sa, va)
    vb = fb(sb, vb)
    while va or vb do
      if va then
        silk.stream_write(output_d, va)
        va = fa(sa, va)
      end
      if vb then
        silk.stream_write(output_d, vb)
        vb = fb(sb, vb)
      end
    end
    silk.stream_close(output_d)
    ia = silk.stream_open_reader(loc_a, "ipfix")
    fa, sa, va = silk.stream_iter(ia)
    ib = silk.stream_open_reader(loc_b, "ipfix")
    fb, sb, vb = silk.stream_iter(ib)
    local id = silk.stream_open_reader(loc_d, "ipfix")
    va = fa(sa, va)
    vb = fb(sb, vb)
    for rec in silk.stream_iter(id) do
      if silk.fixrec_get_schema(rec)["sourceIPv6Address"] then
        assertEquals(rec.sourceIPv6Address, vb.sourceIPv6Address)
        assertEquals(rec.protocolIdentifier, vb.protocolIdentifier)
        counts.b = counts.b + 1
        vb = fb(sb, vb)
      else
        assertEquals(rec.sourceIPv4Address, va.sourceIPv4Address)
        assertEquals(rec.protocolIdentifier, va.protocolIdentifier)
        counts.a = counts.a + 1
        va = fa(sa, va)
      end
      counts.c = counts.c + 1
    end
    assertEquals(va, nil)
    assertEquals(vb, nil)
    assertEquals(counts.a, self.counts.output_a)
    assertEquals(counts.b, self.counts.output_b)
    assertEquals(counts.c, self.counts.output_c)
  end

  function test_schema:test_empty()
    local input = silk.stream_open_reader(testfiles.empty, "ipfix")
    for rec in silk.stream_iter(input) do
      assert(false)
    end
    local output = silk.stream_open_writer(loc_e, "ipfix")
    silk.stream_close(output)
    input = silk.stream_open_reader(loc_e, "ipfix")
    for rec in silk.stream_iter(input) do
      assert(false)
    end
  end
end

test_schema:initial_setup()
local failures = LuaUnit:run("test_schema")
os.exit(0 == failures, true)

-- Local Variables:
-- mode:lua
-- indent-tabs-mode:nil
-- lua-indent-level:2
-- End:
