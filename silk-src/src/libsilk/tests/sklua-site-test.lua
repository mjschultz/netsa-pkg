local site_file = arg[1]

-- For test simplicity, put the silk.site functions into the current
-- environment (except for type)
for k, v in pairs(silk.site) do
  if k ~= 'type' then
    _ENV[k] = v
  end
end

local LuaUnit = require 'luaunit'

test_site_load = {}
do
  function test_site_load:test_site_load ()
    assertEquals(have_site_config(), false)
    assertEquals(init_site('NONEXISTANT'), false)
    assertEquals(have_site_config(), false)
    assertEquals(init_site(nil, '/NONEXISTANT'), false)
    assertEquals(have_site_config(), false)
    assertEquals(init_site(site_file), true)
    assertEquals(have_site_config(), true)
  end
end

test_site = {}
do
  local all_sensors = {"Sensor1", "Sensor3", "Sensor4", "Sensor5",
                       "Sensor11", "UndescribedSensor17", "S"}

  local all_classtypes = {"any/type0", "any/type1", "foo-class/typename2",
                          "foo-class/type1", "foo-class/type5",
                          "bar-class/type1","bar-class/type8",
                          "any/type10", "any/type11"}

  local function assert_set_equal(a, b)
    local s = {}
    for _, v in ipairs(b) do
      s[v] = true
    end
    for _, v in ipairs(a) do
      assertEquals(s[v], true)
      s[v] = false
    end
    for k, v in pairs(s) do
      assertEquals(v, false)
    end
  end

  local function setone (x)
    x[1] = "foo"
  end

  function test_site:test_default_class ()
    assertEquals(default_class(), "foo-class")
  end

  function test_site:test_sensors ()
    assert_set_equal(sensors(), all_sensors)
    assertError(setone, sensors())
  end

  function test_site:test_class_sensors ()
    assert_set_equal(class_sensors('any'), all_sensors)
    assert_set_equal(class_sensors('foo-class'),
                     {"Sensor3", "Sensor4"})
    assert_set_equal(class_sensors('bar-class'),
                     {"Sensor4", "Sensor5"})
    assertError(class_sensors, 'all')
    assertError(class_sensors)
    assertError(setone, class_sensors('any'))
  end

  function test_site:test_classes ()
    assert_set_equal(classes(), {"any", "foo-class", "bar-class"})
    assertError(setone, classes())
  end

  function test_site:test_sensor_classes ()
    assert_set_equal(sensor_classes('Sensor1'),
                     {"any"})
    assert_set_equal(sensor_classes('Sensor3'),
                     {"any", "foo-class"})
    assert_set_equal(sensor_classes('Sensor4'),
                     {"any", "foo-class", "bar-class"})
    assert_set_equal(sensor_classes('Sensor5'),
                     {"any", "bar-class"})
    assert_set_equal(sensor_classes('Sensor11'),
                     {"any"})
    assert_set_equal(sensor_classes('UndescribedSensor17'),
                     {"any"})
    assert_set_equal(sensor_classes('S'),
                     {"any"})
    assertError(sensor_classes, 'none')
    assertError(sensor_classes)
    assertError(setone, sensor_classes('Sensor1'))
  end

  function test_site:test_sensor_description ()
    assertEquals(sensor_description('Sensor1'), "Sensor 1")
    assertEquals(sensor_description('Sensor3'), "Sensor 3")
    assertEquals(sensor_description('Sensor4'), "Sensor 4")
    assertEquals(sensor_description('Sensor5'), "Sensor 5")
    assertEquals(sensor_description('Sensor11'),
                 "A rather long sensor description for eleven")
    assertEquals(sensor_description('UndescribedSensor17'), nil)
    assertEquals(sensor_description('S'),
                 "A sensor with a very short name")
    assertError(sensor_description, 'none')
    assertError(sensor_description)
  end

  function test_site:test_classtypes ()
    local ct = classtypes()
    local cct = {}
    for i, v in ipairs(ct) do
      cct[i] = table.concat(v, '/')
    end
    assert_set_equal(cct, all_classtypes)
    assertError(setone, ct)
    assertError(setone, ct[1])
  end

  function test_site:test_flowtypes ()
    assert_set_equal(flowtypes(),
                     {"t0", "t1", "t2", "t5", "t7", "t8",
                      "t10", "t11", "tt1"})
    assertError(setone, flowtypes())
  end

  function test_site:test_types ()
    assert_set_equal(types('any'),
                     {"type0", "type1", "type10", "type11"})
    assert_set_equal(types('foo-class'),
                     {"typename2", "type1", "type5"})
    assert_set_equal(types('bar-class'),
                     {"type1", "type8"})
    assertError(setone, types('bar-class'))
    assertError(types, 'all')
    assertError(types)
  end

  function test_site:test_default_types ()
    assert_set_equal(default_types('any'),
                     {"type1", "type10"})
    assert_set_equal(default_types('foo-class'),
                     {"type5", "type1"})
    assert_set_equal(default_types('bar-class'), {})
    assertError(setone, default_types('foo-class'))
    assertError(default_types, 'all')
    assertError(default_types)
  end

  function test_site:test_sensor_id ()
    assertEquals(sensor_id('Sensor1'), 1)
    assertEquals(sensor_id('Sensor3'), 3)
    assertEquals(sensor_id('Sensor4'), 4)
    assertEquals(sensor_id('Sensor5'), 5)
    assertEquals(sensor_id('Sensor11'), 11)
    assertEquals(sensor_id('UndescribedSensor17'), 17)
    assertEquals(sensor_id('S'), 2)
    assertError(sensor_id, 'none')
    assertError(sensor_id)
  end

  function test_site:test_sensor_from_id ()
    assertEquals(sensor_from_id(1), 'Sensor1')
    assertEquals(sensor_from_id(3), 'Sensor3')
    assertEquals(sensor_from_id(4), 'Sensor4')
    assertEquals(sensor_from_id(5), 'Sensor5')
    assertEquals(sensor_from_id(11), 'Sensor11')
    assertEquals(sensor_from_id(17), 'UndescribedSensor17')
    assertEquals(sensor_from_id(2), 'S')
    assertError(sensor_from_id, 6)
    assertError(sensor_from_id)
  end

  function test_site:test_classtype_id ()
    assertEquals(classtype_id("any", "type0"), 0)
    assertEquals(classtype_id("any", "type1"), 1)
    assertEquals(classtype_id("foo-class", "typename2"), 2)
    assertEquals(classtype_id("foo-class", "type1"), 3)
    assertEquals(classtype_id("foo-class", "type5"), 5)
    assertEquals(classtype_id("bar-class", "type1"), 7)
    assertEquals(classtype_id("bar-class", "type8"), 8)
    assertEquals(classtype_id("any", "type10"), 10)
    assertEquals(classtype_id("any", "type11"), 11)
    assertEquals(classtype_id{"any", "type0"}, 0)
    assertEquals(classtype_id{"any", "type1"}, 1)
    assertEquals(classtype_id{"foo-class", "typename2"}, 2)
    assertEquals(classtype_id{"foo-class", "type1"}, 3)
    assertEquals(classtype_id{"foo-class", "type5"}, 5)
    assertEquals(classtype_id{"bar-class", "type1"}, 7)
    assertEquals(classtype_id{"bar-class", "type8"}, 8)
    assertEquals(classtype_id{"any", "type10"}, 10)
    assertEquals(classtype_id{"any", "type11"}, 11)
    assertError(classtype_id, 'all', 'type1')
    assertError(classtype_id, 'any', 'type5')
    assertError(classtype_id, {'all', 'type1'})
    assertError(classtype_id, {'any', 'type5'})
    assertError(classtype_id, 'any')
    assertError(classtype_id, {'any'})
    assertError(classtype_id, {'any', 'type1', 'type0'})
    assertError(classtype_id)
  end

  function test_site:test_flowtype_id ()
    assertEquals(flowtype_id("t0"), 0)
    assertEquals(flowtype_id("t1"), 1)
    assertEquals(flowtype_id("t2"), 2)
    assertEquals(flowtype_id("t5"), 5)
    assertEquals(flowtype_id("t7"), 7)
    assertEquals(flowtype_id("t8"), 8)
    assertEquals(flowtype_id("t10"), 10)
    assertEquals(flowtype_id("t11"), 11)
    assertEquals(flowtype_id("tt1"), 3)
    assertError(flowtype_id, "t3")
    assertError(flowtype_id)
  end

  local function assert_pair_equals (a, b)
    assertEquals(#a, 2)
    assertEquals(#b, 2)
    assertEquals(a[1], b[1])
    assertEquals(a[2], b[2])
  end

  function test_site:test_classtype_from_id ()
    assert_pair_equals(classtype_from_id(0), {"any", "type0"})
    assert_pair_equals(classtype_from_id(1), {"any", "type1"})
    assert_pair_equals(classtype_from_id(2), {"foo-class", "typename2"})
    assert_pair_equals(classtype_from_id(3), {"foo-class", "type1"})
    assert_pair_equals(classtype_from_id(5), {"foo-class", "type5"})
    assert_pair_equals(classtype_from_id(7), {"bar-class", "type1"})
    assert_pair_equals(classtype_from_id(8), {"bar-class", "type8"})
    assert_pair_equals(classtype_from_id(10), {"any", "type10"})
    assert_pair_equals(classtype_from_id(11), {"any", "type11"})
    assertError(setone, classtype_from_id(11))
    assertError(classtype_from_id, 4)
    assertError(classtype_from_id)
  end

  function test_site:test_flowtype_from_id ()
    assertEquals(flowtype_from_id(0), "t0")
    assertEquals(flowtype_from_id(1), "t1")
    assertEquals(flowtype_from_id(2), "t2")
    assertEquals(flowtype_from_id(5), "t5")
    assertEquals(flowtype_from_id(7), "t7")
    assertEquals(flowtype_from_id(8), "t8")
    assertEquals(flowtype_from_id(10), "t10")
    assertEquals(flowtype_from_id(11), "t11")
    assertEquals(flowtype_from_id(3), "tt1")
    assertError(flowtype_from_id, 4)
    assertError(flowtype_from_id)
  end

  function test_site:test_classtype_from_flowtype ()
    assert_pair_equals(classtype_from_flowtype("t0"), {"any", "type0"})
    assert_pair_equals(classtype_from_flowtype("t1"), {"any", "type1"})
    assert_pair_equals(classtype_from_flowtype("t2"),
                       {"foo-class", "typename2"})
    assert_pair_equals(classtype_from_flowtype("tt1"), {"foo-class", "type1"})
    assert_pair_equals(classtype_from_flowtype("t5"), {"foo-class", "type5"})
    assert_pair_equals(classtype_from_flowtype("t7"), {"bar-class", "type1"})
    assert_pair_equals(classtype_from_flowtype("t8"), {"bar-class", "type8"})
    assert_pair_equals(classtype_from_flowtype("t10"), {"any", "type10"})
    assert_pair_equals(classtype_from_flowtype("t11"), {"any", "type11"})
    assertError(setone, classtype_from_flowtype("t11"))
    assertError(classtype_from_flowtype, "t3")
    assertError(classtype_from_flowtype)
  end

  function test_site:test_flowtype_from_classtype ()
    assertEquals(flowtype_from_classtype("any", "type0"), "t0")
    assertEquals(flowtype_from_classtype("any", "type1"), "t1")
    assertEquals(flowtype_from_classtype("foo-class", "typename2"), "t2")
    assertEquals(flowtype_from_classtype("foo-class", "type1"), "tt1")
    assertEquals(flowtype_from_classtype("foo-class", "type5"), "t5")
    assertEquals(flowtype_from_classtype("bar-class", "type1"), "t7")
    assertEquals(flowtype_from_classtype("bar-class", "type8"), "t8")
    assertEquals(flowtype_from_classtype("any", "type10"), "t10")
    assertEquals(flowtype_from_classtype("any", "type11"), "t11")
    assertEquals(flowtype_from_classtype{"any", "type0"}, "t0")
    assertEquals(flowtype_from_classtype{"any", "type1"}, "t1")
    assertEquals(flowtype_from_classtype{"foo-class", "typename2"}, "t2")
    assertEquals(flowtype_from_classtype{"foo-class", "type1"}, "tt1")
    assertEquals(flowtype_from_classtype{"foo-class", "type5"}, "t5")
    assertEquals(flowtype_from_classtype{"bar-class", "type1"}, "t7")
    assertEquals(flowtype_from_classtype{"bar-class", "type8"}, "t8")
    assertEquals(flowtype_from_classtype{"any", "type10"}, "t10")
    assertEquals(flowtype_from_classtype{"any", "type11"}, "t11")
    assertError(flowtype_from_classtype, 'all', 'type1')
    assertError(flowtype_from_classtype, 'any', 'type5')
    assertError(flowtype_from_classtype, {'all', 'type1'})
    assertError(flowtype_from_classtype, {'any', 'type5'})
    assertError(flowtype_from_classtype, 'any')
    assertError(flowtype_from_classtype, {'any'})
    assertError(flowtype_from_classtype, {'any', 'type1', 'type0'})
    assertError(flowtype_from_classtype)
  end

end

local failures = LuaUnit:run("test_site_load", "test_site")
os.exit(0 == failures, true)

-- Local Variables:
-- mode:lua
-- indent-tabs-mode:nil
-- lua-indent-level:2
-- End:
