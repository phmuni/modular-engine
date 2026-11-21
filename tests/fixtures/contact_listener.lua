-- An entity script for tests/lua/physics_events.lua: counts its onContact and onContactEnd calls, and remembers who the
-- last of each was
function onStart(self)
  self.begins = 0
  self.ends = 0
end

function onContact(self, other, point, normal)
  self.begins = self.begins + 1
  self.lastBegin = other.name
end

function onContactEnd(self, other)
  self.ends = self.ends + 1
  self.lastEnd = other.name
end
