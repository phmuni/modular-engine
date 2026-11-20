-- The player: turns and walks with the "moveX" / "moveY" axes, jumps with "jump" (assets/input.json maps them to
-- keys and a gamepad), and the camera follows it.

local START = vec3(0, 0.6, 3)

-- Tunable values: they show in the Properties panel (select the player in the editor, F2) and are set on `self`
-- before onStart (self.speed, ...). Changing them there while the game runs takes effect at once.
properties = {
  speed = 6,       -- units per second
  turnSpeed = 160, -- degrees per second
  jumpSpeed = 6,   -- units per second, upward
}

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

  -- Behind and above it, looking ahead
  camera.lookAt(self.position - self.forward * 7 + vec3(0, 4, 0), self.position + self.forward * 2)
end
