-- A third-person knight (assets/models/knight, animators/knight.json): runs camera-relative with WASD (Shift walks),
-- Space jumps, the left mouse button attacks (press again during the swing for the second one), the right one blocks
-- (hold), Q dodges, H plays the hit reaction, K lies down and gets up again. The mouse turns the camera.
--
-- The entity with the CharacterController is the capsule; the model hangs under it as a child, lowered to the capsule's
-- feet, because the capsule's Transform is its middle. The animation state machine is on that child, so this script
-- gives it what a CharacterController on the same entity would: `speed` and `grounded`.
--
-- The sword only hurts while the swing's `damage` window is open (animation events in animators/knight.json): then
-- whatever "enemy" is in front of the knight, once per swing, is hit through its own `takeHit`, as the training dummy
-- (scripts/knight_dummy.lua) has it. The model has no script, so this one asks the model whether the window is open.

properties = {
  walkSpeed = 0.68,   -- units per second: the pace Walking_A covers at the model's scale, so the feet do not slide
  runSpeed = 2.84,    -- same for Running_A
  acceleration = 8,   -- units per second, per second
  turnSpeed = 540,    -- degrees per second
  jumpSpeed = 5.5,    -- upward
  blockSlowdown = 0.5, -- movement while blocking, as a share
  dodgeSpeed = 3.0,   -- units per second along the dodge
  dodgeTime = 0.4,    -- seconds, the length of Dodge_Forward
  reach = 0.9,        -- how far in front of the knight the sword's hit is centered
  hitRadius = 0.6,    -- and how far around that it reaches
}

local START = vec3(0, 0.95, 0)

function onStart(self)
  self.model = self:children()[1]
  self.pace = 0        -- how fast it is walking now
  self.dodging = 0     -- seconds of dodge left
  self.heading = vec3(0)
  self.struck = {}     -- who this swing has hit already, by entity number
  self.dead = false
  -- The scene browser flies the camera with the same keys: off, so the camera is the knight's
  engine.setToggle("freeCamera", false)
  -- The camera trails it and turns around it with the mouse; movement is relative to where it looks
  camera.follow(self, { offset = vec3(0, 1.8, 4.5), smoothing = 0.1, followRotation = false, orbit = true })
end

local function key(name)
  return input.key(name) and 1 or 0
end

-- The way the camera looks, flat on the ground, and to its right
local function cameraAxes()
  local look = camera.forward()
  local forward = vec3(look.x, 0, look.z)
  if forward:length() < 0.001 then
    forward = vec3(0, 0, -1)
  end
  forward = forward:normalized()
  return forward, vec3(-forward.z, 0, forward.x)
end

local function turnToward(self, direction, dt)
  local wanted = math.deg(math.atan(-direction.x, -direction.z)) -- a heading of 0 looks down -Z
  local rotation = self.rotation
  local difference = (wanted - rotation.y + 540) % 360 - 180
  local step = self.turnSpeed * dt
  rotation.y = rotation.y + math.max(-step, math.min(step, difference))
  self.rotation = rotation
end

-- While the damage window is open, hits what is in front of the sword: the "enemy" entities with a `takeHit`
local function swordHits(self)
  if not self.model:inAnimationWindow("damage") then
    if next(self.struck) then
      self.struck = {} -- the next swing (or the next window of a combo) may hit them again
    end
    return
  end
  local center = self.position + self.forward * self.reach
  for _, target in ipairs(physics.overlapSphere(center, self.hitRadius, { characters = true, ignore = self })) do
    if target:hasTag("enemy") and target.takeHit and not self.struck[target.id] then
      self.struck[target.id] = true
      target:takeHit()
    end
  end
end

function onUpdate(self, dt)
  local model = self.model
  swordHits(self)
  local grounded = self.onGround
  local state = model.animationState

  if input.keyPressed("K") then
    self.dead = not self.dead
    model:setParam("dead", self.dead)
  end
  if input.keyPressed("H") then
    model:trigger("hit")
  end

  local alive = not self.dead
  local forward, right = cameraAxes()
  local direction = forward * (key("W") - key("S")) + right * (key("D") - key("A"))
  if direction:length() > 0 then
    direction = direction:normalized()
  end

  -- Actions that only start on the ground
  local free = alive and grounded and state ~= "dodge"
  local blocking = free and input.mouseDown(3)
  model:setParam("block", blocking)
  if free and not blocking and input.click(1) then -- a trigger raised behind the shield would wait and swing later
    model:trigger("attack")
  end
  if free and input.keyPressed("Q") then
    model:trigger("dodge")
    self.dodging = self.dodgeTime
    self.dodgeDirection = direction:length() > 0 and direction or self.forward
  end
  if free and input.keyPressed("Space") then
    self:jump(self.jumpSpeed)
  end

  -- How fast it wants to go, and the pace it has got to
  local wanted = 0
  if alive and direction:length() > 0 then
    wanted = input.key("Left Shift") and self.walkSpeed or self.runSpeed
    if model:param("block") > 0 then
      wanted = wanted * self.blockSlowdown
    end
  end
  local change = self.acceleration * dt
  self.pace = self.pace + math.max(-change, math.min(change, wanted - self.pace))

  if self.dodging > 0 then
    self.dodging = self.dodging - dt
    self:move(self.dodgeDirection * self.dodgeSpeed)
    turnToward(self, self.dodgeDirection, dt)
    self.pace = 0
  elseif alive and direction:length() > 0 then
    self.heading = direction
    self:move(direction * self.pace)
    turnToward(self, direction, dt)
  else
    -- Letting go: it slows down along the way it was going
    self:move(self.heading * self.pace)
  end

  model:setParam("speed", self.pace)
  model:setParam("grounded", grounded)

  -- Fell off the level: back to the start
  if self.position.y < -10 then
    self.position = START
  end
end
