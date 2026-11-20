-- A coin: spins, and when the player walks into it, counts (see game.lua) and disappears.

function onStart(self)
  game.coins = (game.coins or 0) + 1
end

function onUpdate(self, dt)
  local rotation = self.rotation
  rotation.y = rotation.y + 120 * dt
  self.rotation = rotation
end

-- Its Collision box overlaps another entity's
function onOverlap(self, other)
  if other.name == "Player" then
    game.collect()
    self:destroy()
  end
end
