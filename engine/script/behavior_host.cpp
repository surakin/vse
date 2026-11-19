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
local instance_name = {}  -- instance_name[serial] = the behavior's name
local persisted = {}  -- persisted[serial] = { field name = true }: the fields `persist` marked (Day 469)
local reloading = false
local previous = {}  -- during a reload: previous[name] = the class a behavior had before the reload
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
  if reloading and behaviors[name] ~= nil and previous[name] == nil then
    previous[name] = behaviors[name]
  end
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
  instance_name[serial] = name
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
  instance_name[serial] = nil
  persisted[serial] = nil
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

-- Persistence (docs/guide/scripting.md, "Saving scripted state"). `persist("hp", "inventory")` marks fields of `self`
-- a save keeps; everything else (closures, userdata, coroutines, anything the script rebuilds) is not saved. The engine
-- reads the marked fields with `__behavior_save` between ticks and gives them back with `__behavior_load`, before
-- `on_load` and `on_start`.
local MAX_PERSISTED_FIELDS = 256
local persist_nodes = 0

function persist(...)
  if current == nil then
    error('persist: only a behavior callback can mark fields', 2)
  end
  local set = persisted[current]
  if set == nil then
    set = { 0 }
    persisted[current] = set
  end
  local names = { ... }
  for i = 1, table.maxn(names) do
    local name = names[i]
    if type(name) ~= 'string' or name == '' or #name > 64 then
      error('persist: a field name is a non-empty string of at most 64 bytes', 2)
    end
    if set[name] == nil then
      if set[1] >= MAX_PERSISTED_FIELDS then
        error('persist: too many persistent fields', 2)
      end
      set[name] = true
      set[1] = set[1] + 1
    end
  end
end

-- Copies a value for saving: numbers, strings, booleans and the engine's own values (vectors, entities, bytes: tables
-- with a metatable) pass through; a plain table is copied without the entries that cannot be saved (functions,
-- threads, userdata). The budget bounds a table that holds itself.
local function clean(v, depth)
  local t = type(v)
  if t == 'number' or t == 'string' or t == 'boolean' then
    return v
  end
  if t ~= 'table' then
    return nil
  end
  if getmetatable(v) ~= nil then
    return v
  end
  persist_nodes = persist_nodes + 1
  if depth > 12 or persist_nodes > 100000 then
    return nil
  end
  local copy = {}
  for k, x in pairs(v) do
    local tk = type(k)
    if tk == 'string' or tk == 'number' then
      local c = clean(x, depth + 1)
      if c ~= nil then
        copy[k] = c
      end
    end
  end
  return copy
end

local function persisted_names(serial)
  local names = {}
  local set = persisted[serial]
  if set ~= nil then
    for name in pairs(set) do
      if type(name) == 'string' then
        names[#names + 1] = name
      end
    end
    table.sort(names)
  end
  return names
end

-- The marked fields as a list of { name, value } pairs, sorted by name; fields that are nil are left out.
function __behavior_save(serial)
  local self = instances[serial]
  local pairs_out = {}
  if self == nil then
    return pairs_out
  end
  persist_nodes = 0
  local names = persisted_names(serial)
  for i = 1, #names do
    local c = clean(self[names[i]], 0)
    if c ~= nil then
      pairs_out[#pairs_out + 1] = { names[i], c }
    end
  end
  return pairs_out
end

function __behavior_save_names(serial)
  return persisted_names(serial)
end

-- One field, for the engine's fallback when `__behavior_save` as a whole could not cross (a value inside a field is
-- not a value the engine can hold): the engine asks again field by field and leaves out the ones that fail.
function __behavior_save_field(serial, name)
  local self = instances[serial]
  if self == nil then
    return nil
  end
  persist_nodes = 0
  return clean(self[name], 0)
end

-- Puts saved fields back on an instance just made (and marks them persistent again).
function __behavior_load(serial, saved)
  local self = instances[serial]
  if self == nil or type(saved) ~= 'table' then
    return 0
  end
  local set = persisted[serial]
  if set == nil then
    set = { 0 }
    persisted[serial] = set
  end
  local n = 0
  for i = 1, #saved do
    local name, value = saved[i][1], saved[i][2]
    if type(name) == 'string' and name ~= '' and #name <= 64 then
      self[name] = value
      if set[name] == nil and set[1] < MAX_PERSISTED_FIELDS then
        set[name] = true
        set[1] = set[1] + 1
      end
      n = n + 1
    end
  end
  return n
end

-- Hot reload (docs/guide/scripting.md, "Hot reload"): between `begin` and `commit` a redefined behavior remembers the
-- class it replaces; `commit` moves every instance of such a behavior to the class that replaced it, calling the first
-- class's `on_reload_save(self)` and then the second's `on_reload_restore(self, saved)` when they exist (an instance
-- keeps its own fields either way); `abort` puts the old classes back.
function __behavior_reload_begin()
  reloading = true
  previous = {}
end

function __behavior_reload_abort()
  for name, old in pairs(previous) do
    behaviors[name] = old
  end
  previous = {}
  reloading = false
end

function __behavior_reload_commit()
  local serials = {}
  for serial in pairs(instances) do
    serials[#serials + 1] = serial
  end
  table.sort(serials)
  local rebound, failures, first = 0, 0, ''
  for i = 1, #serials do
    local serial = serials[i]
    local self = instances[serial]
    local name = instance_name[serial]
    local old = name and previous[name]
    if old ~= nil and self ~= nil then
      current = serial
      running_task = nil
      local saved = nil
      if old.on_reload_save ~= nil then
        local ok, result = pcall(old.on_reload_save, self)
        if ok then
          saved = result
        else
          failures = failures + 1
          if first == '' then first = tostring(result) end
        end
      end
      local new = behaviors[name]
      setmetatable(self, new)
      if new.on_reload_restore ~= nil then
        local ok, result = pcall(new.on_reload_restore, self, saved)
        if not ok then
          failures = failures + 1
          if first == '' then first = tostring(result) end
        end
      end
      current = nil
      running_task = nil
      rebound = rebound + 1
    end
  end
  current = nil  -- no instance's identity outlives the commit
  running_task = nil
  previous = {}
  reloading = false
  return rebound, failures, first
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
local native_wait, native_wait_frames, native_wait_event, native_wait_task = wait, wait_frames, wait_event, wait_task
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

function wait_task(handle)
  return waiting(native_wait_task, handle)
end

-- Tasks: parallel work on the task pool (docs/guide/scripting.md, "Script tasks"). The binding table is read-only, so
-- the host publishes its own `task` table that falls back to it.
local native_task = task
local task = setmetatable({}, { __index = native_task })
_G.task = task

local function enqueue(mode, source, data, seed, count, chunk)
  if type(source) ~= 'string' then
    error('task: the function must be given as source text; a Lua closure cannot cross into a task state', 3)
  end
  local r = native_task.enqueue(mode, source, data, seed or 0, count or 0, chunk or 0)
  if type(r) == 'string' then
    return nil, r
  end
  return r
end

function task.run(source, args, seed)
  return enqueue(1, source, args, seed)
end

function task.map(source, array, chunk, seed)
  return enqueue(2, source, array, seed, 0, chunk)
end

function task.parallel_for(count, source, args, chunk, seed)
  return enqueue(3, source, args, seed, count, chunk)
end

function task.result(handle)
  local pair = native_task.take(handle)
  local status, value = pair[1], pair[2]
  if status == 1 then
    return value
  end
  if status == 2 then
    return nil, value
  end
  return nil, status == 0 and 'pending' or (status == 4 and 'that task belongs to another script' or 'no such task')
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
  if t ~= nil and current ~= nil and t.owner == instances[current].entity then
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
