#include "script/script_world.h"

namespace engine_placeholder::script
{

// The host of minted scripts (Phase 27 Day 463). The backend compiles each script without running it and hands the
// function to `__module_define`; `__module_run` runs one once, with `require` allowed only while it runs and only for
// the keys it declared. A module's value is what its chunk returns (`true` when it returns nothing, as in Lua).
const char* LuaModuleHostSource()
{
    return R"LUA(
local type, error, pcall, tostring = type, error, pcall, tostring
local gmatch = string.gmatch
local fns, vals, ran, running, allowed = {}, {}, {}, {}, {}
local current = nil  -- the key of the module whose chunk is running

function __module_define(fn, key, deps)
  if type(fn) ~= 'function' or type(key) ~= 'string' or type(deps) ~= 'string' then
    error('module: bad arguments', 2)
  end
  if fns[key] ~= nil then
    error('module ' .. key .. ' is defined twice', 0)
  end
  fns[key] = fn
  local set = {}
  for k in gmatch(deps, '[^\n]+') do
    set[k] = true
  end
  allowed[key] = set
end

function __module_run(key)
  if ran[key] then
    return true
  end
  local fn = fns[key]
  if fn == nil then
    error('no such module ' .. tostring(key), 0)
  end
  if running[key] then
    error('a dependency cycle through module ' .. key, 0)
  end
  running[key] = true
  local previous = current
  current = key
  local ok, result = pcall(fn)
  current = previous
  running[key] = nil
  if not ok then
    error(result, 0)
  end
  ran[key] = true
  if result == nil then
    result = true
  end
  vals[key] = result
  return true
end

function __require(key)
  if current == nil then
    error('require is only available while a script loads', 2)
  end
  if type(key) ~= 'string' or not allowed[current][key] then
    error('module ' .. tostring(key) .. ' is not a declared dependency of this script', 2)
  end
  if not ran[key] then
    __module_run(key)
  end
  return vals[key]
end
)LUA";
}

}  // namespace engine_placeholder::script
