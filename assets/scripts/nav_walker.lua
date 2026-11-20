-- Walks to the "Target" entity around the walls of the navigation scene: builds the navigation map from the static
-- bodies once, asks for a path, then follows it point by point through the character controller.

local SPEED = 4         -- units per second
local REACHED = 0.25    -- how close to a point counts as being there

function onStart(self)
  -- Everything the level is made of is static, so the map covers the scene as it is
  nav.build({ agentRadius = 0.35, agentHeight = 1.8 })

  -- The path is on the ground; the character's position is its middle, half its height up
  self.halfHeight = 0.9
  local target = engine.find("Target")
  self.path = nav.path(self.position - vec3(0, self.halfHeight, 0), target.position)
  self.next = 2 -- the first point is where it stands
  if self.path then
    print(string.format("path of %d points", #self.path))
  else
    print("no way to the target")
  end
end

function onUpdate(self, dt)
  local path = self.path
  if not path or self.next > #path then
    self:move(vec3(0))
    return
  end

  local toPoint = path[self.next] - (self.position - vec3(0, self.halfHeight, 0))
  toPoint.y = 0
  if toPoint:length() < REACHED then
    self.next = self.next + 1
    if self.next > #path then
      print("reached the target")
    end
    return
  end

  local direction = toPoint:normalized()
  self:move(direction * SPEED)
  -- Faces where it goes (a heading of 0 looks down -Z)
  self.rotation = vec3(0, math.deg(math.atan(-direction.x, -direction.z)), 0)
end
