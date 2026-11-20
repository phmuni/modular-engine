-- The player: turns and walks with the "moveX" / "moveY" axes, jumps with "jump" (assets/input.json maps them to
-- keys and a gamepad), and the camera follows it (camera.follow).

local START = vec3(0, 0.6, 3)

-- Tunable values: they show in the Properties panel (select the player in the editor, F2) and are set on `self`
-- before onStart (self.speed, ...). Changing them there while the game runs takes effect at once.
properties = {
  speed = 6,       -- units per second
  turnSpeed = 160, -- degrees per second
  jumpSpeed = 6,   -- units per second, upward
}

function onStart(self)
  -- The camera trails it from behind and above, looking a little ahead. The engine moves it after the physics and the
  -- scripts, so it does not jitter; a bit of smoothing keeps it calm when the player turns.
  camera.follow(self, { offset = vec3(0, 4, 7), lookAhead = 2, smoothing = 0.1 })
end

function onUpdate(self, dt)
  -- Turning is ours; walking goes through the character controller, which keeps it on the ground and out of walls
  local rotation = self.rotation
  rotation.y = rotation.y - input.axis("moveX") * self.turnSpeed * dt
  self.rotation = rotation
  self:move(self.forward * input.axis("moveY") * self.speed)
  if input.pressed("jump") and self.onGround then
    self:jump(self.jumpSpeed)
  end

  -- Fell off the level: back to the start
  if self.position.y < -10 then
    self.position = START
  end
end
