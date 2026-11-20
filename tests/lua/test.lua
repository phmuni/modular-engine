-- The helper every Lua test gets as the global `test` (loaded by tests/cpp/luaHost.cpp, which also gives test.fail,
-- test.finish, test.expectError and test.frame). A failed check is recorded with its file and line and the test goes on;
-- the process exits non-zero when any failed.
--
--   test.ok(value, message)            value is truthy
--   test.eq(actual, expected, message) equal: numbers, strings, booleans, vec3s, entities; tables item by item
--   test.near(actual, expected, eps, message)   numbers or vec3s, within eps (0.001 by default)
--   test.expectError("part")           the engine will report a script error containing this (it is not a failure)
--   test.run(function() ... end)       engine.run, then test.done() when the function returns (it may wait())
--   test.done()                        the test is over
--   test.frame()                       frames since the start

local function describe(value)
  if type(value) == "string" then
    return string.format("%q", value)
  elseif type(value) == "table" then
    local parts = {}
    for index, item in ipairs(value) do
      parts[index] = describe(item)
    end
    return "{" .. table.concat(parts, ", ") .. "}"
  end
  return tostring(value)
end

-- "file:line:" of whoever called the assertion (error() with a level adds that position to its message)
local function where()
  local _, position = pcall(error, "", 4)
  return position
end

local function report(position, message, detail)
  test.fail(position .. " " .. (message and (message .. ": ") or "") .. detail)
  return false
end

local function same(a, b)
  if type(a) == "table" and type(b) == "table" then
    if #a ~= #b then
      return false
    end
    for index = 1, #a do
      if not same(a[index], b[index]) then
        return false
      end
    end
    return true
  end
  return a == b
end

function test.ok(value, message)
  if value then
    return true
  end
  return report(where(), message, "expected a truthy value, got " .. describe(value))
end

function test.eq(actual, expected, message)
  if same(actual, expected) then
    return true
  end
  return report(where(), message, "expected " .. describe(expected) .. ", got " .. describe(actual))
end

function test.near(actual, expected, epsilon, message)
  epsilon = epsilon or 0.001
  local close
  if type(actual) == "number" and type(expected) == "number" then
    close = math.abs(actual - expected) <= epsilon
  elseif type(actual) == "userdata" and type(expected) == "userdata" then
    close = (actual - expected):length() <= epsilon
  else
    return report(where(), message, "near needs two numbers or two vec3s, got " .. describe(actual) .. " and " .. describe(expected))
  end
  if close then
    return true
  end
  return report(where(), message, "expected " .. describe(expected) .. " (within " .. epsilon .. "), got " .. describe(actual))
end

function test.done()
  test.finish()
end

function test.run(body)
  return engine.run(function()
    body()
    test.done()
  end)
end
