-- An entity script for tests/lua/entity_script.lua: moves right at `speed`, counts its updates, and runs a routine
properties = { speed = 2 }

function onStart(self)
  self.started = (self.started or 0) + 1
  self.updates = 0
  self:run(function()
    wait(0.3)
    self.routineRan = true
  end)
end

function onUpdate(self, dt)
  self.updates = self.updates + 1
  self.position = self.position + vec3(self.speed * dt, 0, 0)
end
