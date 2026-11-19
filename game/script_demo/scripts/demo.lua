-- The script demo's game (Phase 27 Day 475): a crowd of bodies that wander, flock and flee, a player that walks a square, pickups
-- that count who touches them, and a planner that searches a field on every core with task.parallel_for. Nothing here uses sin or
-- cos: a world built from + - * / compares equal across machines.

-- Pushes a body in a random direction; the stream is the entity's own, so the push is the same at any thread count.
behavior('wander', {
  on_update = function(self, dt)
    local d = vec3.new(random.float() - 0.5, 0, random.float() - 0.5)
    physics.add_force(self.entity, d * 6.0)
  end,
})

-- Moves toward the middle of the bodies around it.
behavior('flock', {
  on_update = function(self, dt)
    local p = transform.position(self.entity)
    local near = physics.overlap_sphere(p, 4.0, nil)
    local sum = vec3.new(0, 0, 0)
    local n = 0
    for _, other in ipairs(near) do
      local q = transform.position(other)
      if vec3.distance(p, q) > 0.01 and q.y > 0.1 then
        sum = sum + q
        n = n + 1
      end
    end
    if n > 0 then
      local to = sum / n - p
      to.y = 0
      physics.add_force(self.entity, to * 0.8)
    end
  end,
})

-- Moves away from the bodies close to it.
behavior('flee', {
  on_update = function(self, dt)
    local p = transform.position(self.entity)
    local near = physics.overlap_sphere(p, 2.5, nil)
    local away = vec3.new(0, 0, 0)
    for _, other in ipairs(near) do
      local q = transform.position(other)
      if vec3.distance(p, q) > 0.01 and q.y > 0.1 then
        away = away + (p - q)
      end
    end
    away.y = 0
    if vec3.length(away) > 0.001 then
      physics.add_force(self.entity, vec3.normalize(away) * 5.0)
    end
  end,
})

-- The player walks the sides of a square, one side per second.
behavior('player', {
  on_update = function(self, dt)
    local k = time.tick() % 240
    local side = math.floor(k / 60)
    local f = (k % 60) / 60
    local r = 8
    local x, z = -r, r - 2 * r * f
    if side == 0 then x, z = -r + 2 * r * f, -r
    elseif side == 1 then x, z = r, -r + 2 * r * f
    elseif side == 2 then x, z = r - 2 * r * f, r end
    transform.set_position(self.entity, vec3.new(x, 0.5, z))
  end,
})

-- A trigger: counts the bodies that enter it and tells the game.
behavior('pickup', {
  on_create = function(self)
    self.count = 0
    event.on('on_trigger_enter', function(me, info)
      me.count = me.count + 1
      event.emit('pickup_collected', me.params.id)
    end)
  end,
})

-- Searches a field of cells for its highest value on every core, and puts the marker there. One round every ten ticks.
local FIELD = [[
  local best, at = -1, 0
  for i = first, last do
    local v = (i * 1103515245 + args.round * 12345 + 12345) % 2147483648
    for k = 1, 24 do v = (v * 1103515245 + 12345) % 2147483648 end
    if v > best then best = v at = i end
  end
  return { best, at }
]]

behavior('planner', {
  on_create = function(self)
    self.round = 0
    while true do
      self.round = self.round + 1
      local parts, err = wait_task(task.parallel_for(self.params.cells, FIELD, { round = self.round }, 256))
      if not parts then error(err) end
      local best, at = -1, 0
      for _, r in ipairs(parts) do
        if r[1] > best then best = r[1] at = r[2] end
      end
      local w = self.params.width
      transform.set_position(self.params.marker, vec3.new((at % w) - w / 2, 1.5, math.floor(at / w) - w / 2))
      wait_frames(10)
    end
  end,
})
