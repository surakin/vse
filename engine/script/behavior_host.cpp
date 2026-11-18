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
local tasks = {}  -- tasks[id] = { fn, cancelled }: the coroutines `spawn` made
local owned = {}  -- owned[serial][id] = true
local spawn_hooked = {}
local running_task = nil  -- the spawned task whose code is running now (nil in an ordinary callback)
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
  running_task = nil
  local class = behaviors[name]
  if class == nil then
    error("unknown behavior '" .. tostring(name) .. "'", 0)
  end
  instances[serial] = setmetatable({ entity = entity, params = params }, class)
  return true
end

function __behavior_call(serial, callback, dt)
  current = serial
  running_task = nil
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
  spawn_hooked[serial] = nil
  local mine = owned[serial]
  if mine ~= nil then
    for id in pairs(mine) do
      tasks[id] = nil
    end
    owned[serial] = nil
  end
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
  running_task = nil
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

-- Latent helpers. The waits are the backend's (they suspend the callback; the engine resumes it in a later tick); the
-- host wraps them so that `stop` can end a spawned task the next time it wakes.
local native_wait, native_wait_frames, native_wait_event = wait, wait_frames, wait_event
local next_task = 0
local CANCELLED = {}

-- A wait remembers the task that called it and, when the engine resumes it, restores that and honors a `stop`.
local function waiting(native, ...)
  local mine = running_task
  local serial = current
  local a, b = native(...)
  running_task = mine
  current = serial  -- other instances ran while this coroutine waited
  if mine ~= nil and mine.cancelled then
    error(CANCELLED, 0)
  end
  return a, b
end

function wait(seconds)
  waiting(native_wait, seconds)
end

function wait_frames(frames)
  waiting(native_wait_frames, frames)
end

function wait_event(name)
  return waiting(native_wait_event, name)
end

local function hook_spawn(serial)
  if spawn_hooked[serial] then
    return
  end
  spawn_hooked[serial] = true
  add_handler('__spawn', function(me, id, sender)
    local t = tasks[id]
    if sender ~= me.entity or t == nil or t.cancelled or t.started or t.owner ~= me.entity then
      return
    end
    t.started = true
    running_task = t
    local ok, err = pcall(t.fn, me)
    tasks[id] = nil
    if not ok and err ~= CANCELLED then
      error(err, 0)
    end
  end, false)
end

-- Starts `fn(self)` as a coroutine of the instance at the start of the next tick; it may wait. Returns a handle.
function spawn(fn)
  if type(fn) ~= 'function' then
    error('spawn: expects a function', 2)
  end
  if current == nil then
    error('spawn: only a behavior callback can spawn', 2)
  end
  next_task = next_task + 1
  local id = next_task
  tasks[id] = { fn = fn, cancelled = false, started = false, owner = instances[current].entity }
  local mine = owned[current]
  if mine == nil then
    mine = {}
    owned[current] = mine
  end
  mine[id] = true
  hook_spawn(current)
  native_event.emit_to(instances[current].entity, '__spawn', id)
  return id
end

-- Cancels a spawned coroutine: it ends the next time it would have continued from a wait.
function stop(handle)
  local t = tasks[handle]
  if t ~= nil then
    t.cancelled = true
    return true
  end
  return false
end

function after(seconds, fn)
  return spawn(function(me)
    wait(seconds)
    fn(me)
  end)
end

function every(seconds, fn)
  return spawn(function(me)
    while true do
      wait(seconds)
      fn(me)
    end
  end)
end

local easings = {
  linear = function(a) return a end,
  in_quad = function(a) return a * a end,
  out_quad = function(a) return a * (2 - a) end,
  in_out_quad = function(a) if a < 0.5 then return 2 * a * a end return -1 + (4 - 2 * a) * a end,
}

-- Calls fn(self, value) once per tick with `value` moving from `from` to `to` over `seconds`; the last call is exactly `to`.
function tween(from, to, seconds, easing, fn)
  local ease = easings[easing or 'linear']
  if ease == nil then
    error("tween: unknown easing '" .. tostring(easing) .. "'", 2)
  end
  return spawn(function(me)
    local elapsed = 0
    while elapsed < seconds do
      wait_frames(1)
      elapsed = elapsed + time.dt()
      local a = elapsed / seconds
      if a >= 1 then
        break
      end
      fn(me, from + (to - from) * ease(a))
    end
    fn(me, to)
  end)
end
)LUA";
}

}  // namespace engine_placeholder::script
