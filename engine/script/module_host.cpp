#include "script/script_world.h"

namespace vse::script
{

// The host of minted scripts (Phase 27 Day 463, reload Day 464). The chunk returns its functions to the backend
// (`State::LoadHost`), which keeps them outside the scripts' globals: `module_env` makes the environment of a module
// (its own `__require`, bound to its key and to the keys it declared; reads and writes of other names go to the
// state's globals), `module_store` keeps a compiled module, `module_run` runs one once, and the `reload_*` functions
// let a reload replace modules and put everything back when it fails. A module's value is what its chunk returns
// (`true` when nothing, as in Lua).
const char* LuaModuleHostSource()
{
    return R"LUA(
local type, error, pcall, tostring, setmetatable, pairs = type, error, pcall, tostring, setmetatable, pairs
local gmatch = string.gmatch
local globals = _G
local fns, vals, ran, running, allowed = {}, {}, {}, {}, {}
local reloading = false
local saved = nil  -- during a reload: what to put back

local run

local function module_env(key, deps)
  if type(key) ~= 'string' or type(deps) ~= 'string' then
    error('module: bad arguments', 2)
  end
  if fns[key] ~= nil and not reloading then
    error('module ' .. key .. ' is defined twice', 0)
  end
  local set = {}
  for k in gmatch(deps, '[^\n]+') do
    set[k] = true
  end
  allowed[key] = set
  local env = {}
  env.__require = function(wanted)
    if not running[key] then
      error('require is only available while a script loads', 2)
    end
    if type(wanted) ~= 'string' or not set[wanted] then
      error('module ' .. tostring(wanted) .. ' is not a declared dependency of this script', 2)
    end
    if not ran[wanted] then
      run(wanted)
    end
    return vals[wanted]
  end
  return setmetatable(env, { __index = globals, __newindex = globals })
end

local function module_store(fn, key)
  if reloading and saved.fns[key] == nil and fns[key] ~= nil then
    saved.fns[key] = fns[key]
  end
  fns[key] = fn
end

run = function(key)
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
  local ok, result = pcall(fn)
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

local function module_rerun(key)
  if reloading and saved.ran[key] == nil then
    saved.ran[key] = ran[key] or false
    saved.vals[key] = vals[key]
  end
  ran[key] = nil
  vals[key] = nil
  return run(key)
end

local function reload_begin()
  reloading = true
  saved = { fns = {}, vals = {}, ran = {} }
end

local function reload_commit()
  reloading = false
  saved = nil
end

local function reload_abort()
  if saved ~= nil then
    for key, fn in pairs(saved.fns) do
      fns[key] = fn
    end
    for key, was_run in pairs(saved.ran) do
      ran[key] = was_run or nil
      vals[key] = saved.vals[key]
      running[key] = nil
    end
  end
  reloading = false
  saved = nil
end

return {
  module_env = module_env,
  module_store = module_store,
  module_run = run,
  module_rerun = module_rerun,
  reload_begin = reload_begin,
  reload_commit = reload_commit,
  reload_abort = reload_abort,
}
)LUA";
}

}  // namespace vse::script
