-- For test simplicity, put the silk functions into the current
-- environment (except for type)
for k, v in pairs(silk) do
  if k ~= 'type' then
    _ENV[k] = v
  end
end

local LuaUnit = require 'luaunit'

test_ipaddr = {}
do
  -- Test ipaddr construction
  function test_ipaddr:test_construction ()
    local a = ipaddr("0.0.0.0")
    assertEquals(silk.is_ipaddr(a), true)
    assertEquals(silk.type(a), 'ipaddr')
    local b = ipv4addr("0.0.0.0")
    assertEquals(a == b, true)
    b = ipaddr("0")
    assertEquals(a == b, true)
    b = ipaddr(a)
    assertEquals(a == b, true)
    b = ipv4addr(a)
    assertEquals(a == b, true)
    b = ipv6addr(a)
    assertEquals(a == b, true)
    b = ipv4addr("0")
    assertEquals(a == b, true)
    b = ipv4addr(0)
    assertEquals(a == b, true)
    b = ipaddr("::ffff:0.0.0.0")
    assertEquals(a == b, true)
    b = ipv6addr("::ffff:0.0.0.0")
    assertEquals(a == b, true)
    a = ipaddr("255.255.255.255")
    assertEquals(silk.is_ipaddr(a), true)
    assertEquals(silk.type(a), 'ipaddr')
    b = ipv4addr("255.255.255.255")
    assertEquals(a == b, true)
    b = ipv4addr("4294967295")
    assertEquals(a == b, true)
    b = ipv4addr(0xffffffff)
    assertEquals(a == b, true)
    b = ipv6addr("255.255.255.255")
    assertEquals(a == b, true)
    b = ipv6addr("4294967295")
    assertEquals(a == b, true)
    b = ipaddr("::ffff:ffff:ffff")
    assertEquals(a == b, true)
    b = ipv6addr("::ffff:ffff:ffff")
    assertEquals(a == b, true)
    b = ipv4addr(a)
    assertEquals(a == b, true)
    b = ipv6addr(a)
    assertEquals(a == b, true)
    b = ipaddr(a)
    assertEquals(a == b, true)
    assertError(ipaddr)
    assertError(ipaddr, "")
    assertError(ipaddr, "0.0.0.256")
    assertError(ipaddr, 0)
    assertError(ipv6addr, 0)
    assertError(ipv4addr, -1)
    assertError(ipv4addr, 0x100000000)
    ipv6addr("2001:db8:10:11::12:13")
    assertError(ipv4addr, "2001:db8:10:11::12:13")
  end -- function test_ipaddr:test_construction

  -- Test converting ipaddrs to strings
  function test_ipaddr:test_tostring ()
    assertEquals(tostring(ipaddr("0.0.0.0")), "0.0.0.0")
    assertEquals(tostring(ipaddr("255.255.255.255")), "255.255.255.255")
    assertEquals(tostring(ipaddr("::")), "::")
    assertEquals(tostring(ipaddr("0:0:0:0:0:0:0:0")), "::")
    assertEquals(tostring(ipaddr("10:0:0:0:0:0:0:0")), "10::")
    --assertEquals(tostring(ipaddr("0:0:0:0:0:0:0:10")), "::10")
    assertEquals(tostring(ipaddr("10:0:0:0:0:0:0:10")), "10::10")
    assertEquals(tostring(ipaddr("::ffff:ffff:ffff")),
                 "::ffff:255.255.255.255")
    assertEquals(ipaddr_to_string(ipaddr("0.0.0.0")), "0.0.0.0")
    assertEquals(ipaddr_to_string(ipaddr("0.0.0.0"), 'canonical'), "0.0.0.0")
    assertEquals(ipaddr_to_string(ipaddr("0.0.0.0"), 'zero-padded'),
                 "000.000.000.000")
    assertEquals(ipaddr_to_string(ipaddr("0.0.0.0"), 'decimal'), "0")
    assertEquals(ipaddr_to_string(ipaddr("0.0.0.0"), 'hexadecimal'), "0")
    assertEquals(ipaddr_to_string(ipaddr("0.0.0.0"), 'force-ipv6'),
                 "::ffff:0:0")
    assertEquals(ipaddr_to_string(ipaddr("255.255.255.255")),
                 "255.255.255.255")
    assertEquals(ipaddr_to_string(ipaddr("255.255.255.255"), 'canonical'),
                 "255.255.255.255")
    assertEquals(ipaddr_to_string(ipaddr("255.255.255.255"), 'zero-padded'),
                 "255.255.255.255")
    assertEquals(ipaddr_to_string(ipaddr("255.255.255.255"), 'decimal'),
                 "4294967295")
    assertEquals(ipaddr_to_string(ipaddr("255.255.255.255"), 'hexadecimal'),
                 "ffffffff")
    assertEquals(ipaddr_to_string(ipaddr("255.255.255.255"), 'force-ipv6'),
                 "::ffff:ffff:ffff")
    assertEquals(ipaddr_to_string(ipaddr("0.1.10.100")), "0.1.10.100")
    assertEquals(ipaddr_to_string(ipaddr("0.1.10.100"), 'canonical'),
                 "0.1.10.100")
    assertEquals(ipaddr_to_string(ipaddr("0.1.10.100"), 'zero-padded'),
                 "000.001.010.100")
    assertEquals(ipaddr_to_string(ipaddr("0.1.10.100"), 'decimal'), "68196")
    assertEquals(ipaddr_to_string(ipaddr("0.1.10.100"), 'hexadecimal'),
                 "10a64")
    assertEquals(ipaddr_to_string(ipaddr("0.1.10.100"), 'force-ipv6'),
                 "::ffff:1:a64")
    assertEquals(ipaddr_to_string(ipaddr("::")), "::")
    assertEquals(ipaddr_to_string(ipaddr("::"), 'canonical'), "::")
    assertEquals(ipaddr_to_string(ipaddr("::"), 'zero-padded'),
                 "0000:0000:0000:0000:0000:0000:0000:0000")
    assertEquals(ipaddr_to_string(ipaddr("::"), 'decimal'), "0")
    assertEquals(ipaddr_to_string(ipaddr("::"), 'hexadecimal'), "0")
    assertEquals(ipaddr_to_string(ipaddr("::"), 'force-ipv6'), "::")
    assertEquals(ipaddr_to_string(ipaddr("::ffff:ffff:ffff")),
                 "::ffff:255.255.255.255")
    assertEquals(ipaddr_to_string(ipaddr("::ffff:ffff:ffff"), 'canonical'),
                 "::ffff:255.255.255.255")
    assertEquals(ipaddr_to_string(ipaddr("::ffff:ffff:ffff"), 'zero-padded'),
                 "0000:0000:0000:0000:0000:ffff:ffff:ffff")
    assertEquals(ipaddr_to_string(ipaddr("::ffff:ffff:ffff"), 'decimal'),
                 "281474976710655")
    assertEquals(ipaddr_to_string(ipaddr("::ffff:ffff:ffff"), 'hexadecimal'),
                 "ffffffffffff")
    assertEquals(ipaddr_to_string(ipaddr("::ffff:ffff:ffff"), 'force-ipv6'),
                 "::ffff:ffff:ffff")
    assertEquals(ipaddr_to_string(
                   ipaddr("ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff")),
                 "ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff")
    assertEquals(ipaddr_to_string(
                   ipaddr("ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff"),
                   'canonical'),
                 "ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff")
    assertEquals(ipaddr_to_string(
                   ipaddr("ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff"),
                   'zero-padded'),
                 "ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff")
    assertEquals(ipaddr_to_string(
                   ipaddr("ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff"),
                   'decimal'),
                 "340282366920938463463374607431768211455")
    assertEquals(ipaddr_to_string(
                   ipaddr("ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff"),
                   'hexadecimal'),
                 "ffffffffffffffffffffffffffffffff")
    assertEquals(ipaddr_to_string(
                   ipaddr("ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff"),
                   'force-ipv6'),
                 "ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff")
    assertEquals(ipaddr_to_string(ipaddr("1000:100:10:1:0:0:ffff:f")),
                 "1000:100:10:1::ffff:f")
    assertEquals(ipaddr_to_string(ipaddr("1000:100:10:1:0:0:ffff:f"),
                                  'canonical'),
                 "1000:100:10:1::ffff:f")
    assertEquals(ipaddr_to_string(ipaddr("1000:100:10:1:0:0:ffff:f"),
                                  'zero-padded'),
                 "1000:0100:0010:0001:0000:0000:ffff:000f")
    assertEquals(ipaddr_to_string(ipaddr("1000:100:10:1:0:0:ffff:f"),
                                  'decimal'),
                 "21267668214987600449691915056536551439")
    assertEquals(ipaddr_to_string(ipaddr("1000:100:10:1:0:0:ffff:f"),
                                  'hexadecimal'),
                 "100001000010000100000000ffff000f")
    assertEquals(ipaddr_to_string(ipaddr("1000:100:10:1:0:0:ffff:f"),
                                  'force-ipv6'),
                 "1000:100:10:1::ffff:f")
    assertEquals(ipaddr_to_string(ipaddr("0:0:0:0:0:0:0:0")), "::")
    assertEquals(ipaddr_to_string(ipaddr("10:0:0:0:0:0:0:0")), "10::")
    --assertEquals(ipaddr_to_string(ipaddr("0:0:0:0:0:0:0:10")), "::10")
    assertEquals(ipaddr_to_string(ipaddr("10:0:0:0:0:0:0:10")), "10::10")
    assertEquals(ipaddr_to_string(ipaddr("10.0.0.0"), 'zero-padded'),
                 "010.000.000.000")
    assertEquals(ipaddr_to_string(ipaddr("10.10.10.10"), 'zero-padded'),
                 "010.010.010.010")
    assertEquals(ipaddr_to_string(ipaddr("10.11.12.13"), 'zero-padded'),
                 "010.011.012.013")
    assertEquals(ipaddr_to_string(ipaddr("0:0:0:0:0:0:0:0"), 'zero-padded'),
                 "0000:0000:0000:0000:0000:0000:0000:0000")
    assertEquals(ipaddr_to_string(ipaddr("10:0:0:0:0:0:0:0"), 'zero-padded'),
                 "0010:0000:0000:0000:0000:0000:0000:0000")
    assertEquals(ipaddr_to_string(ipaddr("10:10:10:10:10:10:10:10"),
                                  'zero-padded'),
                 "0010:0010:0010:0010:0010:0010:0010:0010")
    assertEquals(ipaddr_to_string(
                   ipaddr("1010:1010:1010:1010:1010:1010:1010:1010"),
                   'zero-padded'),
                 "1010:1010:1010:1010:1010:1010:1010:1010")
    assertEquals(ipaddr_to_string(
                   ipaddr("1011:1213:1415:1617:2021:2223:2425:2627"),
                   'zero-padded'),
                 "1011:1213:1415:1617:2021:2223:2425:2627")
    assertEquals(ipaddr_to_string(
                   ipaddr("f0ff:f2f3:f4f5:f6f7:202f:2223:2425:2627"),
                   'zero-padded'),
                 "f0ff:f2f3:f4f5:f6f7:202f:2223:2425:2627")
    assertEquals(ipaddr_to_string(
                   ipaddr("f0ff:faf3:f4f5:f6f7:a0af:aaa3:a4a5:a6a7"),
                   'zero-padded'),
                 "f0ff:faf3:f4f5:f6f7:a0af:aaa3:a4a5:a6a7")
    assertEquals(ipaddr_to_string(ipaddr("1234::5678"), 'zero-padded'),
                 "1234:0000:0000:0000:0000:0000:0000:5678")
  end -- function test_ipaddr:test_tostring

  -- Test converting ipaddrs to integers
  function test_ipaddr:test_to_int ()
    assertEquals(ipaddr_to_int(ipaddr("0.0.0.0")), 0)
    assertEquals(ipaddr_to_int(ipaddr("255.255.255.255")), 4294967295)
    assertEquals(ipaddr_to_int(ipaddr("10.0.0.0")), 167772160)
    assertEquals(ipaddr_to_int(ipaddr("10.10.10.10")), 168430090)
    assertEquals(ipaddr_to_int(ipaddr("10.11.12.13")), 168496141)
    assertEquals(ipaddr_to_int(ipaddr(" 10.0.0.0")), 167772160)
    assertEquals(ipaddr_to_int(ipaddr("10.0.0.0 ")), 167772160)
    assertEquals(ipaddr_to_int(ipaddr("  10.0.0.0  ")), 167772160)
    assertEquals(ipaddr_to_int(ipaddr("010.000.000.000")), 167772160)
    assertEquals(ipaddr_to_int(ipaddr("4294967295")), 4294967295)
    assertEquals(ipaddr_to_int(ipaddr("167772160")), 167772160)
    assertEquals(ipaddr_to_int(ipaddr("168430090")), 168430090)
    assertEquals(ipaddr_to_int(ipaddr("168496141")), 168496141)
    assertEquals(ipaddr_to_int(ipaddr("167772160")), 167772160)
    assertEquals(ipaddr_to_int(ipaddr("0:0:0:0:0:0:0:0")), 0)
    assertEquals(ipaddr_to_int(
                   ipaddr("ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff")),
                 0xffffffffffffffffffffffffffffffff.0)
    assertEquals(ipaddr_to_int(ipaddr("10:0:0:0:0:0:0:0")),
                 0x00100000000000000000000000000000.0)
    assertEquals(ipaddr_to_int(ipaddr("10:10:10:10:10:10:10:10")),
                 0x00100010001000100010001000100010.0)
    assertEquals(ipaddr_to_int(
                   ipaddr("1010:1010:1010:1010:1010:1010:1010:1010")),
                 0x10101010101010101010101010101010.0)
    assertEquals(ipaddr_to_int(
                   ipaddr("1011:1213:1415:1617:2021:2223:2425:2627")),
                 0x10111213141516172021222324252627.0)
    assertEquals(ipaddr_to_int(
                   ipaddr("f0ff:f2f3:f4f5:f6f7:202f:2223:2425:2627")),
                 0xf0fff2f3f4f5f6f7202f222324252627.0)
    assertEquals(ipaddr_to_int(
                   ipaddr("f0ff:faf3:f4f5:f6f7:a0af:aaa3:a4a5:a6a7")),
                 0xf0fffaf3f4f5f6f7a0afaaa3a4a5a6a7.0)
  end -- function test_ipaddr:test_to_int

  -- Test inequalities on ipaddrs
  function test_ipaddr:test_ordering ()
    local a = ipv4addr(0)
    local b = ipv4addr(0)
    assertEquals(a == b, true)
    assertEquals(a <= b, true)
    assertEquals(a >= b, true)
    local c = ipv4addr(256)
    assertEquals(a < c, true)
    assertEquals(a <= c, true)
    assertEquals(c > a, true)
    assertEquals(c >= a, true)
    assertEquals(a ~= c, true)
    assertEquals(c ~= a, true)
    assertEquals(c == ipaddr("0.0.1.0"), true)
    local d = ipv4addr(0xffffffff)
    assertEquals(d == ipaddr("255.255.255.255"), true)
    local e = ipaddr("ffff::")
    assertEquals(d < e, true)
    assertEquals(d <= e, true)
    local f = ipaddr("::255.255.255.255")
    assertEquals(d ~= f, true)
    local g = ipaddr("::ffff:0.0.0.0")
    assertEquals(a == g, true)
    local h = ipaddr("::ffff:0.0.0.1")
    assertEquals(a < h, true)
    assertEquals(a <= h, true)
  end -- function test_ipaddr:test_ordering

  function test_ipaddr:test_is_ipv6 ()
    assertEquals(ipaddr_is_ipv6(ipaddr("::")), true)
    assertEquals(ipaddr_is_ipv6(ipaddr("::ffff:0.0.0.1")), true)
    assertEquals(ipaddr_is_ipv6(ipaddr("0.0.0.0")), false)
    assertEquals(ipaddr_is_ipv6(ipaddr("0.0.0.1")), false)
  end -- function test_ipaddr:test_is_ipv6

  function test_ipaddr:test_convert ()
    local a = ipaddr("0.0.0.0")
    assertEquals(a, ipaddr(a))
    assertEquals(a, ipaddr_to_ipv4(a))
    local b = ipaddr("::")
    local c = ipaddr("::ffff:0.0.0.0")
    assertEquals(b, ipaddr(b))
    assertEquals(c, ipaddr(c))
    assertEquals(c, ipaddr_to_ipv6(a))
    assertEquals(a, ipaddr_to_ipv4(c))
    assert_nil(ipaddr_to_ipv4(b))
  end -- function test_ipaddr:test_convert

  function test_ipaddr:test_octets ()
    local a = ipaddr_octets(ipaddr("10.11.12.13"))
    local b = {10, 11, 12, 13}
    assertEquals(#a == 4, true)
    for i = 1, 4 do
      assertEquals(a[i] == b[i], true)
    end
    a = ipaddr_octets(ipaddr("2001:db8:10:11::12:13"))
    b = {0x20, 0x01, 0x0d, 0xb8, 0x00, 0x10, 0x00, 0x11,
         0x00, 0x00, 0x00, 0x00, 0x00, 0x12, 0x00, 0x13}
    assertEquals(#a == 16, true)
    for i = 1, 4 do
      assertEquals(a[i] == b[i], true)
    end
  end -- function test_ipaddr:test_octets

  function test_ipaddr:test_mask ()
    local a = ipaddr("10.11.12.13")
    assertEquals(a, ipaddr_mask_prefix(a, 32))
    assertEquals(ipv4addr(0), ipaddr_mask_prefix(a, 0))
    assertError(ipaddr_mask_prefix, a, 33)
    local b = ipaddr("0.0.0.0")
    assertEquals(b, ipaddr_mask(a, b))
    assertEquals(b, ipaddr_mask(b, a))
    assertEquals(b, ipaddr_mask(b, b))
    b = ipaddr("255.255.255.0")
    local c = ipaddr("10.11.12.0")
    assertEquals(c, ipaddr_mask(a, b))
    assertEquals(c, ipaddr_mask_prefix(a, 24))
    assertEquals(c, ipaddr_mask(c, b))
    assertEquals(c, ipaddr_mask_prefix(c, 24))
    b = ipaddr("255.255.0.0")
    c = ipaddr("10.11.0.0")
    assertEquals(c, ipaddr_mask(a, b))
    assertEquals(c, ipaddr_mask_prefix(a, 16))
    assertEquals(c, ipaddr_mask(c, b))
    assertEquals(c, ipaddr_mask_prefix(c, 16))
    a = ipaddr("2001:db8:10:11::12:13")
    assertEquals(a, ipaddr_mask_prefix(a, 128))
    assertEquals(ipaddr("::"), ipaddr_mask_prefix(a, 0))
    assertError(ipaddr_mask_prefix, a, 129)
    b = ipaddr("::")
    assertEquals(b, ipaddr_mask(a, b))
    assertEquals(b, ipaddr_mask(b, a))
    assertEquals(b, ipaddr_mask(b, b))
    b = ipaddr("ffff:ffff:ffff:ffff:ffff:ffff:ffff:0")
    c = ipaddr("2001:db8:10:11::12:0")
    assertEquals(c, ipaddr_mask(a, b))
    assertEquals(c, ipaddr_mask_prefix(a, 112))
    assertEquals(c, ipaddr_mask(c, b))
    assertEquals(c, ipaddr_mask_prefix(c, 112))
    b = ipaddr("ffff:ffff:ffff:ffff::")
    c = ipaddr("2001:db8:10:11::")
    assertEquals(c, ipaddr_mask(a, b))
    assertEquals(c, ipaddr_mask_prefix(a, 64))
    assertEquals(c, ipaddr_mask(c, b))
    assertEquals(c, ipaddr_mask_prefix(c, 64))
    -- Mixed IPv4 and IPv6
    a = ipaddr("::FFFF:10.11.12.13")
    assertEquals(a, ipaddr_mask_prefix(a, 128))
    b = ipaddr("::FFFF:0.0.0.0")
    assertEquals(b, ipaddr_mask(a, b))
    assertEquals(b, ipaddr_mask(b, a))
    assertEquals(b, ipaddr_mask(b, b))
    b = ipaddr("255.255.255.0")
    c = ipaddr("::FFFF:10.11.12.0")
    assertEquals(c, ipaddr_mask(a, b))
    assertEquals(c, ipaddr_mask_prefix(a, 120))
    assertEquals(c, ipaddr_mask(c, b))
    assertEquals(c, ipaddr_mask_prefix(c, 120))
    b = ipaddr("255.255.0.0")
    c = ipaddr("::FFFF:10.11.0.0")
    assertEquals(c, ipaddr_mask(a, b))
    assertEquals(c, ipaddr_mask_prefix(a, 112))
    assertEquals(c, ipaddr_mask(c, b))
    assertEquals(c, ipaddr_mask_prefix(c, 112))
  end -- function test_ipaddr:test_mask
end -- test_ipaddr

test_ipwildcard = {}
do
  function test_ipwildcard:test_construction()
    local a = ipwildcard("0.0.0.0")
    assertEquals(is_ipwildcard(a), true)
    a = ipwildcard("0.0.0.0/31")
    assertEquals(is_ipwildcard(a), true)
    a = ipwildcard("255.255.255.254-255")
    assertEquals(is_ipwildcard(a), true)
    a = ipwildcard("3,2,1.4.5.6")
    assertEquals(is_ipwildcard(a), true)
    a = ipwildcard("0.0.0.1,31,51,71,91,101,121,141,161,181,211,231,251")
    assertEquals(is_ipwildcard(a), true)
    a = ipwildcard("0,255.0,255.0,255.0,255")
    assertEquals(is_ipwildcard(a), true)
    a = ipwildcard("1.1.128.0/22")
    assertEquals(is_ipwildcard(a), true)
    a = ipwildcard("128.x.0.0")
    assertEquals(is_ipwildcard(a), true)
    a = ipwildcard("128.0-255.0.0")
    assertEquals(is_ipwildcard(a), true)
    a = ipwildcard("128.0,128-255,1-127.0.0")
    assertEquals(is_ipwildcard(a), true)
    a = ipwildcard("128.0,128,129-253,255-255,254,1-127.0.0")
    assertEquals(is_ipwildcard(a), true)
    a = ipwildcard(ipwildcard("::"))
    assertEquals(is_ipwildcard(a), true)
    assertError(ipwildcard, ":::")
    assertError(ipwildcard, 0)
  end -- function test_ipwildcard:test_construction

  function test_ipwildcard:test_containment()
    local wild = ipwildcard("0.0.0.0")
    assertEquals(wild[ipaddr("0.0.0.0")], true)
    assertEquals(wild[ipaddr("0.0.0.1")], false)
    assertEquals(wild["0.0.0.0"], true)
    assertEquals(wild["0.0.0.1"], false)
    assertEquals(ipwildcard_contains(wild, ipaddr("0.0.0.0")), true)
    assertEquals(ipwildcard_contains(wild, ipaddr("0.0.0.1")), false)
    assertEquals(ipwildcard_contains(wild, "0.0.0.0"), true)
    assertEquals(ipwildcard_contains(wild, "0.0.0.1"), false)
    wild = ipwildcard("0.0.0.0/31")
    assertEquals(wild[ipaddr("0.0.0.0")], true)
    assertEquals(wild[ipaddr("0.0.0.1")], true)
    assertEquals(wild[ipaddr("0.0.0.2")], false)
    assertEquals(wild["0.0.0.0"], true)
    assertEquals(wild["0.0.0.1"], true)
    assertEquals(wild["0.0.0.2"], false)
    wild = ipwildcard("255.255.255.254-255")
    assertEquals(wild[ipaddr("255.255.255.254")], true)
    assertEquals(wild[ipaddr("255.255.255.255")], true)
    assertEquals(wild[ipaddr("255.255.255.253")], false)
    assertEquals(wild["255.255.255.254"], true)
    assertEquals(wild["255.255.255.255"], true)
    assertEquals(wild["255.255.255.253"], false)
    wild = ipwildcard("3,2,1.4.5.6")
    assertEquals(wild[ipaddr("1.4.5.6")], true)
    assertEquals(wild[ipaddr("2.4.5.6")], true)
    assertEquals(wild[ipaddr("3.4.5.6")], true)
    assertEquals(wild[ipaddr("4.4.5.6")], false)
    assertEquals(wild["1.4.5.6"], true)
    assertEquals(wild["2.4.5.6"], true)
    assertEquals(wild["3.4.5.6"], true)
    assertEquals(wild["4.4.5.6"], false)
    wild = ipwildcard("0,255.0,255.0,255.0,255")
    assertEquals(wild["0.0.0.0"], true)
    assertEquals(wild["0.0.0.255"], true)
    assertEquals(wild["0.0.255.0"], true)
    assertEquals(wild["0.255.0.0"], true)
    assertEquals(wild["255.0.0.0"], true)
    assertEquals(wild["255.255.0.0"], true)
    assertEquals(wild["255.0.255.0"], true)
    assertEquals(wild["255.0.0.255"], true)
    assertEquals(wild["0.255.0.255"], true)
    assertEquals(wild["0.255.255.0"], true)
    assertEquals(wild["0.0.255.255"], true)
    assertEquals(wild["0.255.255.255"], true)
    assertEquals(wild["255.0.255.255"], true)
    assertEquals(wild["255.255.0.255"], true)
    assertEquals(wild["255.255.255.0"], true)
    assertEquals(wild["255.255.255.255"], true)
    assertEquals(wild["255.255.255.254"], false)
    assertEquals(wild["255.255.254.255"], false)
    assertEquals(wild["255.254.255.255"], false)
    assertEquals(wild["254.255.255.255"], false)
    wild = ipwildcard("::")
    assertEquals(wild[ipaddr("::")], true)
    assertEquals(wild[ipaddr("::1")], false)
    assertEquals(wild["::"], true)
    assertEquals(wild["::1"], false)
    wild = ipwildcard("::/127")
    assertEquals(wild["::"], true)
    assertEquals(wild["::1"], true)
    assertEquals(wild["::2"], false)
    wild = ipwildcard("0:ffff::0.0.0.0,1")
    assertEquals(wild["0:ffff::0.0.0.0"], true)
    assertEquals(wild["0:ffff::0.0.0.1"], true)
    assertEquals(wild["0:ffff::0.0.0.2"], false)
    wild = ipwildcard("0:ffff:0:0:0:0:0.253-254.125-126,255.x")
    assertEquals(wild["0:ffff::0.253.125.1"], true)
    assertEquals(wild["0:ffff::0.254.125.2"], true)
    assertEquals(wild["0:ffff::0.253.126.3"], true)
    assertEquals(wild["0:ffff::0.254.126.4"], true)
    assertEquals(wild["0:ffff::0.253.255.5"], true)
    assertEquals(wild["0:ffff::0.254.255.6"], true)
    assertEquals(wild["0:ffff::0.255.255.7"], false)
    wild = ipwildcard("0.0.0.0")
    assertEquals(wild["::ffff:0:0"], true)
    assertEquals(wild["::"], false)
    wild = ipwildcard("::ffff:0:0")
    assertEquals(wild["0.0.0.0"], true)
    wild = ipwildcard("::")
    assertEquals(wild["0.0.0.0"], false)
  end -- function test_ipwildcard:test_containment

  function test_ipwildcard:test_iteration ()

    -- Create an array of sorted ipaddrs from a set of strings.
    -- Implement set equality for these.
    local function ips(...)
      local set = {...}
      for i, v in ipairs(set) do
        set[i] = ipaddr(set[i])
      end
      table.sort(set)
      local function eq(a, b)
        if #a ~= #b then return false end
        for i, v in a do
          if v ~= b[i] then return false end
        end
        return true
      end
      setmetatable(set, { __eq = eq })
      return set
    end

    -- Create an array of ip addresses from an ipwildcard, using the
    -- wildcard iterator
    local function ipw(wild)
      local set = {}
      for ip in ipwildcard_iter(ipwildcard(wild)) do
        set[#set + 1] = ip
      end
      return set
    end

    assertEquals(ipw("0.0.0.0"), ips("0.0.0.0"))
    assertEquals(ipw("0.0.0.0/31"), ips("0.0.0.0", "0.0.0.1"))
    assertEquals(ipw("255.255.255.254-255"),
                 ips("255.255.255.254", "255.255.255.255"))
    assertEquals(ipw("3,2,1.4.5.6"), ips("1.4.5.6", "2.4.5.6", "3.4.5.6"))
    assertEquals(ipw("0,255.0,255.0,255.0,255"),
                 ips("0.0.0.0", "0.0.0.255", "0.0.255.0", "0.255.0.0",
                     "255.0.0.0", "255.255.0.0", "255.0.255.0",
                     "255.0.0.255", "0.255.0.255", "0.255.255.0",
                     "0.0.255.255", "0.255.255.255", "255.0.255.255",
                     "255.255.0.255", "255.255.255.0", "255.255.255.255"))
    assertEquals(ipw("::"), ips("::"))
    assertEquals(ipw("::/127"), ips("::0", "::1"))
    assertEquals(ipw("0:ffff::0.0.0.0,1"), ips("0:ffff::0", "0:ffff::1"))
    assertEquals(ipw("0:ffff::0.253-254.125-126,255.1"),
                 ips("0:ffff::0.253.125.1", "0:ffff::0.253.126.1",
                     "0:ffff::0.253.255.1", "0:ffff::0.254.125.1",
                     "0:ffff::0.254.126.1", "0:ffff::0.254.255.1"))
  end -- function test_ipwildcard:test_iteration

  function test_ipwildcard:test_is_ipv6 ()
    assertEquals(ipwildcard_is_ipv6(ipwildcard("0.0.0.0")), false)
    assertEquals(ipwildcard_is_ipv6(ipwildcard("0.0.0.0/31")), false)
    assertEquals(ipwildcard_is_ipv6(ipwildcard("255.255.255.254-255")), false)
    assertEquals(ipwildcard_is_ipv6(ipwildcard("3,2,1.4.5.6")), false)
    assertEquals(ipwildcard_is_ipv6(ipwildcard("0,255.0,255.0,255.0,255")), false)
    assertEquals(ipwildcard_is_ipv6(ipwildcard("::")), true)
    assertEquals(ipwildcard_is_ipv6(ipwildcard("::/127")), true)
    assertEquals(ipwildcard_is_ipv6(ipwildcard("0:ffff::0.0.0.0,1")), true)
    assert(ipwildcard_is_ipv6(
             ipwildcard("0:ffff:0:0:0:0:0.253-254.125-126,255.x")))
  end -- function test_ipwildcard:test_is_ipv6

end -- test_ipwildcard

test_ipset = {}
do
  function test_ipset:setup()
    self.tmpfile = os.tmpname()
  end

  function test_ipset:rmfile()
    if self.tmpfile then
      os.remove(self.tmpfile)
    end
  end

  function test_ipset:teardown()
    self:rmfile()
  end

  function test_ipset:test_construction()
    local s = ipset()
    self:rmfile()
    ipset_save(s, self.tmpfile)
    s = ipset_load(self.tmpfile)
    self:rmfile()
    assertEquals(0, #s)
    ipset{"1.2.3.4"}
    ipset{"1.2.3.4", "5.6.7.8"}
    ipset{ipaddr("1.2.3.4")}
    ipset{ipaddr("1.2.3.4"), ipaddr("5.6.7.8")}
    ipset{"2001:db8:1:2::3:4"}
    ipset{"2001:db8:1:2::3:4", "10.10.10.10"}
    ipset{"2001:db8:1:2::3:4", "2001:db8:5:6::7:8"}
  end -- function test_ipset:test_construction

  function test_ipset:test_adding_and_containment()
    local s = ipset()
    ipset_add(s, "1.2.3.4")
    assertEquals(s["1.2.3.4"], true)
    assertEquals(ipset_contains(s, "1.2.3.4"), true)
    assertEquals(s["0.0.0.0"], false)
    assertEquals(ipset_contains(s, "0.0.0.0"), false)
    assertEquals(s[ipaddr("0.0.0.0")], false)
    assertEquals(ipset_contains(s, ipaddr("0.0.0.0")), false)
    ipset_add(s, ipaddr("5.6.7.8"))
    assertEquals(s["1.2.3.4"], true)
    assertEquals(s["5.6.7.8"], true)
    assertEquals(s["0.0.0.0"], false)
    assertEquals(s[ipaddr("1.2.3.4")], true)
    assertEquals(s[ipaddr("5.6.7.8")], true)
    assertEquals(s[ipaddr("0.0.0.0")], false)
    assertEquals(ipset_contains(s, "1.2.3.4"), true)
    assertEquals(ipset_contains(s, "5.6.7.8"), true)
    assertEquals(ipset_contains(s, "0.0.0.0"), false)
    assertEquals(ipset_contains(s, ipaddr("1.2.3.4")), true)
    assertEquals(ipset_contains(s, ipaddr("5.6.7.8")), true)
    assertEquals(ipset_contains(s, ipaddr("0.0.0.0")), false)
    assertError(ipset_add, s, 0)
    s = ipset()
    ipset_add_range(s, "10.2.4.1", "10.2.4.255")
    ipset_add_range(s, ipaddr("10.2.5.0"), "10.2.5.254")
    ipset_add_range(s, ipaddr("10.2.4.0"), ipaddr("10.2.4.0"))
    ipset_add(s, ipaddr("10.2.5.255"))
    assertEquals(s, ipset{"10.2.4.0/23"})
    assertError(ipset_add_range, s,
                ipaddr("10.10.10.10"), ipaddr("10.10.10.9"))
    s = ipset()
    ipset_add(s, "2001:db8:1:2::3:4")
    assertEquals(s["2001:db8:1:2::3:4"], true)
    assertEquals(s["2001:db8:0:0::0:0"], false)
    assertEquals(s[ipaddr("2001:db8:1:2::3:4")], true)
    assertEquals(s[ipaddr("2001:db8:0:0::0:0")], false)
    assertEquals(ipset_contains(s, "2001:db8:1:2::3:4"), true)
    assertEquals(ipset_contains(s, "2001:db8:0:0::0:0"), false)
    assertEquals(ipset_contains(s, ipaddr("2001:db8:1:2::3:4")), true)
    assertEquals(ipset_contains(s, ipaddr("2001:db8:0:0::0:0")), false)
    ipset_add(s, ipaddr("2001:db8:5:6::7:8"))
    assertEquals(s["2001:db8:1:2::3:4"], true)
    assertEquals(s["2001:db8:5:6::7:8"], true)
    assertEquals(s["2001:db8:0:0::0:0"], false)
    assertEquals(s[ipaddr("2001:db8:1:2::3:4")], true)
    assertEquals(s[ipaddr("2001:db8:5:6::7:8")], true)
    assertEquals(s[ipaddr("2001:db8:0:0::0:0")], false)
    assertEquals(ipset_contains(s, "2001:db8:1:2::3:4"), true)
    assertEquals(ipset_contains(s, "2001:db8:5:6::7:8"), true)
    assertEquals(ipset_contains(s, "2001:db8:0:0::0:0"), false)
    assertEquals(ipset_contains(s, ipaddr("2001:db8:1:2::3:4")), true)
    assertEquals(ipset_contains(s, ipaddr("2001:db8:5:6::7:8")), true)
    assertEquals(ipset_contains(s, ipaddr("2001:db8:0:0::0:0")), false)
    assertError(ipset_add, s, 0)
  end -- function test_ipset:test_adding_and_containment

  function test_ipset:test_type_and_convert()
    local a = ipset{"1.2.3.4"}
    local ac = ipset_copy(a)
    assertEquals(ipset_is_ipv6(a), false)
    ipset_convert_v4(a)
    assertEquals(ipset_is_ipv6(a), false)
    assertEquals(a, ac)
    local b = ipset{"::ffff:1.2.3.4"}
    local bc = ipset_copy(b)
    assertEquals(ipset_is_ipv6(b), true)
    ipset_convert_v6(b)
    assertEquals(ipset_is_ipv6(b), true)
    assertEquals(b, bc)
    ipset_convert_v6(a)
    assertEquals(ipset_is_ipv6(a), true)
    assertEquals(a, ac)
    ipset_convert_v4(b)
    assertEquals(ipset_is_ipv6(b), false)
    assertEquals(b, bc)
    ipset_add(a, "::")
    assertError(ipset_convert_v4, a)
  end -- function test_ipset:test_type_and_convert

  function test_ipset:test_ip_promotion()
    local s = ipset()
    assertEquals(ipset_is_ipv6(s), false)
    ipset_add(s, "1.2.3.4")
    assertEquals(ipset_is_ipv6(s), false)
    ipset_add(s, "2001:db8:1:2::3:4")
    assertEquals(ipset_is_ipv6(s), true)
    assertEquals(s["1.2.3.4"], true)
    assertEquals(s["0.0.0.0"], false)
    assertEquals(s[ipaddr("1.2.3.4")], true)
    assertEquals(s[ipaddr("0.0.0.0")], false)
    assertEquals(s["2001:db8:1:2::3:4"], true)
    assertEquals(s["2001:db8::"], false)
    assertEquals(s[ipaddr("2001:db8:1:2::3:4")], true)
    assertEquals(s[ipaddr("2001:db8::")], false)
    assertEquals(s["::ffff:1.2.3.4"], true)
    assertEquals(s["::ffff:0.0.0.0"], false)
    assertEquals(s[ipaddr("::ffff:1.2.3.4")], true)
    assertEquals(s[ipaddr("::ffff:0.0.0.0")], false)
  end -- function test_ipset:test_ip_promotion

  function test_ipset:test_copy()
    local s1 = ipset()
    local s2 = s1
    local s3 = ipset_copy(s1)
    assertEquals(s1 == s2, true)
    assertEquals(s1 == s3, true)
    assertEquals(rawequal(s1, s2), true)
    assertEquals(rawequal(s1, s3), false)
    ipset_add(s1, "1.2.3.4")
    ipset_add(s1, "5.6.7.8")
    s3 = ipset_copy(s1)
    assertEquals(s1 == s2, true)
    assertEquals(s1 == s3, true)
    assertEquals(rawequal(s1, s2), true)
    assertEquals(rawequal(s1, s3), false)
    s1 = ipset()
    ipset_convert_v6(s1)
    s2 = s1
    s3 = ipset_copy(s1)
    assertEquals(s1 == s2, true)
    assertEquals(s1 == s3, true)
    assertEquals(rawequal(s1, s2), true)
    assertEquals(rawequal(s1, s3), false)
    ipset_add(s1, "::ffff:1.2.3.4")
    s3 = ipset_copy(s1)
    assertEquals(s1 == s2, true)
    assertEquals(s1 == s3, true)
    assertEquals(rawequal(s1, s2), true)
    assertEquals(rawequal(s1, s3), false)
    ipset_add(s1, "2001:db8:1:2::3:4")
    ipset_add(s1, "2001:db8:5:6::7:8")
    s3 = ipset_copy(s1)
    assertEquals(s1 == s2, true)
    assertEquals(s1 == s3, true)
    assertEquals(rawequal(s1, s2), true)
    assertEquals(rawequal(s1, s3), false)
  end -- function test_ipset:test_copy

  function test_ipset:test_remove ()
    local s = ipset()
    ipset_add(s, "1.2.3.4")
    ipset_add(s, "5.6.7.8")
    assertEquals(s["1.2.3.4"], true)
    assertEquals(s["5.6.7.8"], true)
    ipset_remove(s, "1.2.3.4")
    assertEquals(s["1.2.3.4"], false)
    assertEquals(s["5.6.7.8"], true)
    ipset_remove(s, "5.6.7.8")
    assertEquals(s["1.2.3.4"], false)
    assertEquals(s["5.6.7.8"], false)
    assertError(ipset_remove, s, "1.2.3.4")
    s = ipset()
    ipset_add(s, "2001:db8:1:2::3:4")
    ipset_add(s, "2001:db8:5:6::7:8")
    assertEquals(s["2001:db8:1:2::3:4"], true)
    assertEquals(s["2001:db8:5:6::7:8"], true)
    ipset_remove(s, "2001:db8:1:2::3:4")
    assertEquals(s["2001:db8:1:2::3:4"], false)
    assertEquals(s["2001:db8:5:6::7:8"], true)
    ipset_remove(s, "2001:db8:5:6::7:8")
    assertEquals(s["2001:db8:1:2::3:4"], false)
    assertEquals(s["2001:db8:5:6::7:8"], false)
    assertError(ipset_remove, s, "2001:db8:1:2::3:4")
  end -- function test_ipset:test_remove

  function test_ipset:test_discard ()
    local s = ipset()
    ipset_add(s, "1.2.3.4")
    ipset_add(s, "5.6.7.8")
    assertEquals(s["1.2.3.4"], true)
    assertEquals(s["5.6.7.8"], true)
    ipset_discard(s, "1.2.3.4")
    assertEquals(s["1.2.3.4"], false)
    assertEquals(s["5.6.7.8"], true)
    ipset_discard(s, "5.6.7.8")
    assertEquals(s["1.2.3.4"], false)
    assertEquals(s["5.6.7.8"], false)
    ipset_discard(s, "1.2.3.4")
    assertEquals(s["1.2.3.4"], false)
    assertEquals(s["5.6.7.8"], false)
    s = ipset()
    ipset_add(s, "2001:db8:1:2::3:4")
    ipset_add(s, "2001:db8:5:6::7:8")
    assertEquals(s["2001:db8:1:2::3:4"], true)
    assertEquals(s["2001:db8:5:6::7:8"], true)
    ipset_discard(s, "2001:db8:1:2::3:4")
    assertEquals(s["2001:db8:1:2::3:4"], false)
    assertEquals(s["2001:db8:5:6::7:8"], true)
    ipset_discard(s, "2001:db8:5:6::7:8")
    assertEquals(s["2001:db8:1:2::3:4"], false)
    assertEquals(s["2001:db8:5:6::7:8"], false)
    ipset_discard(s, "2001:db8:1:2::3:4")
    assertEquals(s["2001:db8:1:2::3:4"], false)
    assertEquals(s["2001:db8:5:6::7:8"], false)
  end -- function test_ipset:test_discard

  function test_ipset:test_clear ()
    local s = ipset()
    ipset_add(s, "1.2.3.4")
    ipset_add(s, "5.6.7.8")
    assertEquals(s["1.2.3.4"], true)
    assertEquals(s["5.6.7.8"], true)
    ipset_clear(s)
    assertEquals(s["1.2.3.4"], false)
    assertEquals(s["5.6.7.8"], false)
    s = ipset()
    ipset_add(s, "2001:db8:1:2::3:4")
    ipset_add(s, "2001:db8:5:6::7:8")
    assertEquals(s["2001:db8:1:2::3:4"], true)
    assertEquals(s["2001:db8:5:6::7:8"], true)
    ipset_clear(s)
    assertEquals(s["2001:db8:1:2::3:4"], false)
    assertEquals(s["2001:db8:5:6::7:8"], false)
  end -- function test_ipset:test_clear

  function test_ipset:test_length ()
    local s = ipset()
    assertEquals(#s, 0)
    ipset_add(s, "1.2.3.4")
    ipset_add(s, "5.6.7.8")
    assertEquals(#s, 2)
    ipset_remove(s, "1.2.3.4")
    assertEquals(#s, 1)
    s = ipset()
    ipset_convert_v6(s)
    assertEquals(#s, 0)
    ipset_add(s, "2001:db8:1:2::3:4")
    ipset_add(s, "2001:db8:5:6::7:8")
    assertEquals(#s, 2)
    ipset_remove(s, "2001:db8:1:2::3:4")
    assertEquals(#s, 1)
  end -- function test_ipset:test_length

  function test_ipset:test_subset_superset ()
    local s1 = ipset()
    local s2 = ipset()
    assertEquals(ipset_is_subset(s1, s2), true)
    assertEquals(ipset_is_subset(s2, s1), true)
    assertEquals(ipset_is_superset(s1, s2), true)
    assertEquals(ipset_is_superset(s2, s1), true)
    assertEquals(s1 <= s2, true)
    assertEquals(s2 <= s1, true)
    assertEquals(s1 >= s2, true)
    assertEquals(s2 >= s1, true)
    ipset_add(s1, "1.2.3.4")
    ipset_add(s1, "5.6.7.8")
    ipset_add(s2, "5.6.7.8")
    assertEquals(ipset_is_subset(s1, s2), false)
    assertEquals(ipset_is_subset(s2, s1), true)
    assertEquals(ipset_is_superset(s1, s2), true)
    assertEquals(ipset_is_superset(s2, s1), false)
    assertEquals((s1 <= s2), false)
    assertEquals(s2 <= s1, true)
    assertEquals(s1 >= s2, true)
    assertEquals((s2 >= s1), false)
    assertError(ipset_is_subset, s1, {"1.2.3.4"})
    assertError(ipset_is_superset, s1, {"1.2.3.4"})
    s1 = ipset()
    s2 = ipset()
    ipset_convert_v6(s1)
    ipset_convert_v6(s2)
    assertEquals(ipset_is_subset(s1, s2), true)
    assertEquals(ipset_is_subset(s2, s1), true)
    assertEquals(ipset_is_superset(s1, s2), true)
    assertEquals(ipset_is_superset(s2, s1), true)
    assertEquals(s1 <= s2, true)
    assertEquals(s2 <= s1, true)
    assertEquals(s1 >= s2, true)
    assertEquals(s2 >= s1, true)
    ipset_add(s1, "::ffff:1.2.3.4")
    ipset_add(s1, "2001:db8:1:2::3:4")
    ipset_add(s1, "2001:db8:5:6::7:8")
    ipset_add(s2, "2001:db8:5:6::7:8")
    assertEquals(ipset_is_subset(s1, s2), false)
    assertEquals(ipset_is_subset(s2, s1), true)
    assertEquals(ipset_is_superset(s1, s2), true)
    assertEquals(ipset_is_superset(s2, s1), false)
    assertEquals((s1 <= s2), false)
    assertEquals(s2 <= s1, true)
    assertEquals(s1 >= s2, true)
    assertEquals((s2 >= s1), false)
    local s3 = ipset{"1.2.3.4"}
    assertEquals(ipset_is_subset(s1, s3), false)
    assertEquals(ipset_is_subset(s3, s1), true)
    assertEquals(ipset_is_superset(s1, s3), true)
    assertEquals(ipset_is_superset(s3, s1), false)
    assertEquals((s1 <= s3), false)
    assertEquals(s3 <= s1, true)
    assertEquals(s1 >= s3, true)
    assertError(ipset_is_subset, s1, {"2001:db8:1:2::3:4"})
    assertError(ipset_is_superset, s1, {"2001:db8:1:2::3:4"})
  end -- function test_ipset:test_subset_superset

  function test_ipset:test_union ()
    local s1 = ipset()
    local s2 = ipset()
    ipset_add(s1, "1.2.3.4")
    ipset_add(s1, "5.6.7.8")
    ipset_add(s2, "5.6.7.8")
    ipset_add(s2, "9.10.11.12")
    local s3 = ipset_union(s1, s2)
    local s4 = ipset_union(s2, s1)
    local s5 = ipset_copy(s1)
    ipset_update(s5, s2)
    assertEquals(s3 == s4, true)
    assertEquals(s3 == s5, true)
    assertEquals(s1 <= s3, true)
    assertEquals(s2 <= s3, true)
    assertEquals(s1 <= s4, true)
    assertEquals(s2 <= s4, true)
    assertEquals(s1 <= s5, true)
    assertEquals(s2 <= s5, true)
    assertEquals(#s3, 3)
    assertEquals(#s4, 3)
    assertEquals(#s5, 3)
    s3 = s1 + s2
    s4 = s2 + s1
    s5 = ipset_copy(s1)
    s5 = s5 + s2
    assertEquals(s3 == s4, true)
    assertEquals(s3 == s5, true)
    assertEquals(s1 <= s3, true)
    assertEquals(s2 <= s3, true)
    assertEquals(s1 <= s4, true)
    assertEquals(s2 <= s4, true)
    assertEquals(s1 <= s5, true)
    assertEquals(s2 <= s5, true)
    assertEquals(#s3, 3)
    assertEquals(#s4, 3)
    assertEquals(#s5, 3)
    local s6 = ipset_copy(s1)
    ipset_update(s6, ipwildcard("10.x.x.10"))
    assertEquals(s6["10.10.10.10"], true)
    assertEquals(s6["10.0.255.10"], true)
    assertEquals(s6["10.0.0.10"], true)
    assertEquals(s6["10.255.255.10"], true)
    assertEquals(s6["10.10.10.0"], false)
    assertEquals(s6["0.10.10.10"], false)
    assertEquals(#s6, 0x10002)
    local s7 = ipset_union(s1, s2, ipwildcard("10.x.x.10"),
                           "192.168.1.2", {ipaddr("192.168.3.4")})
    local s8 = ipset_copy(s1)
    ipset_update(s8, s2, ipwildcard("10.x.x.10"),
                 {"192.168.1.2", ipaddr("192.168.3.4")})
    assertEquals(s7 == s8, true)
    assertEquals(s8["1.2.3.4"], true)
    assertEquals(s8["5.6.7.8"], true)
    assertEquals(s8["192.168.1.2"], true)
    assertEquals(s8["10.10.10.10"], true)
    assertEquals(s8["10.0.255.10"], true)
    assertEquals(s8["10.0.0.10"], true)
    assertEquals(s8["10.255.255.10"], true)
    assertEquals(s8["10.10.10.0"], false)
    assertEquals(s8["0.10.10.10"], false)
    s1 = ipset()
    s2 = ipset()
    ipset_add(s1, "2001:db8:1:2::3:4")
    ipset_add(s1, "2001:db8:5:6::7:8")
    ipset_add(s2, "2001:db8:5:6::7:8")
    ipset_add(s2, "2001:db8:9:10::11:12")
    s3 = ipset_union(s1, s2)
    s4 = ipset_union(s2, s1)
    s5 = ipset_copy(s1)
    ipset_update(s5, s2)
    assertEquals(s3 == s4, true)
    assertEquals(s3 == s5, true)
    assertEquals(s1 <= s3, true)
    assertEquals(s2 <= s3, true)
    assertEquals(s1 <= s4, true)
    assertEquals(s2 <= s4, true)
    assertEquals(s1 <= s5, true)
    assertEquals(s2 <= s5, true)
    assertEquals(#s3, 3)
    assertEquals(#s4, 3)
    assertEquals(#s5, 3)
    s3 = s1 + s2
    s4 = s2 + s1
    s5 = ipset_copy(s1)
    s5 = s5 + s2
    assertEquals(s3 == s4, true)
    assertEquals(s3 == s5, true)
    assertEquals(s1 <= s3, true)
    assertEquals(s2 <= s3, true)
    assertEquals(s1 <= s4, true)
    assertEquals(s2 <= s4, true)
    assertEquals(s1 <= s5, true)
    assertEquals(s2 <= s5, true)
    assertEquals(#s3, 3)
    assertEquals(#s4, 3)
    assertEquals(#s5, 3)
    s6 = ipset_copy(s1)
    ipset_update(s6, ipwildcard("::ffff:10.x.x.10"))
    assertEquals(s6["::ffff:10.10.10.10"], true)
    assertEquals(s6["::ffff:10.0.255.10"], true)
    assertEquals(s6["::ffff:10.0.0.10"], true)
    assertEquals(s6["::ffff:10.255.255.10"], true)
    assertEquals(s6["::ffff:10.10.10.0"], false)
    assertEquals(s6["::ffff:0.10.10.10"], false)
    assertEquals(#s6, 0x10002)
    s7 = ipset{"1.2.3.4"}
    s8 = ipset{"::"}
    local s9 = ipset_convert_v6(ipset{"3.4.5.6"})
    local a, b, c = (s7 + s8), (s7 + s9), (s8 + s9)
    local d, e, f = (s8 + s7), (s9 + s7), (s9 + s8)
    assertEquals(a == d and b == e and c == f, true)
    for _, x in ipairs{a, b, c, d, e, f} do
      assertEquals(#x, 2)
    end
  end -- function test_ipset:test_union

  function test_ipset:test_intersection ()
    local s1 = ipset()
    local s2 = ipset()
    ipset_add(s1, "1.2.3.4")
    ipset_add(s1, "5.6.7.8")
    ipset_add(s2, "5.6.7.8")
    ipset_add(s2, "9.10.11.12")
    local s3 = ipset_intersection(s1, s2)
    local s4 = ipset_intersection(s2, s1)
    local s5 = ipset_copy(s1)
    ipset_intersection_update(s5, s2)
    assertEquals(s3 == s4 and s3 == s5, true)
    assertEquals(s1 >= s3, true)
    assertEquals(s2 >= s3, true)
    assertEquals(s1 >= s4, true)
    assertEquals(s2 >= s4, true)
    assertEquals(s1 >= s5, true)
    assertEquals(s2 >= s5, true)
    assertEquals(#s3, 1)
    assertEquals(#s4, 1)
    assertEquals(#s5, 1)
    s3 = ipset_intersection(s1, {"5.6.7.8", "9.10.11.12"})
    s4 = ipset_intersection(s2, {"1.2.3.4", "5.6.7.8"})
    s5 = ipset_copy(s1)
    ipset_intersection_update(s5, {"5.6.7.8", "9.10.11.12"})
    assertEquals(s3 == s4 and s3 == s5, true)
    assertEquals(s1 >= s3, true)
    assertEquals(s2 >= s3, true)
    assertEquals(s1 >= s4, true)
    assertEquals(s2 >= s4, true)
    assertEquals(s1 >= s5, true)
    assertEquals(s2 >= s5, true)
    assertEquals(#s3, 1)
    assertEquals(#s4, 1)
    assertEquals(#s5, 1)
    local s6 = ipset_intersection(s1, s2, {"5.6.7.8", "9.10.11.12"})
    local s7 = ipset_copy(s1)
    ipset_intersection_update(s7, s2, {"5.6.7.8", "9.10.11.12"})
    assertEquals(s6, s7)
    assertEquals(s6["5.6.7.8"], true)
    assertEquals(#s6, 1)
    s1 = ipset()
    s2 = ipset()
    ipset_add(s1, "2001:db8:1:2::3:4")
    ipset_add(s1, "2001:db8:5:6::7:8")
    ipset_add(s2, "2001:db8:5:6::7:8")
    ipset_add(s2, "2001:db8:9:10::11:12")
    s3 = ipset_intersection(s1, s2)
    s4 = ipset_intersection(s2, s1)
    s5 = ipset_copy(s1)
    ipset_intersection_update(s5, s2)
    assertEquals(s3 == s4 and s3 == s5, true)
    assertEquals(s1 >= s3, true)
    assertEquals(s2 >= s3, true)
    assertEquals(s1 >= s4, true)
    assertEquals(s2 >= s4, true)
    assertEquals(s1 >= s5, true)
    assertEquals(s2 >= s5, true)
    assertEquals(#s3, 1)
    assertEquals(#s4, 1)
    assertEquals(#s5, 1)
    s3 = ipset_intersection(s1,
                            {"2001:db8:5:6::7:8", "2001:db8:9:10::11:12"})
    s4 = ipset_intersection(s2, {"2001:db8:1:2::3:4", "2001:db8:5:6::7:8"})
    s5 = ipset_copy(s1)
    ipset_intersection_update(s5,
                              {"2001:db8:5:6::7:8", "2001:db8:9:10::11:12"})
    assertEquals(s3 == s4 and s3 == s5, true)
    assertEquals(s1 >= s3, true)
    assertEquals(s2 >= s3, true)
    assertEquals(s1 >= s4, true)
    assertEquals(s2 >= s4, true)
    assertEquals(s1 >= s5, true)
    assertEquals(s2 >= s5, true)
    assertEquals(#s3, 1)
    assertEquals(#s4, 1)
    assertEquals(#s5, 1)
    local s6 = ipset{"1.2.3.4", "3.4.5.6", "8.9.10.11"}
    local s7 = ipset{"::", "::ffff:8.9.10.11", "::ffff:6.5.4.3"}
    local s8 = ipset_convert_v6(ipset{"3.4.5.6", "6.5.4.3", "2.7.6.5"})
    local a, b = ipset_intersection(s7, s8), ipset_intersection(s7, s6)
    local c, d = ipset_intersection(s8, s6), ipset_intersection(s8, s7)
    local e, f = ipset_intersection(s6, s7), ipset_intersection(s6, s8)
    assertEquals(a == d and b == e and c == f, true)
    for _, x in ipairs{a, b, c, d, e, f} do
      assertEquals(#x, 1)
    end
  end -- function test_ipset:test_intersection

  function test_ipset:test_difference ()
    local s1 = ipset()
    local s2 = ipset()
    ipset_add(s1, "1.2.3.4")
    ipset_add(s1, "5.6.7.8")
    ipset_add(s2, "5.6.7.8")
    ipset_add(s2, "9.10.11.12")
    local s3 = ipset_difference(s1, s2)
    local s4 = ipset_difference(s2, s1)
    local s5 = ipset_copy(s1)
    ipset_difference_update(s5, s2)
    assertEquals(s3 ~= s4, true)
    assertEquals(s5 ~= s4, true)
    assertEquals(s1 >= s3, true)
    assertEquals(#ipset_intersection(s3, s2), 0)
    assertEquals(s1 >= s5, true)
    assertEquals(#ipset_intersection(s5, s2), 0)
    assertEquals(s2 >= s4, true)
    assertEquals(#ipset_intersection(s4, s1), 0)
    assertEquals(#s3, 1)
    assertEquals(#s4, 1)
    assertEquals(#s5, 1)
    s3 = s1 - s2
    s4 = s2 - s1
    s5 = ipset_copy(s1)
    s5 = s5 - s2
    assertEquals(s3 ~= s4, true)
    assertEquals(s5 ~= s4, true)
    assertEquals(s1 >= s3, true)
    assertEquals(#ipset_intersection(s3, s2), 0)
    assertEquals(s1 >= s5, true)
    assertEquals(#ipset_intersection(s5, s2), 0)
    assertEquals(s2 >= s4, true)
    assertEquals(#ipset_intersection(s4, s1), 0)
    assertEquals(#s3, 1)
    assertEquals(#s4, 1)
    assertEquals(#s5, 1)
    s3 = ipset_difference(s1, {"5.6.7.8", "9.10.11.12"})
    s4 = ipset_difference(s2, {"1.2.3.4", "5.6.7.8"})
    s5 = ipset_copy(s1)
    ipset_difference_update(s5, {"5.6.7.8", "9.10.11.12"})
    assertEquals(s3 ~= s4, true)
    assertEquals(s5 ~= s4, true)
    assertEquals(s1 >= s3, true)
    assertEquals(#ipset_intersection(s3, s2), 0)
    assertEquals(s1 >= s5, true)
    assertEquals(#ipset_intersection(s5, s2), 0)
    assertEquals(s2 >= s4, true)
    assertEquals(#ipset_intersection(s4, s1), 0)
    assertEquals(#s3, 1)
    assertEquals(#s4, 1)
    assertEquals(#s5, 1)
    local s6 = ipset_copy(s1)
    ipset_add(s6, "7.7.7.7")
    ipset_add(s6, "8.8.8.8")
    local s7 = ipset_copy(s6)
    local s8 = ipset_difference(s6, s2, {"8.8.8.8", "9.9.9.9"})
    ipset_difference_update(s7, s2, {"8.8.8.8", "9.9.9.9"})
    assertEquals(s8, s7)
    assertEquals(#s7, 2)
    assertEquals(s7["1.2.3.4"], true)
    assertEquals(s7["7.7.7.7"], true)
    s1 = ipset()
    s2 = ipset()
    ipset_add(s1, "2001:db8:1:2::3:4")
    ipset_add(s1, "2001:db8:5:6::7:8")
    ipset_add(s2, "2001:db8:5:6::7:8")
    ipset_add(s2, "2001:db8:9:10::11:12")
    s3 = ipset_difference(s1, s2)
    s4 = ipset_difference(s2, s1)
    s5 = ipset_copy(s1)
    ipset_difference_update(s5, s2)
    assertEquals(s3 ~= s4, true)
    assertEquals(s5 ~= s4, true)
    assertEquals(s1 >= s3, true)
    assertEquals(#ipset_intersection(s3, s2), 0)
    assertEquals(s1 >= s5, true)
    assertEquals(#ipset_intersection(s5, s2), 0)
    assertEquals(s2 >= s4, true)
    assertEquals(#ipset_intersection(s4, s1), 0)
    assertEquals(#s3, 1)
    assertEquals(#s4, 1)
    assertEquals(#s5, 1)
    s3 = s1 - s2
    s4 = s2 - s1
    s5 = ipset_copy(s1)
    s5 = s5 - s2
    assertEquals(s3 ~= s4, true)
    assertEquals(s5 ~= s4, true)
    assertEquals(s1 >= s3, true)
    assertEquals(#ipset_intersection(s3, s2), 0)
    assertEquals(s1 >= s5, true)
    assertEquals(#ipset_intersection(s5, s2), 0)
    assertEquals(s2 >= s4, true)
    assertEquals(#ipset_intersection(s4, s1), 0)
    assertEquals(#s3, 1)
    assertEquals(#s4, 1)
    assertEquals(#s5, 1)
    s3 = ipset_difference(s1, {"2001:db8:5:6::7:8", "2001:db8:9:10::11:12"})
    s4 = ipset_difference(s2, {"2001:db8:1:2::3:4", "2001:db8:5:6::7:8"})
    s5 = ipset_copy(s1)
    ipset_difference_update(s5,
                            {"2001:db8:5:6::7:8", "2001:db8:9:10::11:12"})
    assertEquals(s3 ~= s4, true)
    assertEquals(s5 ~= s4, true)
    assertEquals(s1 >= s3, true)
    assertEquals(#ipset_intersection(s3, s2), 0)
    assertEquals(s1 >= s5, true)
    assertEquals(#ipset_intersection(s5, s2), 0)
    assertEquals(s2 >= s4, true)
    assertEquals(#ipset_intersection(s4, s1), 0)
    assertEquals(#s3, 1)
    assertEquals(#s4, 1)
    assertEquals(#s5, 1)
    local s6 = ipset{"1.2.3.4", "3.4.5.6", "8.9.10.11"}
    local s7 = ipset{"::", "::ffff:8.9.10.11", "::ffff:6.5.4.3"}
    local s8 = ipset_convert_v6(ipset{"3.4.5.6", "6.5.4.3", "2.7.6.5"})
    local a, b, c = (s7 - s8), (s7 - s6), (s8 - s6)
    local d, e, f = (s8 - s7), (s6 - s7), (s6 - s8)
    for _, x in ipairs{a, b, c, d, e, f} do
      assertEquals(#x, 2)
    end
  end -- function test_ipset:test_difference

  function test_ipset:test_symmetric_difference ()
    local s1 = ipset()
    local s2 = ipset()
    ipset_add(s1, "1.2.3.4")
    ipset_add(s1, "5.6.7.8")
    ipset_add(s2, "5.6.7.8")
    ipset_add(s2, "9.10.11.12")
    local s3 = ipset_symmetric_difference(s1, s2)
    local s4 = ipset_symmetric_difference(s2, s1)
    local s5 = ipset_copy(s1)
    ipset_symmetric_difference_update(s5, s2)
    assertEquals(s3 == s4, true)
    assertEquals(s3 == s5, true)
    assertEquals(s1 >= s3, false)
    assertEquals(s2 >= s3, false)
    assertEquals(s1 >= s4, false)
    assertEquals(s2 >= s4, false)
    assertEquals(s1 >= s5, false)
    assertEquals(s2 >= s5, false)
    assertEquals(s1 <= s3, false)
    assertEquals(s2 <= s3, false)
    assertEquals(s1 <= s4, false)
    assertEquals(s2 <= s4, false)
    assertEquals(s1 <= s5, false)
    assertEquals(s2 <= s5, false)
    assertEquals(#s3, 2)
    assertEquals(#s4, 2)
    assertEquals(#s5, 2)
    s3 = ipset_symmetric_difference(s1, {"5.6.7.8", "9.10.11.12"})
    s4 = ipset_symmetric_difference(s2, {"1.2.3.4", "5.6.7.8"})
    s5 = ipset_copy(s1)
    ipset_symmetric_difference_update(s5, {"5.6.7.8", "9.10.11.12"})
    assertEquals(s3 == s4, true)
    assertEquals(s3 == s5, true)
    assertEquals(s1 >= s3, false)
    assertEquals(s2 >= s3, false)
    assertEquals(s1 >= s4, false)
    assertEquals(s2 >= s4, false)
    assertEquals(s1 >= s5, false)
    assertEquals(s2 >= s5, false)
    assertEquals(s1 <= s3, false)
    assertEquals(s2 <= s3, false)
    assertEquals(s1 <= s4, false)
    assertEquals(s2 <= s4, false)
    assertEquals(s1 <= s5, false)
    assertEquals(s2 <= s5, false)
    assertEquals(#s3, 2)
    assertEquals(#s4, 2)
    assertEquals(#s5, 2)
    s1 = ipset()
    s2 = ipset()
    ipset_add(s1, "2001:db8:1:2::3:4")
    ipset_add(s1, "2001:db8:5:6::7:8")
    ipset_add(s2, "2001:db8:5:6::7:8")
    ipset_add(s2, "2001:db8:9:10::11:12")
    s3 = ipset_symmetric_difference(s1, s2)
    s4 = ipset_symmetric_difference(s2, s1)
    s5 = ipset_copy(s1)
    ipset_symmetric_difference_update(s5, s2)
    assertEquals(s3 == s4, true)
    assertEquals(s3 == s5, true)
    assertEquals(s1 >= s3, false)
    assertEquals(s2 >= s3, false)
    assertEquals(s1 >= s4, false)
    assertEquals(s2 >= s4, false)
    assertEquals(s1 >= s5, false)
    assertEquals(s2 >= s5, false)
    assertEquals(s1 <= s3, false)
    assertEquals(s2 <= s3, false)
    assertEquals(s1 <= s4, false)
    assertEquals(s2 <= s4, false)
    assertEquals(s1 <= s5, false)
    assertEquals(s2 <= s5, false)
    assertEquals(#s3, 2)
    assertEquals(#s4, 2)
    assertEquals(#s5, 2)
    s3 = ipset_symmetric_difference(s1, {"2001:db8:5:6::7:8",
                                         "2001:db8:9:10::11:12"})
    s4 = ipset_symmetric_difference(s2, {"2001:db8:1:2::3:4",
                                         "2001:db8:5:6::7:8"})
    s5 = ipset_copy(s1)
    ipset_symmetric_difference_update(s5, {"2001:db8:5:6::7:8",
                                           "2001:db8:9:10::11:12"})
    assertEquals(s3 == s4, true)
    assertEquals(s3 == s5, true)
    assertEquals(s1 >= s3, false)
    assertEquals(s2 >= s3, false)
    assertEquals(s1 >= s4, false)
    assertEquals(s2 >= s4, false)
    assertEquals(s1 >= s5, false)
    assertEquals(s2 >= s5, false)
    assertEquals(s1 <= s3, false)
    assertEquals(s2 <= s3, false)
    assertEquals(s1 <= s4, false)
    assertEquals(s2 <= s4, false)
    assertEquals(s1 <= s5, false)
    assertEquals(s2 <= s5, false)
    assertEquals(#s3, 2)
    assertEquals(#s4, 2)
    assertEquals(#s5, 2)
    local s6 = ipset{"1.2.3.4", "3.4.5.6", "8.9.10.11"}
    local s7 = ipset{"::", "::ffff:8.9.10.11", "::ffff:6.5.4.3"}
    local s8 = ipset_convert_v6(ipset{"3.4.5.6", "6.5.4.3", "2.7.6.5"})
    local a = ipset_symmetric_difference(s7, s8)
    local b = ipset_symmetric_difference(s7, s6)
    local c = ipset_symmetric_difference(s8, s6)
    local d = ipset_symmetric_difference(s8, s7)
    local e = ipset_symmetric_difference(s6, s7)
    local f = ipset_symmetric_difference(s6, s8)
    assertEquals(a == d, true)
    assertEquals(b == e, true)
    assertEquals(c == f, true)
    for _, x in ipairs{a, b, c, d, e, f} do
      assertEquals(#x, 4)
    end
  end -- function test_ipset:test_symmetric_difference

  function test_ipset:test_is_disjoint ()
    local s1 = ipset{"1.1.1.1", "2.2.2.2"}
    local s2 = ipset{"3.3.3.3", "4.4.4.4"}
    local s3 = ipset{"1.1.1.1", "3.3.3.3"}
    assertEquals(ipset_is_disjoint(s1, s1), false)
    assertEquals(ipset_is_disjoint(s1, s2), true)
    assertEquals(ipset_is_disjoint(s2, s1), true)
    assertEquals(ipset_is_disjoint(s2, s3), false)
    assertEquals(ipset_is_disjoint(s3, s2), false)
    assertEquals(ipset_is_disjoint(s1, s3), false)
    assertEquals(ipset_is_disjoint(s3, s1), false)
    local s4 = ipset{"2001:db8::1", "2001:db8::2:0"}
    local s5 = ipset{"2001:db8::3:0:0", "2001:db8:4::"}
    local s6 = ipset{"2001:db8::2:0", "2001:db8::3:0:0"}
    assertEquals(ipset_is_disjoint(s4, s4), false)
    assertEquals(ipset_is_disjoint(s4, s5), true)
    assertEquals(ipset_is_disjoint(s5, s4), true)
    assertEquals(ipset_is_disjoint(s5, s6), false)
    assertEquals(ipset_is_disjoint(s6, s5), false)
    assertEquals(ipset_is_disjoint(s4, s6), false)
    assertEquals(ipset_is_disjoint(s6, s4), false)
    assertEquals(ipset_is_disjoint(s1, s4), true)
    assertEquals(ipset_is_disjoint(s4, s1), true)
    assertEquals(ipset_is_disjoint(s2, s5), true)
    assertEquals(ipset_is_disjoint(s5, s2), true)
    assertEquals(ipset_is_disjoint(s3, s6), true)
    assertEquals(ipset_is_disjoint(s6, s3), true)
    ipset_update(s4, s1)
    assertEquals(ipset_is_disjoint(s1, s4), false)
    assertEquals(ipset_is_disjoint(s4, s1), false)
    assertEquals(ipset_is_disjoint(s2, s4), true)
    assertEquals(ipset_is_disjoint(s4, s2), true)
    assertEquals(ipset_is_disjoint(s3, s4), false)
    assertEquals(ipset_is_disjoint(s4, s3), false)
    ipset_update(s5, s2)
    assertEquals(ipset_is_disjoint(s1, s5), true)
    assertEquals(ipset_is_disjoint(s5, s1), true)
    assertEquals(ipset_is_disjoint(s2, s5), false)
    assertEquals(ipset_is_disjoint(s5, s2), false)
    assertEquals(ipset_is_disjoint(s3, s5), false)
    assertEquals(ipset_is_disjoint(s5, s3), false)
    assertEquals(ipset_is_disjoint(s4, s5), true)
    assertEquals(ipset_is_disjoint(s5, s4), true)
    ipset_update(s6, s3)
    assertEquals(ipset_is_disjoint(s1, s6), false)
    assertEquals(ipset_is_disjoint(s6, s1), false)
    assertEquals(ipset_is_disjoint(s2, s6), false)
    assertEquals(ipset_is_disjoint(s6, s2), false)
    assertEquals(ipset_is_disjoint(s3, s6), false)
    assertEquals(ipset_is_disjoint(s6, s3), false)
    assertEquals(ipset_is_disjoint(s5, s6), false)
    assertEquals(ipset_is_disjoint(s6, s5), false)
    assertEquals(ipset_is_disjoint(s4, s6), false)
    assertEquals(ipset_is_disjoint(s6, s4), false)
  end -- function test_ipset:test_is_disjoint

  function test_ipset:test_iterators ()
    local function ips (x)
      local rm = {}
      for i, v in ipairs(x) do
        x[i] = ipaddr(v)
        rm[ipaddr_to_bytes(x[i])] = true
      end
      return x, rm
    end
    local function list (...)
      local a = {}
      local b = {}
      for xa, xb in ... do
        a[#a + 1], b[#b + 1] = xa, xb
      end
      return a, b
    end
    local ipaddrs, rs = ips{"1.2.3.4", "1.2.3.5", "1.2.3.6", "1.2.3.7",
                            "1.2.3.8", "1.2.3.9", "0.0.0.0"}
    local s = ipset(ipaddrs)
    local count = 0
    for x in ipset_iter(s) do
      assertEquals(s[x], true)
      assertEquals(rs[ipaddr_to_bytes(x)], true)
      count = count + 1
    end
    assertEquals(count, #ipaddrs)
    local cidrs, pfxs = list(ipset_cidr_iter(s))
    assertEquals(#cidrs, 3)
    assertEquals(#pfxs, #cidrs)
    local blocks = ips{'0.0.0.0', '1.2.3.4', '1.2.3.8'}
    local prefixes = {32, 30, 31}
    for i = 1,#cidrs do
      assertEquals(cidrs[i], blocks[i])
      assertEquals(pfxs[i], prefixes[i])
    end
    ipaddrs, rs = ips{"2001:db8:1:2::3:4", "2001:db8:1:2::3:5",
                      "2001:db8:1:2::3:6", "2001:db8:1:2::3:7",
                      "2001:db8:1:2::3:8", "2001:db8:1:2::3:9",
                      "2001:db8::"}
    s = ipset(ipaddrs)
    count = 0
    for x in ipset_iter(s) do
      assertEquals(s[x], true)
      assertEquals(rs[ipaddr_to_bytes(x)], true)
      count = count + 1
    end
    assertEquals(count, #ipaddrs)
    cidrs, pfxs = list(ipset_cidr_iter(s))
    assertEquals(#cidrs, 3)
    assertEquals(#pfxs, #cidrs)
    blocks = ips{'2001:db8:0:0::0:0', '2001:db8:1:2::3:4',
                 '2001:db8:1:2::3:8'}
    prefixes = {128, 126, 127}
    for i = 1,#cidrs do
      assertEquals(cidrs[i], blocks[i])
      assertEquals(pfxs[i], prefixes[i])
    end
  end -- function test_ipset:test_iterators

  function test_ipset:test_io ()
    local s = ipset{"1.2.3.4", "1.2.3.5", "1.2.3.6", "1.2.3.7",
                    "1.2.3.8", "1.2.3.9", "0.0.0.0"}
    self:rmfile()
    ipset_save(s, self.tmpfile)
    local ns = ipset_load(self.tmpfile)
    assertEquals(rawequal(s, ns), false)
    assertEquals(s, ns)
    self:rmfile()
    s = ipset{"2001:db8:1:2::3:4", "2001:db8:1:2::3:5",
              "2001:db8:1:2::3:6", "2001:db8:1:2::3:7",
              "2001:db8:1:2::3:8", "2001:db8:1:2::3:9",
              "2001:db8::"}
    ipset_save(s, self.tmpfile)
    ns = ipset_load(self.tmpfile)
    assertEquals(rawequal(s, ns), false)
    assertEquals(s, ns)
    self:rmfile()
    s = ipset{"2001:db8:1:2::3:4", "127.0.0.1"}
    ipset_discard(s, "2001:db8:1:2::3:4")
    ipset_save(s, self.tmpfile)
    ns = ipset_load(self.tmpfile)
    assertEquals(rawequal(s, ns), false)
    ipset_add(s, "2001:db::1")
    ipset_add(ns, "2001:db::1")
    assertEquals(s, ns)
    self:rmfile()
  end -- function test_ipset:test_io

end -- test_ipset

test_pmap = {}
do
  function test_pmap:setup ()
    local srcloc = "../.."
    local testloc = srcloc .. "/tests/?"
    local testmaps = {ipmap = "ip-map.pmap";
                      ppmap = "proto-port-map.pmap";
                      ipmapv6 = "ip-map-v6.pmap"}
    self.testmaps = {}
    for k, v in pairs(testmaps) do
      local file, err = package.searchpath(v, testloc, '.', '.')
      if err then error(err) end
      self.testmaps[k] = file
    end
  end

  function test_pmap:test_load ()
    local ipmap = pmap_load(self.testmaps["ipmap"])
    assertEquals(silk.type(ipmap), 'pmap')
    local ppmap = pmap_load(self.testmaps["ppmap"])
    assertEquals(silk.type(ppmap), 'pmap')
    pmap_load(self.testmaps["ipmapv6"])
  end -- function test_pmap:test_load

  function test_pmap:test_get_ipaddr ()
    local ipmap = pmap_load(self.testmaps["ipmap"])
    assertEquals(ipmap[ipaddr("192.168.4.5")], "internal")
    assertEquals(ipmap[ipaddr("192.168.0.0")], "internal")
    assertEquals(ipmap[ipaddr("192.168.255.255")], "internal")
    assertEquals(ipmap[ipaddr("172.17.0.0")], "internal services")
    assertEquals(ipmap[ipaddr("172.31.0.0")], "internal services")
    assertEquals(ipmap[ipaddr("172.16.0.0")], "ntp")
    assertEquals(ipmap[ipaddr("172.24.0.0")], "dns")
    assertEquals(ipmap[ipaddr("172.30.0.0")], "dhcp")
    assertEquals(ipmap[ipaddr("0.0.0.0")], "external")
    assertEquals(ipmap[ipaddr("255.255.255.255")], "external")
    assertEquals(ipmap[ipaddr("::")], "UNKNOWN")
    assertEquals(ipmap[ipaddr("::ffff:192.168.0.0")], "internal")
    assertEquals(ipmap[ipaddr("::ffff:0.0.0.0")], "external")
    assertEquals(pmap_get(ipmap, ipaddr("192.168.4.5")), "internal")
    assertEquals(pmap_get(ipmap, ipaddr("192.168.0.0")), "internal")
    assertEquals(pmap_get(ipmap, ipaddr("192.168.255.255")), "internal")
    assertEquals(pmap_get(ipmap, ipaddr("172.17.0.0")), "internal services")
    assertEquals(pmap_get(ipmap, ipaddr("172.31.0.0")), "internal services")
    assertEquals(pmap_get(ipmap, ipaddr("172.16.0.0")), "ntp")
    assertEquals(pmap_get(ipmap, ipaddr("172.24.0.0")), "dns")
    assertEquals(pmap_get(ipmap, ipaddr("172.30.0.0")), "dhcp")
    assertEquals(pmap_get(ipmap, ipaddr("0.0.0.0")), "external")
    assertEquals(pmap_get(ipmap, ipaddr("255.255.255.255")), "external")
    assertEquals(pmap_get(ipmap, ipaddr("::")), "UNKNOWN")
    assertEquals(pmap_get(ipmap, ipaddr("::ffff:192.168.0.0")), "internal")
    assertEquals(pmap_get(ipmap, ipaddr("::ffff:0.0.0.0")), "external")
    local ipmapv6 = pmap_load(self.testmaps["ipmapv6"])
    assertEquals(ipmapv6[ipaddr("2001:db8:c0:a8::1")], "internal")
    assertEquals(pmap_get(ipmapv6, ipaddr("2001:db8:c0:a8::1")), "internal")
    assertEquals(ipmapv6[ipaddr("2001:db8:ac:11::1")], "internal services")
    assertEquals(ipmapv6[ipaddr("2001:db8:ac:1f::1")], "internal services")
    assertEquals(ipmapv6[ipaddr("2001:db8:ac:10::1")], "ntp")
    assertEquals(ipmapv6[ipaddr("2001:db8:ac:18::1")], "dns")
    assertEquals(ipmapv6[ipaddr("2001:db8:ac:1e::1")], "dhcp")
    assertEquals(ipmapv6[ipaddr("0.0.0.0")], "external")
    assertEquals(ipmapv6[ipaddr("192.168.0.0")], "external")
  end -- function test_pmap:test_get_ipaddr


  function test_pmap:test_get_port_proto ()
    local ppmap = pmap_load(self.testmaps["ppmap"])
    assertEquals(ppmap[{1, 0}], "ICMP")
    assertEquals(pmap_get(ppmap, {1, 0}), "ICMP")
    assertEquals(ppmap[{1, 0xffff}], "ICMP")
    assertEquals(ppmap[{17, 1}], "UDP")
    assertEquals(ppmap[{17, 0xffff}], "UDP")
    assertEquals(ppmap[{17, 53}], "UDP/DNS")
    assertEquals(ppmap[{17, 66}], "UDP")
    assertEquals(ppmap[{17, 67}], "UDP/DHCP")
    assertEquals(ppmap[{17, 68}], "UDP/DHCP")
    assertEquals(ppmap[{17, 69}], "UDP")
    assertEquals(ppmap[{17, 122}], "UDP")
    assertEquals(ppmap[{17, 123}], "UDP/NTP")
    assertEquals(ppmap[{17, 124}], "UDP")
    assertEquals(ppmap[{6, 0}], "TCP")
    assertEquals(ppmap[{6, 0xffff}], "TCP")
    assertEquals(ppmap[{6, 22}], "TCP/SSH")
    assertEquals(ppmap[{6, 24}], "TCP")
    assertEquals(ppmap[{6, 25}], "TCP/SMTP")
    assertEquals(ppmap[{6, 26}], "TCP")
    assertEquals(ppmap[{6, 80}], "TCP/HTTP")
    assertEquals(ppmap[{6, 443}], "TCP/HTTPS")
    assertEquals(ppmap[{6, 8080}], "TCP/HTTP")
    assertEquals(ppmap[{2, 80}], "unknown")
    assertEquals(ppmap[{5, 80}], "unknown")
    assertEquals(ppmap[{7, 80}], "unknown")
    assertEquals(pmap_get(ppmap, 1, 0), "ICMP")
    assertEquals(pmap_get(ppmap, 1, 0xffff), "ICMP")
    assertEquals(pmap_get(ppmap, 17, 1), "UDP")
    assertEquals(pmap_get(ppmap, 17, 0xffff), "UDP")
    assertEquals(pmap_get(ppmap, 17, 53), "UDP/DNS")
    assertEquals(pmap_get(ppmap, 17, 66), "UDP")
    assertEquals(pmap_get(ppmap, 17, 67), "UDP/DHCP")
    assertEquals(pmap_get(ppmap, 17, 68), "UDP/DHCP")
    assertEquals(pmap_get(ppmap, 17, 69), "UDP")
    assertEquals(pmap_get(ppmap, 17, 122), "UDP")
    assertEquals(pmap_get(ppmap, 17, 123), "UDP/NTP")
    assertEquals(pmap_get(ppmap, 17, 124), "UDP")
    assertEquals(pmap_get(ppmap, 6, 0), "TCP")
    assertEquals(pmap_get(ppmap, 6, 0xffff), "TCP")
    assertEquals(pmap_get(ppmap, 6, 22), "TCP/SSH")
    assertEquals(pmap_get(ppmap, 6, 24), "TCP")
    assertEquals(pmap_get(ppmap, 6, 25), "TCP/SMTP")
    assertEquals(pmap_get(ppmap, 6, 26), "TCP")
    assertEquals(pmap_get(ppmap, 6, 80), "TCP/HTTP")
    assertEquals(pmap_get(ppmap, 6, 443), "TCP/HTTPS")
    assertEquals(pmap_get(ppmap, 6, 8080), "TCP/HTTP")
    assertEquals(pmap_get(ppmap, 2, 80), "unknown")
    assertEquals(pmap_get(ppmap, 5, 80), "unknown")
    assertEquals(pmap_get(ppmap, 7, 80), "unknown")
    assertError(ppmap, {1})
    assertError(ppmap, 1)
    assertError(ppmap_get, ppmap, 1)
    assertError(ppmap, ipaddr("0.0.0.0"))
    assertError(ppmap, {0x100, 1})
    assertError(ppmap, {1, 0x10000})
    assertError(ppmap, {-1, 1})
    assertError(ppmap, {1, -1})
  end -- function test_pmap:test_get_port_proto ()

end -- test_pmap


test_bitmap = {}
do

  local function CHECK_MAP(cm_bmap, cm_size, cm_entries)
    if silk.type(cm_bmap) ~= "bitmap" then
      error("expected bitmap as first arg, got " .. silk.type(cm_bmap))
    end
    if silk.type(cm_size) ~= "number" then
      error("expected number as second arg, got " .. silk.type(cm_size))
    end
    if silk.type(cm_entries) ~= "table" then
      error("expected table as third arg, got " .. silk.type(cm_entries))
    end
    if silk.bitmap_get_size(cm_bmap) ~= cm_size then
      error("expected bitmap of size " .. cm_size
              .. ", got " .. silk.bitmap_get_size(cm_bmap))
    end
    local t = false
    local idx = 0
    local pos = 0
    while pos < cm_size do
      local cons = silk.bitmap_count_consecutive(cm_bmap, pos, t)
      idx = idx + 1
      if cons ~= cm_entries[idx] then
        local p
        if t then p = "true" else p = "false" end
        error("expected call #" .. idx .. " to count_consecutive("
                .. pos .. "," .. p .. ") to be " .. cm_entries[idx]
                .. ", got " .. cons)
      end
      if idx > cm_size then
        error("idx is larger than size of bitmap " .. idx
                .. " > " .. cm_size)
      end
      pos = pos + cons
      t = not t
    end
    if pos ~= cm_size then
      error("expected pos to be " .. cm_size .. ", got " .. pos)
    end
    return true
  end

  local BITMAP_SIZE = 160

  function test_bitmap:test_construction()
    local b;
    local c;

    b = bitmap(32)
    assertEquals(true, CHECK_MAP(b, 32, {32}))
    b = bitmap("32")
    assertEquals(true, CHECK_MAP(b, 32, {32}))
    b = bitmap(0x20)
    assertEquals(true, CHECK_MAP(b, 32, {32}))

    assertError(bitmap, "32xxxx")
    assertError(bitmap, 0)
    assertError(bitmap, -4)
    assertError(bitmap, {24})

    bitmap_set_range(b, 8, 23)
    assertEquals(true, CHECK_MAP(b, 32, {8, 16, 8}))
    assertEquals(bitmap_get_count(b), 16)
    c = bitmap_copy(b)
    assertEquals(false, c == b)
    assertEquals(true, CHECK_MAP(c, 32, {8, 16, 8}))
    assertEquals(bitmap_get_count(c), 16)
    bitmap_clear_all(b)
    assertEquals(true, CHECK_MAP(b, 32, {32}))
    assertEquals(bitmap_get_count(b), 0)
    assertEquals(true, CHECK_MAP(c, 32, {8, 16, 8}))
    assertEquals(bitmap_get_count(c), 16)

  end -- function test_bitmap:test_construction

  function test_bitmap:test_union_intersection()
    local bmap;
    local bmap2;
    local i;
    local j;

    bmap = bitmap(BITMAP_SIZE);
    assertEquals(bitmap_get_size(bmap), BITMAP_SIZE);
    assertEquals(true, CHECK_MAP(bmap, BITMAP_SIZE, { BITMAP_SIZE }));
    assertEquals(bitmap_get_count(bmap), 0);

    i = 96;
    assertEquals(bitmap_get_bit(bmap, i), false);
    bitmap_set_bit(bmap, i);
    assertEquals(bitmap_get_bit(bmap, i), true);
    assertEquals(bitmap_get_count(bmap), 1);
    assertEquals(true, CHECK_MAP(bmap, BITMAP_SIZE,
                                 { i, 1, BITMAP_SIZE-(i+1) }));

    bmap2 = bitmap(BITMAP_SIZE);
    assertEquals(bitmap_get_size(bmap2), BITMAP_SIZE);
    assertEquals(true, CHECK_MAP(bmap2, BITMAP_SIZE, { BITMAP_SIZE }));
    assertEquals(bitmap_get_count(bmap2), 0);

    j = 127;
    bitmap_set_bit(bmap2, j);
    assertEquals(bitmap_get_bit(bmap2, j), true);
    assertEquals(bitmap_get_count(bmap2), 1);
    assertEquals(true, CHECK_MAP(bmap2, BITMAP_SIZE,
                                 { j, 1, BITMAP_SIZE-(j+1) }));

    bitmap_union_update(bmap2, bmap);
    assertEquals(bitmap_get_count(bmap), 1);
    assertEquals(bitmap_get_count(bmap2), 2);
    assertEquals(bitmap_get_size(bmap2), BITMAP_SIZE);
    assertEquals(bitmap_get_bit(bmap2, i), true);
    assertEquals(true, i + 1 < j)
    assertEquals(true, CHECK_MAP(bmap2, BITMAP_SIZE,
                                 { i, 1, j-i-1, 1, BITMAP_SIZE-(j+1) }));

    bitmap_intersect_update(bmap2, bmap);
    assertEquals(bitmap_get_count(bmap2), 1);
    assertEquals(bitmap_get_bit(bmap2, i), true);
    assertEquals(bitmap_get_bit(bmap2, j), false);
    assertEquals(true, CHECK_MAP(bmap2, BITMAP_SIZE,
                                 { i, 1, BITMAP_SIZE-(i+1) }));

    bitmap_compliment_update(bmap2);
    assertEquals(bitmap_get_count(bmap2), (BITMAP_SIZE - 1));
    assertEquals(bitmap_get_size(bmap2), BITMAP_SIZE);
    assertEquals(bitmap_get_bit(bmap2, i), false);
    assertEquals(true, CHECK_MAP(bmap2, BITMAP_SIZE,
                                 { 0, i, 1, BITMAP_SIZE-(i+1) }));

    j = 97;
    assertEquals(bitmap_get_bit(bmap, j), false);
    bitmap_clear_bit(bmap, j);
    assertEquals(bitmap_get_bit(bmap, j), false);
    assertEquals(bitmap_get_bit(bmap, i), true);
    assertEquals(bitmap_get_count(bmap), 1);
    assertEquals(true, CHECK_MAP(bmap, BITMAP_SIZE,
                                 { i, 1, BITMAP_SIZE-(i+1) }));

    bmap2 = nil;

    local BITMAP_SIZE2 = BITMAP_SIZE + 10

    bmap2 = bitmap(BITMAP_SIZE2);
    assertEquals(bitmap_get_size(bmap2), BITMAP_SIZE2);
    assertEquals(true, CHECK_MAP(bmap2, BITMAP_SIZE2, { BITMAP_SIZE2 }));
    assertEquals(bitmap_get_count(bmap2), 0);

    j = 127;
    bitmap_set_bit(bmap2, j);
    assertEquals(bitmap_get_bit(bmap2, j), true);
    assertEquals(bitmap_get_count(bmap2), 1);
    assertEquals(true, CHECK_MAP(bmap2, BITMAP_SIZE2,
                                 { j, 1, BITMAP_SIZE2-(j+1) }));

    assertError(bitmap_union_update, bmap2, bmap);
    assertError(bitmap_union_update, bmap, bmap2);

    assertEquals(true, CHECK_MAP(bmap, BITMAP_SIZE,
                                 { i, 1, BITMAP_SIZE-(i+1) }));
    assertEquals(true, CHECK_MAP(bmap2, BITMAP_SIZE2,
                                 { j, 1, BITMAP_SIZE2-(j+1) }));

    assertError(bitmap_intersection_update, bmap2, bmap);
    assertError(bitmap_intersection_update, bmap, bmap2);

    assertEquals(true, CHECK_MAP(bmap, BITMAP_SIZE,
                                 { i, 1, BITMAP_SIZE-(i+1) }));
    assertEquals(true, CHECK_MAP(bmap2, BITMAP_SIZE2,
                                 { j, 1, BITMAP_SIZE2-(j+1) }));

    bmap2 = nil;
    bmap = nil;
  end

  function test_bitmap:test_set_get()
    local bmap;
    local i;
    local j;

    bmap = bitmap(BITMAP_SIZE);
    assertEquals(bitmap_get_size(bmap), BITMAP_SIZE);
    assertEquals(true, CHECK_MAP(bmap, BITMAP_SIZE, { BITMAP_SIZE }));
    assertEquals(bitmap_get_count(bmap), 0);

    i = 96;
    assertEquals(bitmap_get_bit(bmap, i), false);
    bitmap_set_bit(bmap, i);
    assertEquals(bitmap_get_bit(bmap, i), true);
    assertEquals(bitmap_get_count(bmap), 1);
    assertEquals(true, CHECK_MAP(bmap, BITMAP_SIZE,
                                 { i, 1, BITMAP_SIZE-(i+1) }));

    j = 97;
    bitmap_set_bit(bmap, j);
    assertEquals(bitmap_get_count(bmap), 2);
    assertEquals(bitmap_get_bit(bmap, j), true);
    assertEquals(true, i + 1 == j)
    assertEquals(true, CHECK_MAP(bmap, BITMAP_SIZE,
                                 { i, 2, BITMAP_SIZE-(j+1) }));

    bitmap_clear_bit(bmap, i);
    assertEquals(true, CHECK_MAP(bmap, BITMAP_SIZE,
                                 { j, 1, BITMAP_SIZE-(j+1) }));
    assertEquals(bitmap_get_count(bmap), 1);

    bitmap_clear_all(bmap)
    assertEquals(true, CHECK_MAP(bmap, BITMAP_SIZE, { BITMAP_SIZE }));
    assertEquals(bitmap_get_count(bmap), 0);

    local p = 0;
    local q = BITMAP_SIZE;
    while p < BITMAP_SIZE do
      assertEquals(q, bitmap_count_consecutive(bmap, p, 0));
      p = p + 32;
      q = q - 32;
    end

    assertEquals(bitmap_get_size(bmap), BITMAP_SIZE);

    i = 95;
    bitmap_set_bit(bmap, i);
    assertEquals(true, CHECK_MAP(bmap, BITMAP_SIZE,
                                 { i, 1, BITMAP_SIZE-(i+1) }));
    assertEquals(bitmap_get_bit(bmap, i), true);
    assertEquals(bitmap_get_count(bmap), 1);
    assertEquals(bitmap_get_bit(bmap, j), false);

    bitmap_clear_bit(bmap, j);
    assertEquals(true, CHECK_MAP(bmap, BITMAP_SIZE,
                                 { i, 1, BITMAP_SIZE-(i+1) }));
    assertEquals(bitmap_get_bit(bmap, j), false);
    assertEquals(bitmap_get_bit(bmap, i), true);
    assertEquals(bitmap_get_count(bmap), 1);

    bitmap_clear_bit(bmap, i);
    assertEquals(true, CHECK_MAP(bmap, BITMAP_SIZE, { BITMAP_SIZE }));
    assertEquals(bitmap_get_count(bmap), 0);

    assertError(bitmap_get_bit, bmap, -4);
    assertError(bitmap_get_bit, bmap, BITMAP_SIZE);
    assertError(bitmap_get_bit, bmap, BITMAP_SIZE + 4);
    assertError(bitmap_get_bit, bmap, "24xxx");

    i = 0;

    bitmap_set_bit(bmap, i);
    assertEquals(true, CHECK_MAP(bmap, BITMAP_SIZE,
                                 { i, 1, BITMAP_SIZE-(i+1) }));
    assertEquals(bitmap_get_bit(bmap, i), true);
    assertEquals(bitmap_get_count(bmap), 1);
    assertEquals(bitmap_get_bit(bmap, j), false);

    bitmap_clear_bit(bmap, j);
    assertEquals(true, CHECK_MAP(bmap, BITMAP_SIZE,
                                 { i, 1, BITMAP_SIZE-(i+1) }));
    assertEquals(bitmap_get_bit(bmap, j), false);
    assertEquals(bitmap_get_bit(bmap, i), true);
    assertEquals(bitmap_get_count(bmap), 1);

    bitmap_clear_bit(bmap, i);
    assertEquals(true, CHECK_MAP(bmap, BITMAP_SIZE, { BITMAP_SIZE }));
    assertEquals(bitmap_get_count(bmap), 0);

    i = BITMAP_SIZE - 1;
    bitmap_set_bit(bmap, i);
    assertEquals(true, CHECK_MAP(bmap, BITMAP_SIZE,
                                 { i, 1, BITMAP_SIZE-(i+1) }));
    assertEquals(bitmap_get_bit(bmap, i), true);
    assertEquals(bitmap_get_count(bmap), 1);

    assertEquals(bitmap_get_bit(bmap, j), false);
    bitmap_clear_bit(bmap, j);
    assertEquals(true, CHECK_MAP(bmap, BITMAP_SIZE,
                                 { i, 1, BITMAP_SIZE-(i+1) }));
    assertEquals(bitmap_get_bit(bmap, j), false);
    assertEquals(bitmap_get_bit(bmap, i), true);
    assertEquals(bitmap_get_count(bmap), 1);

    bitmap_clear_bit(bmap, i);
    assertEquals(true, CHECK_MAP(bmap, BITMAP_SIZE, { BITMAP_SIZE }));
    assertEquals(bitmap_get_count(bmap), 0);

    bmap = nil;
  end

  function test_bitmap:test_ranges()
    local bmap;
    local i;
    local j;
    local p;
    local q;

    bmap = bitmap(BITMAP_SIZE);
    bitmap_set_all(bmap);
    assertEquals(true, CHECK_MAP(bmap, BITMAP_SIZE, { 0, BITMAP_SIZE }));
    assertEquals(bitmap_get_count(bmap), BITMAP_SIZE);

    p = BITMAP_SIZE - 34;
    q = 34;
    while p < BITMAP_SIZE do
      assertEquals(q, bitmap_count_range(bmap, p, BITMAP_SIZE - 1))
      p = p + 1;
      q = q - 1;
    end

    q = 5;
    p = 62;
    while p < 96 do
      assertEquals(q, bitmap_count_range(bmap, p, p + q - 1))
      p = p + 1;
    end

    q = 33;
    p = 62;
    while p < 96 do
      assertEquals(q, bitmap_count_range(bmap, p, p + q - 1))
      p = p + 1;
    end

    bitmap_clear_all(bmap);
    assertEquals(true, CHECK_MAP(bmap, BITMAP_SIZE, { BITMAP_SIZE }));

    p = 0;
    q = BITMAP_SIZE - 1;

    bitmap_set_range(bmap, q, q);
    assertEquals(true, CHECK_MAP(bmap, BITMAP_SIZE, { BITMAP_SIZE-1, 1 }));
    assertEquals(bitmap_get_count(bmap), 1);
    bitmap_set_range(bmap, p, p);
    assertEquals(bitmap_get_count(bmap), 2);

    assertEquals(1, bitmap_count_consecutive(bmap, p, 1));
    assertEquals(1, bitmap_count_consecutive(bmap, q, 1));
    assertEquals(0, bitmap_count_consecutive(bmap, p, 0));
    assertEquals(0, bitmap_count_consecutive(bmap, q, 0));
    assertEquals(q - p - 1, bitmap_count_consecutive(bmap, p+1, 0));

    bitmap_clear_range(bmap, q, q);
    assertEquals(true, CHECK_MAP(bmap, BITMAP_SIZE,
                                 { 0, 1, BITMAP_SIZE-(p+1) }));
    assertEquals(bitmap_get_count(bmap), 1);
    bitmap_clear_range(bmap, p, p);
    assertEquals(true, CHECK_MAP(bmap, BITMAP_SIZE, { BITMAP_SIZE }));
    assertEquals(bitmap_get_count(bmap), 0);

    j = 62;
    bitmap_set_range(bmap, j, j+1);
    assertEquals(bitmap_get_count(bmap), 2);
    assertEquals(bitmap_count_consecutive(bmap, j, 1), 2);
    assertEquals(true, CHECK_MAP(bmap, BITMAP_SIZE,
                                 { j, 2, BITMAP_SIZE-(j+2) }));

    j = 61;
    bitmap_set_range(bmap, j, j+3);
    assertEquals(bitmap_get_count(bmap), 4);
    assertEquals(bitmap_count_consecutive(bmap, j, 1), 4);

    j = 64;
    bitmap_set_range(bmap, j, j+1);
    assertEquals(bitmap_get_count(bmap), 5);
    assertEquals(bitmap_count_consecutive(bmap, j, 2), 2);
    assertEquals(bitmap_count_consecutive(bmap, 61, 1), 5);

    j = 62;
    bitmap_clear_range(bmap, j, j+1);
    assertEquals(bitmap_get_count(bmap), 3);

    j = 61;
    bitmap_clear_range(bmap, j, j+3);
    assertEquals(true, CHECK_MAP(bmap, BITMAP_SIZE,
                                 { 65, 1, BITMAP_SIZE-(65+1) }));
    assertEquals(bitmap_get_count(bmap), 1);

    j = 64;
    bitmap_clear_range(bmap, j, j+1);
    assertEquals(true, CHECK_MAP(bmap, BITMAP_SIZE, { BITMAP_SIZE }));
    assertEquals(bitmap_get_count(bmap), 0);

    assertError(bitmap_set_range, bmap, BITMAP_SIZE + 3, BITMAP_SIZE + 4)
    assertError(bitmap_set_range, bmap, BITMAP_SIZE - 3, BITMAP_SIZE + 4)
    assertError(bitmap_set_range, bmap, BITMAP_SIZE, BITMAP_SIZE + 4)
    assertError(bitmap_set_range, bmap, BITMAP_SIZE - 3, BITMAP_SIZE)
    assertError(bitmap_set_range, bmap, BITMAP_SIZE + 3, BITMAP_SIZE - 4)
    assertError(bitmap_set_range, bmap, BITMAP_SIZE - 3, BITMAP_SIZE - 4)

    assertError(bitmap_set_range, bmap, -3, 4)
    assertError(bitmap_set_range, bmap, -3, 0)
    assertError(bitmap_set_range, bmap, 3, -4)
    assertError(bitmap_set_range, bmap, -3, -4)

    assertError(bitmap_set_range, bmap, 0, "foo")
    assertError(bitmap_set_range, bmap, "foo", 1)
    assertError(bitmap_set_range, "foo", 0, 1)

    assertError(bitmap_get_range, bmap, BITMAP_SIZE + 3, BITMAP_SIZE + 4)
    assertError(bitmap_get_range, bmap, BITMAP_SIZE - 3, BITMAP_SIZE + 4)
    assertError(bitmap_get_range, bmap, BITMAP_SIZE, BITMAP_SIZE + 4)
    assertError(bitmap_get_range, bmap, BITMAP_SIZE - 3, BITMAP_SIZE)
    assertError(bitmap_get_range, bmap, BITMAP_SIZE + 3, BITMAP_SIZE - 4)
    assertError(bitmap_get_range, bmap, BITMAP_SIZE - 3, BITMAP_SIZE - 4)

    assertError(bitmap_get_range, bmap, -3, 4)
    assertError(bitmap_get_range, bmap, -3, 0)
    assertError(bitmap_get_range, bmap, 3, -4)
    assertError(bitmap_get_range, bmap, -3, -4)

    assertError(bitmap_get_range, bmap, 0, "foo")
    assertError(bitmap_get_range, bmap, "foo", 1)
    assertError(bitmap_get_range, "foo", 0, 1)

    assertError(bitmap_clear_range, bmap, BITMAP_SIZE + 3, BITMAP_SIZE + 4)
    assertError(bitmap_clear_range, bmap, BITMAP_SIZE - 3, BITMAP_SIZE + 4)
    assertError(bitmap_clear_range, bmap, BITMAP_SIZE, BITMAP_SIZE + 4)
    assertError(bitmap_clear_range, bmap, BITMAP_SIZE - 3, BITMAP_SIZE)
    assertError(bitmap_clear_range, bmap, BITMAP_SIZE + 3, BITMAP_SIZE - 4)
    assertError(bitmap_clear_range, bmap, BITMAP_SIZE - 3, BITMAP_SIZE - 4)

    assertError(bitmap_clear_range, bmap, -3, 4)
    assertError(bitmap_clear_range, bmap, -3, 0)
    assertError(bitmap_clear_range, bmap, 3, -4)
    assertError(bitmap_clear_range, bmap, -3, -4)

    assertError(bitmap_clear_range, bmap, 0, "24xxx")
    assertError(bitmap_clear_range, bmap, "24xxx", 1)
    assertError(bitmap_clear_range, "24xxx", 0, 1)

    bmap = nil;
  end

  function test_bitmap:test_iterators()

    local function CHECK_ITER(ci_bmap, ci_vals)
      if silk.type(ci_bmap) ~= "bitmap" then
        error("expected bitmap as first arg, got "
                .. silk.type(ci_bmap))
      end
      if silk.type(ci_vals) ~= "table" then
        error("expected table as third arg, got "
                .. silk.type(ci_vals))
      end
      local iter = bitmap_iter(ci_bmap);
      local i = 1;
      local j;
      for j in iter do
        assertEquals(i <= #ci_vals, true);
        assertEquals(j, ci_vals[i]);
        i = i + 1;
      end
      assertEquals(i, 1 + #ci_vals)
    end

    local bmap;
    local vals;

    bmap = bitmap(BITMAP_SIZE);
    assertEquals(true, CHECK_MAP(bmap, BITMAP_SIZE, { BITMAP_SIZE }));
    assertEquals(bitmap_get_size(bmap), BITMAP_SIZE);
    assertEquals(bitmap_get_count(bmap), 0);

    CHECK_ITER(bmap, { })

    bitmap_set_bit(bmap, 0)
    CHECK_ITER(bmap, { 0 })
    bitmap_clear_all(bmap)

    bitmap_set_bit(bmap, BITMAP_SIZE-1)
    CHECK_ITER(bmap, { BITMAP_SIZE-1 })
    bitmap_clear_all(bmap)

    vals = {
      32, 63, 65, 96,
      98, 99, 100, 102,
      105, 106, 120, 121,
      126, 127, 128, 159
    };
    for k,v in pairs(vals) do
      bitmap_set_bit(bmap, v);
      assertEquals(true, bitmap_get_bit(bmap, v));
    end
    assertEquals(bitmap_get_count(bmap), #vals);

    CHECK_ITER(bmap, vals);

    bmap = nil;
  end
end -- test_bitmap

test_datetime = {}
do

  local NIL = {}

  -- Test datetime construction
  local dates  =  {
    ["2004/11/04"] = 1099526400000,
    ["2004/11/04   "] = 1099526400000,
    ["   2004/11/04"] = 1099526400000,
    [" 2004/11/04  "] = 1099526400000,
    ["2004/11/04:11"] = 1099566000000,
    ["2004/11/04T11"] = 1099566000000,
    ["2004/11/4:11:12"] = 1099566720000,
    ["2004/11/4:11:12:13"] = 1099566733000,
    ["2004/11/4:11:12:13.456"] = 1099566733456,
    ["1099566733"] = 1099566733000,
    ["1099566733.456"] = 1099566733456,
    ["2004/11/4:11:12:13.4"] = 1099566733400,
    ["2004/11/4:11:12:13.45"] = 1099566733450,
    ["2004/11/4T11:12:13.45"] = 1099566733450,
    ["2004/11/4:11:12:13.456111111"] = 1099566733456,
    ["2004/11/4:11:12:13.456999999"] = 1099566733456,
    ["2004/11/4:11:12:13:14"] = NIL,
    ["2004/11/4:11:12:13-2004/11/4:11:12:14"] = NIL,
    ["2004-11-4"] = NIL,
    ["2004/11/4:11:12:13  x"] = NIL,
    ["200411.04"] = NIL,
    ["109956673345629384756"] = NIL,
    ["2004"] = NIL,
    ["2004/"] = NIL,
    ["  2004/11/4 11:12:13  "] = NIL,
    ["2004/11"] = NIL,
    ["2004/11/"] = NIL,
    ["2004/0/4"] = NIL,
    ["2004/13/4"] = NIL,
    ["1959/01/01"] = NIL,
    ["2004/11/4:-3:-3:-3"] = NIL,
    ["2004/11/4::11:12"] = NIL,
    ["2004/11/31"] = NIL,
    ["2004/11/4:24"] = NIL,
    ["2004/11/4:23:60:59"] = NIL,
    ["2004/11/4:23:59:60"] = NIL,
    ["2004/11/40"] = NIL,
    ["2004/11/4:110"] = NIL,
    ["2004/11/4:11:120"] = NIL,
    ["2004/11/4:11:12:130"] = NIL,
    ["   "] = NIL,
    [""] = NIL,
  }

  local ranges =
    {
      ["2004/11/04"] = NIL,
      ["2004/11/04   "] = NIL,
      ["   2004/11/04"] = NIL,
      [" 2004/11/04  "] = NIL,
      ["2004/11/04:11"] = NIL,
      ["2004/11/4:11:12"] = NIL,
      ["2004/11/4:11:12:13"] = NIL,
      ["2004/11/4:11:12:13:14"] = NIL,
      ["2004/11/04-2004/11/05"] =
        {1099526400000, 1099699199999},
      ["2004/11/4:11:12:13-2004/11/4:11:12:13"] =
        {1099566733000, 1099566734999},
      ["2004/11/4:11:12:13-   2004/11/4:11:12:14"] =
        {1099566733000, 1099566734999},
      ["2004/11/4:11:12:13.000-2004/11/4:11:12:14.000"] =
        {1099566733000, 1099566734000},
      ["2004/11/4:11:12:13-2004/11/4:11:12:14"] =
        {1099566733000, 1099566734999},
      ["2004/11/4:11:12:13-2004/11/4:11:12:13"] =
        {1099566733000, 1099566733999},
      ["2004/11/4:11:12:13-2004/11/4:11:12:12"] = NIL,
      ["2004/11/4:11:12:13-2004/11/4:11:13"] =
        {1099566733000, 1099566839999},
      ["2004/11/4:11:12:13-2004/11/4:12"] =
        {1099566733000, 1099573199999},
      ["2004/11/4:11:12:13-2004/11/5"] =
        {1099566733000, 1099699199999},
      ["2004/11/4:11:12:13-2004"] = NIL,
      ["2004/11/4:11:12:13-2004/"] = NIL,
      ["2004/11/4:11:12:13-  2004/11/4 11:12:13  "] = NIL,
      ["2004/11/4:11:12:13-2004/11/"] = NIL,
      ["2004/11/4:11:12:13-2004/0/4"] = NIL,
      ["2004/11/4:11:12:13-2004/13/4"] = NIL,
      ["2004/11/4:11:12:13-1959/01/01"] = NIL,
      ["2004/11/4:11:12:13-2004/11/4:-3:-3:-3"] = NIL,
      ["2004/11/4:11:12:13-2004/11/4::11:12"] = NIL,
      ["2004/11/4:11:12:13-2004/11/31"] = NIL,
      ["2004/11/4:11:12:13-2004/11/4:24"] = NIL,
      ["2004/11/4:11:12:13-2004/11/4:23:60:59"] = NIL,
      ["2004/11/4:11:12:13-2004/11/4:23:59:60"] = NIL,
      ["   "] = NIL,
      [""] = NIL,
    }


  function test_datetime:test_construction ()
    for s, n in pairs(dates) do
      if n == NIL then
        assertEquals(pcall(datetime, s), false)
      else
        local ds = datetime(s)
        local dn = datetime(n)
        assertEquals(ds, dn)
      end
    end
  end

  function test_datetime:test_ranges ()
    for s, n in pairs(ranges) do
      if n == NIL then
        assertEquals(pcall(datetime_parse_range, s), false)
      else
        local s1, e1 = datetime_parse_range(s)
        local s2, e2 = datetime(n[1]), datetime(n[2])
        assertEquals(s1, s2)
        assertEquals(e1, e2)
      end
    end
  end

  function test_datetime:test_arithmetic ()
    local a = datetime("2014/08/05T13:00:00")
    local b = datetime("2014/08/05T13:00:10")
    local c = datetime_difference(b, a)
    assertEquals(c, 10000)
    local d = datetime_add_duration(a, c)
    assertEquals(d, b)
    d = datetime_add_duration(b, -c)
    assertEquals(d, a)
  end

  function test_datetime:test_ordering ()
    local a = datetime("2014/08/05T13:00:00")
    local b = datetime("2014/08/05T13:00:10")
    local c = datetime("2014/08/05T13:00:10")
    assertEquals(a <  b, true)
    assertEquals(a <= b, true)
    assertEquals(a == b, false)
    assertEquals(a >= b, false)
    assertEquals(a >  b, false)
    assertEquals(b <  a, false)
    assertEquals(b <= a, false)
    assertEquals(b == a, false)
    assertEquals(b >= a, true)
    assertEquals(b >  a, true)
    assertEquals(b <  c, false)
    assertEquals(b <= c, true)
    assertEquals(b == c, true)
    assertEquals(b >= c, true)
    assertEquals(b >  c, false)
  end

  function test_datetime:test_to_string ()
    local a = datetime("2014/08/05T13:00:00")
    assertEquals(datetime_to_string(a), "2014/08/05T13:00:00.000")
    assertEquals(tostring(a), "2014/08/05T13:00:00.000")
    assertEquals(datetime_to_string(a, "y/m/d"), "2014/08/05T13:00:00.000")
    assertEquals(datetime_to_string(a, "no-msec"), "2014/08/05T13:00:00")
    assertEquals(datetime_to_string(a, "m/d/y"), "08/05/2014 13:00:00.000")
    assertEquals(datetime_to_string(a, "m/d/y", "no-msec"),
                 "08/05/2014 13:00:00")
    assertEquals(datetime_to_string(a, "epoch"), "1407243600.000")
    assertEquals(datetime_to_string(a, "epoch", "no-msec"), "1407243600")
    assertEquals(datetime_to_string(a, "iso"), "2014-08-05 13:00:00.000")
    assertEquals(datetime_to_string(a, "iso", "no-msec"),
                 "2014-08-05 13:00:00")
  end

end -- test_datetime


local failures = LuaUnit:run()
os.exit(0 == failures, true)


-- Local Variables:
-- mode:lua
-- indent-tabs-mode:nil
-- lua-indent-level:2
-- End:
