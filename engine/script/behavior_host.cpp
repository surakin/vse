#include "script/script_runtime.h"

namespace engine_placeholder::script
{

// The host every state loads first (Phase 27 Day 457). A behavior is a table of callbacks registered by name; an
// instance is a table made with the behavior as its metatable, so `self` reaches the callbacks and holds the instance's
// own fields. The instance table is private to the partition's state: the host only ever passes numbers and text in.
const char* LuaBehaviorHostSource()
{
    return R"LUA(
local behaviors = {}
local instances = {}

function behavior(name, class)
  if type(name) ~= 'string' or name == '' then
    error('behavior: the name must be a non-empty string', 2)
  end
  if type(class) ~= 'table' then
    error('behavior: the second argument must be a table of callbacks', 2)
  end
  class.__index = class
  behaviors[name] = class
  return class
end

function __behavior_create(serial, name, entity, params)
  local class = behaviors[name]
  if class == nil then
    error("unknown behavior '" .. tostring(name) .. "'", 0)
  end
  instances[serial] = setmetatable({ entity = entity, params = params }, class)
  return true
end

function __behavior_call(serial, callback, dt)
  local self = instances[serial]
  if self == nil then
    error('the instance is gone from this state', 0)
  end
  local fn = self[callback]
  if fn == nil then
    return false
  end
  fn(self, dt)
  return true
end

function __behavior_drop(serial)
  instances[serial] = nil
end
)LUA";
}

}  // namespace engine_placeholder::script
