-- A coin: spins on its edge and bobs up and down, and when the player walks into it, counts (see game.lua) and
-- disappears. It is spawned from prefabs/coin.json, which gives it this script.

properties = {
  spin = 120, -- degrees per second
  bob = 0.15, -- how far it moves up and down, in units
}

function onStart(self)
  self.baseY = self.position.y
  self.age = self.position.x + self.position.z -- so the coins of the ring do not bob in step
end

function onUpdate(self, dt)
  self.age = self.age + dt

  -- The prefab stands the disc on its edge (rotation.x = 90), so turning around Y spins it like a coin on a table
  local rotation = self.rotation
  rotation.y = rotation.y + self.spin * dt
  self.rotation = rotation

  local position = self.position
  position.y = self.baseY + math.sin(self.age * 3) * self.bob
  self.position = position
end

-- Its Collision box overlaps another entity's
function onOverlap(self, other)
  if other.name == "Player" then
    game.collect()
    self:destroy()
  end
end
