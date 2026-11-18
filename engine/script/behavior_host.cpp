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
local handlers = {}  -- handlers[serial][event name] = list of { fn = function, once = bool }
local current = nil  -- the serial of the instance whose callback is running
-- The bindings' `event` table is read-only: the host puts the handler functions in a table of its own that falls
-- back to it, and publishes that as the global.
local native_event = event
local event = setmetatable({}, { __index = native_event })
_G.event = event

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
  current = serial
  local class = behaviors[name]
  if class == nil then
    error("unknown behavior '" .. tostring(name) .. "'", 0)
  end
  instances[serial] = setmetatable({ entity = entity, params = params }, class)
  return true
end

function __behavior_call(serial, callback, dt)
  current = serial
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
  handlers[serial] = nil
end

local function add_handler(name, fn, once)
  if current == nil then
    error('event.on: only a behavior callback can subscribe', 3)
  end
  if type(name) ~= 'string' or type(fn) ~= 'function' then
    error('event.on: expects an event name and a function', 3)
  end
  local by_name = handlers[current]
  if by_name == nil then
    by_name = {}
    handlers[current] = by_name
  end
  local list = by_name[name]
  if list == nil then
    list = {}
    by_name[name] = list
    native_event.subscribe(name)
  end
  list[#list + 1] = { fn = fn, once = once }
end

function event.on(name, fn)
  add_handler(name, fn, false)
end

function event.once(name, fn)
  add_handler(name, fn, true)
end

-- Removes one handler (fn given) or every handler of the event; returns how many were removed.
function event.off(name, fn)
  local by_name = current and handlers[current]
  local list = by_name and by_name[name]
  if list == nil then
    return 0
  end
  local kept = {}
  local removed = 0
  for i = 1, #list do
    if fn == nil or list[i].fn == fn then
      removed = removed + 1
    else
      kept[#kept + 1] = list[i]
    end
  end
  if #kept == 0 then
    by_name[name] = nil
    native_event.unsubscribe(name)
  else
    by_name[name] = kept
  end
  return removed
end

function __behavior_event(serial, name, payload, sender)
  current = serial
  local self = instances[serial]
  local by_name = handlers[serial]
  local list = by_name and by_name[name]
  if self == nil or list == nil then
    return 0
  end
  local snapshot = {}
  for i = 1, #list do
    snapshot[i] = list[i]
  end
  for i = 1, #snapshot do
    local h = snapshot[i]
    if h.once then
      local live = by_name[name]
      if live ~= nil then
        local kept = {}
        for j = 1, #live do
          if live[j] ~= h then
            kept[#kept + 1] = live[j]
          end
        end
        if #kept == 0 then
          by_name[name] = nil
          native_event.unsubscribe(name)
        else
          by_name[name] = kept
        end
      end
    end
    h.fn(self, payload, sender)
  end
  return #snapshot
end
)LUA";
}

}  // namespace engine_placeholder::script
